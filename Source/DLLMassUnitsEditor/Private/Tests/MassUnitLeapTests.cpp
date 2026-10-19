// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "Core/MassUnitGameplayTags.h"
#include "GameFramework/DefaultPawn.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "Tests/AutomationCommon.h"

/**
 * Leapers: LeapUnit sends a living unit on an arc to a point and lands it on its feet (no knockdown, no stun left), is
 * refused while airborne and for the dead; an engaged group with bEnablePounce pounces from its range window, only as
 * many at once as the attack-token pool allows, and a pounce that lands on the target counts as a hit.
 * The test world never ticks: launches are driven by TickLaunches and crowd decisions by ForceCrowdGroupUpdate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitLeapTest,
	"DLLMassUnits.Units.Leap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitLeapTest::RunTest(const FString& Parameters)
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
	auto UnitLocation = [UnitManager](const FMassUnitHandle& Unit)
	{
		FTransform Transform;
		UnitManager->GetUnitTransform(Unit, Transform);
		return Transform.GetLocation();
	};

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->AttackRange = 150.0f;
	Template->AttackCooldown = 1.0f;

	// ---- A direct leap ----
	const FMassUnitHandle Leaper = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(0.0f, 0.0f, 100.0f)));
	const FVector Point(600.0f, 0.0f, 100.0f);
	TestTrue(TEXT("A living unit leaps"), UnitManager->LeapUnit(Leaper, Point, 150.0f));
	TestTrue(TEXT("...is airborne"), UnitManager->IsUnitAirborne(Leaper));
	TestTrue(TEXT("...and leaping"), UnitManager->IsUnitLeaping(Leaper));
	FMassUnitStateFragment State;
	UnitManager->GetUnitState(Leaper, State);
	TestTrue(TEXT("...held for the flight with the leap flag"), State.CurrentState == EMassUnitState::Stunned && State.bLeaping);
	TestFalse(TEXT("A second leap in the air is refused"), UnitManager->LeapUnit(Leaper, FVector::ZeroVector));
	float TopZ = 0.0f;
	for (int32 Frame = 0; Frame < 300 && UnitManager->IsUnitAirborne(Leaper); ++Frame)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
		TopZ = FMath::Max(TopZ, static_cast<float>(UnitLocation(Leaper).Z));
	}
	const FVector Landed = UnitLocation(Leaper);
	TestFalse(TEXT("The leap ends"), UnitManager->IsUnitAirborne(Leaper));
	TestTrue(FString::Printf(TEXT("...on the point (landed %.0f, %.0f, %.0f)"), Landed.X, Landed.Y, Landed.Z), FVector::Dist(Landed, Point) < 15.0f);
	TestTrue(FString::Printf(TEXT("...after an arc over it (top %.0f)"), TopZ), TopZ > Point.Z + 120.0f);
	UnitManager->GetUnitState(Leaper, State);
	TestTrue(TEXT("...on its feet: not stunned, no leap flag"), State.CurrentState != EMassUnitState::Stunned && !State.bLeaping);

	// A knockback mid-leap turns it into an ordinary launch: knocked down on landing.
	const FMassUnitHandle Knocked = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(0.0f, 800.0f, 100.0f)));
	TestTrue(TEXT("A second unit leaps"), UnitManager->LeapUnit(Knocked, FVector(600.0f, 800.0f, 100.0f)));
	UnitManager->TickLaunches(1.0f / 60.0f);
	TestTrue(TEXT("A knockback hits it mid-leap"), UnitManager->LaunchUnit(Knocked, FVector(-600.0f, 0.0f, 400.0f), nullptr, false));
	TestFalse(TEXT("...which ends the leap"), UnitManager->IsUnitLeaping(Knocked));
	for (int32 Frame = 0; Frame < 300 && UnitManager->IsUnitAirborne(Knocked); ++Frame)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
	}
	UnitManager->GetUnitState(Knocked, State);
	TestTrue(TEXT("...and it lands knocked down, without the leap flag"), State.CurrentState == EMassUnitState::Stunned && !State.bLeaping);

	// A template immune to the Launched status cannot be held in the air, so it never leaps.
	UUnitTemplate* Immune = NewObject<UUnitTemplate>(GetTransientPackage());
	Immune->StatusImmunityTags.AddTag(UE::MassUnitSystem::Tags::StatusLaunched());
	const FMassUnitHandle Steady = UnitManager->CreateUnitFromTemplate(Immune, FTransform(FVector(0.0f, 1200.0f, 100.0f)));
	TestFalse(TEXT("A unit immune to Launched does not leap"), UnitManager->LeapUnit(Steady, FVector(600.0f, 1200.0f, 100.0f)));

	const FMassUnitHandle Corpse = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(0.0f, 400.0f, 100.0f)));
	UnitManager->ApplyDamage(Corpse, 1000000.0f);
	TestFalse(TEXT("The dead do not leap"), UnitManager->LeapUnit(Corpse, Point));

	// ---- Pounce from an engaged group ----
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Origin(5000.0f, 0.0f, 100.0f);
	APawn* Target = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), Origin, FRotator::ZeroRotator, SpawnParameters);
	TArray<FMassUnitHandle> Pack;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Angle = 2.0f * PI * static_cast<float>(Index) / 4.0f;
		Pack.Add(UnitManager->CreateUnitFromTemplate(Template, FTransform(Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 500.0f)));
	}
	FMassUnitCrowdConfig Crowd;
	Crowd.MaxSimulationDistance = 0.0f;
	Crowd.bEnableInteractions = false;
	const int32 Group = CrowdSystem->RegisterCrowdGroup(Pack, Origin, Crowd, false, 25.0f);
	FMassUnitPlayerEngagementConfig Engagement;
	Engagement.ActivationMode = EMassUnitCrowdActivationMode::Manual;
	Engagement.bAutoDeactivate = false;
	Engagement.bApplyActorDamage = false;
	Engagement.MaxSimultaneousAttackers = 2;
	Engagement.bEnablePounce = true;
	Engagement.PounceMinRange = 300.0f;
	Engagement.PounceMaxRange = 800.0f;
	CrowdSystem->ConfigureCrowdGroupEngagement(Group, Engagement, true);
	TestTrue(TEXT("The pack engages the target"), CrowdSystem->ActivateCrowdGroupForActor(Group, Target));
	const int32 StartedBefore = CrowdSystem->GetCrowdStats().TotalPouncesStarted;
	CrowdSystem->ForceCrowdGroupUpdate(Group);
	int32 Leaping = 0;
	for (const FMassUnitHandle& Unit : Pack)
	{
		Leaping += UnitManager->IsUnitLeaping(Unit) ? 1 : 0;
	}
	TestEqual(TEXT("Units in the range window pounce, as many as the token pool allows (2 of 4)"), Leaping, 2);
	TestEqual(TEXT("...counted as started"), CrowdSystem->GetCrowdStats().TotalPouncesStarted - StartedBefore, 2);

	const int32 HitsBefore = CrowdSystem->GetCrowdStats().TotalPounceHits;
	for (int32 Frame = 0; Frame < 300; ++Frame)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
	}
	CrowdSystem->ForceCrowdGroupUpdate(Group);
	TestEqual(TEXT("Both pounces land on the target and hit"), CrowdSystem->GetCrowdStats().TotalPounceHits - HitsBefore, 2);
	return true;
}

#endif
