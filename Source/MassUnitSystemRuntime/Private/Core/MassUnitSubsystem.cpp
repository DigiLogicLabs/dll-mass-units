// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Core/MassUnitSubsystem.h"
#include "Core/MassUnitStats.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitSystemRuntime.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Entity/MassUnitEntityManager.h"
#include "Gameplay/GASUnitIntegration.h"
#include "Gameplay/MassUnitBehaviorIntegration.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "Gameplay/UnitGameplayEventSystem.h"
#include "MassEntitySubsystem.h"
#include "Navigation/FormationSystem.h"
#include "Navigation/MassUnitNavigationSystem.h"
#include "Visual/NiagaraUnitSystem.h"
#include "Visual/CrowdInstanceRenderer.h"
#include "Visual/UnitMeshPool.h"

void UMassUnitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>();
	if (!EntitySubsystem || !GetWorld())
	{
		UE_LOG(LogMassUnitSystem, Error, TEXT("Mass Unit subsystem could not resolve its world Mass entity subsystem"));
		return;
	}

	UnitManager = NewObject<UMassUnitEntityManager>(this);
	UnitManager->Initialize(EntitySubsystem);

	FormationSystem = NewObject<UFormationSystem>(this);
	FormationSystem->Initialize(EntitySubsystem);

	NavigationSystem = NewObject<UMassUnitNavigationSystem>(this);
	NavigationSystem->Initialize(GetWorld(), EntitySubsystem);

	CrowdSystem = NewObject<UMassUnitCrowdSystem>(this);
	CrowdSystem->Initialize(GetWorld(), EntitySubsystem, UnitManager, NavigationSystem);

	NiagaraSystem = NewObject<UNiagaraUnitSystem>(this);
	NiagaraSystem->Initialize(GetWorld(), EntitySubsystem);

	MeshPool = NewObject<UUnitMeshPool>(this);
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	MeshPool->Initialize(GetWorld(), EntitySubsystem, Settings ? Settings->MaxSkeletalMeshUnits : 100);

	GASIntegration = NewObject<UGASUnitIntegration>(this);
	GASIntegration->Initialize(EntitySubsystem);

	BehaviorIntegration = NewObject<UMassUnitBehaviorIntegration>(this);
	BehaviorIntegration->Initialize(GASIntegration);

	GameplayEventSystem = NewObject<UUnitGameplayEventSystem>(this);
	GameplayEventSystem->Initialize();

	UE_LOG(LogMassUnitSystem, Log, TEXT("Mass Unit subsystem initialized for %s"), *GetWorld()->GetName());
}

void UMassUnitSubsystem::Deinitialize()
{
	if (CrowdSystem)
	{
		CrowdSystem->Deinitialize();
	}
	if (GameplayEventSystem)
	{
		GameplayEventSystem->Deinitialize();
	}
	if (BehaviorIntegration)
	{
		BehaviorIntegration->Deinitialize();
	}
	if (GASIntegration)
	{
		GASIntegration->Deinitialize();
	}
	if (MeshPool)
	{
		MeshPool->Deinitialize();
	}
	if (NiagaraSystem)
	{
		NiagaraSystem->Deinitialize();
	}
	if (NavigationSystem)
	{
		NavigationSystem->Deinitialize();
	}
	if (FormationSystem)
	{
		FormationSystem->Deinitialize();
	}
	if (UnitManager)
	{
		UnitManager->Deinitialize();
	}

	GameplayEventSystem = nullptr;
	BehaviorIntegration = nullptr;
	CrowdSystem = nullptr;
	GASIntegration = nullptr;
	MeshPool = nullptr;
	NiagaraSystem = nullptr;
	NavigationSystem = nullptr;
	FormationSystem = nullptr;
	UnitManager = nullptr;
	EntitySubsystem = nullptr;

	Super::Deinitialize();
}

