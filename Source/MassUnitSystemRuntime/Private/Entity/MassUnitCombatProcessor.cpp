// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Entity/MassUnitCombatProcessor.h"
#include "Core/MassUnitStats.h"

#include "Core/MassUnitGameplayTags.h"
#include "Core/MassUnitSubsystem.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"

UMassUnitCombatProcessor::UMassUnitCombatProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = FName(TEXT("MassUnitSystem.Combat"));
	ExecutionOrder.ExecuteAfter.Add(FName(TEXT("MassUnitSystem.Movement")));
}

void UMassUnitCombatProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassUnitTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassUnitStateFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitTargetFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassUnitTeamFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassUnitVisualFragment>(EMassFragmentAccess::ReadWrite);
}

DECLARE_CYCLE_STAT(TEXT("Combat Processor"), STAT_MassUnit_CombatProcessor, STATGROUP_MassUnitSystem);

void UMassUnitCombatProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_CombatProcessor);
	const float DeltaTime = FMath::Min(Context.GetDeltaTimeSeconds(), 0.1f);
	UMassUnitEntityManager* UnitManager = nullptr;
	if (UWorld* World = Context.GetWorld())
	{
		if (UMassUnitSubsystem* UnitSubsystem = UMassUnitSubsystem::Get(World))
		{
			UnitManager = UnitSubsystem->GetUnitManager();
		}
	}
	EntityQuery.ForEachEntityChunk(Context, [this, &EntityManager, DeltaTime, UnitManager](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassUnitTransformFragment> Transforms = ChunkContext.GetFragmentView<FMassUnitTransformFragment>();
		TArrayView<FMassUnitStateFragment> States = ChunkContext.GetMutableFragmentView<FMassUnitStateFragment>();
		TArrayView<FMassUnitTargetFragment> Targets = ChunkContext.GetMutableFragmentView<FMassUnitTargetFragment>();
		const TConstArrayView<FMassUnitTeamFragment> Teams = ChunkContext.GetFragmentView<FMassUnitTeamFragment>();
		TArrayView<FMassUnitVisualFragment> Visuals = ChunkContext.GetMutableFragmentView<FMassUnitVisualFragment>();

		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FMassUnitStateFragment& State = States[It];
			FMassUnitTargetFragment& Target = Targets[It];
			FMassUnitVisualFragment& Visual = Visuals[It];
			State.AttackCooldownRemaining = FMath::Max(0.0f, State.AttackCooldownRemaining - DeltaTime);

			if (State.CurrentState == EMassUnitState::Dead
				|| State.CurrentState == EMassUnitState::Stunned
				|| !Target.TargetEntity.IsValid())
			{
				State.PendingAttackDamage = 0.0f;
				continue;
			}

			const FMassEntityHandle TargetHandle = Target.TargetEntity.ToMassEntityHandle();
			if (!EntityManager.IsEntityValid(TargetHandle))
			{
				State.PendingAttackDamage = 0.0f;
				Target.Clear();
				continue;
			}

			FMassUnitStateFragment* TargetState = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(TargetHandle);
			const FMassUnitTransformFragment* TargetTransform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(TargetHandle);
			const FMassUnitTeamFragment* TargetTeam = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(TargetHandle);
			if (!TargetState || !TargetTransform || !TargetTeam || TargetState->CurrentState == EMassUnitState::Dead || TargetTeam->TeamID == Teams[It].TeamID)
			{
				// The swing was aimed at this target; it must not land on whatever is acquired next.
				State.PendingAttackDamage = 0.0f;
				if (TargetState && TargetState->CurrentState == EMassUnitState::Dead)
				{
					Target.Clear();
				}
				continue;
			}

			Target.TargetLocation = TargetTransform->GetTransform().GetLocation();
			const float DistanceSquared = FVector::DistSquared2D(Transforms[It].GetTransform().GetLocation(), Target.TargetLocation);
			if (DistanceSquared > FMath::Square(State.AttackRange))
			{
				// The target stepped out of reach mid-swing: the swing whiffs.
				State.PendingAttackDamage = 0.0f;
				continue;
			}

			if (State.CurrentState != EMassUnitState::Attacking)
			{
				State.CurrentState = EMassUnitState::Attacking;
				State.StateTime = 0.0f;
			}
			Visual.CurrentAnimation = UE::MassUnitSystem::Tags::AnimationAttack();

			if (State.AttackCooldownRemaining <= 0.0f && State.PendingAttackDamage <= 0.0f)
			{
				// Every swing restarts the attack clip; damage lands at the template's impact delay.
				State.BeginAttackSwing(State.BaseDamage * FMath::Max(1, State.UnitLevel) * DamageMultiplier);
				State.AttackCooldownRemaining = State.AttackCooldown;
			}
			const float Damage = State.ConsumeAttackImpact();
			if (Damage > 0.0f)
			{
				if (UnitManager)
				{
					// The facade owns health/death events and defers them until this Mass
					// processor finishes, keeping Blueprint mutations outside chunk iteration.
					UnitManager->ApplyDamageInternal(FMassUnitEntityHandle(TargetHandle), Damage, nullptr, true);
					continue;
				}
				TargetState->Health = FMath::Max(0.0f, TargetState->Health - Damage);
				if (TargetState->Health <= 0.0f)
				{
					TargetState->CurrentState = EMassUnitState::Dead;
					TargetState->StateTime = 0.0f;
					Target.Clear();
				}
			}
		}
	});
}
