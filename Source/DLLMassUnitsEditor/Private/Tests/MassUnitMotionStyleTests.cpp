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
 * Crowd motion styles (FMassUnitCrowdConfig::MotionStyle): Flock units head for one shared goal and get alignment +
 * cohesion steering, Swarm units orbit their goal inside Swarm Radius at different points, Hive units stay home or
 * forage by ForageChance, Wander groups have no motion goal (unchanged behaviour), and engaging a styled group drops
 * its ambient goal.
 *
 * The test world never ticks, so world time is constant: goals do not move, and each check reads the decision the
 * forced update (RegisterCrowdGroup / ForceCrowdGroupUpdate) just made.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitCrowdMotionStyleTest,
	"DLLMassUnits.Crowd.MotionStyles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitCrowdMotionStyleTest::RunTest(const FString& Parameters)
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
	if (!TestNotNull(TEXT("Unit manager"), UnitManager) || !TestNotNull(TEXT("Crowd system"), CrowdSystem))
	{
		return false;
	}
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();
	auto Destination = [&EntityManager](const FMassUnitHandle& Unit, bool& bOutHas) -> FVector
	{
		const FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Unit.EntityHandle.ToMassEntityHandle());
		bOutHas = Target && Target->bHasTargetLocation;
		return Target ? Target->TargetLocation : FVector::ZeroVector;
	};
	auto Crowd = [&EntityManager](const FMassUnitHandle& Unit) -> const FMassUnitCrowdFragment*
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->MoveSpeed = 300.0f;
	auto SpawnBlock = [&](const FVector& Origin, int32 Count)
	{
		TArray<FMassUnitHandle> Units;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Units.Add(UnitManager->CreateUnitFromTemplate(Template, FTransform(Origin + FVector((Index % 4) * 80.0f, (Index / 4) * 80.0f, 0.0f))));
		}
		return Units;
	};

	FMassUnitCrowdConfig Base;
	Base.WanderRadius = 1200.0f;
	Base.MaxSimulationDistance = 0.0f;
	Base.bEnableInteractions = false;
	Base.bEnableManagedSubgroups = false;

	// --- Wander: unchanged, no motion goal --------------------------------------------------------
	const FVector WanderCentre(0.0f, 0.0f, 100.0f);
	const int32 WanderGroup = CrowdSystem->RegisterCrowdGroup(SpawnBlock(WanderCentre, 8), WanderCentre, Base, false, 25.0f);
	FVector Goal;
	TestEqual(TEXT("Default style is Wander"), CrowdSystem->GetCrowdGroupMotionStyle(WanderGroup), EMassUnitCrowdMotionStyle::Wander);
	TestFalse(TEXT("A Wander group has no motion goal"), CrowdSystem->GetCrowdGroupMotionGoal(WanderGroup, Goal));

	// --- Flock ------------------------------------------------------------------------------------
	FMassUnitCrowdConfig FlockConfig = Base;
	FlockConfig.MotionStyle = EMassUnitCrowdMotionStyle::Flock;
	const FVector FlockCentre(10000.0f, 0.0f, 100.0f);
	const TArray<FMassUnitHandle> FlockUnits = SpawnBlock(FlockCentre, 12);
	const int32 FlockGroup = CrowdSystem->RegisterCrowdGroup(FlockUnits, FlockCentre, FlockConfig, false, 25.0f);
	TestTrue(TEXT("A Flock group has a goal after its first update"), CrowdSystem->GetCrowdGroupMotionGoal(FlockGroup, Goal));
	bool bAllToGoal = true;
	bool bAnySteering = false;
	for (const FMassUnitHandle& Unit : FlockUnits)
	{
		bool bHas = false;
		const FVector Dest = Destination(Unit, bHas);
		bAllToGoal &= bHas && FVector::Dist2D(Dest, Goal) < 1.0f;
		bAnySteering |= Crowd(Unit) && !Crowd(Unit)->SteeringDirection.IsNearlyZero();
	}
	TestTrue(TEXT("Every flock unit heads for the shared flock goal"), bAllToGoal);
	TestTrue(TEXT("Flock units get separation/alignment/cohesion steering"), bAnySteering);

	// --- Swarm ------------------------------------------------------------------------------------
	FMassUnitCrowdConfig SwarmConfig = Base;
	SwarmConfig.MotionStyle = EMassUnitCrowdMotionStyle::Swarm;
	SwarmConfig.Motion.SwarmRadius = 300.0f;
	SwarmConfig.Motion.SwarmJitter = 0.2f;
	const FVector SwarmCentre(20000.0f, 0.0f, 100.0f);
	const TArray<FMassUnitHandle> SwarmUnits = SpawnBlock(SwarmCentre, 12);
	const int32 SwarmGroup = CrowdSystem->RegisterCrowdGroup(SwarmUnits, SwarmCentre, SwarmConfig, false, 25.0f);
	TestTrue(TEXT("A Swarm group has a centre"), CrowdSystem->GetCrowdGroupMotionGoal(SwarmGroup, Goal));
	bool bInsideCloud = true;
	TSet<int32> DistinctPoints;
	for (const FMassUnitHandle& Unit : SwarmUnits)
	{
		bool bHas = false;
		const FVector Dest = Destination(Unit, bHas);
		bInsideCloud &= bHas && FVector::Dist2D(Dest, Goal) <= 300.0f * 1.2f + 1.0f;
		DistinctPoints.Add(FMath::RoundToInt(Dest.X) * 7 + FMath::RoundToInt(Dest.Y));
	}
	TestTrue(TEXT("Swarm units aim inside the cloud around the centre"), bInsideCloud);
	TestTrue(TEXT("at different points of it (they orbit, not stack)"), DistinctPoints.Num() >= SwarmUnits.Num() - 1);

	// --- Hive -------------------------------------------------------------------------------------
	FMassUnitCrowdConfig HiveConfig = Base;
	HiveConfig.MotionStyle = EMassUnitCrowdMotionStyle::Hive;
	HiveConfig.Motion.HiveRadius = 300.0f;
	HiveConfig.Motion.ForageChance = 0.0f;
	const FVector HomeCentre(30000.0f, 0.0f, 100.0f);
	const TArray<FMassUnitHandle> HomeUnits = SpawnBlock(HomeCentre, 8);
	CrowdSystem->RegisterCrowdGroup(HomeUnits, HomeCentre, HiveConfig, false, 25.0f);
	bool bAllHome = true;
	for (const FMassUnitHandle& Unit : HomeUnits)
	{
		bool bHas = false;
		const FVector Dest = Destination(Unit, bHas);
		bAllHome &= bHas && FVector::Dist2D(Dest, HomeCentre) <= 300.0f + 1.0f && Crowd(Unit)->MotionPhase == 0;
	}
	TestTrue(TEXT("ForageChance 0: every hive unit stays inside the hive radius"), bAllHome);

	HiveConfig.Motion.ForageChance = 1.0f;
	const FVector ForageCentre(40000.0f, 0.0f, 100.0f);
	const TArray<FMassUnitHandle> Foragers = SpawnBlock(ForageCentre, 8);
	CrowdSystem->RegisterCrowdGroup(Foragers, ForageCentre, HiveConfig, false, 25.0f);
	bool bAllOut = true;
	for (const FMassUnitHandle& Unit : Foragers)
	{
		bool bHas = false;
		const float Distance = FVector::Dist2D(Destination(Unit, bHas), ForageCentre);
		bAllOut &= bHas && Distance >= 300.0f * 1.5f - 1.0f && Distance <= 1200.0f + 1.0f && Crowd(Unit)->MotionPhase == 1;
	}
	TestTrue(TEXT("ForageChance 1: every unit forages between 1.5x hive radius and the wander radius"), bAllOut);

	// --- Engagement takes over --------------------------------------------------------------------
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* Intruder = World->SpawnActor<APawn>(APawn::StaticClass(), FlockCentre + FVector(400.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParameters);
	FMassUnitPlayerEngagementConfig Engagement;
	Engagement.ActivationMode = EMassUnitCrowdActivationMode::Manual;
	Engagement.bAutoDeactivate = false;
	Engagement.bEnableAttacks = false;
	CrowdSystem->ConfigureCrowdGroupEngagement(FlockGroup, Engagement, true);
	TestTrue(TEXT("The flock engages an intruder"), Intruder && CrowdSystem->ActivateCrowdGroupForActor(FlockGroup, Intruder));
	CrowdSystem->ForceCrowdGroupUpdate(FlockGroup);
	TestFalse(TEXT("An engaged flock drops its ambient goal (engagement steers it)"), CrowdSystem->GetCrowdGroupMotionGoal(FlockGroup, Goal));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
