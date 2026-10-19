// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Entity/MassUnitMovementProcessor.h"
#include "Core/MassUnitStats.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Entity/UnitTemplate.h"
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

namespace MassUnitMovement
{
	bool TemplateHasClip(const FMassUnitVisualFragment& Visual, const FGameplayTag& Tag)
	{
		const UUnitTemplate* Template = Visual.SourceTemplate.Get();
		return Template && Template->VertexAnimationClips.ContainsByPredicate(
			[&Tag](const FMassUnitVATClip& Clip) { return Clip.AnimationTag == Tag; });
	}
}

void UMassUnitMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_MovementProcessor);
	const float DeltaTime = FMath::Min(Context.GetDeltaTimeSeconds(), 0.1f);
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const float PresentationClimbSpeed = Settings ? Settings->PresentationClimbSpeed : 300.0f;
	EntityQuery.ForEachEntityChunk(Context, [this, &EntityManager, DeltaTime, PresentationClimbSpeed](FMassExecutionContext& ChunkContext)
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
				if (State.CurrentState == EMassUnitState::Dead)
				{
					Visual.CurrentAnimation = UE::MassUnitSystem::Tags::AnimationDeath();
				}
				else if (State.bLeaping)
				{
					// A pounce is held like a launch but reads as an attack, not a stun.
					Visual.CurrentAnimation = MassUnitMovement::TemplateHasClip(Visual, UE::MassUnitSystem::Tags::AnimationLeap())
						? UE::MassUnitSystem::Tags::AnimationLeap()
						: UE::MassUnitSystem::Tags::AnimationAttack();
				}
				else
				{
					Visual.CurrentAnimation = UE::MassUnitSystem::Tags::AnimationStun();
				}
				continue;
			}

			// Climbing or standing on another unit: UMassUnitStackSystem owns the position and the clip.
			if (Crowd && Crowd->bStackDriven)
			{
				Velocity.Value = FVector::ZeroVector;
				Force.Value = FVector::ZeroVector;
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
				// Free 3D altitude band (flyers): never sink below the group's floor. A clamp, not a trace. A floor that rose
				// (the group stopped chasing a target down in a pit) lifts units at a capped rate instead of teleporting them.
				if (bUse3DMovement && Crowd->bAltitudeFloor)
				{
					FVector Location = MutableTransform.GetLocation();
					if (Location.Z < Crowd->AltitudeFloorZ)
					{
						constexpr float MaxFloorLiftSpeed = 400.0f;   // cm/s
						// Above the floor before this step: stop at the floor. Already below it (the floor rose): rise gently.
						const float PreviousZ = Location.Z - Velocity.Value.Z * DeltaTime;
						Location.Z = FMath::Min(Crowd->AltitudeFloorZ, FMath::Max(Location.Z, PreviousZ) + MaxFloorLiftSpeed * DeltaTime);
						MutableTransform.SetLocation(Location);
						Velocity.Value.Z = FMath::Max(0.0f, Velocity.Value.Z);
					}
				}
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

			// Client puppet of a stacked unit (UMassUnitStackSystem::ApplyPresentationStackRole): ease to the replicated
			// height instead of snapping per record, and show the pile pose. Never set on real units.
			if (Crowd && Crowd->PresentationStackRole != 0)
			{
				if (bHasDestination)
				{
					FTransform& MutableTransform = Transform.GetMutableTransform();
					FVector Location = MutableTransform.GetLocation();
					Location.Z = FMath::FInterpConstantTo(Location.Z, Destination.Z, DeltaTime, PresentationClimbSpeed);
					MutableTransform.SetLocation(Location);
				}
				if (State.CurrentState != EMassUnitState::Attacking && State.CurrentState != EMassUnitState::Interacting)
				{
					constexpr uint8 RoleClimbing = 1;
					constexpr uint8 RoleHolding = 3;
					if (Crowd->PresentationStackRole == RoleClimbing)
					{
						Visual.CurrentAnimation = MassUnitMovement::TemplateHasClip(Visual, UE::MassUnitSystem::Tags::AnimationClimb())
							? UE::MassUnitSystem::Tags::AnimationClimb()
							: UE::MassUnitSystem::Tags::AnimationRun();
					}
					else if (Crowd->PresentationStackRole == RoleHolding && State.CurrentState != EMassUnitState::Moving
						&& MassUnitMovement::TemplateHasClip(Visual, UE::MassUnitSystem::Tags::AnimationHold()))
					{
						Visual.CurrentAnimation = UE::MassUnitSystem::Tags::AnimationHold();
					}
				}
			}
		}
	});
}
