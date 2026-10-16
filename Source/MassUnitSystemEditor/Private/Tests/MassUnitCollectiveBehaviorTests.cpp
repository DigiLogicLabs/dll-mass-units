// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Collective crowd behavior: alerts recruit nearby groups, morale routs a group and spreads fear,
 * the threat table retargets with hysteresis and rescues a lost target, and groups WITHOUT the
 * collective config keep their previous behavior (the non-destructive guarantee).
 *
 * The test world never ticks, so world time is constant; every config below removes time gates
 * (reaction delay, rout stagger, switch interval) so each step resolves in one forced update.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitCrowdCollectiveBehaviorTest,
	"MassUnitSystem.Crowd.CollectiveBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MassUnitCollectiveTest
{
	TArray<FMassUnitHandle> SpawnLine(UMassUnitEntityManager* UnitManager, UUnitTemplate* Template, const FVector& Origin, int32 Count)
	{
		TArray<FMassUnitHandle> Units;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Units.Add(UnitManager->CreateUnitFromTemplate(Template, FTransform(Origin + FVector(0.0f, Index * 60.0f, 0.0f))));
		}
		return Units;
	}
}

bool FMassUnitCrowdCollectiveBehaviorTest::RunTest(const FString& Parameters)
{
	using namespace MassUnitCollectiveTest;

	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UMassUnitSubsystem* UnitSubsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem ? UnitSubsystem->GetCrowdSystem() : nullptr;
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager)
		|| !TestNotNull(TEXT("Crowd system is available"), CrowdSystem))
	{
		return false;
	}
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->TeamID = 2;
	Template->BaseHealth = 100;
	Template->BaseDamage = 5;
	Template->MoveSpeed = 300.0f;
	Template->AttackRange = 150.0f;
	Template->AttackCooldown = 1.0f;

	const TArray<FMassUnitHandle> UnitsA = SpawnLine(UnitManager, Template, FVector(0.0f, 0.0f, 0.0f), 4);
	const TArray<FMassUnitHandle> UnitsB = SpawnLine(UnitManager, Template, FVector(1500.0f, 0.0f, 0.0f), 4);
	const TArray<FMassUnitHandle> UnitsFar = SpawnLine(UnitManager, Template, FVector(20000.0f, 0.0f, 0.0f), 4);
	const TArray<FMassUnitHandle> UnitsLegacyManual = SpawnLine(UnitManager, Template, FVector(-1500.0f, 0.0f, 0.0f), 2);
	const TArray<FMassUnitHandle> UnitsLegacyInteraction = SpawnLine(UnitManager, Template, FVector(-40000.0f, 0.0f, 0.0f), 2);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* TargetA = World->SpawnActor<APawn>(APawn::StaticClass(), FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParameters);
	APawn* TargetB = World->SpawnActor<APawn>(APawn::StaticClass(), FVector(1800.0f, 400.0f, 0.0f), FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("First target pawn spawns"), TargetA) || !TestNotNull(TEXT("Second target pawn spawns"), TargetB))
	{
		return false;
	}

	FMassUnitCrowdConfig CrowdConfig;
	CrowdConfig.WanderRadius = 300.0f;
	CrowdConfig.MaxSimulationDistance = 0.0f;
	CrowdConfig.bEnableInteractions = false;
	auto Register = [&](const TArray<FMassUnitHandle>& Units)
	{
		return CrowdSystem->RegisterCrowdGroup(Units, FVector::ZeroVector, CrowdConfig, false, 25.0f);
	};
	const int32 GroupA = Register(UnitsA);
	const int32 GroupB = Register(UnitsB);
	const int32 GroupFar = Register(UnitsFar);
	const int32 GroupLegacyManual = Register(UnitsLegacyManual);
	const int32 GroupLegacyInteraction = Register(UnitsLegacyInteraction);

	// Manual activation everywhere: only damage (collective) or alerts can engage these groups.
	FMassUnitPlayerEngagementConfig EngagementConfig;
	EngagementConfig.ActivationMode = EMassUnitCrowdActivationMode::Manual;
	EngagementConfig.bAutoDeactivate = false;
	EngagementConfig.bEnableAttacks = false;
	for (const int32 GroupHandle : {GroupA, GroupB, GroupFar, GroupLegacyManual})
	{
		CrowdSystem->ConfigureCrowdGroupEngagement(GroupHandle, EngagementConfig, true);
	}
	FMassUnitPlayerEngagementConfig InteractionConfig = EngagementConfig;
	InteractionConfig.ActivationMode = EMassUnitCrowdActivationMode::OnInteraction;
	CrowdSystem->ConfigureCrowdGroupEngagement(GroupLegacyInteraction, InteractionConfig, true);

	FMassUnitCrowdCollectiveConfig Collective;
	Collective.AlertRadius = 4000.0f;
	Collective.AlertStrength = 1.0f;
	Collective.AlertThreshold = 0.5f;
	Collective.AlertRebroadcastInterval = 0.0f;
	Collective.MinAlertReactionDelay = 0.0f;
	Collective.MaxAlertReactionDelay = 0.0f;
	Collective.RoutStaggerTime = 0.0f;
	Collective.MinTargetSwitchInterval = 0.0f;
	Collective.ProximityThreatRadius = 0.0f;
	for (const int32 GroupHandle : {GroupA, GroupB, GroupFar})
	{
		TestTrue(TEXT("Collective behavior is configurable per group"),
			CrowdSystem->ConfigureCrowdGroupCollectiveBehavior(GroupHandle, Collective, true));
	}

	// ---- Non-destructive guarantees for groups WITHOUT collective config.
	TestFalse(TEXT("A Manual group without collective config is not engaged by damage reports"),
		CrowdSystem->ReportCrowdUnitDamaged(UnitsLegacyManual[0], 10.0f, TargetA));
	TestFalse(TEXT("The Manual legacy group stays ambient"), CrowdSystem->IsCrowdGroupEngaged(GroupLegacyManual));
	TestTrue(TEXT("An OnInteraction group engages when damage is reported (the documented contract)"),
		CrowdSystem->ReportCrowdUnitDamaged(UnitsLegacyInteraction[0], 10.0f, TargetA)
		&& CrowdSystem->IsCrowdGroupEngaged(GroupLegacyInteraction));
	TestEqual(TEXT("Groups without collective config report full morale"), CrowdSystem->GetCrowdGroupMorale(GroupLegacyManual), 1.0f);
	TestEqual(TEXT("Unit-to-group lookup resolves crowd membership"), CrowdSystem->GetUnitCrowdGroup(UnitsB[1]), GroupB);

	// ---- Alerts: damage engages A (Manual mode, but bActivateOnDamage), A's alert recruits B, not Far.
	TestTrue(TEXT("Damage engages a collective group regardless of activation mode"),
		CrowdSystem->ReportCrowdUnitDamaged(UnitsA[0], 10.0f, TargetA) && CrowdSystem->IsCrowdGroupEngaged(GroupA));
	TestFalse(TEXT("Recruitment waits for the next crowd update (one hop per tick)"), CrowdSystem->IsCrowdGroupEngaged(GroupB));
	CrowdSystem->ForceCrowdGroupUpdate(GroupA);
	TestTrue(TEXT("A nearby same-team group is recruited by the alert"), CrowdSystem->IsCrowdGroupEngaged(GroupB));
	TestTrue(TEXT("The recruited group adopts the alert's target"), CrowdSystem->GetCrowdGroupTargetActor(GroupB) == TargetA);
	TestFalse(TEXT("A group outside the alert radius is not recruited"), CrowdSystem->IsCrowdGroupEngaged(GroupFar));
	TestFalse(TEXT("Alerts never reach groups without collective config"), CrowdSystem->IsCrowdGroupEngaged(GroupLegacyManual));
	TestEqual(TEXT("Diagnostics count alert recruitments"), CrowdSystem->GetCrowdStats().GroupsAlerted, 1);

	// ---- Morale: a shock routs A; its units flee; its fear reaches B next update.
	TestTrue(TEXT("Morale shocks apply to morale-tracking groups"), CrowdSystem->ApplyCrowdGroupMoraleShock(GroupA, 0.8f));
	CrowdSystem->ForceCrowdGroupUpdate(GroupA);
	TestTrue(TEXT("Morale below the rout threshold routs an engaged group"), CrowdSystem->IsCrowdGroupRouting(GroupA));
	{
		FTransform UnitTransform;
		UnitManager->GetUnitTransform(UnitsA[0], UnitTransform);
		const FMassUnitTargetFragment* FleeTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(UnitsA[0].EntityHandle.ToMassEntityHandle());
		TestTrue(TEXT("A routed unit is sent AWAY from the target"),
			FleeTarget && FleeTarget->bHasTargetLocation
			&& FVector::DistSquared2D(FleeTarget->TargetLocation, TargetA->GetActorLocation())
				> FVector::DistSquared2D(UnitTransform.GetLocation(), TargetA->GetActorLocation()));
	}
	CrowdSystem->ForceCrowdGroupUpdate(GroupB);
	TestTrue(TEXT("Fear from a rout lowers a nearby group's morale"), CrowdSystem->GetCrowdGroupMorale(GroupB) < 1.0f);
	TestFalse(TEXT("Moderate fear alone does not rout a healthy group"), CrowdSystem->IsCrowdGroupRouting(GroupB));

	// ---- Threat: B switches to the actor that hurt it most, and is rescued when that actor vanishes.
	CrowdSystem->ReportCrowdUnitDamaged(UnitsB[0], 5.0f, TargetA);
	CrowdSystem->ReportCrowdUnitDamaged(UnitsB[0], 100.0f, TargetB);
	TestTrue(TEXT("An engaged group does not jump targets on a single hit"), CrowdSystem->GetCrowdGroupTargetActor(GroupB) == TargetA);
	CrowdSystem->ForceCrowdGroupUpdate(GroupB);
	TestTrue(TEXT("The threat table moves the group onto the most threatening actor"), CrowdSystem->GetCrowdGroupTargetActor(GroupB) == TargetB);
	TestEqual(TEXT("Diagnostics count threat retargets"), CrowdSystem->GetCrowdStats().TargetSwitches, 1);
	World->DestroyActor(TargetB);
	CrowdSystem->ForceCrowdGroupUpdate(GroupB);
	TestTrue(TEXT("Losing the target hands the fight to the next threat instead of disengaging"),
		CrowdSystem->IsCrowdGroupEngaged(GroupB) && CrowdSystem->GetCrowdGroupTargetActor(GroupB) == TargetA);

	// ---- Disabling collective behavior mid-rout restores a normal group.
	TestTrue(TEXT("Collective behavior can be disabled at runtime"),
		CrowdSystem->ConfigureCrowdGroupCollectiveBehavior(GroupA, Collective, false));
	TestFalse(TEXT("Disabling collective behavior ends a rout"), CrowdSystem->IsCrowdGroupRouting(GroupA));

	// ---- World alerts (alarm / gunshot) recruit without a source group.
	TestTrue(TEXT("World alerts can be raised"),
		CrowdSystem->RaiseCrowdAlertAtLocation(FVector(20000.0f, 0.0f, 0.0f), 1000.0f, 1.0f, TargetA, 2));
	CrowdSystem->ForceCrowdGroupUpdate(GroupFar);
	TestTrue(TEXT("A world alert recruits a group in range"), CrowdSystem->IsCrowdGroupEngaged(GroupFar));

	for (const int32 GroupHandle : {GroupA, GroupB, GroupFar, GroupLegacyManual, GroupLegacyInteraction})
	{
		CrowdSystem->UnregisterCrowdGroup(GroupHandle, true);
	}
	for (const TArray<FMassUnitHandle>* Units : {&UnitsA, &UnitsB, &UnitsFar, &UnitsLegacyManual, &UnitsLegacyInteraction})
	{
		for (const FMassUnitHandle& Unit : *Units)
		{
			UnitManager->DestroyUnit(Unit);
		}
	}
	World->DestroyActor(TargetA);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
