// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Entity/MassUnitMovementProcessor.h"
#include "Core/MassUnitStats.h"

#include "Core/MassUnitGameplayTags.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassUnitCommonFragments.h"

UMassUnitMovementProcessor::UMassUnitMovementProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("MassUnitSystem.Movement"));
}

void UMassUnitMovementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassUnitTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitForceFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitLookAtFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitStateFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitTargetFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitNavigationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitVisualFragment>(EMassFragmentAccess::ReadWrite);
	// Keep this optional so project-defined/legacy archetypes that use the core movement
	// fragments continue to move even when they were not built from UUnitTemplate.
	EntityQuery.AddRequirement<FMassUnitCrowdFragment>(
		EMassFragmentAccess::ReadOnly,
		EMassFragmentPresence::Optional);
}

DECLARE_CYCLE_STAT(TEXT("Movement Processor"), STAT_MassUnit_MovementProcessor, STATGROUP_MassUnitSystem);

void UMassUnitMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_MovementProcessor);
	const float DeltaTime = FMath::Min(Context.GetDeltaTimeSeconds(), 0.1f);
	EntityQuery.ForEachEntityChunk(Context, [this, &EntityManager, DeltaTime](FMassExecutionContext& ChunkContext)
	{
		TArrayView<FMassUnitTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FMassUnitTransformFragment>();
		TArrayView<FMassUnitVelocityFragment> Velocities = ChunkContext.GetMutableFragmentView<FMassUnitVelocityFragment>();
		TArrayView<FMassUnitForceFragment> Forces = ChunkContext.GetMutableFragmentView<FMassUnitForceFragment>();
		TArrayView<FMassUnitLookAtFragment> LookAts = ChunkContext.GetMutableFragmentView<FMassUnitLookAtFragment>();
		TArrayView<FMassUnitStateFragment> States = ChunkContext.GetMutableFragmentView<FMassUnitStateFragment>();
		TArrayView<FMassUnitTargetFragment> Targets = ChunkContext.GetMutableFragmentView<FMassUnitTargetFragment>();
		TArrayView<FMassUnitNavigationFragment> Navigation = ChunkContext.GetMutableFragmentView<FMassUnitNavigationFragment>();
		TArrayView<FMassUnitVisualFragment> Visuals = ChunkContext.GetMutableFragmentView<FMassUnitVisualFragment>();
		const TConstArrayView<FMassUnitCrowdFragment> Crowds = ChunkContext.GetFragmentView<FMassUnitCrowdFragment>();
		const bool bHasCrowdData = !Crowds.IsEmpty();

		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FMassUnitTransformFragment& Transform = Transforms[It];
			FMassUnitVelocityFragment& Velocity = Velocities[It];
			FMassUnitForceFragment& Force = Forces[It];
			FMassUnitLookAtFragment& LookAt = LookAts[It];
			FMassUnitStateFragment& State = States[It];
			FMassUnitTargetFragment& Target = Targets[It];
			FMassUnitNavigationFragment& Nav = Navigation[It];
			FMassUnitVisualFragment& Visual = Visuals[It];
			const FMassUnitCrowdFragment* Crowd = bHasCrowdData ? &Crowds[It] : nullptr;
			State.StateTime += DeltaTime;

			if (State.CurrentState == EMassUnitState::Dead || State.CurrentState == EMassUnitState::Stunned)
			{
				Velocity.Value = FVector::ZeroVector;
				Force.Value = FVector::ZeroVector;
				Visual.CurrentAnimation = State.CurrentState == EMassUnitState::Dead
					? UE::MassUnitSystem::Tags::AnimationDeath()
					: UE::MassUnitSystem::Tags::AnimationStun();
				continue;
			}

			const FVector CurrentLocation = Transform.GetTransform().GetLocation();
			const bool bUse3DMovement = Crowd && Crowd->bEnabled && Crowd->bUse3DMovement;
			const bool bFollowNavmeshHeight = Crowd
				&& Crowd->bEnabled
				&& !bUse3DMovement
				&& Crowd->bConformToNavmeshHeight
				&& Nav.bPathUsesNavmesh;
			auto AdjustNavigationHeight = [Crowd, bFollowNavmeshHeight](FVector Point)
			{
				if (bFollowNavmeshHeight)
				{
					Point.Z += Crowd->NavigationHeightOffset;
				}
				return Point;
			};
			FVector Destination = FVector::ZeroVector;
			float StopDistance = Nav.AcceptanceRadius;
			bool bHasDestination = false;

			if (Target.TargetEntity.IsValid())
			{
				const FMassEntityHandle TargetHandle = Target.TargetEntity.ToMassEntityHandle();
				if (EntityManager.IsEntityValid(TargetHandle))
				{
					if (const FMassUnitTransformFragment* TargetTransform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(TargetHandle))
					{
						Destination = TargetTransform->GetTransform().GetLocation();
						Target.TargetLocation = Destination;
						StopDistance = FMath::Max(Nav.AcceptanceRadius, State.AttackRange * 0.9f);
						bHasDestination = true;
					}
				}
				else
				{
					Target.Clear();
				}
			}

			if (!bHasDestination && Nav.bPathValid)
			{
				while (Nav.PathPoints.IsValidIndex(Nav.CurrentPathIndex)
					&& (bUse3DMovement || bFollowNavmeshHeight
						? FVector::DistSquared(CurrentLocation, AdjustNavigationHeight(Nav.PathPoints[Nav.CurrentPathIndex]))
						: FVector::DistSquared2D(CurrentLocation, Nav.PathPoints[Nav.CurrentPathIndex]))
							<= FMath::Square(Nav.AcceptanceRadius))
				{
					++Nav.CurrentPathIndex;
				}
				if (Nav.PathPoints.IsValidIndex(Nav.CurrentPathIndex))
				{
					Destination = AdjustNavigationHeight(Nav.PathPoints[Nav.CurrentPathIndex]);
					bHasDestination = true;
				}
				else
				{
					Nav.bPathValid = false;
				}
			}

			if (!bHasDestination && Target.HasTarget())
			{
				Destination = AdjustNavigationHeight(Target.TargetLocation);
				bHasDestination = true;
			}

			FVector DesiredVelocity = FVector::ZeroVector;
			const bool bCrowdMovementSuppressed = Crowd && Crowd->bEnabled
				&& (Crowd->bSleeping || State.CurrentState == EMassUnitState::Interacting);
			if (bHasDestination && !bCrowdMovementSuppressed)
			{
				const FVector ToDestination = Destination - CurrentLocation;
				const bool bUseVerticalMovement = bUse3DMovement || bFollowNavmeshHeight;
				const float DistanceSquared = bUseVerticalMovement ? ToDestination.SizeSquared() : ToDestination.SizeSquared2D();
				if (DistanceSquared > FMath::Square(StopDistance))
				{
					FVector DesiredDirection = bUseVerticalMovement ? ToDestination.GetSafeNormal() : ToDestination.GetSafeNormal2D();
					if (Crowd && Crowd->bEnabled && !Crowd->SteeringDirection.IsNearlyZero())
					{
						DesiredDirection = bUseVerticalMovement
							? (DesiredDirection + Crowd->SteeringDirection).GetSafeNormal()
							: (DesiredDirection + Crowd->SteeringDirection).GetSafeNormal2D();
					}
					// Status slows (chill, tar) and roots multiply in here; 1 when the unit has none.
					DesiredVelocity = DesiredDirection * State.MoveSpeed * FMath::Max(0.0f, State.StatusMoveSpeedMultiplier);
				}
			}

			const float InterpSpeed = DesiredVelocity.IsNearlyZero() ? Deceleration : Acceleration;
			const FVector PreviousVelocity = Velocity.Value;
			Velocity.Value = FMath::VInterpConstantTo(Velocity.Value, DesiredVelocity, DeltaTime, InterpSpeed);
			Force.Value = DeltaTime > UE_SMALL_NUMBER ? (Velocity.Value - PreviousVelocity) / DeltaTime : FVector::ZeroVector;

			if (!Velocity.Value.IsNearlyZero())
			{
				FTransform& MutableTransform = Transform.GetMutableTransform();
				MutableTransform.AddToTranslation(Velocity.Value * DeltaTime);
				const FRotator VelocityRotation = Velocity.Value.Rotation();
				const FRotator DesiredRotation = bUse3DMovement
					? VelocityRotation
					: FRotator(0.0f, VelocityRotation.Yaw, 0.0f);
				MutableTransform.SetRotation(FMath::RInterpConstantTo(MutableTransform.Rotator(), DesiredRotation, DeltaTime, TurningRate).Quaternion());
				LookAt.Direction = Velocity.Value.GetSafeNormal();
				if (State.CurrentState != EMassUnitState::Attacking && State.CurrentState != EMassUnitState::Interacting)
				{
					if (State.CurrentState != EMassUnitState::Moving)
					{
						State.StateTime = 0.0f;
					}
					State.CurrentState = EMassUnitState::Moving;
					const bool bRunning = Visual.RunSpeedThreshold > 0.0f
						&& Velocity.Value.SizeSquared2D() >= FMath::Square(Visual.RunSpeedThreshold);
					Visual.CurrentAnimation = bRunning
						? UE::MassUnitSystem::Tags::AnimationRun()
						: UE::MassUnitSystem::Tags::AnimationWalk();
				}
			}
			else if (State.CurrentState == EMassUnitState::Moving)
			{
				State.CurrentState = EMassUnitState::Idle;
				State.StateTime = 0.0f;
				Visual.CurrentAnimation = UE::MassUnitSystem::Tags::AnimationIdle();
			}
		}
	});
}
