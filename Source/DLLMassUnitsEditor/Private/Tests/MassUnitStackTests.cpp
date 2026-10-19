// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "Gameplay/MassUnitStackSystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Stacking (UMassUnitStackSystem): opt-in per template, a climber climbs to its support one StackHeight up and the
 * movement processor's flag is set, rider and level caps hold, a dead support collapses the units on it in a cascade
 * (each fall lands on the pile's ground height), release sets a unit down, and the pile planner starts climbers only in
 * a crowd, outer units onto supports nearer the target.
 *
 * The test world never ticks, so the stack table and launches are driven by calling TickStacks / TickLaunches.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitStackTest,
	"DLLMassUnits.Units.Stacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitStackTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UMassUnitSubsystem* UnitSubsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	UMassUnitStackSystem* Stacks = UnitSubsystem ? UnitSubsystem->GetStackSystem() : nullptr;
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager) || !TestNotNull(TEXT("Stack system is available"), Stacks))
	{
		return false;
	}
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (!Settings->bEnableStacking)
	{
		AddInfo(TEXT("bEnableStacking is off in this project's settings; the stack checks are skipped."));
		return true;
	}
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();
	auto GetLocation = [&EntityManager](const FMassUnitHandle& Unit) -> FVector
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Unit.EntityHandle.ToMassEntityHandle())->GetTransform().GetLocation();
	};
	auto IsDriven = [&EntityManager](const FMassUnitHandle& Unit) -> bool
	{
		const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit.EntityHandle.ToMassEntityHandle());
		return Crowd && Crowd->bStackDriven;
	};
	auto TickStacks = [Stacks](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Stacks->TickStacks(1.0f / 60.0f);
		}
	};

	UUnitTemplate* Plain = NewObject<UUnitTemplate>(GetTransientPackage());
	UUnitTemplate* Biter = NewObject<UUnitTemplate>(GetTransientPackage());
	Biter->Stacking.bCanStack = true;
	Biter->Stacking.StackHeight = 45.0f;
	Biter->Stacking.ClimbSpeed = 400.0f;
	Biter->Stacking.MaxStackLevel = 2;
	Biter->Stacking.StackForwardOffset = 0.0f;
	UUnitTemplate* Slippery = NewObject<UUnitTemplate>(GetTransientPackage());
	Slippery->Stacking.bCanBeClimbed = false;

	const float GroundZ = 100.0f;
	const FMassUnitHandle Base = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(0.0f, 0.0f, GroundZ)));
	const FMassUnitHandle First = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(-80.0f, 0.0f, GroundZ)));
	const FMassUnitHandle Second = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(-160.0f, 0.0f, GroundZ)));
	const FMassUnitHandle Third = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(-240.0f, 0.0f, GroundZ)));
	const FMassUnitHandle Walker = UnitManager->CreateUnitFromTemplate(Plain, FTransform(FVector(0.0f, 300.0f, GroundZ)));
	const FMassUnitHandle Wall = UnitManager->CreateUnitFromTemplate(Slippery, FTransform(FVector(0.0f, -300.0f, GroundZ)));
	if (!TestTrue(TEXT("Units spawned"), Base.IsValid() && First.IsValid() && Second.IsValid() && Third.IsValid() && Walker.IsValid() && Wall.IsValid()))
	{
		return false;
	}

	// --- Puppet presentation (C4): a role on a unit that is NOT in the table ------------------------
	TestTrue(TEXT("A puppet role can be applied"), Stacks->ApplyPresentationStackRole(Walker, EMassUnitStackRole::Climbing));
	TestEqual(TEXT("and reads back"), Stacks->GetPresentationStackRole(Walker), EMassUnitStackRole::Climbing);
	TestFalse(TEXT("Presentation only: the puppet is not in the stack table"), Stacks->IsUnitStacked(Walker));
	TestFalse(TEXT("and is not stack-driven (the movement processor still moves it)"), IsDriven(Walker));
	Stacks->ApplyPresentationStackRole(Walker, EMassUnitStackRole::None);
	TestEqual(TEXT("Clearing the role"), Stacks->GetPresentationStackRole(Walker), EMassUnitStackRole::None);

	// --- Opt-in -----------------------------------------------------------------------------------
	TestFalse(TEXT("A template without bCanStack does not climb"), Stacks->StackUnitOn(Walker, Base));
	TestFalse(TEXT("A support whose template forbids climbing is refused"), Stacks->StackUnitOn(First, Wall));
	TestFalse(TEXT("A unit cannot stand on itself"), Stacks->StackUnitOn(First, First));
	TestEqual(TEXT("Refusals leave the table empty"), Stacks->GetStackedUnitCount(), 0);

	// --- Climb ------------------------------------------------------------------------------------
	TestTrue(TEXT("A stack-capable unit climbs onto a ground unit"), Stacks->StackUnitOn(First, Base));
	TestEqual(TEXT("The climber is level 1"), Stacks->GetUnitStackLevel(First), 1);
	TestEqual(TEXT("and climbing"), Stacks->GetUnitStackRole(First), EMassUnitStackRole::Climbing);
	TestEqual(TEXT("The ground unit holds it up"), Stacks->GetUnitStackRole(Base), EMassUnitStackRole::Holding);
	TestTrue(TEXT("The climber's support is the ground unit"), Stacks->GetUnitStackSupport(First) == Base);
	TestTrue(TEXT("The movement processor skips the climber"), IsDriven(First));
	TestTrue(TEXT("and the frozen support"), IsDriven(Base));
	TestTrue(TEXT("The climber carries the Stacked tag"), UnitManager->UnitHasTag(First, UE::MassUnitSystem::Tags::Stacked()));
	TestFalse(TEXT("MaxRiders 1: a second unit cannot stand on the same support"), Stacks->StackUnitOn(Second, Base));

	TickStacks(60);
	const FVector OnTop = GetLocation(First);
	TestEqual(TEXT("The climber arrives"), Stacks->GetUnitStackRole(First), EMassUnitStackRole::Standing);
	TestTrue(TEXT("one StackHeight above its support"), FMath::IsNearlyEqual(OnTop.Z, GroundZ + 45.0f, 1.0f));
	TestTrue(TEXT("over its support"), FVector::Dist2D(OnTop, GetLocation(Base)) < 2.0f);

	TestTrue(TEXT("A unit climbs onto a climber (level 2)"), Stacks->StackUnitOn(Second, First));
	TestEqual(TEXT("level 2"), Stacks->GetUnitStackLevel(Second), 2);
	TestFalse(TEXT("MaxStackLevel 2: nobody climbs onto level 2"), Stacks->StackUnitOn(Third, Second));
	TickStacks(60);
	TestTrue(TEXT("Level 2 stands two StackHeights up"), FMath::IsNearlyEqual(GetLocation(Second).Z, GroundZ + 90.0f, 1.5f));

	FMassUnitStackStats Stats = Stacks->GetStackStats();
	TestEqual(TEXT("Stats: three units in the table"), Stats.StackedUnits, 3);
	TestEqual(TEXT("two climbers"), Stats.Climbers, 2);
	TestEqual(TEXT("one ground support"), Stats.GroundSupports, 1);
	TestEqual(TEXT("top level 2"), Stats.HighestLevel, 2);

	// --- Collapse cascade -------------------------------------------------------------------------
	UnitManager->ApplyDamage(Base, 100000.0f);
	TickStacks(1);
	if (Settings->SupportCorpseHoldSeconds > 0.0f)
	{
		TestFalse(TEXT("A support killed in place holds its rider a moment"), UnitManager->IsUnitAirborne(First));
		TestTrue(TEXT("its corpse is still in the table"), Stacks->IsUnitStacked(Base));
	}
	for (int32 Frame = 0; Frame < 600 && !UnitManager->IsUnitAirborne(First); ++Frame)
	{
		TickStacks(1);
	}
	TestTrue(TEXT("The support died: level 1 falls off"), UnitManager->IsUnitAirborne(First));
	TestFalse(TEXT("and leaves the table"), Stacks->IsUnitStacked(First));
	TestFalse(TEXT("and the dead support leaves too"), Stacks->IsUnitStacked(Base));
	TestFalse(TEXT("The fallen climber is no longer stack-driven"), IsDriven(First));
	TickStacks(1);
	TestTrue(TEXT("Cascade: level 2 falls the next tick"), UnitManager->IsUnitAirborne(Second));
	TestEqual(TEXT("The table is empty"), Stacks->GetStackedUnitCount(), 0);
	TestEqual(TEXT("Two collapses counted"), Stacks->GetStackStats().TotalCollapses, 2);
	for (int32 Step = 0; Step < 600 && (UnitManager->IsUnitAirborne(First) || UnitManager->IsUnitAirborne(Second)); ++Step)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
	}
	TestFalse(TEXT("Both falls land"), UnitManager->IsUnitAirborne(First) || UnitManager->IsUnitAirborne(Second));
	TestTrue(TEXT("Level 1 lands on the pile's ground height, not mid-air"), FMath::IsNearlyEqual(GetLocation(First).Z, GroundZ, 1.0f));
	TestTrue(TEXT("Level 2 too"), FMath::IsNearlyEqual(GetLocation(Second).Z, GroundZ, 1.0f));

	// --- Release sets a unit down; a reset crowd fragment drops it -------------------------------
	const FMassUnitHandle Base2 = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(1000.0f, 0.0f, GroundZ)));
	const FMassUnitHandle Rider2 = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(920.0f, 0.0f, GroundZ)));
	TestTrue(TEXT("Second stack"), Stacks->StackUnitOn(Rider2, Base2));
	TickStacks(60);
	TestTrue(TEXT("Release without a fall"), Stacks->ReleaseStackedUnit(Rider2, false));
	TestTrue(TEXT("sets it straight down"), FMath::IsNearlyEqual(GetLocation(Rider2).Z, GroundZ, 0.5f));
	TestFalse(TEXT("without a launch"), UnitManager->IsUnitAirborne(Rider2));
	TickStacks(1);
	TestFalse(TEXT("A support with no riders goes back to the crowd"), Stacks->IsUnitStacked(Base2));

	TestTrue(TEXT("Third stack"), Stacks->StackUnitOn(Rider2, Base2));
	EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Rider2.EntityHandle.ToMassEntityHandle())->Reset();
	TickStacks(1);
	TestFalse(TEXT("A unit whose crowd fragment was reset (group unregistered) leaves the table"), Stacks->IsUnitStacked(Rider2));

	// --- A support recycled as a new ground entry is not its old riders' support ------------------
	{
		const FMassUnitHandle Ground = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(2000.0f, 0.0f, GroundZ)));
		const FMassUnitHandle Middle = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(1920.0f, 0.0f, GroundZ)));
		const FMassUnitHandle Top = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(1840.0f, 0.0f, GroundZ)));
		const FMassUnitHandle Newcomer = UnitManager->CreateUnitFromTemplate(Biter, FTransform(FVector(2080.0f, 0.0f, GroundZ)));
		TestTrue(TEXT("Recycle setup: Middle on Ground"), Stacks->StackUnitOn(Middle, Ground));
		TestTrue(TEXT("Recycle setup: Top on Middle"), Stacks->StackUnitOn(Top, Middle));
		TickStacks(60);
		TestTrue(TEXT("Middle is set down"), Stacks->ReleaseStackedUnit(Middle, false));
		TestTrue(TEXT("and becomes a fresh ground support before Top notices"), Stacks->StackUnitOn(Newcomer, Middle));
		TickStacks(1);
		TestFalse(TEXT("Top does not keep standing on the recycled entry"), Stacks->IsUnitStacked(Top));
		TestTrue(TEXT("The newcomer keeps its support"), Stacks->GetUnitStackSupport(Newcomer) == Middle);
		TickStacks(5);
		TestTrue(TEXT("Top's departure did not release the newcomer's support"), Stacks->IsUnitStacked(Newcomer) && Stacks->IsUnitStacked(Middle));
		Stacks->ReleaseStackedUnit(Newcomer, false);
		TickStacks(2);
	}

	// --- Pile planner ----------------------------------------------------------------------------
	UUnitTemplate* PileBiter = NewObject<UUnitTemplate>(GetTransientPackage());
	PileBiter->Stacking.bCanStack = true;
	PileBiter->Stacking.PileRadius = 400.0f;
	PileBiter->Stacking.PileMinUnits = 8;
	PileBiter->Stacking.SupportReach = 130.0f;
	const FVector PileCentre(5000.0f, 0.0f, GroundZ);
	TArray<FMassUnitEntityHandle> Ring;
	auto SpawnRing = [&](int32 Count, float Radius)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = 2.0f * PI * static_cast<float>(Index) / static_cast<float>(Count);
			const FMassUnitHandle Unit = UnitManager->CreateUnitFromTemplate(PileBiter,
				FTransform(PileCentre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius));
			Ring.Add(Unit.EntityHandle);
		}
	};
	SpawnRing(4, 120.0f);
	TestEqual(TEXT("Below PileMinUnits nothing climbs"), Stacks->PlanPile(Ring, PileCentre, nullptr, 77), 0);
	SpawnRing(8, 220.0f);
	const int32 Started = Stacks->PlanPile(Ring, PileCentre, nullptr, 77);
	TestTrue(TEXT("A crowd around the target starts climbers"), Started > 0);
	TestTrue(TEXT("no more than MaxClimbersPerPilePass per plan"), Started <= Settings->MaxClimbersPerPilePass);
	bool bSupportsNearer = true;
	for (const FMassUnitEntityHandle& Unit : Ring)
	{
		const FMassUnitHandle Handle(Unit);
		if (Stacks->GetUnitStackLevel(Handle) > 0)
		{
			const FMassUnitHandle Support = Stacks->GetUnitStackSupport(Handle);
			bSupportsNearer &= FVector::Dist2D(GetLocation(Support), PileCentre) < FVector::Dist2D(GetLocation(Handle), PileCentre);
		}
	}
	TestTrue(TEXT("Every climber stands on a unit nearer the target (the pile leans in)"), bSupportsNearer);
	TestEqual(TEXT("The pile counts as active"), Stacks->GetStackStats().ActivePiles, 1);
	TestEqual(TEXT("CollapseAllStacks throws every climber off"), Stacks->CollapseAllStacks(), Started);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
