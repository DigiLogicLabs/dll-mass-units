// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/UnitTemplate.h"
#include "GameFramework/DefaultPawn.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "Gameplay/MassUnitStackSystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Multi-role piles (FMassUnitPlayerEngagementConfig::ExtraAttackersWhileStacked): with the normal attack-token pool
 * full, units on a pile (a climber and the ground unit holding it) still take bonus tokens and swing; with the bonus at 0
 * they wait like everyone else; units that are not stacked never use the bonus pool.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitStackedAttackTest,
	"MassUnitSystem.Crowd.StackedAttackers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitStackedAttackTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UMassUnitSubsystem* UnitSubsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	UMassUnitStackSystem* Stacks = UnitSubsystem ? UnitSubsystem->GetStackSystem() : nullptr;
	if (!TestNotNull(TEXT("Unit manager"), UnitManager) || !TestNotNull(TEXT("Crowd"), CrowdSystem) || !TestNotNull(TEXT("Stacks"), Stacks))
	{
		return false;
	}

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->AttackRange = 300.0f;
	Template->AttackCooldown = 1.0f;
	Template->Stacking.bCanStack = true;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// One scenario at Origin: 5 units in range of a target, normal pool 1, optionally a 2-unit stack (climber + holder).
	// The stack is made AFTER registering: registration resets crowd fragments, which would drop bStackDriven.
	auto Run = [&](const FVector& Origin, int32 ExtraStacked, bool bStack) -> int32
	{
		// ADefaultPawn, not APawn: a bare APawn has no root component, so it ignores the spawn location (stays at 0,0,0).
		APawn* Target = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), Origin, FRotator::ZeroRotator, SpawnParameters);
		TArray<FMassUnitHandle> Units;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const float Angle = 2.0f * PI * static_cast<float>(Index) / 5.0f;
			Units.Add(UnitManager->CreateUnitFromTemplate(Template, FTransform(Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 120.0f)));
		}
		FMassUnitCrowdConfig Crowd;
		Crowd.MaxSimulationDistance = 0.0f;
		Crowd.bEnableInteractions = false;
		const int32 Group = CrowdSystem->RegisterCrowdGroup(Units, Origin, Crowd, false, 25.0f);
		if (bStack)
		{
			TestTrue(TEXT("A climber stacks onto a neighbour"), Stacks->StackUnitOn(Units[4], Units[3], Target));
		}
		FMassUnitPlayerEngagementConfig Engagement;
		Engagement.ActivationMode = EMassUnitCrowdActivationMode::Manual;
		Engagement.bAutoDeactivate = false;
		Engagement.bApplyActorDamage = false;
		Engagement.MaxSimultaneousAttackers = 1;
		Engagement.ExtraAttackersWhileStacked = ExtraStacked;
		CrowdSystem->ConfigureCrowdGroupEngagement(Group, Engagement, true);
		const bool bActivated = CrowdSystem->ActivateCrowdGroupForActor(Group, Target);
		const bool bForced = CrowdSystem->ForceCrowdGroupUpdate(Group);
		int32 Attacking = 0;
		FString States;
		for (const FMassUnitHandle& Unit : Units)
		{
			FMassUnitStateFragment State;
			const bool bHas = UnitManager->GetUnitState(Unit, State);
			Attacking += bHas && State.CurrentState == EMassUnitState::Attacking ? 1 : 0;
			FTransform Transform;
			UnitManager->GetUnitTransform(Unit, Transform);
			States += FString::Printf(TEXT(" [%d st=%d d=%.0f stacked=%d]"), Unit.EntityHandle.ToMassEntityHandle().Index, bHas ? static_cast<int32>(State.CurrentState) : -1,
				FVector::Dist2D(Transform.GetLocation(), Target->GetActorLocation()), Stacks->IsUnitStacked(Unit) ? 1 : 0);
		}
		AddInfo(FString::Printf(TEXT("group %d activated=%d forced=%d engaged=%d attackers=%d stats.active=%d:%s"), Group, bActivated, bForced,
			CrowdSystem->IsCrowdGroupEngaged(Group), Attacking, CrowdSystem->GetCrowdStats().ActiveAttackers, *States));
		return Attacking;
	};

	TestEqual(TEXT("No stack: the pool of 1 gives 1 attacker"), Run(FVector(0, 0, 100), 4, false), 1);
	TestEqual(TEXT("Stack, bonus 4: the climber and its holder swing on top of the pool (3)"), Run(FVector(5000, 0, 100), 4, true), 3);
	TestEqual(TEXT("Stack, bonus 1: only one stacked unit gets the bonus (2)"), Run(FVector(10000, 0, 100), 1, true), 2);
	TestEqual(TEXT("Stack, bonus 0: stacked units wait like everyone else (1)"), Run(FVector(15000, 0, 100), 0, true), 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
