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
#include "Gameplay/MassUnitAttitudeProfile.h"
#include "Gameplay/MassUnitCommandSystem.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "Gameplay/MassUnitStackSystem.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Attitudes (UMassUnitAttitudeProfile): rolls are deterministic per seed byte and stay in range, linked rolls give
 * coherent temperaments, courage and climb chance roll as configured; spawned units take the template's profile on
 * top of the template's values, units without a profile keep the template's values exactly (even a cooldown shorter than
 * the impact delay), re-assigning updates a grouped unit's crowd base speed, and the pile planner never makes an
 * unwilling unit climb.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitAttitudeTest,
	"MassUnitSystem.Units.Attitudes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitAttitudeTest::RunTest(const FString& Parameters)
{
	UMassUnitAttitudeProfile* Frantic = NewObject<UMassUnitAttitudeProfile>(GetTransientPackage());
	Frantic->MoveSpeedRange = FVector2D(0.8, 1.4);
	Frantic->AttackCooldownRange = FVector2D(0.6, 1.2);
	Frantic->AnimationRateRange = FVector2D(0.9, 1.5);
	Frantic->bLinkRolls = true;
	Frantic->bRollCourage = true;
	Frantic->CourageRange = FVector2D(0.2, 0.6);
	Frantic->ClimbChance = 0.5f;

	// --- Rolls -------------------------------------------------------------------------------------
	bool bInRange = true;
	bool bCoherent = true;
	int32 Climbers = 0;
	for (int32 SeedByte = 0; SeedByte <= 255; ++SeedByte)
	{
		const FMassUnitRolledAttitude A = Frantic->RollAttitude(SeedByte / 255.0f);
		bInRange &= A.MoveSpeedMultiplier >= 0.8f - KINDA_SMALL_NUMBER && A.MoveSpeedMultiplier <= 1.4f + KINDA_SMALL_NUMBER;
		bInRange &= A.AttackCooldownMultiplier >= 0.6f - KINDA_SMALL_NUMBER && A.AttackCooldownMultiplier <= 1.2f + KINDA_SMALL_NUMBER;
		bInRange &= A.AnimationRate >= 0.9f - KINDA_SMALL_NUMBER && A.AnimationRate <= 1.5f + KINDA_SMALL_NUMBER;
		bInRange &= A.Courage >= 0.2f - KINDA_SMALL_NUMBER && A.Courage <= 0.6f + KINDA_SMALL_NUMBER;
		// Linked: the speed and tempo alphas are equal and the cooldown alpha is their mirror.
		const float SpeedAlpha = (A.MoveSpeedMultiplier - 0.8f) / 0.6f;
		const float TempoAlpha = (A.AnimationRate - 0.9f) / 0.6f;
		const float CooldownAlpha = (A.AttackCooldownMultiplier - 0.6f) / 0.6f;
		bCoherent &= FMath::IsNearlyEqual(SpeedAlpha, TempoAlpha, 0.01f) && FMath::IsNearlyEqual(SpeedAlpha, 1.0f - CooldownAlpha, 0.01f);
		Climbers += A.bWillClimb ? 1 : 0;
	}
	TestTrue(TEXT("Every rolled value stays inside its range"), bInRange);
	TestTrue(TEXT("Linked rolls: fast units have a quick tempo and attack more often"), bCoherent);
	TestTrue(TEXT("Climb chance 0.5 gives a mix of climbers and holders"), Climbers > 64 && Climbers < 192);
	{
		const FMassUnitRolledAttitude A = Frantic->RollAttitude(0.3f);
		const FMassUnitRolledAttitude B = Frantic->RollAttitude(0.3f + 0.0015f);
		TestTrue(TEXT("The same seed byte rolls the same attitude"), FMath::IsNearlyEqual(A.MoveSpeedMultiplier, B.MoveSpeedMultiplier) && A.bWillClimb == B.bWillClimb);
	}
	UMassUnitAttitudeProfile* Calm = NewObject<UMassUnitAttitudeProfile>(GetTransientPackage());
	TestEqual(TEXT("Courage off rolls 'random rout order'"), Calm->RollAttitude(0.5f).Courage, -1.0f);
	TestTrue(TEXT("Climb chance 1 always climbs"), Calm->RollAttitude(0.77f).bWillClimb);

	// --- Spawned units -----------------------------------------------------------------------------
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
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager) || !TestNotNull(TEXT("Crowd system"), CrowdSystem) || !TestNotNull(TEXT("Stack system"), Stacks))
	{
		return false;
	}
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();
	auto State = [&EntityManager](const FMassUnitHandle& Unit)
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};
	auto Visual = [&EntityManager](const FMassUnitHandle& Unit)
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};

	UUnitTemplate* Plain = NewObject<UUnitTemplate>(GetTransientPackage());
	Plain->MoveSpeed = 300.0f;
	Plain->AttackCooldown = 0.1f;
	Plain->AttackImpactDelay = 0.3f;   // odd on purpose: without a profile it must be left exactly as authored
	const FMassUnitHandle PlainUnit = UnitManager->CreateUnitFromTemplate(Plain, FTransform(FVector(0.0f, 0.0f, 100.0f)));
	TestEqual(TEXT("No profile: template speed as is"), State(PlainUnit)->MoveSpeed, 300.0f);
	TestEqual(TEXT("No profile: template cooldown as is"), State(PlainUnit)->AttackCooldown, 0.1f);
	TestEqual(TEXT("No profile: normal tempo"), Visual(PlainUnit)->AnimationRate, 1.0f);
	TestEqual(TEXT("No profile: random rout order"), State(PlainUnit)->Courage, -1.0f);
	TestFalse(TEXT("No profile: willing to climb"), State(PlainUnit)->bAvoidsClimbing);

	UUnitTemplate* Swarmer = NewObject<UUnitTemplate>(GetTransientPackage());
	Swarmer->MoveSpeed = 300.0f;
	Swarmer->AttackCooldown = 1.0f;
	Swarmer->AttackImpactDelay = 0.2f;
	Swarmer->AttitudeProfile = Frantic;
	TArray<FMassUnitHandle> Units;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Units.Add(UnitManager->CreateUnitFromTemplate(Swarmer, FTransform(FVector(Index * 120.0f, 500.0f, 100.0f))));
	}
	bool bApplied = true;
	TSet<int32> Speeds;
	for (const FMassUnitHandle& Unit : Units)
	{
		FMassUnitRolledAttitude Expected;
		UnitManager->GetUnitAttitude(Unit, Expected);
		bApplied &= FMath::IsNearlyEqual(State(Unit)->MoveSpeed, 300.0f * Expected.MoveSpeedMultiplier, 0.01f)
			&& FMath::IsNearlyEqual(State(Unit)->AttackCooldown, FMath::Max(1.0f * Expected.AttackCooldownMultiplier, 0.25f), 0.01f)
			&& FMath::IsNearlyEqual(Visual(Unit)->AnimationRate, Expected.AnimationRate, 0.001f)
			&& FMath::IsNearlyEqual(State(Unit)->Courage, Expected.Courage, 0.001f)
			&& State(Unit)->bAvoidsClimbing == !Expected.bWillClimb;
		Speeds.Add(FMath::RoundToInt(State(Unit)->MoveSpeed));
	}
	TestTrue(TEXT("The template's profile is applied on top of the template's values"), bApplied);
	TestTrue(TEXT("Units of one template move at different paces"), Speeds.Num() > 1);
	TestEqual(TEXT("The unit knows its profile"), UnitManager->GetUnitAttitudeProfile(Units[0]), Frantic);

	// Batch override and clear.
	FMassUnitBatchSpawnOptions Options;
	Options.AttitudeProfile = Calm;
	const TArray<FMassUnitHandle> Batch = UnitManager->CreateUnitsFromTemplate(Swarmer, { FTransform(FVector(0.0f, 900.0f, 100.0f)) }, Options);
	if (TestEqual(TEXT("The batch spawned"), Batch.Num(), 1))
	{
		TestEqual(TEXT("A batch attitude override replaces the template's profile"), State(Batch[0])->MoveSpeed, 300.0f);
	}

	// Re-assigning updates a grouped unit's crowd base speed.
	FMassUnitCrowdConfig Wander;
	Wander.MaxSimulationDistance = 0.0f;
	Wander.bEnableInteractions = false;
	CrowdSystem->RegisterCrowdGroup({ Units[0] }, FVector(0.0f, 500.0f, 100.0f), Wander, false, 25.0f);
	UnitManager->SetUnitAttitudeProfile(Units[0], nullptr);
	const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Units[0].EntityHandle.ToMassEntityHandle());
	TestEqual(TEXT("Clearing the profile restores the template speed"), State(Units[0])->MoveSpeed, 300.0f);
	TestTrue(TEXT("...and the crowd's base speed follows"), Crowd && FMath::IsNearlyEqual(Crowd->BaseMoveSpeed, 300.0f));

	// --- Commanded units: the order keeps its speed, release restores the new temperament -----------
	{
		UMassUnitCommandSystem* Commands = UnitSubsystem->GetCommandSystem();
		UMassUnitAttitudeProfile* Quick = NewObject<UMassUnitAttitudeProfile>(GetTransientPackage());
		Quick->MoveSpeedRange = FVector2D(1.5, 1.5);
		FMassUnitMoveOrder Move;
		Move.Destination = FVector(0.0f, 3000.0f, 100.0f);
		Move.bUseNavigation = false;
		Move.bMatchSpeed = false;
		const FMassUnitHandle Marcher = UnitManager->CreateUnitFromTemplate(Plain, FTransform(FVector(0.0f, 1500.0f, 100.0f)));
		if (Commands && Commands->IssueMoveOrder({ Marcher }, Move) != INDEX_NONE)
		{
			UnitManager->SetUnitAttitudeProfile(Marcher, Quick);
			TestEqual(TEXT("Mid-order, the order keeps the unit's live speed"), State(Marcher)->MoveSpeed, 300.0f);
			Commands->ReleaseUnits({ Marcher });
			TestEqual(TEXT("Release restores the new temperament's speed"), State(Marcher)->MoveSpeed, 450.0f);
		}
	}

	// --- Piles: unwilling units never climb --------------------------------------------------------
	if (GetDefault<UMassUnitSystemSettings>()->bEnableStacking)
	{
		UMassUnitAttitudeProfile* Grounded = NewObject<UMassUnitAttitudeProfile>(GetTransientPackage());
		Grounded->ClimbChance = 0.0f;
		UUnitTemplate* PileBiter = NewObject<UUnitTemplate>(GetTransientPackage());
		PileBiter->Stacking.bCanStack = true;
		PileBiter->Stacking.PileRadius = 400.0f;
		PileBiter->Stacking.PileMinUnits = 8;
		PileBiter->Stacking.SupportReach = 130.0f;
		auto SpawnRing = [&](const FVector& Centre, UMassUnitAttitudeProfile* Attitude)
		{
			TArray<FMassUnitEntityHandle> Ring;
			for (int32 Index = 0; Index < 12; ++Index)
			{
				const float Radius = Index < 4 ? 120.0f : 220.0f;
				const float Angle = 2.0f * PI * static_cast<float>(Index % 8) / 8.0f + (Index < 4 ? 0.3f : 0.0f);
				const FMassUnitHandle Unit = UnitManager->CreateUnitFromTemplate(PileBiter,
					FTransform(Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius));
				UnitManager->SetUnitAttitudeProfile(Unit, Attitude);
				Ring.Add(Unit.EntityHandle);
			}
			return Ring;
		};
		const FVector GroundedCentre(8000.0f, 0.0f, 100.0f);
		TestEqual(TEXT("A crowd of units unwilling to climb never piles"),
			Stacks->PlanPile(SpawnRing(GroundedCentre, Grounded), GroundedCentre, nullptr, 901), 0);
		const FVector WillingCentre(12000.0f, 0.0f, 100.0f);
		TestTrue(TEXT("The same crowd willing to climb piles"),
			Stacks->PlanPile(SpawnRing(WillingCentre, nullptr), WillingCentre, nullptr, 902) > 0);
	}
	return true;
}

#endif