DECLARE_CYCLE_STAT(TEXT("Subsystem Tick (total)"), STAT_MassUnit_SubsystemTick, STATGROUP_MassUnitSystem);
DECLARE_CYCLE_STAT(TEXT("Skeletal Mesh Pool"), STAT_MassUnit_MeshPool, STATGROUP_MassUnitSystem);
DECLARE_CYCLE_STAT(TEXT("Visual Update (Niagara / instances)"), STAT_MassUnit_VisualUpdate, STATGROUP_MassUnitSystem);
DECLARE_DWORD_COUNTER_STAT(TEXT("Units"), STAT_MassUnit_Units, STATGROUP_MassUnitSystem);
DECLARE_DWORD_COUNTER_STAT(TEXT("Airborne Units"), STAT_MassUnit_Airborne, STATGROUP_MassUnitSystem);
DECLARE_DWORD_COUNTER_STAT(TEXT("Skeletal Mesh Units"), STAT_MassUnit_SkeletalMeshes, STATGROUP_MassUnitSystem);
DECLARE_DWORD_COUNTER_STAT(TEXT("Ragdolls"), STAT_MassUnit_Ragdolls, STATGROUP_MassUnitSystem);
DECLARE_DWORD_COUNTER_STAT(TEXT("Render Instances"), STAT_MassUnit_RenderInstances, STATGROUP_MassUnitSystem);
DECLARE_DWORD_COUNTER_STAT(TEXT("Crowd Groups"), STAT_MassUnit_CrowdGroups, STATGROUP_MassUnitSystem);

void UMassUnitSubsystem::Tick(float DeltaTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_SubsystemTick);
	if (!UnitManager)
	{
		return;
	}
	UnitManager->PruneInvalidUnits();
	if (const UWorld* World = GetWorld())
	{
		UnitManager->TickStatuses(World->GetTimeSeconds());
	}
	// Airborne units (knockback, kill flings): one sparse loop, free with nothing in the air.
	UnitManager->TickLaunches(DeltaTime);

	if (FormationSystem)
	{
		FormationSystem->Tick(DeltaTime);
	}
	if (NavigationSystem)
	{
		NavigationSystem->ProcessPathRequests();
	}
	if (BehaviorIntegration)
	{
		BehaviorIntegration->Tick(DeltaTime);
	}

	const TArray<FMassUnitEntityHandle>& Units = UnitManager->GetAllUnitsInternal();
	if (MeshPool)
	{
		MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_MeshPool);
		MeshPool->UpdateUnitMeshes(Units);
		// After the update, so a kill promoted this frame can already ragdoll.
		TArray<TPair<FMassUnitEntityHandle, FVector>> RagdollRequests = UnitManager->ConsumeRagdollRequests();
		if (RagdollRequests.Num() > 0)
		{
			MeshPool->StartRagdolls(RagdollRequests);
		}
	}
	if (NiagaraSystem)
	{
		MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_VisualUpdate);
		NiagaraSystem->UpdateUnitVisuals(Units);
	}

	// Per-frame counters (stat MassUnitSystem + CSV "MassUnits"): what a stress run's numbers are read against.
	MASSUNIT_SET_COUNTER(STAT_MassUnit_Units, Units, Units.Num());
	MASSUNIT_SET_COUNTER(STAT_MassUnit_Airborne, Airborne, UnitManager->GetAirborneUnitCount());
	MASSUNIT_SET_COUNTER(STAT_MassUnit_SkeletalMeshes, SkeletalMeshUnits, MeshPool ? MeshPool->GetActiveSkeletalMeshCount() : 0);
	MASSUNIT_SET_COUNTER(STAT_MassUnit_Ragdolls, Ragdolls, MeshPool ? MeshPool->GetActiveRagdollCount() : 0);
	const UCrowdInstanceRenderer* Renderer = NiagaraSystem ? NiagaraSystem->GetCrowdInstanceRenderer() : nullptr;
	MASSUNIT_SET_COUNTER(STAT_MassUnit_RenderInstances, RenderInstances, Renderer ? Renderer->GetLiveInstanceCount() : 0);
	MASSUNIT_SET_COUNTER(STAT_MassUnit_CrowdGroups, CrowdGroups, CrowdSystem ? CrowdSystem->GetCrowdGroupCount() : 0);
}

TStatId UMassUnitSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMassUnitSubsystem, STATGROUP_Tickables);
}

bool UMassUnitSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::GamePreview;
}

UMassUnitSubsystem* UMassUnitSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
	{
		return World->GetSubsystem<UMassUnitSubsystem>();
	}
	return nullptr;
}
