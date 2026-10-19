// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "Gameplay/MassUnitBehaviorPreset.h"
#include "Gameplay/MassUnitCommandSystem.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Behaviour presets (UMassUnitBehaviorPreset): a group registers from a preset, a live group swaps presets without a new
 * handle or losing units, the motion style follows, the managed-subgroup layout from registration is kept, commanded
 * units are left alone, a hand-set config clears the preset, and swapping back restores the original style.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitBehaviorPresetTest,
	"DLLMassUnits.Crowd.BehaviorPresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitBehaviorPresetTest::RunTest(const FString& Parameters)
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
	UMassUnitCommandSystem* Commands = UnitSubsystem ? UnitSubsystem->GetCommandSystem() : nullptr;
	if (!TestNotNull(TEXT("Unit manager"), UnitManager) || !TestNotNull(TEXT("Crowd system"), CrowdSystem) || !TestNotNull(TEXT("Command system"), Commands))
	{
		return false;
	}
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->MoveSpeed = 300.0f;
	TArray<FMassUnitHandle> Units;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Units.Add(UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector((Index % 5) * 80.0f, (Index / 5) * 80.0f, 100.0f))));
	}

	UMassUnitBehaviorPreset* Ambient = NewObject<UMassUnitBehaviorPreset>(GetTransientPackage());
	Ambient->Crowd.MotionStyle = EMassUnitCrowdMotionStyle::Wander;
	Ambient->Crowd.WanderRadius = 900.0f;
	Ambient->Crowd.MaxSimulationDistance = 0.0f;
	Ambient->Crowd.bEnableInteractions = false;
	Ambient->Crowd.bEnableManagedSubgroups = true;
	Ambient->Crowd.ManagedSubgroupSize = 4;

	UMassUnitBehaviorPreset* Hunting = NewObject<UMassUnitBehaviorPreset>(GetTransientPackage());
	Hunting->Crowd = Ambient->Crowd;
	Hunting->Crowd.MotionStyle = EMassUnitCrowdMotionStyle::Swarm;
	Hunting->Crowd.bEnableManagedSubgroups = false;   // must not change the registered layout
	Hunting->bEnableEngagement = true;
	Hunting->Engagement.ActivationMode = EMassUnitCrowdActivationMode::Manual;
	Hunting->bEnableCollectiveBehavior = true;

	const FVector Centre(160.0f, 40.0f, 100.0f);
	const int32 Group = CrowdSystem->RegisterCrowdGroupFromPreset(Units, Centre, Ambient);
	if (!TestTrue(TEXT("A group registers from a preset"), Group != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("The group remembers its preset"), CrowdSystem->GetCrowdGroupPreset(Group), Ambient);
	TestEqual(TEXT("The preset's motion style is used"), CrowdSystem->GetCrowdGroupMotionStyle(Group), EMassUnitCrowdMotionStyle::Wander);
	const int32 Subgroups = CrowdSystem->GetCrowdGroupSubgroupCount(Group);
	TestEqual(TEXT("The preset's subgroup layout is used at registration (10 units / 4)"), Subgroups, 3);
	TestEqual(TEXT("A preset without engagement or collective needs no extra calls"), CrowdSystem->RegisterCrowdGroupFromPreset({}, Centre, Ambient), INDEX_NONE);

	// One unit under orders before the swap.
	FMassUnitMoveOrder Move;
	Move.Destination = FVector(4000.0f, 0.0f, 100.0f);
	Move.bUseNavigation = false;
	Commands->IssueMoveOrder({ Units[0] }, Move);
	FVector Slot;
	Commands->GetUnitOrderSlot(Units[0], Slot);

	TestTrue(TEXT("A live group swaps presets"), CrowdSystem->ApplyCrowdGroupPreset(Group, Hunting));
	TestTrue(TEXT("Same handle after the swap"), CrowdSystem->IsCrowdGroupRegistered(Group));
	TestEqual(TEXT("No unit lost in the swap"), CrowdSystem->GetCrowdGroupUnitCount(Group), Units.Num());
	TestEqual(TEXT("The new preset's motion style is live"), CrowdSystem->GetCrowdGroupMotionStyle(Group), EMassUnitCrowdMotionStyle::Swarm);
	TestEqual(TEXT("The group reports the new preset"), CrowdSystem->GetCrowdGroupPreset(Group), Hunting);
	TestEqual(TEXT("The registered subgroup layout is kept"), CrowdSystem->GetCrowdGroupSubgroupCount(Group), Subgroups);
	const FMassUnitTargetFragment* OrderedTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Units[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("A commanded unit keeps its order through the swap"),
		OrderedTarget && OrderedTarget->bHasTargetLocation && FVector::Dist2D(OrderedTarget->TargetLocation, Slot) < 1.0f);
	TestFalse(TEXT("A Manual-activation preset does not engage by itself"), CrowdSystem->IsCrowdGroupEngaged(Group));

	FMassUnitCrowdConfig ByHand = Ambient->Crowd;
	ByHand.MotionStyle = EMassUnitCrowdMotionStyle::Hive;
	TestTrue(TEXT("A config can be set by hand on a live group"), CrowdSystem->SetCrowdGroupConfig(Group, ByHand, false, 50.0f));
	TestEqual(TEXT("...its style applies"), CrowdSystem->GetCrowdGroupMotionStyle(Group), EMassUnitCrowdMotionStyle::Hive);
	TestNull(TEXT("...and the group no longer claims a preset"), CrowdSystem->GetCrowdGroupPreset(Group));

	TestTrue(TEXT("Swapping back works"), CrowdSystem->ApplyCrowdGroupPreset(Group, Ambient));
	TestEqual(TEXT("...the original style is back"), CrowdSystem->GetCrowdGroupMotionStyle(Group), EMassUnitCrowdMotionStyle::Wander);
	TestFalse(TEXT("A null preset is refused"), CrowdSystem->ApplyCrowdGroupPreset(Group, nullptr));
	Commands->ReleaseAllUnits();
	return true;
}

#endif
