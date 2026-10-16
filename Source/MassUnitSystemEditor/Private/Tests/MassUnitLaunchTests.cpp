// Copyright Digi Logic Labs LLC. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MassUnitGameplayTags.h"
#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitSubsystem.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Tests/AutomationCommon.h"

/**
 * LaunchUnit (knockback, juggles, kill flings): a living unit rises, is stunned by MassUnit.Status.Launched, lands at
 * its launch height (the test world has no ground to trace) upright, and stays knocked down; a corpse tumbles, bounces
 * once and settles flat; a template with LaunchVelocityScale 0 refuses; events fire.
 *
 * The test world never ticks, so flight is driven by calling TickLaunches with a fixed step.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitLaunchTest,
	"MassUnitSystem.Units.Launch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitLaunchTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UMassUnitSubsystem* UnitSubsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
	UMassUnitEntityManager* UnitManager = UnitSubsystem ? UnitSubsystem->GetUnitManager() : nullptr;
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager))
	{
		return false;
	}
	FMassEntityManager& EntityManager = UnitSubsystem->GetEntitySubsystem()->GetMutableEntityManager();
	auto GetTransform = [&EntityManager](const FMassUnitHandle& Unit) -> FTransform
	{
		return EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Unit.EntityHandle.ToMassEntityHandle())->GetTransform();
	};
	auto GetState = [&EntityManager](const FMassUnitHandle& Unit) -> FMassUnitStateFragment&
	{
		return *EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit.EntityHandle.ToMassEntityHandle());
	};

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->BaseHealth = 100;
	UUnitTemplate* HeavyTemplate = NewObject<UUnitTemplate>(GetTransientPackage());
	HeavyTemplate->LaunchVelocityScale = 0.0f;
	UUnitTemplate* ImmuneTemplate = NewObject<UUnitTemplate>(GetTransientPackage());
	ImmuneTemplate->StatusImmunityTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("MassUnit.Status")));

	const FVector Start(0.0f, 0.0f, 100.0f);
	const FMassUnitHandle Goblin = UnitManager->CreateUnitFromTemplate(Template, FTransform(Start));
	const FMassUnitHandle Corpse = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(1000.0f, 0.0f, 100.0f)));
	const FMassUnitHandle Ogre = UnitManager->CreateUnitFromTemplate(HeavyTemplate, FTransform(FVector(-1000.0f, 0.0f, 100.0f)));
	const FMassUnitHandle Warded = UnitManager->CreateUnitFromTemplate(ImmuneTemplate, FTransform(FVector(-2000.0f, 0.0f, 100.0f)));
	if (!TestTrue(TEXT("Units spawned"), Goblin.IsValid() && Corpse.IsValid() && Ogre.IsValid()))
	{
		return false;
	}

	// --- A heavy refuses ---------------------------------------------------------------------------
	TestFalse(TEXT("LaunchVelocityScale 0 refuses the launch"), UnitManager->LaunchUnit(Ogre, FVector(0.0f, 0.0f, 800.0f)));
	TestFalse(TEXT("and the heavy is not airborne"), UnitManager->IsUnitAirborne(Ogre));
	TestFalse(TEXT("A status-immune unit cannot be held down, so it is not launched"), UnitManager->LaunchUnit(Warded, FVector(0.0f, 0.0f, 800.0f)));
	TestTrue(TEXT("A client replay (scale already applied) is not refused by the template gate"),
		UnitManager->LaunchUnit(Ogre, FVector(0.0f, 0.0f, 300.0f), nullptr, /*bApplyTemplateScale*/ false));
	for (int32 Step = 0; Step < 200 && UnitManager->IsUnitAirborne(Ogre); ++Step)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
	}

	// --- A living unit: up, stunned, down upright, knocked down ------------------------------------
	TestTrue(TEXT("Living unit launches"), UnitManager->LaunchUnit(Goblin, FVector(400.0f, 0.0f, 800.0f)));
	TestTrue(TEXT("It is airborne"), UnitManager->IsUnitAirborne(Goblin));
	TestTrue(TEXT("It carries the Launched status"), UnitManager->UnitHasTag(Goblin, UE::MassUnitSystem::Tags::StatusLaunched()));
	TestEqual(TEXT("Launched stuns (no moving or attacking)"), GetState(Goblin).CurrentState, EMassUnitState::Stunned);

	float Apex = Start.Z;
	int32 Steps = 0;
	bool bCleansed = false;
	bool bHeldDownAllFlight = true;
	while (UnitManager->IsUnitAirborne(Goblin) && Steps++ < 400)
	{
		if (!bCleansed && Steps == 10)
		{
			// A cleanse mid-air must not let the unit walk or attack in the air: the flight re-arms the hold.
			UnitManager->RemoveUnitStatusesMatching(Goblin, FGameplayTagContainer(FGameplayTag::RequestGameplayTag(TEXT("MassUnit.Status"))));
			bCleansed = true;
		}
		UnitManager->TickLaunches(1.0f / 60.0f);
		bHeldDownAllFlight &= GetState(Goblin).CurrentState == EMassUnitState::Stunned;
		Apex = FMath::Max(Apex, static_cast<float>(GetTransform(Goblin).GetLocation().Z));
	}
	TestTrue(TEXT("It rose"), Apex > Start.Z + 100.0f);
	TestFalse(TEXT("It landed"), UnitManager->IsUnitAirborne(Goblin));
	const FTransform Landed = GetTransform(Goblin);
	TestTrue(TEXT("Landed at launch height (no ground in the test world)"), FMath::IsNearlyEqual(Landed.GetLocation().Z, Start.Z, 1.0f));
	TestTrue(TEXT("Travelled along the launch direction"), Landed.GetLocation().X > Start.X + 100.0f);
	TestTrue(TEXT("Landed upright"), FMath::IsNearlyZero(Landed.Rotator().Pitch, 0.5f) && FMath::IsNearlyZero(Landed.Rotator().Roll, 0.5f));
	TestTrue(TEXT("Faces where the hit came from"), FMath::Abs(FMath::FindDeltaAngleDegrees(Landed.Rotator().Yaw, 180.0f)) < 1.0f);
	TestTrue(TEXT("Held down the whole flight, even through a mid-air cleanse"), bHeldDownAllFlight);
	TestTrue(TEXT("Still down after landing (knockdown)"), UnitManager->UnitHasTag(Goblin, UE::MassUnitSystem::Tags::StatusLaunched()));
	{
		const float Knockdown = GetDefault<UMassUnitSystemSettings>()->KnockdownSeconds;
		const TArray<FMassUnitStatusEffect> Statuses = UnitManager->GetUnitStatuses(Goblin);
		const FMassUnitStatusEffect* Down = Statuses.FindByPredicate([](const FMassUnitStatusEffect& S)
		{
			return S.StatusTag == UE::MassUnitSystem::Tags::StatusLaunched();
		});
		TestTrue(TEXT("The knockdown lasts KnockdownSeconds from landing, not the flight hold"),
			Down && FMath::IsNearlyEqual(Down->ExpireTime - World->GetTimeSeconds(), Knockdown, 0.05f));
	}

	// --- A corpse: tumbles, settles flat ------------------------------------------------------------
	UnitManager->ApplyDamage(Corpse, 1000.0f);
	TestEqual(TEXT("Corpse is dead"), GetState(Corpse).CurrentState, EMassUnitState::Dead);
	TestTrue(TEXT("Dead units can be flung"), UnitManager->LaunchUnit(Corpse, FVector(0.0f, 500.0f, 700.0f)));
	TestEqual(TEXT("A fling queues a near-band ragdoll request"), UnitManager->ConsumeRagdollRequests().Num(), 1);
	bool bTumbled = false;
	Steps = 0;
	while (UnitManager->IsUnitAirborne(Corpse) && Steps++ < 600)
	{
		UnitManager->TickLaunches(1.0f / 60.0f);
		const FRotator Rotation = GetTransform(Corpse).Rotator();
		bTumbled |= FMath::Abs(Rotation.Pitch) > 20.0f || FMath::Abs(Rotation.Roll) > 20.0f;
	}
	TestTrue(TEXT("Corpse tumbled in the air"), bTumbled);
	TestFalse(TEXT("Corpse finished (landed, bounced, settled)"), UnitManager->IsUnitAirborne(Corpse));
	const FRotator Settled = GetTransform(Corpse).Rotator();
	TestTrue(TEXT("Corpse settled flat (yaw only)"), FMath::IsNearlyZero(Settled.Pitch, 0.5f) && FMath::IsNearlyZero(Settled.Roll, 0.5f));
	TestEqual(TEXT("Nothing left airborne"), UnitManager->GetAirborneUnitCount(), 0);

	UnitManager->DestroyUnit(Goblin);
	UnitManager->DestroyUnit(Corpse);
	UnitManager->DestroyUnit(Ogre);
	UnitManager->DestroyUnit(Warded);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
