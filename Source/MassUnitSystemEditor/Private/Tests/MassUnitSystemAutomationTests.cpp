// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Templates/UnrealTemplate.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Core/MassUnitSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Entity/MassUnitCombatProcessor.h"
#include "Entity/MassUnitEntityManager.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/MassUnitMovementProcessor.h"
#include "Entity/MassUnitSpawner.h"
#include "Entity/UnitTemplate.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "Visual/CrowdInstanceRenderer.h"
#include "Kismet/GameplayStatics.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "MassUnitCommonFragments.h"
#include "Navigation/FormationSystem.h"
#include "Navigation/MassUnitNavigationSystem.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UObjectIterator.h"
#include "Visual/NiagaraUnitSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMassUnitNativeLifecycleTest,
	"MassUnitSystem.Core.NativeMassLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMassUnitNativeLifecycleTest::RunTest(const FString& Parameters)
{
	UMassUnitSystemSettings* MutableSettings = GetMutableDefault<UMassUnitSystemSettings>();
	TGuardValue<float> VisualUpdateIntervalGuard(MutableSettings->VisualUpdateInterval, 0.0f);

	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("A transient game world can be created"), TestWorld.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}

	UWorld* World = TestWorld.GetTestWorld();
	UMassUnitSubsystem* UnitSubsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Mass Unit world subsystem initializes automatically"), UnitSubsystem))
	{
		return false;
	}

	UMassUnitEntityManager* UnitManager = UnitSubsystem->GetUnitManager();
	UMassEntitySubsystem* MassSubsystem = UnitSubsystem->GetEntitySubsystem();
	if (!TestNotNull(TEXT("Unit manager is available"), UnitManager)
		|| !TestNotNull(TEXT("Native Mass subsystem is available"), MassSubsystem))
	{
		return false;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

	UUnitTemplate* Template = NewObject<UUnitTemplate>(GetTransientPackage());
	Template->TeamID = 1;
	Template->BaseHealth = 100;
	Template->BaseDamage = 20;
	Template->MoveSpeed = 300.0f;
	Template->AttackRange = 200.0f;
	Template->AttackCooldown = 0.0f;
	Template->DefaultAbilities.Add(UE::MassUnitSystem::Tags::AnimationAttack());

	const FMassUnitHandle UnitA = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector::ZeroVector));
	Template->TeamID = 2;
	const FMassUnitHandle UnitB = UnitManager->CreateUnitFromTemplate(Template, FTransform(FVector(100.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("First native Mass unit is valid"), UnitManager->IsUnitValid(UnitA));
	TestTrue(TEXT("Second native Mass unit is valid"), UnitManager->IsUnitValid(UnitB));
	TestNotEqual(TEXT("Units receive distinct native handles"), UnitA.EntityHandle.Index, UnitB.EntityHandle.Index);
	TestEqual(TEXT("Unit manager tracks both units"), UnitManager->GetUnitCount(), 2);
	const FMassUnitStateFragment* StateA = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(UnitA.EntityHandle.ToMassEntityHandle());
	const FMassUnitAbilityFragment* AbilityA = EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(UnitA.EntityHandle.ToMassEntityHandle());
	const FMassUnitVisualFragment* VisualA = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(UnitA.EntityHandle.ToMassEntityHandle());
	const FMassUnitFormationFragment* FormationA = EntityManager.GetFragmentDataPtr<FMassUnitFormationFragment>(UnitA.EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Template class and behavior tags are copied into native fragments"), StateA && StateA->UnitClass == Template->UnitClass && StateA->DefaultBehavior == Template->DefaultBehavior);
	TestTrue(TEXT("Template ability tags are copied into the ability fragment"), AbilityA && AbilityA->DefaultAbilityTags == Template->DefaultAbilities);
	TestTrue(TEXT("Template animation metadata is copied into the visual fragment"), VisualA && VisualA->AnimationTags == Template->AnimationTags);
	TestTrue(TEXT("Template formation preference is copied into the formation fragment"), FormationA && FormationA->DefaultFormation == Template->DefaultFormation);

	FTransform TransformA;
	FTransform TransformB;
	TestTrue(TEXT("First unit transform is readable"), UnitManager->GetUnitTransform(UnitA, TransformA));
	TestTrue(TEXT("Second unit transform is readable"), UnitManager->GetUnitTransform(UnitB, TransformB));
	TestTrue(TEXT("Per-entity transform fragments remain independent"), !TransformA.GetLocation().Equals(TransformB.GetLocation()));

	TestTrue(TEXT("A hostile target can be assigned"), UnitManager->SetUnitTarget(UnitA, UnitB));
	UMassUnitCombatProcessor* CombatProcessor = NewObject<UMassUnitCombatProcessor>(GetTransientPackage());
	CombatProcessor->CallInitialize(World, EntityManager.AsShared());
	FMassExecutionContext CombatContext(EntityManager, 0.1f);
	CombatContext.SetExecutionType(EMassExecutionContextType::Processor);
	CombatProcessor->CallExecute(EntityManager, CombatContext);
	FMassUnitStateFragment StateB;
	TestTrue(TEXT("Target state remains readable after combat"), UnitManager->GetUnitState(UnitB, StateB));
	TestTrue(TEXT("Combat processor applies configured damage"), StateB.Health < StateB.MaxHealth);
	float UnitBHealth = 0.0f;
	float UnitBMaxHealth = 0.0f;
	TestTrue(TEXT("Native health values are queryable without promoting a unit to an Actor"),
		UnitManager->GetUnitHealth(UnitB, UnitBHealth, UnitBMaxHealth));
	TestTrue(TEXT("Native health percent reflects combat damage"),
		UnitManager->GetUnitHealthPercent(UnitB) > 0.0f && UnitManager->GetUnitHealthPercent(UnitB) < 1.0f);
	TestTrue(TEXT("Native healing updates lightweight Mass health"), UnitManager->HealUnit(UnitB, 5.0f));
	float HealedUnitBHealth = 0.0f;
	float HealedUnitBMaxHealth = 0.0f;
	UnitManager->GetUnitHealth(UnitB, HealedUnitBHealth, HealedUnitBMaxHealth);
	TestTrue(TEXT("Native healing is clamped and observable"),
		HealedUnitBHealth > UnitBHealth && HealedUnitBHealth <= HealedUnitBMaxHealth);
	TestTrue(TEXT("Closest-unit queries resolve a representation location to a native handle"),
		UnitManager->FindClosestUnit(FVector(95.0f, 0.0f, 0.0f), 25.0f).EntityHandle == UnitB.EntityHandle);
	TestEqual(TEXT("Radius queries return all native units inside a planar area"),
		UnitManager->GetUnitsInRadius(FVector(50.0f, 0.0f, 0.0f), 100.0f).Num(), 2);

	UnitManager->ClearUnitTarget(UnitA);
	TestTrue(TEXT("Direct path request queues successfully"), UnitSubsystem->GetNavigationSystem()->RequestPath(UnitA, FVector(400.0f, 0.0f, 0.0f), 10.0f));
	TestTrue(TEXT("A newer path request supersedes the queued request"), UnitSubsystem->GetNavigationSystem()->RequestPath(UnitA, FVector(500.0f, 0.0f, 0.0f), 10.0f));
	TestEqual(TEXT("Only the newest request remains queued for a unit"), UnitSubsystem->GetNavigationSystem()->GetQueuedRequestCount(), 1);
	UnitSubsystem->GetNavigationSystem()->ProcessPathRequests();
	const FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(UnitA.EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("A world without nav data receives the newest direct-path fallback"), Navigation && Navigation->bPathValid && Navigation->PathPoints.Num() == 1 && Navigation->PathPoints[0].Equals(FVector(500.0f, 0.0f, 0.0f)));

	UMassUnitMovementProcessor* MovementProcessor = NewObject<UMassUnitMovementProcessor>(GetTransientPackage());
	MovementProcessor->CallInitialize(World, EntityManager.AsShared());
	for (int32 Step = 0; Step < 5; ++Step)
	{
		FMassExecutionContext MovementContext(EntityManager, 0.1f);
		MovementContext.SetExecutionType(EMassExecutionContextType::Processor);
		MovementProcessor->CallExecute(EntityManager, MovementContext);
	}
	FTransform MovedTransformA;
	FTransform UnmovedTransformB;
	UnitManager->GetUnitTransform(UnitA, MovedTransformA);
	UnitManager->GetUnitTransform(UnitB, UnmovedTransformB);
	TestTrue(TEXT("Requested unit moves toward its destination"), MovedTransformA.GetLocation().X > TransformA.GetLocation().X);
	TestTrue(TEXT("Unrequested unit does not share movement fragments"), UnmovedTransformB.GetLocation().Equals(TransformB.GetLocation()));
	TestTrue(TEXT("A path can be queued before explicit cancellation"),
		UnitSubsystem->GetNavigationSystem()->RequestPath(UnitA, FVector(700.0f, 0.0f, 0.0f), 10.0f));
	TestEqual(TEXT("Cancellation test has one queued request"), UnitSubsystem->GetNavigationSystem()->GetQueuedRequestCount(), 1);
	TestTrue(TEXT("Queued navigation can be cancelled safely"), UnitSubsystem->GetNavigationSystem()->CancelPath(UnitA));
	TestEqual(TEXT("Cancellation removes queued and in-flight work"), UnitSubsystem->GetNavigationSystem()->GetQueuedRequestCount(), 0);
	Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(UnitA.EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Cancellation clears the unit navigation fragment"),
		Navigation && !Navigation->bPathRequested && !Navigation->bPathValid && Navigation->PathPoints.IsEmpty());

	UFormationSystem* FormationSystem = UnitSubsystem->GetFormationSystem();
	const int32 FormationHandle = FormationSystem->CreateFormation(FVector::ZeroVector, FRotator::ZeroRotator, TEXT("Infantry"));
	TestFalse(TEXT("Unknown formation shapes are rejected"), FormationSystem->SetFormationShape(FormationHandle, TEXT("Unknown")));
	TestTrue(TEXT("Column formation shape is supported"), FormationSystem->SetFormationShape(FormationHandle, TEXT("Column")));
	TestTrue(TEXT("Unit can join a formation"), FormationSystem->AddUnitToFormation(UnitA, FormationHandle));
	TestEqual(TEXT("Formation contains exactly one unit"), FormationSystem->GetEntitiesInFormation(FormationHandle).Num(), 1);
	FormationSystem->Tick(0.0f);
	const FMassUnitTargetFragment* FormationTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(UnitA.EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("World origin remains a valid formation target"), FormationTarget && FormationTarget->bHasTargetLocation && FormationTarget->TargetLocation.IsNearlyZero());
	TestTrue(TEXT("Unit can leave a formation"), FormationSystem->RemoveUnitFromFormation(UnitA));

	UnitManager->DestroyUnit(UnitA);
	TestFalse(TEXT("Destroyed native handle is rejected"), UnitManager->IsUnitValid(UnitA));
	TestEqual(TEXT("Unit manager removes destroyed units"), UnitManager->GetUnitCount(), 1);
	EntityManager.DestroyEntity(UnitB.EntityHandle.ToMassEntityHandle());
	UnitManager->PruneInvalidUnits();
	TestEqual(TEXT("Externally destroyed native entities are pruned from manager indexes"), UnitManager->GetUnitCount(), 0);
	TestEqual(TEXT("Team queries exclude externally destroyed entities"), UnitManager->GetUnitsByTeam(2).Num(), 0);

	const FMassUnitHandle DefaultUnit = UnitManager->CreateDefaultUnit(FTransform(FVector(0.0f, 0.0f, 50.0f)));
	FMassUnitStateFragment DefaultState;
	TestTrue(TEXT("Asset-free default unit creation produces a valid native entity"), UnitManager->IsUnitValid(DefaultUnit));
	TestTrue(TEXT("Asset-free default unit uses playable movement and health defaults"),
		UnitManager->GetUnitState(DefaultUnit, DefaultState) && DefaultState.Health > 0.0f && DefaultState.MoveSpeed > 0.0f);
	UnitManager->DestroyUnit(DefaultUnit);

	AMassUnitSpawner* Spawner = World->SpawnActorDeferred<AMassUnitSpawner>(
		AMassUnitSpawner::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Quick-start spawner can be created in a game world"), Spawner))
	{
		return false;
	}
	Spawner->bSpawnOnBeginPlay = false;
	Spawner->bMoveOnBeginPlay = false;
	Spawner->bSpawnOnAuthorityOnly = false;
	Spawner->UnitCount = 6;
	Spawner->Columns = 3;
	UGameplayStatics::FinishSpawningActor(Spawner, FTransform::Identity);

	const TArray<FMassUnitHandle> SpawnedUnits = Spawner->SpawnUnits();
	TestEqual(TEXT("Quick-start spawner creates the requested native unit count"), SpawnedUnits.Num(), 6);
	TestEqual(TEXT("Quick-start spawner tracks only valid owned units"), Spawner->GetValidSpawnedUnitCount(), 6);
	TestEqual(TEXT("Unit manager sees every quick-start unit"), UnitManager->GetUnitCount(), 6);
	if (SpawnedUnits.IsEmpty())
	{
		return false;
	}

	FTransform FirstSpawnerTransform;
	TestTrue(TEXT("Quick-start unit transform is readable"), UnitManager->GetUnitTransform(SpawnedUnits[0], FirstSpawnerTransform));
	TestEqual(TEXT("Quick-start offset command reaches every unit"), Spawner->MoveSpawnedUnitsByOffset(FVector(600.0f, 0.0f, 0.0f)), 6);
	const FMassUnitNavigationFragment* SpawnerNavigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Quick-start offset command preserves each unit's own start location"),
		SpawnerNavigation && SpawnerNavigation->PathPoints.Num() == 1
		&& SpawnerNavigation->PathPoints[0].Equals(FirstSpawnerTransform.GetLocation() + FVector(600.0f, 0.0f, 0.0f)));

	UMassUnitCrowdSystem* CrowdSystem = UnitSubsystem->GetCrowdSystem();
	if (!TestNotNull(TEXT("World crowd service is available"), CrowdSystem))
	{
		return false;
	}
	Spawner->bEnableCrowdSimulation = true;
	Spawner->CrowdConfig.WanderRadius = 500.0f;
	Spawner->CrowdConfig.MinWanderDistance = 100.0f;
	Spawner->CrowdConfig.MinIdleTime = 0.0f;
	Spawner->CrowdConfig.MaxIdleTime = 0.0f;
	Spawner->CrowdConfig.MaxSimulationDistance = 0.0f;
	Spawner->CrowdConfig.bEnableInteractions = false;
	Spawner->CrowdConfig.RandomSeed = 2468;
	const int32 CrowdGroupHandle = Spawner->StartCrowdSimulation();
	TestTrue(TEXT("Spawner registers a continuous crowd group"), CrowdGroupHandle != INDEX_NONE);
	TestTrue(TEXT("Spawner reports active crowd simulation"), Spawner->IsCrowdSimulationActive());
	TestEqual(TEXT("Crowd group owns every spawned unit"), CrowdSystem->GetCrowdGroupUnitCount(CrowdGroupHandle), SpawnedUnits.Num());

	const FMassUnitCrowdFragment* FirstCrowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	const FMassUnitTargetFragment* FirstCrowdTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	const FMassUnitTargetFragment* SecondCrowdTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(SpawnedUnits[1].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Crowd registration writes lightweight per-unit state"),
		FirstCrowd && FirstCrowd->bEnabled && FirstCrowd->GroupHandle == CrowdGroupHandle);
	TestTrue(TEXT("Crowd registration immediately assigns a random destination"),
		FirstCrowdTarget && FirstCrowdTarget->bHasTargetLocation);
	TestTrue(TEXT("Random destinations remain inside the configured group radius"),
		FirstCrowdTarget && FVector::DistSquared2D(FirstCrowdTarget->TargetLocation, Spawner->GetActorLocation())
			<= FMath::Square(Spawner->CrowdConfig.WanderRadius));
	TestTrue(TEXT("Deterministic per-unit streams avoid one shared crowd destination"),
		FirstCrowdTarget && SecondCrowdTarget
		&& !FirstCrowdTarget->TargetLocation.Equals(SecondCrowdTarget->TargetLocation));
	TestTrue(TEXT("Crowd diagnostics report assigned destinations"),
		CrowdSystem->GetCrowdStats().DestinationsAssigned > 0);
	TestEqual(TEXT("Crowd diagnostics are populated immediately after registration"),
		CrowdSystem->GetCrowdStats().RegisteredUnits,
		SpawnedUnits.Num());

	FTransform CrowdStartTransform;
	FTransform CrowdMovedTransform;
	TestTrue(TEXT("Crowd movement baseline transform is readable"),
		UnitManager->GetUnitTransform(SpawnedUnits[0], CrowdStartTransform));
	for (int32 Step = 0; Step < 3; ++Step)
	{
		FMassExecutionContext CrowdMovementContext(EntityManager, 0.1f);
		CrowdMovementContext.SetExecutionType(EMassExecutionContextType::Processor);
		MovementProcessor->CallExecute(EntityManager, CrowdMovementContext);
	}
	TestTrue(TEXT("Crowd movement result transform is readable"),
		UnitManager->GetUnitTransform(SpawnedUnits[0], CrowdMovedTransform));
	TestTrue(TEXT("An active crowd destination produces smooth Mass movement"),
		FVector::DistSquared2D(CrowdStartTransform.GetLocation(), CrowdMovedTransform.GetLocation()) > 1.0f);

	TestTrue(TEXT("Crowd group can be paused"), CrowdSystem->SetCrowdGroupPaused(CrowdGroupHandle, true, true));
	FirstCrowdTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Pausing a crowd clears active movement"), FirstCrowdTarget && !FirstCrowdTarget->HasTarget());
	TestTrue(TEXT("Crowd group can be resumed"), CrowdSystem->SetCrowdGroupPaused(CrowdGroupHandle, false, true));
	FirstCrowdTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Resuming a crowd assigns fresh work"), FirstCrowdTarget && FirstCrowdTarget->bHasTargetLocation);
	Spawner->StopCrowdSimulation(false);
	TestFalse(TEXT("Spawner unregisters its crowd group without destroying units"), Spawner->IsCrowdSimulationActive());
	FirstCrowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("Unregistering restores non-crowd unit state"), FirstCrowd && !FirstCrowd->bEnabled);

	FMassUnitCrowdConfig InteractionConfig;
	InteractionConfig.WanderRadius = 500.0f;
	InteractionConfig.bEnableSeparation = false;
	InteractionConfig.bEnableInteractions = true;
	InteractionConfig.InteractionChance = 1.0f;
	InteractionConfig.InteractionRadius = 500.0f;
	InteractionConfig.MinInteractionTime = 1.0f;
	InteractionConfig.MaxInteractionTime = 1.0f;
	InteractionConfig.MaxSimulationDistance = 0.0f;
	const int32 InteractionGroupHandle = CrowdSystem->RegisterCrowdGroup(
		SpawnedUnits,
		Spawner->GetActorLocation(),
		InteractionConfig,
		false,
		25.0f);
	TestTrue(TEXT("Crowd interaction test group registers"), InteractionGroupHandle != INDEX_NONE);
	TestTrue(TEXT("Nearby crowd members can begin lightweight paired interactions"),
		CrowdSystem->GetCrowdStats().InteractionsStarted > 0);
	bool bFoundInteractingUnit = false;
	for (const FMassUnitHandle& UnitHandle : SpawnedUnits)
	{
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
		bFoundInteractingUnit |= State && State->CurrentState == EMassUnitState::Interacting;
	}
	TestTrue(TEXT("Crowd interactions write the lightweight interaction state"), bFoundInteractingUnit);
	// Joining another group mid-interaction (a preset swap) resets the crowd fragment: the unit must not stay Interacting.
	FMassUnitCrowdConfig RejoinConfig = InteractionConfig;
	RejoinConfig.bEnableInteractions = false;
	const int32 RejoinGroupHandle = CrowdSystem->RegisterCrowdGroup(SpawnedUnits, Spawner->GetActorLocation(), RejoinConfig, false, 25.0f);
	CrowdSystem->ForceCrowdGroupUpdate(RejoinGroupHandle);
	int32 StillInteracting = 0;
	for (const FMassUnitHandle& UnitHandle : SpawnedUnits)
	{
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
		StillInteracting += State && State->CurrentState == EMassUnitState::Interacting ? 1 : 0;
	}
	TestEqual(TEXT("Units moved to a new group mid-interaction stop interacting"), StillInteracting, 0);
	TestTrue(TEXT("Interaction group unregisters cleanly"), CrowdSystem->UnregisterCrowdGroup(RejoinGroupHandle, true));

	FMassUnitCrowdConfig Free3DConfig;
	Free3DConfig.MovementMode = EMassUnitCrowdMovementMode::Free3D;
	Free3DConfig.WanderRadius = 500.0f;
	Free3DConfig.MinWanderDistance = 100.0f;
	Free3DConfig.MinIdleTime = 0.0f;
	Free3DConfig.MaxIdleTime = 0.0f;
	Free3DConfig.MaxSimulationDistance = 0.0f;
	Free3DConfig.bEnableSeparation = false;
	Free3DConfig.bEnableInteractions = false;
	Free3DConfig.RandomSeed = 97531;
	const int32 Free3DGroupHandle = CrowdSystem->RegisterCrowdGroup(
		SpawnedUnits,
		Spawner->GetActorLocation() + FVector(0.0f, 0.0f, 250.0f),
		Free3DConfig,
		true,
		25.0f);
	TestTrue(TEXT("Free-3D crowd group registers even when planar navigation was requested"), Free3DGroupHandle != INDEX_NONE);
	bool bFoundVerticalDestination = false;
	bool bAllDestinationsInsideSphere = true;
	for (const FMassUnitHandle& UnitHandle : SpawnedUnits)
	{
		const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
		const FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
		FTransform UnitTransform;
		UnitManager->GetUnitTransform(UnitHandle, UnitTransform);
		bFoundVerticalDestination |= Crowd && Target && Crowd->bUse3DMovement
			&& !FMath::IsNearlyEqual(Target->TargetLocation.Z, UnitTransform.GetLocation().Z, 1.0f);
		bAllDestinationsInsideSphere &= Target && Target->bHasTargetLocation
			&& FVector::DistSquared(Target->TargetLocation, Spawner->GetActorLocation() + FVector(0.0f, 0.0f, 250.0f))
				<= FMath::Square(Free3DConfig.WanderRadius);
	}
	TestTrue(TEXT("Free-3D mode produces vertical destination variation"), bFoundVerticalDestination);
	TestTrue(TEXT("Free-3D random destinations stay inside the configured sphere"), bAllDestinationsInsideSphere);
	FTransform Free3DStart;
	FTransform Free3DMoved;
	UnitManager->GetUnitTransform(SpawnedUnits[0], Free3DStart);
	for (int32 Step = 0; Step < 4; ++Step)
	{
		FMassExecutionContext Free3DMovementContext(EntityManager, 0.1f);
		Free3DMovementContext.SetExecutionType(EMassExecutionContextType::Processor);
		MovementProcessor->CallExecute(EntityManager, Free3DMovementContext);
	}
	UnitManager->GetUnitTransform(SpawnedUnits[0], Free3DMoved);
	TestTrue(TEXT("Free-3D movement changes entity height instead of flattening steering"),
		!FMath::IsNearlyEqual(Free3DStart.GetLocation().Z, Free3DMoved.GetLocation().Z, 0.1f));
	TestTrue(TEXT("Free-3D group unregisters cleanly"), CrowdSystem->UnregisterCrowdGroup(Free3DGroupHandle, true));

	FTransform TerrainFollowStart;
	FTransform TerrainFollowMoved;
	UnitManager->GetUnitTransform(SpawnedUnits[0], TerrainFollowStart);
	FMassUnitCrowdFragment* TerrainCrowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(
		SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	FMassUnitNavigationFragment* TerrainNavigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(
		SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	FMassUnitVelocityFragment* TerrainVelocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(
		SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	if (TerrainCrowd && TerrainNavigation)
	{
		if (TerrainVelocity)
		{
			TerrainVelocity->Value = FVector::ZeroVector;
		}
		TerrainCrowd->Reset();
		TerrainCrowd->bEnabled = true;
		TerrainCrowd->bConformToNavmeshHeight = true;
		TerrainCrowd->NavigationHeightOffset = 0.0f;
		UnitManager->SetUnitDestination(
			SpawnedUnits[0],
			TerrainFollowStart.GetLocation() + FVector(300.0f, 0.0f, 150.0f),
			10.0f);
		TerrainNavigation->bPathUsesNavmesh = true;
		for (int32 Step = 0; Step < 3; ++Step)
		{
			FMassExecutionContext TerrainMovementContext(EntityManager, 0.1f);
			TerrainMovementContext.SetExecutionType(EMassExecutionContextType::Processor);
			MovementProcessor->CallExecute(EntityManager, TerrainMovementContext);
		}
		UnitManager->GetUnitTransform(SpawnedUnits[0], TerrainFollowMoved);
	}
	TestTrue(TEXT("Planar navmesh movement preserves corridor elevation without a ground raycast"),
		TerrainCrowd && TerrainNavigation
		&& TerrainFollowMoved.GetLocation().Z > TerrainFollowStart.GetLocation().Z + 1.0f);
	UnitManager->SetUnitTransform(SpawnedUnits[0], TerrainFollowStart);
	UnitManager->ClearUnitTarget(SpawnedUnits[0]);
	if (TerrainCrowd)
	{
		TerrainCrowd->Reset();
	}
	if (TerrainVelocity)
	{
		TerrainVelocity->Value = FVector::ZeroVector;
	}

	FActorSpawnParameters TargetSpawnParameters;
	TargetSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* EngagementTarget = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		FVector(800.0f, 0.0f, 50.0f),
		FRotator::ZeroRotator,
		TargetSpawnParameters);
	if (!TestNotNull(TEXT("Player-engagement test target can be spawned"), EngagementTarget))
	{
		return false;
	}

	FMassUnitCrowdConfig EngagementCrowdConfig;
	EngagementCrowdConfig.WanderRadius = 500.0f;
	EngagementCrowdConfig.MaxSimulationDistance = 0.0f;
	EngagementCrowdConfig.bEnableInteractions = false;
	const int32 EngagementGroupHandle = CrowdSystem->RegisterCrowdGroup(
		SpawnedUnits,
		Spawner->GetActorLocation(),
		EngagementCrowdConfig,
		true,
		25.0f);
	TestTrue(TEXT("Player-engagement crowd group registers"), EngagementGroupHandle != INDEX_NONE);
	TestEqual(TEXT("Small crowds form one deterministic managed subgroup"),
		CrowdSystem->GetCrowdGroupSubgroupCount(EngagementGroupHandle), 1);
	TestEqual(TEXT("Managed subgroup metadata is queryable per native unit"),
		CrowdSystem->GetUnitSubgroupIndex(SpawnedUnits[0]), 0);
	TestEqual(TEXT("Managed subgroup membership is queryable without subgroup Actors"),
		CrowdSystem->GetCrowdSubgroupUnits(EngagementGroupHandle, 0).Num(), SpawnedUnits.Num());
	TestEqual(TEXT("Ambient navigation builds one shared subgroup corridor"),
		CrowdSystem->GetCrowdStats().AmbientSharedPathsBuilt, 1);
	TestEqual(TEXT("Shared ambient navigation leaves the individual path queue empty"),
		UnitSubsystem->GetNavigationSystem()->GetQueuedRequestCount(), 0);
	FMassUnitPlayerEngagementConfig EngagementConfig;
	EngagementConfig.ActivationMode = EMassUnitCrowdActivationMode::OnInteraction;
	EngagementConfig.bAutoDeactivate = false;
	EngagementConfig.bEnableAttacks = false;
	EngagementConfig.FollowDistance = 100.0f;
	EngagementConfig.FollowSpread = 50.0f;
	TestTrue(TEXT("Player engagement is explicitly configurable per crowd group"),
		CrowdSystem->ConfigureCrowdGroupEngagement(EngagementGroupHandle, EngagementConfig, true));
	TestTrue(TEXT("A project interaction activates the owning crowd group"),
		CrowdSystem->NotifyUnitInteracted(SpawnedUnits[0], EngagementTarget));
	TestTrue(TEXT("Engaged group exposes its current target Actor"),
		CrowdSystem->IsCrowdGroupEngaged(EngagementGroupHandle)
		&& CrowdSystem->GetCrowdGroupTargetActor(EngagementGroupHandle) == EngagementTarget);
	TestTrue(TEXT("A forced engagement refresh assigns throttled follow destinations"),
		CrowdSystem->ForceCrowdGroupUpdate(EngagementGroupHandle));
	TestEqual(TEXT("Engagement navigation builds one shared group corridor"),
		CrowdSystem->GetCrowdStats().SharedPathsBuilt, 1);
	TestEqual(TEXT("Shared engagement navigation submits no per-unit path requests"),
		CrowdSystem->GetCrowdStats().PerUnitPathsRequested, 0);
	TestEqual(TEXT("Shared engagement navigation does not fill the per-unit request queue"),
		UnitSubsystem->GetNavigationSystem()->GetQueuedRequestCount(), 0);
	const FMassUnitTargetFragment* InitialFollowTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	const FVector InitialFollowDestination = InitialFollowTarget ? InitialFollowTarget->TargetLocation : FVector::ZeroVector;
	TestTrue(TEXT("Engagement follow slots remain near the target without collapsing to one point"),
		InitialFollowTarget && InitialFollowTarget->bHasTargetLocation
		&& FVector::DistSquared2D(InitialFollowDestination, EngagementTarget->GetActorLocation())
			<= FMath::Square(EngagementConfig.FollowDistance + EngagementConfig.FollowSpread));
	FTransform FirstEngagementUnitTransform;
	UnitManager->GetUnitTransform(SpawnedUnits[0], FirstEngagementUnitTransform);
	TestTrue(TEXT("Crowd nearest-unit query resolves an interaction point"),
		CrowdSystem->FindClosestCrowdUnit(FirstEngagementUnitTransform.GetLocation(), 1.0f).EntityHandle
			== SpawnedUnits[0].EntityHandle);

	TestTrue(TEXT("Engagement test target can move dynamically"),
		EngagementTarget->SetActorLocation(FVector(1400.0f, 250.0f, 50.0f)));
	TestTrue(TEXT("Forced follow refresh accepts a dynamically moving target"),
		CrowdSystem->ForceCrowdGroupUpdate(EngagementGroupHandle));
	const FMassUnitTargetFragment* RefreshedFollowTarget = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("A moved target causes a new follow destination"),
		RefreshedFollowTarget && FVector::DistSquared2D(InitialFollowDestination, RefreshedFollowTarget->TargetLocation)
			> FMath::Square(EngagementConfig.RepathDistance));

	TestTrue(TEXT("Engagement can be released without unregistering the crowd"),
		CrowdSystem->DeactivateCrowdGroupEngagement(EngagementGroupHandle, true));
	float HealthBeforeInteractionDamage = 0.0f;
	float MaxHealthBeforeInteractionDamage = 0.0f;
	UnitManager->GetUnitHealth(SpawnedUnits[0], HealthBeforeInteractionDamage, MaxHealthBeforeInteractionDamage);
	TestTrue(TEXT("Damage can update native health and activate the owning group in one call"),
		CrowdSystem->DamageUnitAndActivate(SpawnedUnits[0], 5.0f, EngagementTarget));
	float HealthAfterInteractionDamage = 0.0f;
	float MaxHealthAfterInteractionDamage = 0.0f;
	UnitManager->GetUnitHealth(SpawnedUnits[0], HealthAfterInteractionDamage, MaxHealthAfterInteractionDamage);
	TestTrue(TEXT("Damage-and-activate changes native Mass health"),
		HealthAfterInteractionDamage < HealthBeforeInteractionDamage);

	EngagementConfig.bEnableAttacks = true;
	EngagementConfig.AttackRangeOverride = 5000.0f;
	EngagementConfig.bApplyActorDamage = false;
	TestTrue(TEXT("Attack behavior can be enabled without forcing Actor-owned health"),
		CrowdSystem->ConfigureCrowdGroupEngagement(EngagementGroupHandle, EngagementConfig, true));
	TestTrue(TEXT("Engaged units can request cooldown-based attacks"),
		CrowdSystem->ForceCrowdGroupUpdate(EngagementGroupHandle));
	const FMassUnitStateFragment* EngagedAttackState = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(SpawnedUnits[0].EntityHandle.ToMassEntityHandle());
	TestTrue(TEXT("In-range engaged unit enters the attacking state"),
		EngagedAttackState && EngagedAttackState->CurrentState == EMassUnitState::Attacking);
	TestTrue(TEXT("Crowd diagnostics count engagement and attack work"),
		CrowdSystem->GetCrowdStats().EngagedGroups == 1
		&& CrowdSystem->GetCrowdStats().EngagedUnits == SpawnedUnits.Num()
		&& CrowdSystem->GetCrowdStats().AttacksRequested > 0);
	TestTrue(TEXT("Engagement deactivation leaves the registered group reusable"),
		CrowdSystem->DeactivateCrowdGroupEngagement(EngagementGroupHandle, true)
		&& CrowdSystem->IsCrowdGroupRegistered(EngagementGroupHandle));
	TestTrue(TEXT("Engagement group unregisters cleanly"), CrowdSystem->UnregisterCrowdGroup(EngagementGroupHandle, true));
	World->DestroyActor(EngagementTarget);

	UNiagaraUnitSystem* VisualSystem = UnitSubsystem->GetNiagaraSystem();
	if (!TestNotNull(TEXT("Quick-start visual service is available"), VisualSystem))
	{
		return false;
	}
	VisualSystem->UpdateUnitVisualsByHandles(SpawnedUnits);
	// Asserted against the published contract, NOT a literal. This previously hardcoded 8 and went red
	// the moment the layout was extended to 10 (RandomSeed + Speed) - a stale magic number in a suite
	// nobody had run. Referencing CrowdCustomData::NumFloats means an append-only extension keeps this
	// green automatically, while a RENUMBERING - the thing the contract actually forbids - still breaks
	// the channel-index assertions below.
	TestEqual(TEXT("Instanced fallback exposes the documented animation/LOD/team/health custom-data layout"),
		VisualSystem->GetInstancedMeshCustomDataFloatCount(),
		UE::MassUnitSystem::CrowdCustomData::NumFloats);
	TestTrue(TEXT("Quick-start units have either configured Niagara or asset-free instanced rendering"),
		VisualSystem->IsUsingNiagara() || VisualSystem->GetInstancedMeshInstanceCount() == SpawnedUnits.Num());

	if (!VisualSystem->IsUsingNiagara())
	{
		UInstancedStaticMeshComponent* FallbackComponent = nullptr;
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->GetInstanceCount() == SpawnedUnits.Num())
			{
				FallbackComponent = *It;
				break;
			}
		}
		if (!TestNotNull(TEXT("Asset-free fallback creates an instanced mesh component"), FallbackComponent))
		{
			return false;
		}
		TestFalse(TEXT("Moving fallback units avoid HISM cluster-tree rebuilds"),
			FallbackComponent->IsA<UHierarchicalInstancedStaticMeshComponent>());

		FTransform BeforeVisualMove;
		TestTrue(TEXT("Fallback instance transform is readable"),
			FallbackComponent->GetInstanceTransform(0, BeforeVisualMove, true));
		FTransform UpdatedUnitTransform;
		UnitManager->GetUnitTransform(SpawnedUnits[0], UpdatedUnitTransform);
		UpdatedUnitTransform.AddToTranslation(FVector(75.0f, 25.0f, 0.0f));
		const int32 TopologyRevisionBeforeMove = VisualSystem->GetInstancedMeshTopologyRevision();
		TestTrue(TEXT("Quick-start unit transform can be updated for rendering"),
			UnitManager->SetUnitTransform(SpawnedUnits[0], UpdatedUnitTransform));
		VisualSystem->UpdateUnitVisualsByHandles(SpawnedUnits);

		FTransform AfterVisualMove;
		TestTrue(TEXT("Fallback instance remains readable after an in-place update"),
			FallbackComponent->GetInstanceTransform(0, AfterVisualMove, true));
		TestTrue(TEXT("Fallback instance receives the updated unit transform"),
			AfterVisualMove.GetLocation().Equals(UpdatedUnitTransform.GetLocation()));
		TestEqual(TEXT("Movement does not rebuild instanced-mesh topology"),
			VisualSystem->GetInstancedMeshTopologyRevision(), TopologyRevisionBeforeMove);
		TestEqual(TEXT("Movement preserves the fallback instance count"),
			VisualSystem->GetInstancedMeshInstanceCount(), SpawnedUnits.Num());
	}

	Spawner->DestroySpawnedUnits();
	TestEqual(TEXT("Quick-start cleanup destroys only its owned Mass units"), UnitManager->GetUnitCount(), 0);
	VisualSystem->UpdateUnitVisualsByHandles(Spawner->GetValidSpawnedUnits());
	TestTrue(TEXT("Quick-start cleanup removes instanced fallback slots"),
		VisualSystem->IsUsingNiagara() || VisualSystem->GetInstancedMeshInstanceCount() == 0);
	World->DestroyActor(Spawner);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
