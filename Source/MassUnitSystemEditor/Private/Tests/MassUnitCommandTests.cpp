// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "Gameplay/MassUnitCommandSystem.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "Gameplay/MassUnitStackSystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Commands (UMassUnitCommandSystem, RTS phase R1): formation layouts, a group move hands units over from their crowd
 * group (the crowd no longer moves them), every unit gets its own slot at the slowest unit's speed, arrivals finish
 * the order, a newer order supersedes, a dead unit and a unit that never gets anywhere count as failed, Stop / Hold
 * keep units under orders, the pile planner leaves commanded units alone, and release hands them back.
 *
 * The test world never ticks: units are moved by setting their transform and the table is checked by calling
 * TickCommands with an advancing clock.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitCommandTest,
	"MassUnitSystem.Units.Commands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitCommandTest::RunTest(const FString& Parameters)
{
	// --- Formation layouts ------------------------------------------------------------------------
	const FVector Centre(1000.0f, 500.0f, 100.0f);
	const EMassUnitFormationShape Shapes[] = {
		EMassUnitFormationShape::Box, EMassUnitFormationShape::Line, EMassUnitFormationShape::Column,
		EMassUnitFormationShape::Wedge, EMassUnitFormationShape::Circle };
	for (const EMassUnitFormationShape Shape : Shapes)
	{
		for (const int32 Count : { 1, 7, 25 })
		{
			const TArray<FVector> Slots = UMassUnitCommandSystem::ComputeFormationSlots(Count, Centre, FVector(0, 1, 0), Shape, 100.0f);
			const FString Label = FString::Printf(TEXT("%s x%d"), *UEnum::GetValueAsString(Shape), Count);
			TestEqual(Label + TEXT(": one slot per unit"), Slots.Num(), Count);
			float MinGap = TNumericLimits<float>::Max();
			for (int32 A = 0; A < Slots.Num(); ++A)
			{
				TestEqual(Label + TEXT(": slots keep the destination height"), Slots[A].Z, Centre.Z);
				for (int32 B = A + 1; B < Slots.Num(); ++B)
				{
					MinGap = FMath::Min(MinGap, FVector::Dist2D(Slots[A], Slots[B]));
				}
			}
			if (Count > 1)
			{
				TestTrue(Label + TEXT(": slots are at least ~Spacing apart"), MinGap >= 99.0f);
			}
		}
	}
	{
		const TArray<FVector> Line = UMassUnitCommandSystem::ComputeFormationSlots(5, Centre, FVector(0, 1, 0), EMassUnitFormationShape::Line, 100.0f);
		FVector Mean = FVector::ZeroVector;
		for (const FVector& Slot : Line)
		{
			Mean += Slot;
			TestTrue(TEXT("Line: every slot sits across the facing (same Y as the centre)"), FMath::IsNearlyEqual(Slot.Y, Centre.Y, 0.1f));
		}
		TestTrue(TEXT("Line: centred on the destination"), FVector::Dist(Mean / Line.Num(), Centre) < 0.1f);
		const TArray<FVector> Wedge = UMassUnitCommandSystem::ComputeFormationSlots(6, Centre, FVector(0, 1, 0), EMassUnitFormationShape::Wedge, 100.0f);
		for (int32 Index = 1; Index < Wedge.Num(); ++Index)
		{
			TestTrue(TEXT("Wedge: slot 0 is the point at the front"), Wedge[0].Y > Wedge[Index].Y - 0.1f);
		}
	}

	// --- World ------------------------------------------------------------------------------------
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
	UMassUnitStackSystem* Stacks = UnitSubsystem ? UnitSubsystem->GetStackSystem() : nullptr;
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager) || !TestNotNull(TEXT("Crowd system is available"), CrowdSystem)
		|| !TestNotNull(TEXT("Command system is available"), Commands) || !TestNotNull(TEXT("Stack system is available"), Stacks))
	{
		return false;
	}
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();
	auto Fragment = [&EntityManager](const FMassUnitHandle& Unit)
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};
	auto State = [&EntityManager](const FMassUnitHandle& Unit)
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};
	auto TargetOf = [&EntityManager](const FMassUnitHandle& Unit)
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};
	auto Teleport = [&EntityManager](const FMassUnitHandle& Unit, const FVector& Location)
	{
		EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Unit.EntityHandle.ToMassEntityHandle())->GetMutableTransform().SetLocation(Location);
	};
	float Clock = 1.0f;
	auto Tick = [Commands, &Clock](float Seconds)
	{
		for (float Elapsed = 0.0f; Elapsed < Seconds; Elapsed += 0.25f)
		{
			Clock += 0.25f;
			Commands->TickCommands(Clock);
		}
	};

	UUnitTemplate* Fast = NewObject<UUnitTemplate>(GetTransientPackage());
	Fast->MoveSpeed = 300.0f;
	Fast->Stacking.bCanStack = true;
	UUnitTemplate* Slow = NewObject<UUnitTemplate>(GetTransientPackage());
	Slow->MoveSpeed = 200.0f;
	TArray<FMassUnitHandle> Squad;
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Squad.Add(UnitManager->CreateUnitFromTemplate(Index == 4 ? Slow : Fast, FTransform(FVector((Index % 3) * 90.0f, (Index / 3) * 90.0f, 100.0f))));
	}
	FMassUnitCrowdConfig Wander;
	Wander.WanderRadius = 1500.0f;
	Wander.MinWanderDistance = 200.0f;
	Wander.MaxSimulationDistance = 0.0f;
	Wander.bEnableInteractions = false;
	Wander.bEnableManagedSubgroups = false;
	const int32 Group = CrowdSystem->RegisterCrowdGroup(Squad, FVector(90.0f, 90.0f, 100.0f), Wander, false, 25.0f);
	TestTrue(TEXT("The squad starts in a crowd group"), Group != INDEX_NONE);

	// --- Group move -------------------------------------------------------------------------------
	FMassUnitMoveOrder Move;
	Move.Destination = FVector(3000.0f, 0.0f, 100.0f);
	Move.Shape = EMassUnitFormationShape::Box;
	Move.Spacing = 100.0f;
	Move.bUseNavigation = false;
	const int32 OrderA = Commands->IssueMoveOrder(Squad, Move);
	TestTrue(TEXT("The move order is issued"), OrderA != INDEX_NONE);
	TestEqual(TEXT("All nine units are under orders"), Commands->GetCommandedUnitCount(), 9);
	TSet<FIntVector> SlotKeys;
	bool bAllFlagged = true;
	bool bAllMatched = true;
	bool bAllSlowSpeed = true;
	for (const FMassUnitHandle& Unit : Squad)
	{
		FVector Slot;
		Commands->GetUnitOrderSlot(Unit, Slot);
		SlotKeys.Add(FIntVector(FMath::RoundToInt(Slot.X), FMath::RoundToInt(Slot.Y), 0));
		bAllFlagged &= Fragment(Unit)->bCommanded && Commands->GetUnitOrder(Unit) == EMassUnitOrder::Move;
		bAllMatched &= TargetOf(Unit)->bHasTargetLocation && FVector::Dist2D(TargetOf(Unit)->TargetLocation, Slot) < 1.0f;
		bAllSlowSpeed &= FMath::IsNearlyEqual(State(Unit)->MoveSpeed, 200.0f);
	}
	TestTrue(TEXT("Every unit is flagged bCommanded and on a Move order"), bAllFlagged);
	TestEqual(TEXT("Every unit has its own slot"), SlotKeys.Num(), 9);
	TestTrue(TEXT("Every unit is sent to its slot"), bAllMatched);
	TestTrue(TEXT("The formation moves at the slowest unit's speed"), bAllSlowSpeed);

	// The crowd no longer drives commanded units: a forced decision leaves their destinations alone.
	CrowdSystem->ForceCrowdGroupUpdate(Group);
	bool bCrowdLeftThemAlone = true;
	for (const FMassUnitHandle& Unit : Squad)
	{
		FVector Slot;
		Commands->GetUnitOrderSlot(Unit, Slot);
		bCrowdLeftThemAlone &= TargetOf(Unit)->bHasTargetLocation && FVector::Dist2D(TargetOf(Unit)->TargetLocation, Slot) < 1.0f;
	}
	TestTrue(TEXT("A forced crowd update does not re-route commanded units"), bCrowdLeftThemAlone);

	// The pile planner and manual stacks never take a commanded unit.
	if (GetDefault<UMassUnitSystemSettings>()->bEnableStacking)
	{
		TestFalse(TEXT("A commanded unit cannot be stacked"), Stacks->StackUnitOn(Squad[0], Squad[1]));
	}

	// Arrive: everyone on its slot -> the order finishes, units go Idle and stay under orders.
	for (const FMassUnitHandle& Unit : Squad)
	{
		FVector Slot;
		Commands->GetUnitOrderSlot(Unit, Slot);
		Teleport(Unit, Slot);
	}
	Tick(0.5f);
	FMassUnitCommandStats Stats = Commands->GetCommandStats();
	TestEqual(TEXT("All nine arrived"), Stats.TotalArrivals, 9);
	TestEqual(TEXT("No move order is left running"), Stats.ActiveOrders, 0);
	TestEqual(TEXT("Arrived units are Idle under orders"), Commands->GetUnitOrder(Squad[0]), EMassUnitOrder::Idle);
	TestFalse(TEXT("Arrived units stop (no movement target)"), TargetOf(Squad[0])->HasTarget());

	// --- Supersede, death, stuck ------------------------------------------------------------------
	const TArray<FMassUnitHandle> FirstFour = { Squad[0], Squad[1], Squad[2], Squad[3] };
	Move.Destination = FVector(-2000.0f, 0.0f, 100.0f);
	const int32 OrderB = Commands->IssueMoveOrder(FirstFour, Move);
	const TArray<FMassUnitHandle> TwoOfThem = { Squad[0], Squad[1] };
	Move.Destination = FVector(0.0f, 2500.0f, 100.0f);
	const int32 OrderC = Commands->IssueMoveOrder(TwoOfThem, Move);
	TestTrue(TEXT("Both orders are issued"), OrderB != INDEX_NONE && OrderC != INDEX_NONE && OrderB != OrderC);
	TestEqual(TEXT("The newer order owns the units it took"), Commands->GetUnitOrderId(Squad[0]), OrderC);
	TestEqual(TEXT("The older order keeps the rest"), Commands->GetUnitOrderId(Squad[2]), OrderB);
	Commands->IssueStopOrder({ Squad[2], Squad[3] });
	TestEqual(TEXT("Stopping the rest finishes the older order"), Commands->GetCommandStats().ActiveOrders, 1);
	TestEqual(TEXT("Stopped units are Idle"), Commands->GetUnitOrder(Squad[2]), EMassUnitOrder::Idle);

	UnitManager->DestroyUnit(Squad[0]);
	Tick(0.5f);
	TestEqual(TEXT("A destroyed unit counts as a failure"), Commands->GetCommandStats().TotalFailures, 1);
	TestFalse(TEXT("A destroyed unit leaves the table"), Commands->IsUnitCommanded(Squad[0]));

	// Squad[1] never moves (the test world runs no movement): it repaths, then gives up and stops.
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	Tick(Settings->CommandStuckTime * (Settings->MaxCommandRepaths + 2) + 1.0f);
	Stats = Commands->GetCommandStats();
	TestEqual(TEXT("A unit that never gets closer fails after its repaths"), Stats.TotalFailures, 2);
	TestEqual(TEXT("...and stands Idle"), Commands->GetUnitOrder(Squad[1]), EMassUnitOrder::Idle);
	TestEqual(TEXT("Its order is over"), Stats.ActiveOrders, 0);

	// --- Review fixes: airborne re-orders, speed restore after joining a group mid-order -----------
	{
		Move.Destination = FVector(0.0f, -2500.0f, 100.0f);
		const int32 OrderD = Commands->IssueMoveOrder({ Squad[7], Squad[8], Squad[4] }, Move);
		TestEqual(TEXT("Order D matches the slow unit's speed"), State(Squad[8])->MoveSpeed, 200.0f);
		TestTrue(TEXT("Order D is issued"), OrderD != INDEX_NONE);
		if (Settings->bEnableUnitLaunch && UnitManager->LaunchUnit(Squad[7], FVector(0.0f, 0.0f, 600.0f)))
		{
			TestTrue(TEXT("The launched unit is airborne"), UnitManager->IsUnitAirborne(Squad[7]));
			TestEqual(TEXT("Stop reaches a commanded unit in mid-air"), Commands->IssueStopOrder({ Squad[7] }), 1);
			TestEqual(TEXT("...which is Idle now (it will not walk on after landing)"), Commands->GetUnitOrder(Squad[7]), EMassUnitOrder::Idle);
		}
		else
		{
			AddInfo(TEXT("Unit launches are off in this project's settings; the airborne re-order check is skipped."));
			Commands->IssueStopOrder({ Squad[7] });
		}
		// Joining a group while under orders makes the group read the order's speed as the unit's base.
		const int32 SecondGroup = CrowdSystem->RegisterCrowdGroup({ Squad[8] }, FVector(0.0f, -2500.0f, 100.0f), Wander, false, 25.0f);
		TestTrue(TEXT("A group can be registered around a commanded unit"), SecondGroup != INDEX_NONE);
		TestTrue(TEXT("Registering a group does not drop the unit's orders"), Fragment(Squad[8])->bCommanded && Commands->IsUnitCommanded(Squad[8]));
		Commands->ReleaseUnits({ Squad[8] });
		TestEqual(TEXT("Release restores the unit's own speed, not the matched order speed"), State(Squad[8])->MoveSpeed, 300.0f);
		TestEqual(TEXT("...and its group's base speed"), Fragment(Squad[8])->BaseMoveSpeed, 300.0f);
		Commands->IssueStopOrder({ Squad[8], Squad[4] });
	}

	// --- Hold and release -------------------------------------------------------------------------
	TestEqual(TEXT("Hold reaches the living units"), Commands->IssueHoldOrder({ Squad[5], Squad[6] }), 2);
	TestEqual(TEXT("Held units report Hold"), Commands->GetUnitOrder(Squad[5]), EMassUnitOrder::Hold);
	TestEqual(TEXT("Hold counts in the stats"), Commands->GetCommandStats().HoldingUnits, 2);

	const int32 Released = Commands->ReleaseAllUnits();
	TestEqual(TEXT("Every living commanded unit is released"), Released, 8);
	TestEqual(TEXT("Nobody is under orders"), Commands->GetCommandedUnitCount(), 0);
	TestFalse(TEXT("Released units lose bCommanded"), Fragment(Squad[5])->bCommanded);
	TestEqual(TEXT("Released units get their own speed back"), State(Squad[5])->MoveSpeed, 300.0f);
	TestEqual(TEXT("A released slow unit gets its own speed back"), State(Squad[4])->MoveSpeed, 200.0f);
	CrowdSystem->ForceCrowdGroupUpdate(Group);
	TestTrue(TEXT("The crowd drives released units again (a new wander destination)"), TargetOf(Squad[5])->HasTarget() || EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(Squad[5].EntityHandle.ToMassEntityHandle())->bPathValid);

	TestEqual(TEXT("An empty selection issues nothing"), Commands->IssueMoveOrder({}, Move), INDEX_NONE);
	return true;
}

#endif
