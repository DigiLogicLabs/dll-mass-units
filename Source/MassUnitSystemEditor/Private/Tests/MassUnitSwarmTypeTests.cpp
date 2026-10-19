// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "GameFramework/DefaultPawn.h"
#include "Gameplay/MassUnitAttitudeProfile.h"
#include "Gameplay/MassUnitBehaviorPreset.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "Gameplay/MassUnitSwarmType.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Swarm types (UMassUnitSwarmType) and flyers (FMassUnitCrowdConfig altitude band): a ground type spawns one crowd group
 * with its preset and attitude; an Air type spawns inside its band, its group is Free 3D with a band whose ambient
 * destinations stay inside it, the per-unit floor is set, the same seed gives the same swarm, a killed flyer starts a
 * corpse fall and comes down to the floor when there is no ground, and ground groups are untouched by the band.
 * The test world never ticks: crowd decisions are forced and launches are driven by calling TickLaunches.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitSwarmTypeTest,
	"MassUnitSystem.Crowd.SwarmTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitSwarmTypeTest::RunTest(const FString& Parameters)
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
	auto UnitLocation = [UnitManager](const FMassUnitHandle& Unit)
	{
		FTransform Transform;
		UnitManager->GetUnitTransform(Unit, Transform);
		return Transform.GetLocation();
	};

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->MoveSpeed = 300.0f;

	UMassUnitBehaviorPreset* Preset = NewObject<UMassUnitBehaviorPreset>(GetTransientPackage());
	Preset->Crowd.MotionStyle = EMassUnitCrowdMotionStyle::Wander;
	Preset->Crowd.WanderRadius = 1200.0f;
	Preset->Crowd.MinWanderDistance = 0.0f;
	Preset->Crowd.MaxSimulationDistance = 0.0f;
	Preset->Crowd.bEnableInteractions = false;

	UMassUnitAttitudeProfile* Attitude = NewObject<UMassUnitAttitudeProfile>(GetTransientPackage());
	Attitude->MoveSpeedRange = FVector2D(0.8, 1.2);

	// ---- Ground type ----
	UMassUnitSwarmType* Biters = NewObject<UMassUnitSwarmType>(GetTransientPackage());
	Biters->Template = Template;
	Biters->BehaviorPreset = Preset;
	Biters->AttitudeProfile = Attitude;
	const FVector GroundCentre(0.0f, 0.0f, 100.0f);
	const FMassUnitSwarmSpawnResult Ground = UMassUnitSwarmType::SpawnSwarm(World, Biters, GroundCentre, 12, 500.0f, 7);
	TestEqual(TEXT("A ground swarm spawns the requested count"), Ground.Units.Num(), 12);
	if (!TestTrue(TEXT("...registered as one crowd group"), Ground.CrowdGroupHandle != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("...that reports the type's preset"), CrowdSystem->GetCrowdGroupPreset(Ground.CrowdGroupHandle), Preset);
	TestEqual(TEXT("...with every unit in it"), CrowdSystem->GetCrowdGroupUnitCount(Ground.CrowdGroupHandle), 12);
	TestEqual(TEXT("Units wear the type's attitude"), UnitManager->GetUnitAttitudeProfile(Ground.Units[0]), Attitude);
	float GroundZ = 0.0f, FloorZ = 0.0f, CeilingZ = 0.0f;
	TestFalse(TEXT("A ground group has no altitude band"), CrowdSystem->GetCrowdGroupAltitudeBand(Ground.CrowdGroupHandle, GroundZ, FloorZ, CeilingZ));
	const FMassUnitCrowdFragment* GroundCrowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Ground.Units[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("...and its units have no floor clamp or death fall"), GroundCrowd && !GroundCrowd->bAltitudeFloor && !GroundCrowd->bFallWhenKilled);
	TestEqual(TEXT("No template = nothing spawned"), UMassUnitSwarmType::SpawnSwarm(World, NewObject<UMassUnitSwarmType>(GetTransientPackage()), GroundCentre, 5).Units.Num(), 0);

	// ---- Air type ----
	UMassUnitSwarmType* Bats = NewObject<UMassUnitSwarmType>(GetTransientPackage());
	Bats->Template = Template;
	Bats->BehaviorPreset = Preset;
	Bats->Locomotion = EMassUnitLocomotion::Air;
	Bats->MinAltitude = 200.0f;
	Bats->MaxAltitude = 500.0f;
	const FMassUnitCrowdConfig AirConfig = Bats->MakeCrowdConfig();
	TestEqual(TEXT("An Air type flies Free 3D"), AirConfig.MovementMode, EMassUnitCrowdMovementMode::Free3D);
	TestTrue(TEXT("...with an altitude band"), AirConfig.bUseAltitudeBand);
	TestEqual(TEXT("...keeping the preset's motion style"), AirConfig.MotionStyle, Preset->Crowd.MotionStyle);
	TestEqual(TEXT("The preset itself is not changed"), Preset->Crowd.MovementMode, EMassUnitCrowdMovementMode::Planar2D);

	const FVector AirCentre(8000.0f, 0.0f, 0.0f);   // no geometry in the test world: the ground is the centre's Z
	const FMassUnitSwarmSpawnResult Air = UMassUnitSwarmType::SpawnSwarm(World, Bats, AirCentre, 20, 600.0f, 11);
	if (!TestEqual(TEXT("An air swarm spawns the requested count"), Air.Units.Num(), 20) || !TestTrue(TEXT("...as a group"), Air.CrowdGroupHandle != INDEX_NONE))
	{
		return false;
	}
	bool bAllSpawnedInBand = true;
	for (const FMassUnitHandle& Unit : Air.Units)
	{
		const float Z = UnitLocation(Unit).Z;
		bAllSpawnedInBand &= Z >= AirCentre.Z + 200.0f - 1.0f && Z <= AirCentre.Z + 500.0f + 1.0f;
	}
	TestTrue(TEXT("Flyers spawn inside their band"), bAllSpawnedInBand);
	TestTrue(TEXT("The air group reports its band"), CrowdSystem->GetCrowdGroupAltitudeBand(Air.CrowdGroupHandle, GroundZ, FloorZ, CeilingZ));
	TestEqual(TEXT("...ground = centre Z with nothing under it"), GroundZ, static_cast<float>(AirCentre.Z));
	TestEqual(TEXT("...ceiling = ground + Max Altitude"), CeilingZ, static_cast<float>(AirCentre.Z) + 500.0f);
	const FMassUnitCrowdFragment* AirCrowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Air.Units[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Flyers get the floor clamp"), AirCrowd && AirCrowd->bAltitudeFloor && FMath::IsNearlyEqual(AirCrowd->AltitudeFloorZ, FloorZ));
	TestTrue(TEXT("...and fall when killed"), AirCrowd && AirCrowd->bFallWhenKilled);

	// Ambient decisions: every destination handed out lies inside the band (a sphere around the centre would not).
	CrowdSystem->ForceCrowdGroupUpdate(Air.CrowdGroupHandle);
	int32 WithDestination = 0;
	bool bAllDestinationsInBand = true;
	for (const FMassUnitHandle& Unit : Air.Units)
	{
		const FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Unit.EntityHandle.ToMassEntityHandle());
		if (Target && Target->bHasTargetLocation)
		{
			++WithDestination;
			bAllDestinationsInBand &= Target->TargetLocation.Z >= AirCentre.Z + 200.0f - 1.0f && Target->TargetLocation.Z <= AirCentre.Z + 500.0f + 1.0f;
		}
	}
	TestTrue(TEXT("A forced update hands flyers destinations"), WithDestination > 0);
	TestTrue(TEXT("...all inside the altitude band"), bAllDestinationsInBand);

	// Same seed, same swarm.
	const FMassUnitSwarmSpawnResult Again = UMassUnitSwarmType::SpawnSwarm(World, Bats, AirCentre, 20, 600.0f, 11);
	TestTrue(TEXT("The same seed places the swarm the same way"), Again.Units.Num() == 20
		&& UnitLocation(Again.Units[3]).Equals(UnitLocation(Air.Units[3]), 0.1));

	// A killed flyer falls: a corpse launch starts on the next launch tick and ends on the floor (no ground here).
	const FMassUnitHandle Victim = Air.Units[0];
	const float DeathZ = UnitLocation(Victim).Z;
	UnitManager->ApplyDamage(Victim, 1000000.0f);
	TestFalse(TEXT("The fall is not started inside the damage call"), UnitManager->IsUnitAirborne(Victim));
	UnitManager->TickLaunches(1.0f / 60.0f);
	TestTrue(TEXT("A killed flyer is airborne on the next launch tick"), UnitManager->IsUnitAirborne(Victim));
	for (int32 Frame = 0; Frame < 600 && UnitManager->IsUnitAirborne(Victim); ++Frame)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
	}
	const float RestZ = UnitLocation(Victim).Z;
	TestTrue(FString::Printf(TEXT("...and comes down to the floor (died at %.0f, rests at %.0f, floor %.0f)"), DeathZ, RestZ, FloorZ),
		RestZ < DeathZ - 100.0f && FMath::Abs(RestZ - FloorZ) < 5.0f);

	// A flyer flung on its kill (the adopter's kill fling, a blast) keeps that flight: the queued fall must not replace it.
	const FMassUnitHandle Flung = Air.Units[1];
	const FVector FlungStart = UnitLocation(Flung);
	UnitManager->ApplyDamage(Flung, 1000000.0f);
	TestTrue(TEXT("A kill fling launches the dead flyer"), UnitManager->LaunchUnit(Flung, FVector(900.0f, 0.0f, 300.0f), nullptr, false));
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
	}
	TestTrue(FString::Printf(TEXT("...and keeps its fling through the death-fall drain (moved %.0f cm sideways)"), UnitLocation(Flung).X - FlungStart.X),
		UnitLocation(Flung).X - FlungStart.X > 200.0f);

	// A ground unit that dies does not fall (nothing queued).
	UnitManager->ApplyDamage(Ground.Units[0], 1000000.0f);
	UnitManager->TickLaunches(1.0f / 60.0f);
	TestFalse(TEXT("A killed ground unit is not launched"), UnitManager->IsUnitAirborne(Ground.Units[0]));

	// ---- Engaged flyers: hover or dive, no yo-yo ----
	// One token. The holder is in range; a second flyer hovering over the target (outside the 3D attack range) must keep
	// hovering while the pool is full, and a lone flyer in the same spot with a free token must dive.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Hive(20000.0f, 0.0f, 0.0f);
	const FVector Prey = Hive + FVector(0.0f, 0.0f, 100.0f);
	APawn* PreyPawn = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), Prey, FRotator::ZeroRotator, SpawnParameters);
	UUnitTemplate* Biter = NewObject<UUnitTemplate>(GetTransientPackage());
	Biter->AttackRange = 150.0f;
	Biter->AttackCooldown = 1.0f;
	FMassUnitCrowdConfig HoverConfig = AirConfig;
	HoverConfig.MaxSimulationDistance = 0.0f;
	HoverConfig.bEnableInteractions = false;
	FMassUnitPlayerEngagementConfig OneToken;
	OneToken.ActivationMode = EMassUnitCrowdActivationMode::Manual;
	OneToken.bAutoDeactivate = false;
	OneToken.bApplyActorDamage = false;
	OneToken.MaxSimultaneousAttackers = 1;
	const FVector OverPrey = Prey + FVector(100.0f, 0.0f, HoverConfig.EngagedHoverHeight);
	auto DestinationZ = [&EntityManager](const FMassUnitHandle& Unit)
	{
		const FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Unit.EntityHandle.ToMassEntityHandle());
		return Target && Target->bHasTargetLocation ? static_cast<float>(Target->TargetLocation.Z) : TNumericLimits<float>::Lowest();
	};

	const FMassUnitHandle Holder = UnitManager->CreateUnitFromTemplate(Biter, FTransform(Prey + FVector(60.0f, 0.0f, 0.0f)));
	const int32 Busy = CrowdSystem->RegisterCrowdGroup({Holder}, Hive, HoverConfig, false, 25.0f);
	CrowdSystem->ConfigureCrowdGroupEngagement(Busy, OneToken, true);
	TestTrue(TEXT("A flyer group engages"), CrowdSystem->ActivateCrowdGroupForActor(Busy, PreyPawn));
	CrowdSystem->ForceCrowdGroupUpdate(Busy);
	FMassUnitStateFragment HolderState;
	UnitManager->GetUnitState(Holder, HolderState);
	TestEqual(TEXT("...and the flyer in range takes the token"), HolderState.CurrentState, EMassUnitState::Attacking);
	const FMassUnitHandle Waiter = UnitManager->CreateUnitFromTemplate(Biter, FTransform(OverPrey));
	CrowdSystem->AddUnitsToCrowdGroup(Busy, {Waiter});
	for (int32 Visit = 0; Visit < 3; ++Visit)
	{
		CrowdSystem->ForceCrowdGroupUpdate(Busy);
		TestTrue(FString::Printf(TEXT("A flyer over the target keeps hovering while the pool is full (visit %d, destination Z %.0f)"), Visit, DestinationZ(Waiter)),
			DestinationZ(Waiter) >= Prey.Z + HoverConfig.EngagedHoverHeight - 1.0f);
	}

	const FMassUnitHandle Diver = UnitManager->CreateUnitFromTemplate(Biter, FTransform(OverPrey + FVector(0.0f, 3000.0f, 0.0f)));
	APawn* LonePrey = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), Prey + FVector(0.0f, 3000.0f, 0.0f), FRotator::ZeroRotator, SpawnParameters);
	const int32 Free = CrowdSystem->RegisterCrowdGroup({Diver}, Hive + FVector(0.0f, 3000.0f, 0.0f), HoverConfig, false, 25.0f);
	CrowdSystem->ConfigureCrowdGroupEngagement(Free, OneToken, true);
	CrowdSystem->ActivateCrowdGroupForActor(Free, LonePrey);
	CrowdSystem->ForceCrowdGroupUpdate(Free);
	TestTrue(FString::Printf(TEXT("A flyer over the target with a free token dives (destination Z %.0f, target Z %.0f)"), DestinationZ(Diver), Prey.Z),
		DestinationZ(Diver) < Prey.Z + HoverConfig.EngagedHoverHeight * 0.5f && DestinationZ(Diver) > TNumericLimits<float>::Lowest());
	return true;
}

#endif
