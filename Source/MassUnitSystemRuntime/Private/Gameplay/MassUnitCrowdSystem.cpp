// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Gameplay/MassUnitCrowdSystem.h"
#include "Core/MassUnitStats.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Core/MassUnitSystemRuntime.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "GameFramework/Actor.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassUnitCommonFragments.h"
#include "Navigation/MassUnitNavigationSystem.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"

void UMassUnitCrowdSystem::Initialize(
	UWorld* InWorld,
	UMassEntitySubsystem* InEntitySubsystem,
	UMassUnitEntityManager* InUnitManager,
	UMassUnitNavigationSystem* InNavigationSystem)
{
	World = InWorld;
	EntitySubsystem = InEntitySubsystem;
	UnitManager = InUnitManager;
	NavigationSystem = InNavigationSystem;

	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	UpdateInterval = Settings ? FMath::Max(0.02f, Settings->CrowdUpdateInterval) : 0.1f;
	MaxUnitsPerUpdate = Settings ? FMath::Max(1, Settings->MaxCrowdUnitsPerUpdate) : 500;
	MaxSharedPathBuildsPerUpdate = Settings ? FMath::Max(1, Settings->MaxSharedPathBuildsPerCrowdUpdate) : 8;
	SpatialCellSize = Settings ? FMath::Max(10.0f, Settings->CrowdSpatialCellSize) : 200.0f;
	SimulationLODDistances = Settings
		? Settings->CrowdSimulationLODDistances
		: TArray<float>{2500.0f, 5000.0f, 10000.0f};
	SimulationLODIntervalMultipliers = Settings
		? Settings->CrowdSimulationLODIntervalMultipliers
		: TArray<float>{1.0f, 2.0f, 4.0f, 8.0f};
	SimulationLODDistances.Sort();
	if (SimulationLODIntervalMultipliers.IsEmpty())
	{
		SimulationLODIntervalMultipliers.Add(1.0f);
	}
}

void UMassUnitCrowdSystem::Deinitialize()
{
	if (World)
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}

	TArray<int32> GroupHandles;
	Groups.GetKeys(GroupHandles);
	for (const int32 GroupHandle : GroupHandles)
	{
		UnregisterCrowdGroup(GroupHandle, false);
	}

	Groups.Reset();
	UnitToGroup.Reset();
	PendingSignals.Reset();
	LastStats = {};
	NavigationSystem = nullptr;
	UnitManager = nullptr;
	EntitySubsystem = nullptr;
	World = nullptr;
}

int32 UMassUnitCrowdSystem::RegisterCrowdGroup(
	const TArray<FMassUnitHandle>& UnitHandles,
	FVector Center,
	const FMassUnitCrowdConfig& Config,
	bool bUseNavigation,
	float AcceptanceRadius)
{
	if (!World || !EntitySubsystem || !UnitManager || UnitHandles.IsEmpty())
	{
		return INDEX_NONE;
	}

	FCrowdGroup Group;
	Group.Handle = NextGroupHandle++;
	Group.Center = Center;
	Group.Config = SanitizeConfig(Config);
	Group.bUseNavigation = bUseNavigation;
	Group.AcceptanceRadius = FMath::Max(1.0f, AcceptanceRadius);

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const float CurrentTime = World->GetTimeSeconds();
	int32 ValidUnitIndex = 0;
	for (const FMassUnitHandle& UnitHandle : UnitHandles)
	{
		const FMassUnitEntityHandle Entity = UnitHandle.EntityHandle;
		if (!IsEntityValid(Entity))
		{
			continue;
		}

		RemoveUnitFromPreviousGroup(Entity);
		FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle());
		FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Entity.ToMassEntityHandle());
		if (!Crowd || !State)
		{
			continue;
		}

		Crowd->Reset();
		Crowd->bEnabled = true;
		Crowd->bUse3DMovement = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
		Crowd->bConformToNavmeshHeight = Group.bUseNavigation
			&& !Crowd->bUse3DMovement
			&& Group.Config.bConformToNavmeshHeight;
		Crowd->NavigationHeightOffset = Group.Config.NavigationHeightOffset;
		Crowd->GroupHandle = Group.Handle;
		Crowd->SubgroupIndex = Group.Config.bEnableManagedSubgroups
			? ValidUnitIndex / Group.Config.ManagedSubgroupSize
			: 0;
		Crowd->BaseMoveSpeed = State->MoveSpeed;
		Crowd->NextDecisionTime = CurrentTime;
		Group.Units.AddUnique(Entity);
		UnitToGroup.Add(Entity, Group.Handle);
		++ValidUnitIndex;
	}

	if (Group.Units.IsEmpty())
	{
		return INDEX_NONE;
	}
	Group.SubgroupCount = Group.Config.bEnableManagedSubgroups
		? FMath::Max(1, FMath::DivideAndRoundUp(Group.Units.Num(), Group.Config.ManagedSubgroupSize))
		: 1;
	Group.NextSubgroupCueTimes.Init(0.0f, Group.SubgroupCount);
	Group.Subgroups.SetNum(Group.SubgroupCount);

	Groups.Add(Group.Handle, MoveTemp(Group));
	StartTimerIfNeeded();
	ForceCrowdGroupUpdate(Group.Handle);
	return Group.Handle;
}

bool UMassUnitCrowdSystem::UnregisterCrowdGroup(int32 CrowdGroupHandle, bool bStopUnits)
{
	FCrowdGroup Group;
	if (!Groups.RemoveAndCopyValue(CrowdGroupHandle, Group))
	{
		return false;
	}

	for (const FMassUnitEntityHandle Entity : Group.Units)
	{
		UnitToGroup.Remove(Entity);
		ResetCrowdFragment(Entity, bStopUnits);
	}
	StopTimerIfIdle();
	return true;
}

bool UMassUnitCrowdSystem::SetCrowdGroupPaused(int32 CrowdGroupHandle, bool bPaused, bool bStopUnits)
{
	FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group)
	{
		return false;
	}
	Group->bPaused = bPaused;

	if (bPaused && bStopUnits)
	{
		for (const FMassUnitEntityHandle Entity : Group->Units)
		{
			ClearMovement(Entity, true);
		}
	}
	else if (!bPaused && EntitySubsystem && World)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		for (const FMassUnitEntityHandle Entity : Group->Units)
		{
			if (FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle()))
			{
				Crowd->NextDecisionTime = World->GetTimeSeconds();
			}
		}
		ForceCrowdGroupUpdate(CrowdGroupHandle);
	}
	return true;
}

bool UMassUnitCrowdSystem::SetCrowdGroupCenter(int32 CrowdGroupHandle, FVector NewCenter)
{
	if (FCrowdGroup* Group = Groups.Find(CrowdGroupHandle))
	{
		Group->Center = NewCenter;
		return true;
	}
	return false;
}

bool UMassUnitCrowdSystem::ForceCrowdGroupUpdate(int32 CrowdGroupHandle)
{
	FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group || Group->bPaused || !World || bUpdatingCrowds)
	{
		return false;
	}
	TGuardValue<bool> UpdateGuard(bUpdatingCrowds, true);

	PruneInvalidUnits();
	const float CurrentTime = World->GetTimeSeconds();
	LastStats = {};
	UpdateGroupEngagements(CurrentTime);
	Group = Groups.Find(CrowdGroupHandle);
	if (!Group)
	{
		return false;
	}
	if (Group->bEngaged)
	{
		if (AActor* TargetActor = Group->TargetActor.Get())
		{
			Group->LastTargetLocation = TargetActor->GetActorLocation();
			Group->NextTargetRefreshTime = CurrentTime + Group->EngagementConfig.TargetRefreshInterval;
		}
		const bool bSharedPathStale = Group->SharedPathPoints.IsEmpty()
			|| FVector::DistSquared2D(Group->SharedPathTargetLocation, Group->LastTargetLocation) > 1.0f;
		if (bSharedPathStale)
		{
			RefreshSharedNavigationPath(*Group, CurrentTime, true);
		}
	}

	TArray<FSpatialEntry> Entries;
	TMap<FIntVector, TArray<int32>> Cells;
	TMap<FMassUnitEntityHandle, int32> EntryByEntity;
	TArray<FVector> ObserverLocations;
	BuildSpatialData(Entries, Cells, EntryByEntity);
	BuildObserverLocations(ObserverLocations);
	RefreshManagedSubgroupPaths(CurrentTime, Entries, CrowdGroupHandle, true);

	RefreshPopulationStats(Entries);
	for (const FSpatialEntry& Entry : Entries)
	{
		if (Entry.GroupHandle == CrowdGroupHandle)
		{
			FCrowdGroup* CurrentGroup = Groups.Find(CrowdGroupHandle);
			if (!CurrentGroup || CurrentGroup->bPaused)
			{
				break;
			}
			UpdateUnit(Entry, *CurrentGroup, Entries, Cells, EntryByEntity, ObserverLocations, CurrentTime, true);
			++LastStats.UnitsUpdated;
		}
	}
	PruneInvalidUnits();
	RefreshPopulationStats(Entries);
	return true;
}

bool UMassUnitCrowdSystem::IsCrowdGroupRegistered(int32 CrowdGroupHandle) const
{
	return Groups.Contains(CrowdGroupHandle);
}

int32 UMassUnitCrowdSystem::GetCrowdGroupUnitCount(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	return Group ? Group->Units.Num() : 0;
}

int32 UMassUnitCrowdSystem::GetCrowdGroupSubgroupCount(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	return Group ? Group->SubgroupCount : 0;
}

int32 UMassUnitCrowdSystem::GetUnitSubgroupIndex(FMassUnitHandle UnitHandle) const
{
	if (!EntitySubsystem || !IsEntityValid(UnitHandle.EntityHandle))
	{
		return INDEX_NONE;
	}
	const FMassUnitCrowdFragment* Crowd = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitCrowdFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	return Crowd && Crowd->bEnabled ? Crowd->SubgroupIndex : INDEX_NONE;
}

TArray<FMassUnitHandle> UMassUnitCrowdSystem::GetCrowdSubgroupUnits(
	int32 CrowdGroupHandle,
	int32 SubgroupIndex) const
{
	TArray<FMassUnitHandle> Result;
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group || SubgroupIndex < 0 || SubgroupIndex >= Group->SubgroupCount || !EntitySubsystem)
	{
		return Result;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const FMassUnitEntityHandle Entity : Group->Units)
	{
		if (!IsEntityValid(Entity))
		{
			continue;
		}
		const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle());
		if (Crowd && Crowd->SubgroupIndex == SubgroupIndex)
		{
			Result.Emplace(Entity);
		}
	}
	return Result;
}

bool UMassUnitCrowdSystem::ConfigureCrowdGroupEngagement(
	int32 CrowdGroupHandle,
	const FMassUnitPlayerEngagementConfig& Config,
	bool bEnableEngagement)
{
	FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group)
	{
		return false;
	}

	Group->EngagementConfig = SanitizeEngagementConfig(Config);
	Group->bEngagementEnabled = bEnableEngagement;
	Group->bHoldAfterEngagement = false;
	Group->SharedPathPoints.Reset();
	Group->bSharedPathUsesNavmesh = false;
	Group->SharedPathTargetLocation = FVector::ZeroVector;
	Group->NextSharedPathUpdateTime = 0.0f;
	// Drop attack tokens with the engagement. Leaving them would make a re-engage start with a pool
	// still occupied by units that are no longer attacking anything, throttling the first wave.
	Group->AttackTokenExpiry.Reset();
	if (!bEnableEngagement && Group->bEngaged)
	{
		return DeactivateCrowdGroupEngagement(CrowdGroupHandle, true);
	}

	if (bEnableEngagement && World)
	{
		Group->NextTargetRefreshTime = World->GetTimeSeconds();
	}
	return true;
}

bool UMassUnitCrowdSystem::ActivateCrowdGroupForActor(int32 CrowdGroupHandle, AActor* TargetActor)
{
	FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group || !Group->bEngagementEnabled || !IsValid(TargetActor) || !World || !EntitySubsystem)
	{
		return false;
	}

	const bool bWasEngaged = Group->bEngaged;
	Group->bEngaged = true;
	Group->bHoldAfterEngagement = false;
	Group->TargetActor = TargetActor;
	Group->LastTargetLocation = TargetActor->GetActorLocation();
	Group->NextTargetRefreshTime = World->GetTimeSeconds() + Group->EngagementConfig.TargetRefreshInterval;
	Group->SharedPathPoints.Reset();
	Group->bSharedPathUsesNavmesh = false;
	Group->SharedPathTargetLocation = FVector::ZeroVector;
	Group->NextSharedPathUpdateTime = 0.0f;

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	for (const FMassUnitEntityHandle Entity : Group->Units)
	{
		if (!IsEntityValid(Entity))
		{
			continue;
		}
		const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
		FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(NativeHandle);
		FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		if (!Crowd || !State)
		{
			continue;
		}
		Crowd->bSleeping = false;
		Crowd->InteractionEndTime = 0.0f;
		Crowd->InteractionPartner.Invalidate();
		Crowd->NextFollowUpdateTime = 0.0f;
		Crowd->LastFollowTargetLocation = FVector::ZeroVector;
		Crowd->SteeringDirection = FVector::ZeroVector;
		State->MoveSpeed = Crowd->BaseMoveSpeed * Group->EngagementConfig.EngagedMoveSpeedMultiplier;
		if (State->CurrentState == EMassUnitState::Interacting)
		{
			State->CurrentState = EMassUnitState::Idle;
			State->StateTime = 0.0f;
		}
	}

	if (!bWasEngaged && Group->bCollectiveEnabled)
	{
		// Consume the relay depth the activation path left for us (zero for direct activations) and
		// tell nearby groups. The rebroadcast timer reuses the depth so repeats stay as damped as the
		// first emission and a relay chain cannot re-amplify itself.
		const int32 RelayDepth = Group->PendingAlertRelayDepth;
		Group->PendingAlertRelayDepth = 0;
		Group->EngagementRelayDepth = RelayDepth;
		Group->Awareness = 0.0f;
		Group->PendingAlertActivationTime = -1.0f;
		Group->PendingAlertTarget.Reset();
		const FMassUnitCrowdCollectiveConfig& Collective = Group->CollectiveConfig;
		if (Collective.bBroadcastAlerts && RelayDepth <= Collective.MaxAlertRelayDepth)
		{
			EmitCrowdSignal(
				*Group,
				ECrowdSignalType::Alert,
				Collective.AlertRadius,
				Collective.AlertStrength * FMath::Pow(Collective.AlertRelayFalloff, static_cast<float>(RelayDepth)),
				TargetActor,
				RelayDepth);
		}
		Group->NextAlertBroadcastTime = World->GetTimeSeconds() + Collective.AlertRebroadcastInterval;
		// The activating target gets the switch grace period, so a bystander cannot steal the group instantly.
		Group->LastTargetSwitchTime = World->GetTimeSeconds();
	}

	if (!bWasEngaged)
	{
		RequestPresentationCue(*Group, EMassUnitCrowdCue::EngagementStarted, CalculateGroupAnchor(*Group));
		OnCrowdEngagementStarted.Broadcast(CrowdGroupHandle, TargetActor);
	}
	return Groups.Contains(CrowdGroupHandle);
}

bool UMassUnitCrowdSystem::DeactivateCrowdGroupEngagement(int32 CrowdGroupHandle, bool bReturnToWander)
{
	FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group || !Group->bEngaged)
	{
		return false;
	}

	AActor* PreviousTarget = Group->TargetActor.Get();
	Group->bEngaged = false;
	Group->bHoldAfterEngagement = !bReturnToWander;
	Group->TargetActor.Reset();
	Group->LastTargetLocation = FVector::ZeroVector;
	Group->NextTargetRefreshTime = 0.0f;
	Group->SharedPathPoints.Reset();
	Group->bSharedPathUsesNavmesh = false;
	Group->SharedPathTargetLocation = FVector::ZeroVector;
	Group->NextSharedPathUpdateTime = 0.0f;

	if (EntitySubsystem)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;
		for (const FMassUnitEntityHandle Entity : Group->Units)
		{
			if (!IsEntityValid(Entity))
			{
				continue;
			}
			const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
			FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(NativeHandle);
			FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
			if (Crowd)
			{
				Crowd->LastFollowTargetLocation = FVector::ZeroVector;
				Crowd->NextFollowUpdateTime = 0.0f;
				Crowd->NextDecisionTime = bReturnToWander ? CurrentTime : TNumericLimits<float>::Max();
				Crowd->SteeringDirection = FVector::ZeroVector;
			}
			if (Crowd && State && Crowd->BaseMoveSpeed > 0.0f)
			{
				State->MoveSpeed = Crowd->BaseMoveSpeed;
			}
			ClearMovement(Entity, true);
		}
	}

	const FVector CueLocation = CalculateGroupAnchor(*Group);
	RequestPresentationCue(*Group, EMassUnitCrowdCue::EngagementEnded, CueLocation);
	OnCrowdEngagementEnded.Broadcast(CrowdGroupHandle, PreviousTarget);
	return Groups.Contains(CrowdGroupHandle);
}

bool UMassUnitCrowdSystem::NotifyUnitInteracted(FMassUnitHandle UnitHandle, AActor* InteractingActor)
{
	if (!IsEntityValid(UnitHandle.EntityHandle) || !IsValid(InteractingActor))
	{
		return false;
	}
	const int32* GroupHandle = UnitToGroup.Find(UnitHandle.EntityHandle);
	const FCrowdGroup* Group = GroupHandle ? Groups.Find(*GroupHandle) : nullptr;
	if (!Group || !Group->bEngagementEnabled
		|| Group->EngagementConfig.ActivationMode != EMassUnitCrowdActivationMode::OnInteraction)
	{
		return false;
	}
	return ActivateCrowdGroupForActor(*GroupHandle, InteractingActor);
}

bool UMassUnitCrowdSystem::DamageUnitAndActivate(
	FMassUnitHandle UnitHandle,
	float Damage,
	AActor* DamageInstigator)
{
	if (!UnitManager || !IsValid(DamageInstigator) || Damage <= 0.0f)
	{
		return false;
	}
	const int32* FoundGroupHandle = UnitToGroup.Find(UnitHandle.EntityHandle);
	if (!FoundGroupHandle)
	{
		return false;
	}
	const int32 GroupHandle = *FoundGroupHandle;
	const bool bDamaged = UnitManager->ApplyDamage(UnitHandle, Damage, DamageInstigator);
	const bool bActivated = Groups.Contains(GroupHandle)
		&& ActivateCrowdGroupForActor(GroupHandle, DamageInstigator);
	return bDamaged && bActivated;
}

FMassUnitHandle UMassUnitCrowdSystem::FindClosestCrowdUnit(
	FVector WorldLocation,
	float MaxDistance,
	bool bUse3DDistance,
	bool bIncludeDead) const
{
	if (!EntitySubsystem || MaxDistance < 0.0f)
	{
		return {};
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const float MaximumDistanceSquared = FMath::Square(MaxDistance);
	float BestDistanceSquared = MaximumDistanceSquared;
	FMassUnitEntityHandle BestEntity;
	for (const TPair<int32, FCrowdGroup>& Pair : Groups)
	{
		for (const FMassUnitEntityHandle Entity : Pair.Value.Units)
		{
			if (!IsEntityValid(Entity))
			{
				continue;
			}
			const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
			const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
			const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
			if (!Transform || (!bIncludeDead && State && State->CurrentState == EMassUnitState::Dead))
			{
				continue;
			}
			const FVector Delta = Transform->GetTransform().GetLocation() - WorldLocation;
			const float DistanceSquared = bUse3DDistance ? Delta.SizeSquared() : Delta.SizeSquared2D();
			if (DistanceSquared <= BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestEntity = Entity;
			}
		}
	}
	return FMassUnitHandle(BestEntity);
}

bool UMassUnitCrowdSystem::IsCrowdGroupEngaged(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	return Group && Group->bEngaged && Group->TargetActor.IsValid();
}

AActor* UMassUnitCrowdSystem::GetCrowdGroupTargetActor(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	return Group && Group->bEngaged ? Group->TargetActor.Get() : nullptr;
}

void UMassUnitCrowdSystem::StartTimerIfNeeded()
{
	if (World && !Groups.IsEmpty() && !World->GetTimerManager().IsTimerActive(UpdateTimer))
	{
		World->GetTimerManager().SetTimer(UpdateTimer, this, &UMassUnitCrowdSystem::UpdateCrowds, UpdateInterval, true);
	}
}

void UMassUnitCrowdSystem::StopTimerIfIdle()
{
	if (World && Groups.IsEmpty())
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
		UpdateCursor = 0;
	}
}

DECLARE_CYCLE_STAT(TEXT("Crowd Update"), STAT_MassUnit_CrowdUpdate, STATGROUP_MassUnitSystem);
DECLARE_CYCLE_STAT(TEXT("Crowd Engagements"), STAT_MassUnit_CrowdEngagements, STATGROUP_MassUnitSystem);
DECLARE_CYCLE_STAT(TEXT("Crowd Collective Behavior"), STAT_MassUnit_CrowdCollective, STATGROUP_MassUnitSystem);

void UMassUnitCrowdSystem::UpdateCrowds()
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_CrowdUpdate);
	if (!World || !EntitySubsystem || !UnitManager || bUpdatingCrowds)
	{
		return;
	}
	TGuardValue<bool> UpdateGuard(bUpdatingCrowds, true);

	PruneInvalidUnits();
	if (Groups.IsEmpty())
	{
		StopTimerIfIdle();
		return;
	}
	const float CurrentTime = World->GetTimeSeconds();
	LastStats = {};
	UpdateGroupEngagements(CurrentTime);

	TArray<FSpatialEntry> Entries;
	TMap<FIntVector, TArray<int32>> Cells;
	TMap<FMassUnitEntityHandle, int32> EntryByEntity;
	TArray<FVector> ObserverLocations;
	BuildSpatialData(Entries, Cells, EntryByEntity);
	BuildObserverLocations(ObserverLocations);
	RefreshManagedSubgroupPaths(CurrentTime, Entries);

	RefreshPopulationStats(Entries);

	if (Entries.IsEmpty())
	{
		return;
	}

	const int32 UnitsToUpdate = FMath::Min(MaxUnitsPerUpdate, Entries.Num());
	const int32 StartIndex = UpdateCursor % Entries.Num();
	for (int32 Offset = 0; Offset < UnitsToUpdate; ++Offset)
	{
		const FSpatialEntry& Entry = Entries[(StartIndex + Offset) % Entries.Num()];
		FCrowdGroup* Group = Groups.Find(Entry.GroupHandle);
		if (!Group || Group->bPaused)
		{
			continue;
		}
		UpdateUnit(Entry, *Group, Entries, Cells, EntryByEntity, ObserverLocations, CurrentTime, false);
		++LastStats.UnitsUpdated;
	}
	UpdateCursor = (StartIndex + UnitsToUpdate) % Entries.Num();
	PruneInvalidUnits();
	RefreshPopulationStats(Entries);
	DrawCollectiveDebug(CurrentTime);
}

void UMassUnitCrowdSystem::PruneInvalidUnits()
{
	TArray<int32> EmptyGroups;
	for (TPair<int32, FCrowdGroup>& Pair : Groups)
	{
		Pair.Value.Units.RemoveAll([this](const FMassUnitEntityHandle Entity)
		{
			if (IsEntityValid(Entity))
			{
				return false;
			}
			UnitToGroup.Remove(Entity);
			return true;
		});
		if (Pair.Value.Units.IsEmpty())
		{
			EmptyGroups.Add(Pair.Key);
		}
	}
	for (const int32 GroupHandle : EmptyGroups)
	{
		Groups.Remove(GroupHandle);
	}
}

void UMassUnitCrowdSystem::BuildSpatialData(
	TArray<FSpatialEntry>& OutEntries,
	TMap<FIntVector, TArray<int32>>& OutCells,
	TMap<FMassUnitEntityHandle, int32>& OutEntryByEntity) const
{
	if (!EntitySubsystem)
	{
		return;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const TPair<int32, FCrowdGroup>& Pair : Groups)
	{
		for (const FMassUnitEntityHandle Entity : Pair.Value.Units)
		{
			if (!IsEntityValid(Entity))
			{
				continue;
			}
			const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Entity.ToMassEntityHandle());
			if (!Transform)
			{
				continue;
			}

			FSpatialEntry& Entry = OutEntries.AddDefaulted_GetRef();
			Entry.Entity = Entity;
			Entry.Location = Transform->GetTransform().GetLocation();
			Entry.GroupHandle = Pair.Key;
			const int32 EntryIndex = OutEntries.Num() - 1;
			OutEntryByEntity.Add(Entity, EntryIndex);
			const bool bUse3DMovement = Pair.Value.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
			OutCells.FindOrAdd(ToSpatialCell(Entry.Location, bUse3DMovement)).Add(EntryIndex);
		}
	}
}

void UMassUnitCrowdSystem::BuildObserverLocations(TArray<FVector>& OutObserverLocations) const
{
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (!Controller)
		{
			continue;
		}
		if (const APawn* Pawn = Controller->GetPawn())
		{
			OutObserverLocations.Add(Pawn->GetActorLocation());
		}
		else
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
			OutObserverLocations.Add(ViewLocation);
		}
	}
}

void UMassUnitCrowdSystem::RefreshPopulationStats(const TArray<FSpatialEntry>& Entries)
{
	LastStats.RegisteredGroups = Groups.Num();
	LastStats.RegisteredUnits = 0;
	LastStats.ManagedSubgroups = 0;
	LastStats.ActiveUnits = 0;
	LastStats.SleepingUnits = 0;
	LastStats.EngagedGroups = 0;
	LastStats.EngagedUnits = 0;
	LastStats.ActiveAttackers = 0;
	LastStats.UnitsAwaitingAttackToken = 0;
	LastStats.RoutingGroups = 0;
	for (const TPair<int32, FCrowdGroup>& Pair : Groups)
	{
		LastStats.ManagedSubgroups += Pair.Value.SubgroupCount;
		if (Pair.Value.bCollectiveEnabled && Pair.Value.bRouting)
		{
			++LastStats.RoutingGroups;
		}
		if (Pair.Value.bEngaged && Pair.Value.TargetActor.IsValid())
		{
			++LastStats.EngagedGroups;
			// Held tokens are the authoritative attacker count. Reading it here rather than counting
			// units in the Attacking state keeps the number correct even between crowd updates, when a
			// unit may still be posed mid-swing without having been revisited.
			LastStats.ActiveAttackers += Pair.Value.AttackTokenExpiry.Num();
		}
	}
	if (!EntitySubsystem)
	{
		return;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const FSpatialEntry& Entry : Entries)
	{
		if (!IsEntityValid(Entry.Entity))
		{
			continue;
		}
		++LastStats.RegisteredUnits;
		const FCrowdGroup* Group = Groups.Find(Entry.GroupHandle);
		if (Group && Group->bEngaged && Group->TargetActor.IsValid())
		{
			++LastStats.EngagedUnits;
			if (Group->EngagementConfig.MaxSimultaneousAttackers > 0
				&& Group->EngagementConfig.bEnableAttacks
				&& !Group->AttackTokenExpiry.Contains(Entry.Entity))
			{
				const FMassUnitStateFragment* AttackState =
					EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Entry.Entity.ToMassEntityHandle());
				const float AttackRange = Group->EngagementConfig.AttackRangeOverride > 0.0f
					? Group->EngagementConfig.AttackRangeOverride
					: (AttackState ? AttackState->AttackRange : 0.0f);
				const float StandoffRange = AttackRange * Group->EngagementConfig.StandoffDistanceMultiplier;
				if (AttackRange > 0.0f
					&& FVector::DistSquared2D(Entry.Location, Group->LastTargetLocation) <= FMath::Square(StandoffRange))
				{
					++LastStats.UnitsAwaitingAttackToken;
				}
			}
		}
		const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entry.Entity.ToMassEntityHandle());
		if (Crowd && Crowd->bSleeping)
		{
			++LastStats.SleepingUnits;
		}
		else
		{
			++LastStats.ActiveUnits;
		}
	}
}

void UMassUnitCrowdSystem::RefreshManagedSubgroupPaths(
	float CurrentTime,
	const TArray<FSpatialEntry>& Entries,
	int32 OnlyGroupHandle,
	bool bForceRefresh)
{
	if (!EntitySubsystem || !NavigationSystem)
	{
		return;
	}

	struct FAnchorAccumulator
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
	};
	TMap<uint64, FAnchorAccumulator> Anchors;
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const FSpatialEntry& Entry : Entries)
	{
		const FCrowdGroup* Group = Groups.Find(Entry.GroupHandle);
		if (!Group
			|| (OnlyGroupHandle != INDEX_NONE && Entry.GroupHandle != OnlyGroupHandle)
			|| Group->bPaused
			|| Group->bEngaged
			|| !Group->bUseNavigation
			|| Group->Config.MovementMode != EMassUnitCrowdMovementMode::Planar2D
			|| !Group->Config.bEnableManagedSubgroups)
		{
			continue;
		}
		const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(
			Entry.Entity.ToMassEntityHandle());
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(
			Entry.Entity.ToMassEntityHandle());
		if (!Crowd || (State && State->CurrentState == EMassUnitState::Dead))
		{
			continue;
		}
		const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Entry.GroupHandle)) << 32)
			| static_cast<uint32>(Crowd->SubgroupIndex);
		FAnchorAccumulator& Anchor = Anchors.FindOrAdd(Key);
		Anchor.Sum += Entry.Location;
		++Anchor.Count;
	}

	int32 PathsAttempted = 0;
	for (TPair<int32, FCrowdGroup>& Pair : Groups)
	{
		FCrowdGroup& Group = Pair.Value;
		if ((OnlyGroupHandle != INDEX_NONE && Pair.Key != OnlyGroupHandle)
			|| Group.bPaused
			|| Group.bEngaged
			|| !Group.bUseNavigation
			|| Group.Config.MovementMode != EMassUnitCrowdMovementMode::Planar2D
			|| !Group.Config.bEnableManagedSubgroups)
		{
			continue;
		}

		for (int32 SubgroupIndex = 0; SubgroupIndex < Group.Subgroups.Num(); ++SubgroupIndex)
		{
			if (PathsAttempted >= MaxSharedPathBuildsPerUpdate)
			{
				return;
			}
			const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Pair.Key)) << 32)
				| static_cast<uint32>(SubgroupIndex);
			const FAnchorAccumulator* AnchorData = Anchors.Find(Key);
			if (!AnchorData || AnchorData->Count <= 0)
			{
				continue;
			}
			FCrowdSubgroupState& Subgroup = Group.Subgroups[SubgroupIndex];
			const FVector Anchor = AnchorData->Sum / static_cast<float>(AnchorData->Count);
			const bool bReachedDestination = !Subgroup.SharedPathPoints.IsEmpty()
				&& FVector::DistSquared2D(Anchor, Subgroup.Destination)
					<= FMath::Square(FMath::Max(Group.AcceptanceRadius, Group.Config.SeparationRadius));
			if (!bForceRefresh
				&& !Subgroup.SharedPathPoints.IsEmpty()
				&& !bReachedDestination
				&& CurrentTime < Subgroup.NextPathRefreshTime)
			{
				continue;
			}

			++PathsAttempted;
			uint32 Seed = HashCombine(::GetTypeHash(Group.Config.RandomSeed), ::GetTypeHash(SubgroupIndex));
			Seed = HashCombine(Seed, ::GetTypeHash(Subgroup.DecisionSequence++));
			FRandomStream RandomStream(static_cast<int32>(Seed));
			const FVector WanderCenter = CalculateSubgroupWanderCenter(SubgroupIndex, Group);
			const float WanderRadius = Group.SubgroupCount > 1
				? Group.Config.WanderRadius * Group.Config.SubgroupWanderRadiusScale
				: Group.Config.WanderRadius;
			FVector Destination = Anchor;
			for (int32 Attempt = 0; Attempt < 4; ++Attempt)
			{
				const float Angle = RandomStream.FRandRange(0.0f, 2.0f * UE_PI);
				const float Radius = FMath::Sqrt(RandomStream.FRand()) * WanderRadius;
				Destination = WanderCenter + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
				Destination.Z = Anchor.Z;
				if (FVector::DistSquared2D(Destination, Anchor) >= FMath::Square(Group.Config.MinWanderDistance))
				{
					break;
				}
			}

			TArray<FVector> NewPathPoints;
			bool bUsesNavmesh = false;
			if (!NavigationSystem->FindSharedPath(Anchor, Destination, NewPathPoints, &bUsesNavmesh)
				|| NewPathPoints.IsEmpty())
			{
				Subgroup.NextPathRefreshTime = CurrentTime + 1.0f;
				continue;
			}
			Subgroup.Destination = NewPathPoints.Last();
			Subgroup.SharedPathPoints = MoveTemp(NewPathPoints);
			Subgroup.bUsesNavmesh = bUsesNavmesh;
			Subgroup.NextPathRefreshTime = CurrentTime + Group.Config.MaxMoveTime;
			++Subgroup.Revision;
			++LastStats.AmbientSharedPathsBuilt;

#if ENABLE_DRAW_DEBUG
			if (Group.Config.bEnableVisualDebug && World)
			{
				FVector PreviousPoint = Anchor;
				for (FVector PathPoint : Subgroup.SharedPathPoints)
				{
					if (Subgroup.bUsesNavmesh && Group.Config.bConformToNavmeshHeight)
					{
						PathPoint.Z += Group.Config.NavigationHeightOffset;
					}
					DrawDebugLine(World, PreviousPoint, PathPoint, FColor::Cyan, false, Group.Config.MaxMoveTime, 0, 2.0f);
					PreviousPoint = PathPoint;
				}
			}
#endif
		}
	}
}

bool UMassUnitCrowdSystem::UpdateManagedSubgroupUnit(
	const FSpatialEntry& Entry,
	FCrowdGroup& Group,
	float CurrentTime,
	float LODIntervalMultiplier)
{
	if (!EntitySubsystem
		|| !UnitManager
		|| !Group.bUseNavigation
		|| Group.Config.MovementMode != EMassUnitCrowdMovementMode::Planar2D
		|| !Group.Config.bEnableManagedSubgroups)
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entry.Entity.ToMassEntityHandle());
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Entry.Entity.ToMassEntityHandle());
	FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(
		Entry.Entity.ToMassEntityHandle());
	if (!Crowd || !State || !Navigation || !Group.Subgroups.IsValidIndex(Crowd->SubgroupIndex))
	{
		return false;
	}
	const FCrowdSubgroupState& Subgroup = Group.Subgroups[Crowd->SubgroupIndex];
	if (Subgroup.SharedPathPoints.IsEmpty())
	{
		return false;
	}

	FVector Destination = CalculatePathLookAheadDestination(
		Entry.Location,
		Subgroup.SharedPathPoints,
		Group.Config.SubgroupPathLookAheadDistance);
	if (!Subgroup.bUsesNavmesh || !Group.Config.bConformToNavmeshHeight)
	{
		Destination.Z = Entry.Location.Z;
	}
	if (!UnitManager->SetUnitDestination(FMassUnitHandle(Entry.Entity), Destination, Group.AcceptanceRadius))
	{
		return false;
	}
	// SetUnitDestination deliberately creates a direct one-point path. Preserve
	// the shared corridor's provenance so the movement processor follows its raw
	// navmesh Z and applies the mesh-pivot offset exactly once.
	Navigation->bPathUsesNavmesh = Subgroup.bUsesNavmesh;

	FRandomStream SpeedStream = MakeRandomStream(Entry.Entity, Group, Subgroup.Revision);
	State->MoveSpeed = Crowd->BaseMoveSpeed * SpeedStream.FRandRange(
		Group.Config.MinMoveSpeedMultiplier,
		Group.Config.MaxMoveSpeedMultiplier);
	Crowd->LastDestination = Destination;
	Crowd->SharedPathRevision = Subgroup.Revision;
	Crowd->NextDecisionTime = CurrentTime + Group.Config.MaxMoveTime * LODIntervalMultiplier;
	++LastStats.DestinationsAssigned;
	RequestPresentationCue(
		Group,
		EMassUnitCrowdCue::MovementStarted,
		Entry.Location,
		Crowd->SubgroupIndex);
	return true;
}

void UMassUnitCrowdSystem::UpdateUnit(
	const FSpatialEntry& Entry,
	FCrowdGroup& Group,
	const TArray<FSpatialEntry>& Entries,
	const TMap<FIntVector, TArray<int32>>& Cells,
	const TMap<FMassUnitEntityHandle, int32>& EntryByEntity,
	const TArray<FVector>& ObserverLocations,
	float CurrentTime,
	bool bForceDecision)
{
	if (!IsEntityValid(Entry.Entity) || !EntitySubsystem)
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = Entry.Entity.ToMassEntityHandle();
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(NativeHandle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle);
	FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(NativeHandle);
	if (!Crowd || !State || !Target || !Navigation || !Crowd->bEnabled)
	{
		return;
	}

	float ObserverDistanceSquared = 0.0f;
	const int32 LODLevel = CalculateSimulationLOD(Entry.Location, ObserverLocations, ObserverDistanceSquared);
	const float LODMultiplier = GetSimulationIntervalMultiplier(LODLevel);
	Crowd->SimulationLOD = LODLevel;
	Crowd->bUse3DMovement = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
	Crowd->bConformToNavmeshHeight = Group.bUseNavigation
		&& !Crowd->bUse3DMovement
		&& Group.Config.bConformToNavmeshHeight;
	Crowd->NavigationHeightOffset = Group.Config.NavigationHeightOffset;

	const bool bHasEngagementTarget = Group.bEngaged && Group.TargetActor.IsValid();
	const bool bShouldSleep = !bHasEngagementTarget
		&& !ObserverLocations.IsEmpty()
		&& Group.Config.MaxSimulationDistance > 0.0f
		&& ObserverDistanceSquared > FMath::Square(Group.Config.MaxSimulationDistance);
	if (bShouldSleep)
	{
		SetUnitSleeping(Entry.Entity, true, CurrentTime);
		return;
	}
	if (Crowd->bSleeping)
	{
		SetUnitSleeping(Entry.Entity, false, CurrentTime);
		bForceDecision = true;
	}

	if (State->CurrentState == EMassUnitState::Dead || State->CurrentState == EMassUnitState::Stunned)
	{
		Crowd->SteeringDirection = FVector::ZeroVector;
		return;
	}
	if (bHasEngagementTarget)
	{
		if (bForceDecision || CurrentTime >= Crowd->NextSteeringUpdateTime)
		{
			Crowd->SteeringDirection = CalculateSeparation(Entry, Group, Entries, Cells);
			Crowd->NextSteeringUpdateTime = CurrentTime + (UpdateInterval * LODMultiplier);
		}
		// A routed group's units keep fighting until the staggered rout reaches them, then flee.
		if (Group.bCollectiveEnabled && Group.bRouting && HasRoutReachedUnit(Entry.Entity, Group, CurrentTime))
		{
			UpdateRoutingUnit(Entry, Group, CurrentTime, bForceDecision);
			return;
		}
		UpdateEngagedUnit(Entry, Group, CurrentTime, bForceDecision);
		return;
	}
	if (Group.bHoldAfterEngagement)
	{
		Crowd->SteeringDirection = FVector::ZeroVector;
		return;
	}

	if (Crowd->InteractionEndTime > CurrentTime)
	{
		Crowd->SteeringDirection = FVector::ZeroVector;
		return;
	}
	if (Crowd->InteractionEndTime > 0.0f)
	{
		Crowd->InteractionEndTime = 0.0f;
		Crowd->InteractionPartner.Invalidate();
		if (State->CurrentState == EMassUnitState::Interacting)
		{
			State->CurrentState = EMassUnitState::Idle;
			State->StateTime = 0.0f;
		}
	}

	if (bForceDecision || CurrentTime >= Crowd->NextSteeringUpdateTime)
	{
		Crowd->SteeringDirection = CalculateSeparation(Entry, Group, Entries, Cells);
		Crowd->NextSteeringUpdateTime = CurrentTime + (UpdateInterval * LODMultiplier);
	}

	const bool bUsesManagedAmbientPath = Group.bUseNavigation
		&& Group.Config.MovementMode == EMassUnitCrowdMovementMode::Planar2D
		&& Group.Config.bEnableManagedSubgroups
		&& Group.Subgroups.IsValidIndex(Crowd->SubgroupIndex)
		&& !Group.Subgroups[Crowd->SubgroupIndex].SharedPathPoints.IsEmpty();
	if (bUsesManagedAmbientPath
		&& Crowd->SharedPathRevision != Group.Subgroups[Crowd->SubgroupIndex].Revision
		&& (Target->bHasTargetLocation || Navigation->bPathRequested || Navigation->bPathValid))
	{
		ClearMovement(Entry.Entity, true);
		bForceDecision = true;
	}

	const bool bHasMovementTarget = Target->bHasTargetLocation || Navigation->bPathRequested || Navigation->bPathValid;
	if (bHasMovementTarget)
	{
		const FVector Destination = Target->bHasTargetLocation ? Target->TargetLocation : Navigation->DestinationLocation;
		const FVector ToDestination = Destination - Entry.Location;
		const float DistanceSquared = Crowd->bUse3DMovement ? ToDestination.SizeSquared() : ToDestination.SizeSquared2D();
		const bool bReached = DistanceSquared <= FMath::Square(Group.AcceptanceRadius);
		const bool bTimedOut = CurrentTime >= Crowd->NextDecisionTime;
		if (!bReached && !bTimedOut && !bForceDecision)
		{
			return;
		}

		ClearMovement(Entry.Entity, true);
		Crowd->NextDecisionTime = CurrentTime;
		if (!bForceDecision && bReached)
		{
			FRandomStream IdleStream = MakeRandomStream(Entry.Entity, Group, Crowd->DecisionSequence++);
			Crowd->NextDecisionTime = CurrentTime
				+ IdleStream.FRandRange(Group.Config.MinIdleTime, Group.Config.MaxIdleTime) * LODMultiplier;
			return;
		}
	}

	if (!bForceDecision && CurrentTime < Crowd->NextDecisionTime)
	{
		return;
	}

	FRandomStream RandomStream = MakeRandomStream(Entry.Entity, Group, Crowd->DecisionSequence++);
	if (Group.Config.bEnableInteractions
		&& Group.Config.InteractionChance > 0.0f
		&& RandomStream.FRand() <= Group.Config.InteractionChance)
	{
		const FMassUnitEntityHandle Partner = FindInteractionPartner(Entry, Group, Entries, Cells, CurrentTime);
		if (Partner.IsValid() && EntryByEntity.Contains(Partner))
		{
			BeginInteraction(Entry.Entity, Partner, Group, CurrentTime, RandomStream);
			return;
		}
	}
	if (UpdateManagedSubgroupUnit(Entry, Group, CurrentTime, LODMultiplier))
	{
		return;
	}

	AssignRandomDestination(Entry.Entity, Group, CurrentTime, LODMultiplier, RandomStream);
}

FVector UMassUnitCrowdSystem::CalculateSeparation(
	const FSpatialEntry& Entry,
	const FCrowdGroup& Group,
	const TArray<FSpatialEntry>& Entries,
	const TMap<FIntVector, TArray<int32>>& Cells)
{
	if (!Group.Config.bEnableSeparation || Group.Config.SeparationWeight <= 0.0f)
	{
		return FVector::ZeroVector;
	}

	const float Radius = Group.Config.SeparationRadius;
	const float RadiusSquared = FMath::Square(Radius);
	const int32 CellRange = FMath::Max(1, FMath::CeilToInt(Radius / SpatialCellSize));
	const bool bUse3DMovement = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
	const FIntVector CenterCell = ToSpatialCell(Entry.Location, bUse3DMovement);
	const int32 MinCellZ = bUse3DMovement ? CenterCell.Z - CellRange : 0;
	const int32 MaxCellZ = bUse3DMovement ? CenterCell.Z + CellRange : 0;
	FVector Separation = FVector::ZeroVector;
	for (int32 CellX = CenterCell.X - CellRange; CellX <= CenterCell.X + CellRange; ++CellX)
	{
		for (int32 CellY = CenterCell.Y - CellRange; CellY <= CenterCell.Y + CellRange; ++CellY)
		{
			for (int32 CellZ = MinCellZ; CellZ <= MaxCellZ; ++CellZ)
			{
				const TArray<int32>* CellEntries = Cells.Find(FIntVector(CellX, CellY, CellZ));
				if (!CellEntries)
				{
					continue;
				}
				for (const int32 EntryIndex : *CellEntries)
				{
					if (!Entries.IsValidIndex(EntryIndex)
						|| Entries[EntryIndex].Entity == Entry.Entity
						|| Entries[EntryIndex].GroupHandle != Group.Handle)
					{
						continue;
					}
					++LastStats.NeighborChecks;
					const FVector Away = Entry.Location - Entries[EntryIndex].Location;
					const float DistanceSquared = bUse3DMovement ? Away.SizeSquared() : Away.SizeSquared2D();
					if (DistanceSquared <= UE_SMALL_NUMBER || DistanceSquared > RadiusSquared)
					{
						continue;
					}
					const float Distance = FMath::Sqrt(DistanceSquared);
					const FVector AwayDirection = bUse3DMovement ? Away.GetSafeNormal() : Away.GetSafeNormal2D();
					Separation += AwayDirection * (1.0f - (Distance / Radius));
				}
			}
		}
	}
	return Separation.GetClampedToMaxSize(1.0f) * Group.Config.SeparationWeight;
}

FMassUnitEntityHandle UMassUnitCrowdSystem::FindInteractionPartner(
	const FSpatialEntry& Entry,
	const FCrowdGroup& Group,
	const TArray<FSpatialEntry>& Entries,
	const TMap<FIntVector, TArray<int32>>& Cells,
	float CurrentTime)
{
	if (!EntitySubsystem)
	{
		return {};
	}

	const float RadiusSquared = FMath::Square(Group.Config.InteractionRadius);
	const int32 CellRange = FMath::Max(1, FMath::CeilToInt(Group.Config.InteractionRadius / SpatialCellSize));
	const bool bUse3DMovement = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
	const FIntVector CenterCell = ToSpatialCell(Entry.Location, bUse3DMovement);
	const int32 MinCellZ = bUse3DMovement ? CenterCell.Z - CellRange : 0;
	const int32 MaxCellZ = bUse3DMovement ? CenterCell.Z + CellRange : 0;
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	FMassUnitEntityHandle BestPartner;
	float BestDistanceSquared = RadiusSquared;
	for (int32 CellX = CenterCell.X - CellRange; CellX <= CenterCell.X + CellRange; ++CellX)
	{
		for (int32 CellY = CenterCell.Y - CellRange; CellY <= CenterCell.Y + CellRange; ++CellY)
		{
			for (int32 CellZ = MinCellZ; CellZ <= MaxCellZ; ++CellZ)
			{
				const TArray<int32>* CellEntries = Cells.Find(FIntVector(CellX, CellY, CellZ));
				if (!CellEntries)
				{
					continue;
				}
				for (const int32 EntryIndex : *CellEntries)
				{
					if (!Entries.IsValidIndex(EntryIndex))
					{
						continue;
					}
					const FSpatialEntry& Candidate = Entries[EntryIndex];
					if (Candidate.Entity == Entry.Entity || Candidate.GroupHandle != Group.Handle)
					{
						continue;
					}
					++LastStats.NeighborChecks;
					const FVector Delta = Entry.Location - Candidate.Location;
					const float DistanceSquared = bUse3DMovement ? Delta.SizeSquared() : Delta.SizeSquared2D();
					if (DistanceSquared > BestDistanceSquared)
					{
						continue;
					}
					const FMassUnitCrowdFragment* CandidateCrowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Candidate.Entity.ToMassEntityHandle());
					const FMassUnitStateFragment* CandidateState = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Candidate.Entity.ToMassEntityHandle());
					if (!CandidateCrowd || !CandidateState || !CandidateCrowd->bEnabled || CandidateCrowd->bSleeping
						|| CandidateCrowd->InteractionEndTime > CurrentTime
						|| CandidateState->CurrentState == EMassUnitState::Dead
						|| CandidateState->CurrentState == EMassUnitState::Stunned)
					{
						continue;
					}
					BestPartner = Candidate.Entity;
					BestDistanceSquared = DistanceSquared;
				}
			}
		}
	}
	return BestPartner;
}

void UMassUnitCrowdSystem::BeginInteraction(
	FMassUnitEntityHandle UnitA,
	FMassUnitEntityHandle UnitB,
	FCrowdGroup& Group,
	float CurrentTime,
	FRandomStream& RandomStream)
{
	if (!EntitySubsystem || !IsEntityValid(UnitA) || !IsEntityValid(UnitB))
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const float Duration = RandomStream.FRandRange(Group.Config.MinInteractionTime, Group.Config.MaxInteractionTime);
	const float EndTime = CurrentTime + Duration;
	const FMassUnitTransformFragment* TransformA = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(UnitA.ToMassEntityHandle());
	const FMassUnitTransformFragment* TransformB = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(UnitB.ToMassEntityHandle());
	if (!TransformA || !TransformB)
	{
		return;
	}

	const FVector ToUnitB = TransformB->GetTransform().GetLocation() - TransformA->GetTransform().GetLocation();
	const FVector DirectionA = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D
		? ToUnitB.GetSafeNormal()
		: ToUnitB.GetSafeNormal2D();
	const FVector DirectionB = -DirectionA;
	const TPair<FMassUnitEntityHandle, FVector> Units[] = {
		{UnitA, DirectionA},
		{UnitB, DirectionB}
	};
	for (const TPair<FMassUnitEntityHandle, FVector>& Pair : Units)
	{
		ClearMovement(Pair.Key, false);
		FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Pair.Key.ToMassEntityHandle());
		FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Pair.Key.ToMassEntityHandle());
		FMassUnitLookAtFragment* LookAt = EntityManager.GetFragmentDataPtr<FMassUnitLookAtFragment>(Pair.Key.ToMassEntityHandle());
		if (Crowd && State && LookAt)
		{
			Crowd->InteractionPartner = Pair.Key == UnitA ? UnitB : UnitA;
			Crowd->InteractionEndTime = EndTime;
			Crowd->NextDecisionTime = EndTime + RandomStream.FRandRange(Group.Config.MinIdleTime, Group.Config.MaxIdleTime);
			Crowd->SteeringDirection = FVector::ZeroVector;
			State->CurrentState = EMassUnitState::Interacting;
			State->StateTime = 0.0f;
			LookAt->Direction = Pair.Value;
		}
	}

	++LastStats.InteractionsStarted;
	if (const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(UnitA.ToMassEntityHandle()))
	{
		RequestPresentationCue(
			Group,
			EMassUnitCrowdCue::AmbientInteraction,
			(TransformA->GetTransform().GetLocation() + TransformB->GetTransform().GetLocation()) * 0.5f,
			Crowd->SubgroupIndex);
	}
	OnCrowdInteractionStarted.Broadcast(FMassUnitHandle(UnitA), FMassUnitHandle(UnitB));
#if ENABLE_DRAW_DEBUG
	if (Group.Config.bEnableVisualDebug && World)
	{
		DrawDebugLine(
			World,
			TransformA->GetTransform().GetLocation(),
			TransformB->GetTransform().GetLocation(),
			FColor::Orange,
			false,
			FMath::Max(0.1f, Duration),
			0,
			2.0f);
	}
#endif
}

bool UMassUnitCrowdSystem::AssignRandomDestination(
	FMassUnitEntityHandle Entity,
	FCrowdGroup& Group,
	float CurrentTime,
	float LODIntervalMultiplier,
	FRandomStream& RandomStream)
{
	if (!EntitySubsystem || !UnitManager || !IsEntityValid(Entity))
	{
		return false;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle());
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Entity.ToMassEntityHandle());
	const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Entity.ToMassEntityHandle());
	if (!Crowd || !State || !Transform)
	{
		return false;
	}

	const FVector CurrentLocation = Transform->GetTransform().GetLocation();
	const FVector WanderCenter = CalculateSubgroupWanderCenter(Entity, Group);
	const float WanderRadius = Group.Config.bEnableManagedSubgroups && Group.SubgroupCount > 1
		? Group.Config.WanderRadius * Group.Config.SubgroupWanderRadiusScale
		: Group.Config.WanderRadius;
	FVector Destination = CurrentLocation;
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		if (Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D)
		{
			const float Radius = FMath::Pow(RandomStream.FRand(), 1.0f / 3.0f) * WanderRadius;
			Destination = WanderCenter + RandomStream.VRand() * Radius;
		}
		else
		{
			const float Angle = RandomStream.FRandRange(0.0f, 2.0f * UE_PI);
			const float Radius = FMath::Sqrt(RandomStream.FRand()) * WanderRadius;
			Destination = WanderCenter + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
			Destination.Z = CurrentLocation.Z;
		}
		const FVector ToDestination = Destination - CurrentLocation;
		const float DistanceSquared = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D
			? ToDestination.SizeSquared()
			: ToDestination.SizeSquared2D();
		if (DistanceSquared >= FMath::Square(Group.Config.MinWanderDistance))
		{
			break;
		}
	}

	const bool bCanUsePlanarNavigation = Group.bUseNavigation
		&& Group.Config.MovementMode == EMassUnitCrowdMovementMode::Planar2D;
	const bool bAssigned = bCanUsePlanarNavigation && NavigationSystem
		? NavigationSystem->RequestPathInternal(Entity, Destination, Group.AcceptanceRadius)
		: UnitManager->SetUnitDestination(FMassUnitHandle(Entity), Destination, Group.AcceptanceRadius);
	if (!bAssigned)
	{
		Crowd->NextDecisionTime = CurrentTime + UpdateInterval * LODIntervalMultiplier;
		return false;
	}

	const float SpeedMultiplier = RandomStream.FRandRange(
		Group.Config.MinMoveSpeedMultiplier,
		Group.Config.MaxMoveSpeedMultiplier);
	State->MoveSpeed = Crowd->BaseMoveSpeed * SpeedMultiplier;
	Crowd->LastDestination = Destination;
	Crowd->NextDecisionTime = CurrentTime + Group.Config.MaxMoveTime * LODIntervalMultiplier;
	++LastStats.DestinationsAssigned;
	RequestPresentationCue(
		Group,
		EMassUnitCrowdCue::MovementStarted,
		CurrentLocation,
		Crowd->SubgroupIndex);

#if ENABLE_DRAW_DEBUG
	if (Group.Config.bEnableVisualDebug && World)
	{
		DrawDebugDirectionalArrow(World, CurrentLocation, Destination, 60.0f, FColor::Cyan, false, 1.0f, 0, 1.5f);
		DrawDebugCircle(World, Group.Center, Group.Config.WanderRadius, 64, FColor::White, false, 1.0f, 0, 1.0f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
#endif
	return true;
}

void UMassUnitCrowdSystem::ClearMovement(FMassUnitEntityHandle Entity, bool bSetIdle) const
{
	if (!EntitySubsystem || !IsEntityValid(Entity))
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
	if (NavigationSystem)
	{
		NavigationSystem->CancelPathInternal(Entity);
	}
	if (FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle))
	{
		Target->Clear();
	}
	if (FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(NativeHandle))
	{
		Navigation->ResetPath();
	}
	if (FMassUnitVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(NativeHandle))
	{
		Velocity->Value = FVector::ZeroVector;
	}
	if (bSetIdle)
	{
		if (FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle))
		{
			if (State->CurrentState != EMassUnitState::Dead && State->CurrentState != EMassUnitState::Stunned)
			{
				State->CurrentState = EMassUnitState::Idle;
				State->StateTime = 0.0f;
			}
		}
	}
}

void UMassUnitCrowdSystem::SetUnitSleeping(FMassUnitEntityHandle Entity, bool bSleeping, float CurrentTime) const
{
	if (!EntitySubsystem || !IsEntityValid(Entity))
	{
		return;
	}
	FMassUnitCrowdFragment* Crowd = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle());
	if (!Crowd || Crowd->bSleeping == bSleeping)
	{
		return;
	}
	Crowd->bSleeping = bSleeping;
	Crowd->SteeringDirection = FVector::ZeroVector;
	Crowd->NextDecisionTime = CurrentTime;
	if (bSleeping)
	{
		ClearMovement(Entity, true);
	}
}

void UMassUnitCrowdSystem::ResetCrowdFragment(FMassUnitEntityHandle Entity, bool bStopUnit) const
{
	if (!EntitySubsystem || !IsEntityValid(Entity))
	{
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(NativeHandle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	if (Crowd && State && Crowd->BaseMoveSpeed > 0.0f)
	{
		State->MoveSpeed = Crowd->BaseMoveSpeed;
	}
	if (Crowd)
	{
		Crowd->Reset();
	}
	if (bStopUnit)
	{
		ClearMovement(Entity, true);
	}
}

void UMassUnitCrowdSystem::RemoveUnitFromPreviousGroup(FMassUnitEntityHandle Entity)
{
	int32 PreviousGroupHandle = INDEX_NONE;
	if (!UnitToGroup.RemoveAndCopyValue(Entity, PreviousGroupHandle))
	{
		return;
	}
	if (FCrowdGroup* PreviousGroup = Groups.Find(PreviousGroupHandle))
	{
		PreviousGroup->Units.Remove(Entity);
		if (PreviousGroup->Units.IsEmpty())
		{
			Groups.Remove(PreviousGroupHandle);
		}
	}
}

void UMassUnitCrowdSystem::UpdateGroupEngagements(float CurrentTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_CrowdEngagements);
	// Expire tokens once per update for every group. Acquisition also prunes, but that only happens
	// while somebody is in range; without this pass a group whose units all withdrew would keep
	// reporting its last holders through ActiveAttackers forever.
	for (TPair<int32, FCrowdGroup>& Pair : Groups)
	{
		if (!Pair.Value.AttackTokenExpiry.IsEmpty())
		{
			PruneAttackTokens(Pair.Value, CurrentTime);
		}
	}

	// Collective behavior runs first so a threat-table retarget can rescue an engagement whose target
	// just became invalid, and so recruitment decided this update is visible to the pass below.
	UpdateCollectiveBehavior(CurrentTime);

	TArray<int32> GroupHandles;
	Groups.GetKeys(GroupHandles);
	for (const int32 GroupHandle : GroupHandles)
	{
		FCrowdGroup* Group = Groups.Find(GroupHandle);
		if (!Group || !Group->bEngagementEnabled || Group->bPaused)
		{
			continue;
		}

		if (Group->bEngaged)
		{
			AActor* TargetActor = Group->TargetActor.Get();
			// A target the project's hostility rule now rejects (recruited, team swapped) is dropped like a
			// destroyed one. With collective threat the lost-target rescue then picks the next hostile actor.
			if (!IsValid(TargetActor)
				|| (TargetFilter.IsBound() && !PassesTargetFilter(TargetActor, Group->bCollectiveEnabled ? Group->TeamID : ResolveGroupTeamID(*Group))))
			{
				const bool bReturnToWander = Group->EngagementConfig.bReturnToWanderOnDeactivate;
				DeactivateCrowdGroupEngagement(GroupHandle, bReturnToWander);
				continue;
			}

			if (Group->EngagementConfig.bAutoDeactivate
				&& Group->EngagementConfig.ActivationMode != EMassUnitCrowdActivationMode::Always
				&& CalculateClosestLivingUnitDistanceSquared(*Group, TargetActor->GetActorLocation())
					> FMath::Square(Group->EngagementConfig.DeactivationDistance))
			{
				const bool bReturnToWander = Group->EngagementConfig.bReturnToWanderOnDeactivate;
				DeactivateCrowdGroupEngagement(GroupHandle, bReturnToWander);
				continue;
			}

			const bool bTargetRefreshed = CurrentTime >= Group->NextTargetRefreshTime;
			if (bTargetRefreshed)
			{
				Group->LastTargetLocation = TargetActor->GetActorLocation();
				Group->NextTargetRefreshTime = CurrentTime + Group->EngagementConfig.TargetRefreshInterval;
			}
			RefreshSharedNavigationPath(*Group, CurrentTime);
#if ENABLE_DRAW_DEBUG
			const bool bUsesSharedPath = Group->bUseNavigation
				&& Group->Config.MovementMode == EMassUnitCrowdMovementMode::Planar2D
				&& Group->EngagementConfig.bUseSharedNavigationPath;
			if (bTargetRefreshed && !bUsesSharedPath && Group->EngagementConfig.bEnableVisualDebug && World)
			{
				DrawDebugDirectionalArrow(
					World,
					CalculateGroupAnchor(*Group),
					Group->LastTargetLocation,
					100.0f,
					FColor::Purple,
					false,
					Group->EngagementConfig.TargetRefreshInterval + 0.05f,
					0,
					3.0f);
			}
#endif
			continue;
		}

		// A routed group does not re-acquire players on its own until it rallies.
		if (Group->bCollectiveEnabled && Group->bRouting)
		{
			continue;
		}

		AActor* AutomaticTarget = nullptr;
		switch (Group->EngagementConfig.ActivationMode)
		{
		case EMassUnitCrowdActivationMode::OnPlayerProximity:
			AutomaticTarget = FindClosestPlayerTarget(Group->Center, Group->EngagementConfig.ActivationRadius, TargetFilter.IsBound() ? ResolveGroupTeamID(*Group) : INDEX_NONE);
			break;
		case EMassUnitCrowdActivationMode::Always:
			AutomaticTarget = FindClosestPlayerTarget(Group->Center, 0.0f, TargetFilter.IsBound() ? ResolveGroupTeamID(*Group) : INDEX_NONE);
			break;
		default:
			break;
		}
		if (AutomaticTarget)
		{
			ActivateCrowdGroupForActor(GroupHandle, AutomaticTarget);
		}
	}
}

bool UMassUnitCrowdSystem::UpdateEngagedUnit(
	const FSpatialEntry& Entry,
	FCrowdGroup& Group,
	float CurrentTime,
	bool bForceDecision)
{
	AActor* TargetActor = Group.TargetActor.Get();
	if (!EntitySubsystem || !UnitManager || !IsValid(TargetActor) || !IsEntityValid(Entry.Entity))
	{
		return false;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = Entry.Entity.ToMassEntityHandle();
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(NativeHandle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle);
	FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(NativeHandle);
	FMassUnitLookAtFragment* LookAt = EntityManager.GetFragmentDataPtr<FMassUnitLookAtFragment>(NativeHandle);
	FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle);
	if (!Crowd || !State || !Target || !Navigation || !LookAt || !Visual)
	{
		return false;
	}

	const bool bUse3DMovement = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
	const FVector TargetLocation = Group.LastTargetLocation;
	const FVector ToTarget = TargetLocation - Entry.Location;
	const float DistanceSquared = bUse3DMovement ? ToTarget.SizeSquared() : ToTarget.SizeSquared2D();
	const FVector LookDirection = bUse3DMovement ? ToTarget.GetSafeNormal() : ToTarget.GetSafeNormal2D();
	if (!LookDirection.IsNearlyZero())
	{
		LookAt->Direction = LookDirection;
	}

	State->MoveSpeed = Crowd->BaseMoveSpeed * Group.EngagementConfig.EngagedMoveSpeedMultiplier;
	const float AttackRange = Group.EngagementConfig.AttackRangeOverride > 0.0f
		? Group.EngagementConfig.AttackRangeOverride
		: State->AttackRange;
	const bool bInAttackRange = Group.EngagementConfig.bEnableAttacks
		&& AttackRange > 0.0f
		&& DistanceSquared <= FMath::Square(AttackRange);

	// A unit already mid-cooldown keeps its token so it is not displaced by a newcomer between swings.
	//
	// The hold must outlast the time until this unit is next visited, not just its attack cooldown.
	// Engaged units are processed round-robin under MaxUnitsPerUpdate, so a large group revisits any
	// given unit only every ceil(GroupSize / Budget) updates. If the token expired in that gap, a
	// holder that is still mid-attack would silently release it and a second unit would attack in its
	// place, leaking the cap exactly when the crowd is largest. Over-holding only ever yields fewer
	// simultaneous attackers, so erring long is the safe direction.
	const int32 UpdatesPerCycle = MaxUnitsPerUpdate > 0
		? FMath::CeilToInt(static_cast<float>(Group.Units.Num()) / static_cast<float>(MaxUnitsPerUpdate))
		: 1;
	const float TokenHoldDuration = FMath::Max(
		State->AttackCooldown,
		UpdateInterval * static_cast<float>(FMath::Max(1, UpdatesPerCycle)));
	const bool bHasAttackToken = bInAttackRange
		&& TryAcquireAttackToken(Group, Entry.Entity, CurrentTime, TokenHoldDuration);

	// Attack-state debug. This is the tool for confirming token behaviour with placeholder meshes,
	// before any attack animation exists: red means swinging, yellow means in range but waiting for a
	// token. A solid red ring means MaxSimultaneousAttackers is unset or too high.
	if (bInAttackRange && Group.EngagementConfig.bEnableVisualDebug && World)
	{
		const FColor StateColor = bHasAttackToken ? FColor::Red : FColor::Yellow;
		DrawDebugSphere(World, Entry.Location + FVector(0.0f, 0.0f, 90.0f), 22.0f, 8, StateColor, false, UpdateInterval * 1.5f, 0, 1.5f);
		if (bHasAttackToken)
		{
			DrawDebugLine(World, Entry.Location, TargetLocation, FColor::Red, false, UpdateInterval * 1.5f, 0, 2.0f);
		}
	}

	if (bHasAttackToken)
	{
		ClearMovement(Entry.Entity, false);
		Crowd->SteeringDirection = FVector::ZeroVector;
		if (State->CurrentState != EMassUnitState::Attacking)
		{
			State->CurrentState = EMassUnitState::Attacking;
			State->StateTime = 0.0f;
		}
		Visual->CurrentAnimation = UE::MassUnitSystem::Tags::AnimationAttack();
		if (State->AttackCooldownRemaining <= 0.0f && State->PendingAttackDamage <= 0.0f)
		{
			// Each swing restarts the attack clip (new start stamp and AttackSerial) and lands its damage
			// at the template's AttackImpactDelay. With a zero delay the consume below fires this same
			// update, which is exactly the behaviour from before the delay existed.
			State->BeginAttackSwing(State->BaseDamage
				* static_cast<float>(FMath::Max(1, State->UnitLevel))
				* Group.EngagementConfig.DamageMultiplier);
			State->AttackCooldownRemaining = FMath::Max(UpdateInterval, State->AttackCooldown);
		}
		// Engaged units are revisited round-robin, so a non-zero impact lands on the first visit after
		// the delay elapses rather than on the exact frame.
		const float ImpactDamage = State->ConsumeAttackImpact();
		if (ImpactDamage > 0.0f)
		{
			RequestActorAttack(Entry.Entity, Group, TargetActor, ImpactDamage);
		}
		return true;
	}

	// Out of range or out of a token before the impact frame: the swing whiffs.
	State->PendingAttackDamage = 0.0f;
	if (State->CurrentState == EMassUnitState::Attacking)
	{
		State->CurrentState = EMassUnitState::Idle;
		State->StateTime = 0.0f;
	}

	if (!bForceDecision && CurrentTime < Crowd->NextFollowUpdateTime)
	{
		return true;
	}
	Crowd->NextFollowUpdateTime = CurrentTime + Group.EngagementConfig.TargetRefreshInterval;

	FVector FollowOffset = CalculateFollowOffset(Entry.Entity, Group);
	if (Group.EngagementConfig.bEnableAttacks && AttackRange > 0.0f)
	{
		// Units that wanted to attack but found the token pool full wait on a ring outside attack
		// range instead of pressing in. Without this they crowd the target and shove each other while
		// permanently unable to swing, which reads far worse than simply waiting a turn.
		const bool bWaitingForToken = bInAttackRange && !bHasAttackToken;
		const float MaximumApproachRadius = bWaitingForToken
			? AttackRange * FMath::Max(1.05f, Group.EngagementConfig.StandoffDistanceMultiplier)
			: AttackRange * 0.75f;
		if (bWaitingForToken)
		{
			// Push the unit out to the standoff ring rather than clamping, so it actually backs off.
			// A group configured with zero follow distance and spread yields a zero-length offset, which
			// would put the standoff point on top of the target and oscillate the unit in and out of
			// range forever; fall back to the direction it is already standing in.
			FVector StandoffDirection = FollowOffset.GetSafeNormal2D();
			if (StandoffDirection.IsNearlyZero())
			{
				StandoffDirection = (Entry.Location - TargetLocation).GetSafeNormal2D();
			}
			if (StandoffDirection.IsNearlyZero())
			{
				StandoffDirection = FVector::ForwardVector;
			}
			FollowOffset = StandoffDirection * MaximumApproachRadius;
		}
		else
		{
			FollowOffset = FollowOffset.GetClampedToMaxSize(MaximumApproachRadius);
		}
	}
	const bool bUseSharedPath = Group.bUseNavigation
		&& !bUse3DMovement
		&& Group.EngagementConfig.bUseSharedNavigationPath
		&& !Group.SharedPathPoints.IsEmpty();
	FVector Destination = TargetLocation + FollowOffset;
	if (bUseSharedPath)
	{
		const FVector SharedDestination = CalculateSharedPathDestination(Entry, Group);
		const bool bAtPathEnd = FVector::DistSquared2D(SharedDestination, Group.SharedPathPoints.Last()) <= 1.0f;
		Destination = bAtPathEnd ? Group.SharedPathPoints.Last() + FollowOffset : SharedDestination;
		// A shared corridor avoids an individual path query, but each final
		// formation/follow slot can land on a slightly different part of a slope.
		// Project that endpoint once so spread does not bury edge units in terrain.
		if (bAtPathEnd
			&& Group.bSharedPathUsesNavmesh
			&& Group.Config.bConformToNavmeshHeight
			&& NavigationSystem)
		{
			FVector ProjectedSlot;
			if (NavigationSystem->ProjectPointToNavigation(Destination, ProjectedSlot))
			{
				Destination.Z = ProjectedSlot.Z;
			}
		}
	}
	if (!bUse3DMovement && !bUseSharedPath)
	{
		Destination.Z = Entry.Location.Z;
	}

	const bool bHasMovementTarget = Target->bHasTargetLocation || Navigation->bPathRequested || Navigation->bPathValid;
	const FVector TargetMovement = TargetLocation - Crowd->LastFollowTargetLocation;
	const FVector DestinationMovement = Destination - Crowd->LastDestination;
	const float TargetMovementSquared = bUse3DMovement ? TargetMovement.SizeSquared() : TargetMovement.SizeSquared2D();
	const float DestinationMovementSquared = bUse3DMovement ? DestinationMovement.SizeSquared() : DestinationMovement.SizeSquared2D();
	const float RepathDistanceSquared = FMath::Square(Group.EngagementConfig.RepathDistance);
	if (!bForceDecision && bHasMovementTarget
		&& TargetMovementSquared < RepathDistanceSquared
		&& DestinationMovementSquared < RepathDistanceSquared)
	{
		return true;
	}

	const bool bCanUsePlanarNavigation = Group.bUseNavigation && !bUse3DMovement && NavigationSystem;
	bool bAssigned = false;
	if (bCanUsePlanarNavigation && !Group.EngagementConfig.bUseSharedNavigationPath)
	{
		bAssigned = NavigationSystem->RequestPathInternal(Entry.Entity, Destination, Group.AcceptanceRadius);
		if (bAssigned)
		{
			++LastStats.PerUnitPathsRequested;
		}
	}
	else if (bCanUsePlanarNavigation && Group.EngagementConfig.bUseSharedNavigationPath
		&& Group.SharedPathPoints.IsEmpty())
	{
		bAssigned = false;
	}
	else
	{
		bAssigned = UnitManager->SetUnitDestination(FMassUnitHandle(Entry.Entity), Destination, Group.AcceptanceRadius);
		if (bAssigned && bUseSharedPath)
		{
			// Preserve navmesh provenance after assigning the shared look-ahead
			// point. The movement processor owns the single pivot-offset add.
			Navigation->bPathUsesNavmesh = Group.bSharedPathUsesNavmesh;
		}
	}
	if (!bAssigned)
	{
		Crowd->NextFollowUpdateTime = CurrentTime + UpdateInterval;
		return false;
	}

	Crowd->LastFollowTargetLocation = TargetLocation;
	Crowd->LastDestination = Destination;
	Crowd->NextDecisionTime = TNumericLimits<float>::Max();
	++LastStats.DestinationsAssigned;
#if ENABLE_DRAW_DEBUG
	if (Group.EngagementConfig.bEnableVisualDebug
		&& Group.EngagementConfig.bDrawUnitDestinations
		&& World)
	{
		FVector DebugDestination = Destination;
		if (bUseSharedPath && Group.bSharedPathUsesNavmesh && Group.Config.bConformToNavmeshHeight)
		{
			DebugDestination.Z += Group.Config.NavigationHeightOffset;
		}
		DrawDebugDirectionalArrow(World, Entry.Location, DebugDestination, 60.0f, FColor::Red, false, 0.5f, 0, 1.5f);
	}
#endif
	return true;
}

bool UMassUnitCrowdSystem::RefreshSharedNavigationPath(
	FCrowdGroup& Group,
	float CurrentTime,
	bool bForceRefresh)
{
	if (!NavigationSystem
		|| !Group.bUseNavigation
		|| Group.Config.MovementMode != EMassUnitCrowdMovementMode::Planar2D
		|| !Group.EngagementConfig.bUseSharedNavigationPath
		|| !Group.bEngaged
		|| !Group.TargetActor.IsValid())
	{
		return false;
	}

	const FVector TargetMovement = Group.LastTargetLocation - Group.SharedPathTargetLocation;
	const bool bTargetMovedEnough = TargetMovement.SizeSquared2D()
		>= FMath::Square(Group.EngagementConfig.SharedPathRepathDistance);
	if (!bForceRefresh
		&& ((!Group.SharedPathPoints.IsEmpty() && !bTargetMovedEnough)
			|| CurrentTime < Group.NextSharedPathUpdateTime))
	{
		return false;
	}

	Group.NextSharedPathUpdateTime = CurrentTime + Group.EngagementConfig.SharedPathRepathInterval;
	const FVector GroupAnchor = CalculateGroupAnchor(Group);
	TArray<FVector> NewPathPoints;
	bool bUsesNavmesh = false;
	if (!NavigationSystem->FindSharedPath(
		GroupAnchor,
		Group.LastTargetLocation,
		NewPathPoints,
		&bUsesNavmesh)
		|| NewPathPoints.IsEmpty())
	{
		return false;
	}

	Group.SharedPathPoints = MoveTemp(NewPathPoints);
	Group.bSharedPathUsesNavmesh = bUsesNavmesh;
	Group.SharedPathTargetLocation = Group.LastTargetLocation;
	++LastStats.SharedPathsBuilt;

#if ENABLE_DRAW_DEBUG
	if (Group.EngagementConfig.bEnableVisualDebug && World)
	{
		FVector PreviousPoint = GroupAnchor;
		for (const FVector& PathPoint : Group.SharedPathPoints)
		{
			DrawDebugLine(
				World,
				PreviousPoint,
				PathPoint,
				FColor::Purple,
				false,
				Group.EngagementConfig.SharedPathRepathInterval + 0.1f,
				0,
				4.0f);
			PreviousPoint = PathPoint;
		}
		DrawDebugSphere(
			World,
			Group.LastTargetLocation,
			75.0f,
			12,
			FColor::Red,
			false,
			Group.EngagementConfig.SharedPathRepathInterval + 0.1f,
			0,
			2.0f);
	}
#endif
	return true;
}

FVector UMassUnitCrowdSystem::CalculateSharedPathDestination(
	const FSpatialEntry& Entry,
	const FCrowdGroup& Group) const
{
	return Group.SharedPathPoints.IsEmpty()
		? Group.LastTargetLocation
		: CalculatePathLookAheadDestination(
			Entry.Location,
			Group.SharedPathPoints,
			Group.EngagementConfig.SharedPathLookAheadDistance);
}

FVector UMassUnitCrowdSystem::CalculatePathLookAheadDestination(
	const FVector& Location,
	const TArray<FVector>& PathPoints,
	float DesiredLookAheadDistance)
{
	if (PathPoints.IsEmpty())
	{
		return Location;
	}
	int32 ClosestPointIndex = 0;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 PointIndex = 0; PointIndex < PathPoints.Num(); ++PointIndex)
	{
		const float DistanceSquared = FVector::DistSquared2D(Location, PathPoints[PointIndex]);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestPointIndex = PointIndex;
		}
	}

	float AccumulatedDistance = 0.0f;
	int32 DestinationIndex = ClosestPointIndex;
	while (DestinationIndex + 1 < PathPoints.Num()
		&& AccumulatedDistance < FMath::Max(1.0f, DesiredLookAheadDistance))
	{
		AccumulatedDistance += FVector::Dist2D(
			PathPoints[DestinationIndex],
			PathPoints[DestinationIndex + 1]);
		++DestinationIndex;
	}
	return PathPoints[DestinationIndex];
}

FVector UMassUnitCrowdSystem::CalculateGroupAnchor(const FCrowdGroup& Group) const
{
	if (!EntitySubsystem)
	{
		return Group.Center;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	FVector Anchor = FVector::ZeroVector;
	int32 LivingUnitCount = 0;
	for (const FMassUnitEntityHandle Entity : Group.Units)
	{
		if (!IsEntityValid(Entity))
		{
			continue;
		}
		const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
		if (!Transform || (State && State->CurrentState == EMassUnitState::Dead))
		{
			continue;
		}
		Anchor += Transform->GetTransform().GetLocation();
		++LivingUnitCount;
	}
	return LivingUnitCount > 0 ? Anchor / static_cast<float>(LivingUnitCount) : Group.Center;
}

void UMassUnitCrowdSystem::PruneAttackTokens(FCrowdGroup& Group, float CurrentTime)
{
	for (auto It = Group.AttackTokenExpiry.CreateIterator(); It; ++It)
	{
		if (It->Value <= CurrentTime || !IsEntityValid(It->Key))
		{
			It.RemoveCurrent();
		}
	}
}

bool UMassUnitCrowdSystem::TryAcquireAttackToken(
	FCrowdGroup& Group,
	FMassUnitEntityHandle Entity,
	float CurrentTime,
	float HoldDuration)
{
	const int32 MaxAttackers = Group.EngagementConfig.MaxSimultaneousAttackers;
	if (MaxAttackers <= 0)
	{
		// Unlimited: skip the bookkeeping entirely so the pre-token behaviour costs nothing.
		return true;
	}

	// NO prune here on purpose. UpdateGroupEngagements prunes EVERY group once per update before any
	// unit is processed, and this is that pass's only downstream caller, so pruning again per
	// acquisition is pure redundancy - and expensive redundancy: it made expiry O(InRangeUnits *
	// MaxAttackers) per update instead of O(MaxAttackers), with an IsEntityValid() entity lookup per
	// visited entry. At 25 in-range units and a cap of 20 that is ~500 entry visits per update where
	// 20 suffice, scaling with exactly the two numbers crowd performance is most sensitive to.
	// If this ever gains a caller outside UpdateGroupEngagements, that caller must prune first.

	// Hold for at least one crowd update, otherwise a cooldown shorter than the update interval
	// would free the token before the holder is ever revisited and the cap would not bind.
	const float Expiry = CurrentTime + FMath::Max(UpdateInterval, HoldDuration);

	if (float* Existing = Group.AttackTokenExpiry.Find(Entity))
	{
		*Existing = Expiry;
		return true;
	}
	if (Group.AttackTokenExpiry.Num() >= MaxAttackers)
	{
		return false;
	}
	Group.AttackTokenExpiry.Add(Entity, Expiry);
	return true;
}

void UMassUnitCrowdSystem::RequestActorAttack(
	FMassUnitEntityHandle Entity,
	FCrowdGroup& Group,
	AActor* TargetActor,
	float Damage)
{
	if (!IsValid(TargetActor))
	{
		return;
	}
	++LastStats.AttacksRequested;

	const bool bCanApplyAuthoritativeGameplay = World && World->GetNetMode() != NM_Client;
	if (bCanApplyAuthoritativeGameplay && Group.EngagementConfig.bApplyActorDamage && Damage > 0.0f)
	{
		TSubclassOf<UDamageType> DamageType = Group.EngagementConfig.DamageTypeClass;
		if (!DamageType)
		{
			DamageType = UDamageType::StaticClass();
		}
		UGameplayStatics::ApplyDamage(TargetActor, Damage, nullptr, nullptr, DamageType);
	}
	if (bCanApplyAuthoritativeGameplay && IsValid(TargetActor) && Group.EngagementConfig.GameplayEffectToTarget)
	{
		if (UAbilitySystemComponent* TargetAbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
		{
			FGameplayEffectContextHandle EffectContext = TargetAbilitySystem->MakeEffectContext();
			EffectContext.AddSourceObject(this);
			TargetAbilitySystem->BP_ApplyGameplayEffectToSelf(
				Group.EngagementConfig.GameplayEffectToTarget,
				Group.EngagementConfig.GameplayEffectLevel,
				EffectContext);
		}
	}
	if (EntitySubsystem)
	{
		const FMassUnitCrowdFragment* Crowd = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle());
		RequestPresentationCue(
			Group,
			EMassUnitCrowdCue::Attack,
			TargetActor->GetActorLocation(),
			Crowd ? Crowd->SubgroupIndex : INDEX_NONE);
	}
	OnCrowdAttackRequested.Broadcast(FMassUnitHandle(Entity), IsValid(TargetActor) ? TargetActor : nullptr, Damage);
}

void UMassUnitCrowdSystem::RequestPresentationCue(
	FCrowdGroup& Group,
	EMassUnitCrowdCue Cue,
	const FVector& WorldLocation,
	int32 SubgroupIndex)
{
	if (!World || !Group.Config.bEnableGroupCueEvents || !OnCrowdCueRequested.IsBound())
	{
		return;
	}
	const float CurrentTime = World->GetTimeSeconds();
	float* NextCueTime = &Group.NextGroupCueTime;
	if (Group.NextSubgroupCueTimes.IsValidIndex(SubgroupIndex))
	{
		NextCueTime = &Group.NextSubgroupCueTimes[SubgroupIndex];
	}
	if (CurrentTime < *NextCueTime)
	{
		return;
	}
	*NextCueTime = CurrentTime + Group.Config.GroupCueCooldown;
	OnCrowdCueRequested.Broadcast(Group.Handle, SubgroupIndex, Cue, WorldLocation);
}

bool UMassUnitCrowdSystem::PassesTargetFilter(const AActor* Candidate, int32 CrowdTeamID) const
{
	return !TargetFilter.IsBound() || TargetFilter.Execute(Candidate, CrowdTeamID);
}

AActor* UMassUnitCrowdSystem::FindClosestPlayerTarget(const FVector& Origin, float MaxDistance, int32 CrowdTeamID) const
{
	if (!World)
	{
		return nullptr;
	}
	const float MaximumDistanceSquared = MaxDistance > 0.0f
		? FMath::Square(MaxDistance)
		: TNumericLimits<float>::Max();
	float BestDistanceSquared = MaximumDistanceSquared;
	AActor* BestTarget = nullptr;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!IsValid(Pawn) || !PassesTargetFilter(Pawn, CrowdTeamID))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(Origin, Pawn->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestTarget = Pawn;
		}
	}
	return BestTarget;
}

float UMassUnitCrowdSystem::CalculateClosestLivingUnitDistanceSquared(
	const FCrowdGroup& Group,
	const FVector& Location) const
{
	if (!EntitySubsystem)
	{
		return TNumericLimits<float>::Max();
	}
	const bool bUse3DMovement = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (const FMassUnitEntityHandle Entity : Group.Units)
	{
		if (!IsEntityValid(Entity))
		{
			continue;
		}
		const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
		if (!Transform || (State && State->CurrentState == EMassUnitState::Dead))
		{
			continue;
		}
		const FVector Delta = Transform->GetTransform().GetLocation() - Location;
		const float DistanceSquared = bUse3DMovement ? Delta.SizeSquared() : Delta.SizeSquared2D();
		ClosestDistanceSquared = FMath::Min(ClosestDistanceSquared, DistanceSquared);
	}
	return ClosestDistanceSquared;
}

FVector UMassUnitCrowdSystem::CalculateFollowOffset(
	FMassUnitEntityHandle Entity,
	const FCrowdGroup& Group) const
{
	uint32 Seed = HashCombine(::GetTypeHash(Group.Config.RandomSeed), GetTypeHash(Entity));
	Seed = HashCombine(Seed, 0x5EED1234u);
	FRandomStream RandomStream(static_cast<int32>(Seed));
	const float Radius = Group.EngagementConfig.FollowDistance
		+ RandomStream.FRand() * Group.EngagementConfig.FollowSpread;
	if (Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D)
	{
		return RandomStream.VRand() * Radius;
	}
	// Engagement slots: while engaged, spread units over fixed angular positions around the target
	// rather than random offsets, so the group forms a ring instead of compacting into one point.
	// The slot is derived from the entity index, so it needs no per-unit bookkeeping and survives
	// respawns. Units that collide on a slot are pushed apart by the usual separation steering.
	if (Group.bEngaged
		&& Group.EngagementConfig.bUseEngagementSlots
		&& Group.Config.MovementMode != EMassUnitCrowdMovementMode::Free3D)
	{
		const int32 SlotCount = FMath::Max(3, Group.EngagementConfig.EngagementSlotCount);
		const int32 SlotIndex = static_cast<int32>(Entity.ToMassEntityHandle().Index % SlotCount);
		const float SlotAngle = (2.0f * UE_PI * static_cast<float>(SlotIndex)) / static_cast<float>(SlotCount);
		return FVector(FMath::Cos(SlotAngle) * Radius, FMath::Sin(SlotAngle) * Radius, 0.0f);
	}

	int32 SubgroupIndex = 0;
	if (EntitySubsystem)
	{
		if (const FMassUnitCrowdFragment* Crowd = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle()))
		{
			SubgroupIndex = Crowd->SubgroupIndex;
		}
	}
	const float SectorSize = 2.0f * UE_PI / static_cast<float>(FMath::Max(1, Group.SubgroupCount));
	const float SectorCenter = (static_cast<float>(SubgroupIndex) + 0.5f) * SectorSize;
	const float Angle = Group.SubgroupCount > 1
		? SectorCenter + RandomStream.FRandRange(-0.4f, 0.4f) * SectorSize
		: RandomStream.FRandRange(0.0f, 2.0f * UE_PI);
	return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
}

FVector UMassUnitCrowdSystem::CalculateSubgroupWanderCenter(
	FMassUnitEntityHandle Entity,
	const FCrowdGroup& Group) const
{
	if (!Group.Config.bEnableManagedSubgroups || Group.SubgroupCount <= 1 || !EntitySubsystem)
	{
		return Group.Center;
	}
	const FMassUnitCrowdFragment* Crowd = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitCrowdFragment>(
		Entity.ToMassEntityHandle());
	if (!Crowd)
	{
		return Group.Center;
	}
	return CalculateSubgroupWanderCenter(Crowd->SubgroupIndex, Group);
}

FVector UMassUnitCrowdSystem::CalculateSubgroupWanderCenter(
	int32 SubgroupIndex,
	const FCrowdGroup& Group) const
{
	if (!Group.Config.bEnableManagedSubgroups || Group.SubgroupCount <= 1)
	{
		return Group.Center;
	}
	const float Angle = 2.0f * UE_PI * static_cast<float>(SubgroupIndex)
		/ static_cast<float>(Group.SubgroupCount);
	const float CenterRadius = Group.Config.WanderRadius * (1.0f - Group.Config.SubgroupWanderRadiusScale);
	if (Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D)
	{
		const float HeightPhase = Group.SubgroupCount > 2
			? (static_cast<float>(SubgroupIndex % 3) - 1.0f) * CenterRadius * 0.35f
			: 0.0f;
		return Group.Center + FVector(FMath::Cos(Angle) * CenterRadius, FMath::Sin(Angle) * CenterRadius, HeightPhase);
	}
	return Group.Center + FVector(FMath::Cos(Angle) * CenterRadius, FMath::Sin(Angle) * CenterRadius, 0.0f);
}

FRandomStream UMassUnitCrowdSystem::MakeRandomStream(
	FMassUnitEntityHandle Entity,
	const FCrowdGroup& Group,
	int32 DecisionSequence) const
{
	uint32 Seed = HashCombine(::GetTypeHash(Group.Config.RandomSeed), GetTypeHash(Entity));
	Seed = HashCombine(Seed, ::GetTypeHash(DecisionSequence));
	return FRandomStream(static_cast<int32>(Seed));
}

int32 UMassUnitCrowdSystem::CalculateSimulationLOD(
	const FVector& Location,
	const TArray<FVector>& ObserverLocations,
	float& OutDistanceSquared) const
{
	if (ObserverLocations.IsEmpty())
	{
		OutDistanceSquared = 0.0f;
		return 0;
	}

	OutDistanceSquared = TNumericLimits<float>::Max();
	for (const FVector& ObserverLocation : ObserverLocations)
	{
		OutDistanceSquared = FMath::Min(OutDistanceSquared, FVector::DistSquared(Location, ObserverLocation));
	}
	for (int32 Index = 0; Index < SimulationLODDistances.Num(); ++Index)
	{
		if (OutDistanceSquared <= FMath::Square(FMath::Max(0.0f, SimulationLODDistances[Index])))
		{
			return Index;
		}
	}
	return SimulationLODDistances.Num();
}

float UMassUnitCrowdSystem::GetSimulationIntervalMultiplier(int32 LODLevel) const
{
	const int32 Index = FMath::Clamp(LODLevel, 0, SimulationLODIntervalMultipliers.Num() - 1);
	return FMath::Max(1.0f, SimulationLODIntervalMultipliers[Index]);
}

FIntVector UMassUnitCrowdSystem::ToSpatialCell(const FVector& Location, bool bUse3DMovement) const
{
	return FIntVector(
		FMath::FloorToInt(Location.X / SpatialCellSize),
		FMath::FloorToInt(Location.Y / SpatialCellSize),
		bUse3DMovement ? FMath::FloorToInt(Location.Z / SpatialCellSize) : 0);
}

FMassUnitCrowdConfig UMassUnitCrowdSystem::SanitizeConfig(const FMassUnitCrowdConfig& Config)
{
	FMassUnitCrowdConfig Result = Config;
	Result.WanderRadius = FMath::Max(1.0f, Result.WanderRadius);
	Result.MinWanderDistance = FMath::Clamp(Result.MinWanderDistance, 0.0f, Result.WanderRadius);
	Result.MinIdleTime = FMath::Max(0.0f, Result.MinIdleTime);
	Result.MaxIdleTime = FMath::Max(Result.MinIdleTime, Result.MaxIdleTime);
	Result.MaxMoveTime = FMath::Max(0.1f, Result.MaxMoveTime);
	Result.MinMoveSpeedMultiplier = FMath::Clamp(Result.MinMoveSpeedMultiplier, 0.05f, 4.0f);
	Result.MaxMoveSpeedMultiplier = FMath::Clamp(Result.MaxMoveSpeedMultiplier, Result.MinMoveSpeedMultiplier, 4.0f);
	Result.SeparationRadius = FMath::Max(1.0f, Result.SeparationRadius);
	Result.SeparationWeight = FMath::Clamp(Result.SeparationWeight, 0.0f, 4.0f);
	Result.InteractionChance = FMath::Clamp(Result.InteractionChance, 0.0f, 1.0f);
	Result.InteractionRadius = FMath::Max(1.0f, Result.InteractionRadius);
	Result.MinInteractionTime = FMath::Max(0.0f, Result.MinInteractionTime);
	Result.MaxInteractionTime = FMath::Max(Result.MinInteractionTime, Result.MaxInteractionTime);
	Result.MaxSimulationDistance = FMath::Max(0.0f, Result.MaxSimulationDistance);
	Result.NavigationHeightOffset = FMath::Clamp(Result.NavigationHeightOffset, -100000.0f, 100000.0f);
	Result.ManagedSubgroupSize = FMath::Max(1, Result.ManagedSubgroupSize);
	Result.SubgroupWanderRadiusScale = FMath::Clamp(Result.SubgroupWanderRadiusScale, 0.1f, 1.0f);
	Result.SubgroupPathLookAheadDistance = FMath::Max(1.0f, Result.SubgroupPathLookAheadDistance);
	Result.GroupCueCooldown = FMath::Max(0.0f, Result.GroupCueCooldown);
	return Result;
}

FMassUnitPlayerEngagementConfig UMassUnitCrowdSystem::SanitizeEngagementConfig(
	const FMassUnitPlayerEngagementConfig& Config)
{
	FMassUnitPlayerEngagementConfig Result = Config;
	Result.ActivationRadius = FMath::Max(1.0f, Result.ActivationRadius);
	Result.DeactivationDistance = FMath::Max(1.0f, Result.DeactivationDistance);
	Result.TargetRefreshInterval = FMath::Max(0.02f, Result.TargetRefreshInterval);
	Result.RepathDistance = FMath::Max(1.0f, Result.RepathDistance);
	Result.SharedPathRepathInterval = FMath::Max(0.05f, Result.SharedPathRepathInterval);
	Result.SharedPathRepathDistance = FMath::Max(1.0f, Result.SharedPathRepathDistance);
	Result.SharedPathLookAheadDistance = FMath::Max(1.0f, Result.SharedPathLookAheadDistance);
	Result.FollowDistance = FMath::Max(0.0f, Result.FollowDistance);
	Result.FollowSpread = FMath::Max(0.0f, Result.FollowSpread);
	Result.EngagedMoveSpeedMultiplier = FMath::Clamp(Result.EngagedMoveSpeedMultiplier, 0.05f, 4.0f);
	Result.AttackRangeOverride = FMath::Max(0.0f, Result.AttackRangeOverride);
	Result.DamageMultiplier = FMath::Max(0.0f, Result.DamageMultiplier);
	Result.GameplayEffectLevel = FMath::Max(0.0f, Result.GameplayEffectLevel);
	Result.MaxSimultaneousAttackers = FMath::Max(0, Result.MaxSimultaneousAttackers);
	// Below 1.0 the standoff ring would sit inside attack range, so token-less units would attack anyway.
	Result.StandoffDistanceMultiplier = FMath::Clamp(Result.StandoffDistanceMultiplier, 1.05f, 4.0f);
	Result.EngagementSlotCount = FMath::Clamp(Result.EngagementSlotCount, 3, 64);
	return Result;
}

// ============================================================================
// COLLECTIVE BEHAVIOR - alerts, morale/rout, threat targeting
// ============================================================================
// Everything below is GROUP-level and runs once per crowd update (UpdateGroupEngagements),
// so its cost scales with the number of groups and signals, not units. The only O(group size)
// work - counting living units and finding the anchor - is throttled by MoraleEvaluationInterval
// and its result cached for the signal and threat passes. Groups that never call
// ConfigureCrowdGroupCollectiveBehavior skip every branch here.
// ============================================================================

bool UMassUnitCrowdSystem::ConfigureCrowdGroupCollectiveBehavior(
	int32 CrowdGroupHandle,
	const FMassUnitCrowdCollectiveConfig& Config,
	bool bEnableCollectiveBehavior)
{
	FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group)
	{
		return false;
	}
	const float CurrentTime = World ? World->GetTimeSeconds() : 0.0f;

	// Disabling mid-rout must not leave units at flee speed with stale flee destinations.
	if (!bEnableCollectiveBehavior && Group->bCollectiveEnabled && Group->bRouting)
	{
		EndRout(*Group, CurrentTime);
		Group = Groups.Find(CrowdGroupHandle);
		if (!Group)
		{
			return false;
		}
	}

	Group->CollectiveConfig = SanitizeCollectiveConfig(Config);
	Group->bCollectiveEnabled = bEnableCollectiveBehavior;
	Group->TeamID = ResolveGroupTeamID(*Group);
	Group->Awareness = 0.0f;
	Group->PendingAlertActivationTime = -1.0f;
	Group->PendingAlertTarget.Reset();
	Group->PendingAlertSourceGroup = INDEX_NONE;
	Group->PendingRecruitRelayDepth = 0;
	Group->PendingAlertRelayDepth = 0;
	Group->LastCollectiveUpdateTime = CurrentTime;
	Group->NextMoraleEvaluationTime = 0.0f;

	FVector Anchor = Group->Center;
	const int32 LivingCount = CountLivingUnits(*Group, &Anchor);
	Group->CachedAnchor = Anchor;
	Group->bHasCachedAnchor = LivingCount > 0;
	// Reconfiguring keeps the casualty baseline so tuning a live group does not "forgive" its dead.
	Group->InitialLivingCount = FMath::Max(Group->InitialLivingCount, LivingCount);
	Group->LastLivingCount = LivingCount;

	if (!bEnableCollectiveBehavior)
	{
		Group->Morale = 1.0f;
		Group->bRouting = false;
		Group->Threat.Reset();
	}
	return true;
}

bool UMassUnitCrowdSystem::ReportCrowdUnitDamaged(
	FMassUnitHandle UnitHandle,
	float Damage,
	AActor* DamageInstigator)
{
	if (!World || World->GetNetMode() == NM_Client || !IsEntityValid(UnitHandle.EntityHandle))
	{
		return false;
	}
	const int32* FoundGroupHandle = UnitToGroup.Find(UnitHandle.EntityHandle);
	if (!FoundGroupHandle)
	{
		return false;
	}
	const int32 GroupHandle = *FoundGroupHandle;
	FCrowdGroup* Group = Groups.Find(GroupHandle);
	AActor* Instigator = ResolveThreatActor(DamageInstigator);
	if (!Group || !Instigator)
	{
		return false;
	}
	// Friendly fire (by the project's rules) neither builds threat nor turns the crowd.
	if (TargetFilter.IsBound() && !PassesTargetFilter(Instigator, Group->bCollectiveEnabled ? Group->TeamID : ResolveGroupTeamID(*Group)))
	{
		return false;
	}

	if (Group->bCollectiveEnabled && Group->CollectiveConfig.bEnableThreatTargeting && Damage > 0.0f)
	{
		Group->Threat.FindOrAdd(TWeakObjectPtr<AActor>(Instigator)) += Damage * Group->CollectiveConfig.DamageThreatScale;
	}

	// An engaged group never jumps targets per hit; the threat table (collective) decides that with
	// hysteresis, and without collective behavior the original target is kept exactly as before.
	if (Group->bEngaged)
	{
		return true;
	}
	if (!Group->bEngagementEnabled || Group->bPaused || (Group->bCollectiveEnabled && Group->bRouting))
	{
		return false;
	}
	const bool bActivationAllowed =
		Group->EngagementConfig.ActivationMode == EMassUnitCrowdActivationMode::OnInteraction
		|| (Group->bCollectiveEnabled && Group->CollectiveConfig.bActivateOnDamage);
	if (!bActivationAllowed)
	{
		return false;
	}
	// A hit is a direct activation: discard any relay depth a pending recruitment left behind.
	Group->PendingAlertRelayDepth = 0;
	return ActivateCrowdGroupForActor(GroupHandle, Instigator);
}

bool UMassUnitCrowdSystem::ApplyCrowdGroupMoraleShock(int32 CrowdGroupHandle, float MoraleLoss)
{
	FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group || !Group->bCollectiveEnabled || !Group->CollectiveConfig.bEnableMorale)
	{
		return false;
	}
	Group->Morale = FMath::Clamp(Group->Morale - MoraleLoss, 0.0f, 1.0f);
	return true;
}

bool UMassUnitCrowdSystem::RaiseCrowdAlertAtLocation(
	FVector WorldLocation,
	float Radius,
	float Strength,
	AActor* TargetActor,
	int32 TeamID)
{
	if (!World || World->GetNetMode() == NM_Client || Radius <= 0.0f || Strength <= 0.0f)
	{
		return false;
	}
	FCrowdSignal Signal;
	Signal.Type = ECrowdSignalType::Alert;
	Signal.SourceGroup = INDEX_NONE;
	Signal.TeamID = TeamID >= 0 ? TeamID : INDEX_NONE;
	Signal.Location = WorldLocation;
	Signal.Radius = Radius;
	Signal.Strength = Strength;
	Signal.Target = ResolveThreatActor(TargetActor);
	Signal.RelayDepth = 0;
	const int32 PreviousCount = PendingSignals.Num();
	QueueCrowdSignal(Signal);
	return PendingSignals.Num() > PreviousCount;
}

float UMassUnitCrowdSystem::GetCrowdGroupMorale(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	return Group && Group->bCollectiveEnabled && Group->CollectiveConfig.bEnableMorale ? Group->Morale : 1.0f;
}

bool UMassUnitCrowdSystem::IsCrowdGroupRouting(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	return Group && Group->bCollectiveEnabled && Group->bRouting;
}

float UMassUnitCrowdSystem::GetCrowdGroupAwareness(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	return Group && Group->bCollectiveEnabled ? Group->Awareness : 0.0f;
}

bool UMassUnitCrowdSystem::GetCrowdGroupAnchor(int32 CrowdGroupHandle, FVector& OutLocation) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group)
	{
		return false;
	}
	OutLocation = GetCollectiveAnchor(*Group);
	return true;
}

int32 UMassUnitCrowdSystem::GetCrowdGroupTeamID(int32 CrowdGroupHandle) const
{
	const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
	if (!Group)
	{
		return INDEX_NONE;
	}
	return Group->bCollectiveEnabled && Group->TeamID >= 0 ? Group->TeamID : ResolveGroupTeamID(*Group);
}

int32 UMassUnitCrowdSystem::GetUnitCrowdGroup(FMassUnitHandle UnitHandle) const
{
	const int32* GroupHandle = UnitToGroup.Find(UnitHandle.EntityHandle);
	return GroupHandle ? *GroupHandle : INDEX_NONE;
}

void UMassUnitCrowdSystem::UpdateCollectiveBehavior(float CurrentTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_CrowdCollective);
	// Crowd decisions are server-authoritative; clients only ever see the resulting unit state.
	if (!World || World->GetNetMode() == NM_Client)
	{
		PendingSignals.Reset();
		return;
	}

	TArray<int32> GroupHandles;
	Groups.GetKeys(GroupHandles);

	// ---- Per-group bookkeeping: decay, morale, threat, periodic alert rebroadcast.
	for (const int32 GroupHandle : GroupHandles)
	{
		FCrowdGroup* Group = Groups.Find(GroupHandle);
		if (!Group || !Group->bCollectiveEnabled)
		{
			continue;
		}
		const float DeltaTime = Group->LastCollectiveUpdateTime >= 0.0f
			? FMath::Max(0.0f, CurrentTime - Group->LastCollectiveUpdateTime)
			: 0.0f;
		Group->LastCollectiveUpdateTime = CurrentTime;
		if (Group->bPaused)
		{
			continue;
		}

		// Sub-threshold awareness fades; a recruitment already scheduled is not cancelled by decay.
		if (Group->PendingAlertActivationTime < 0.0f)
		{
			Group->Awareness = FMath::Max(0.0f, Group->Awareness - Group->CollectiveConfig.AwarenessDecayPerSecond * DeltaTime);
		}

		// The throttled O(group size) pass. Runs even with morale off, because signals and threat
		// read the cached anchor it produces.
		if (CurrentTime >= Group->NextMoraleEvaluationTime)
		{
			Group->NextMoraleEvaluationTime = CurrentTime + Group->CollectiveConfig.MoraleEvaluationInterval;
			FVector Anchor = Group->Center;
			const int32 LivingCount = CountLivingUnits(*Group, &Anchor);
			Group->CachedAnchor = Anchor;
			Group->bHasCachedAnchor = LivingCount > 0;
			// Reinforcements raise the baseline instead of reading as negative casualties.
			Group->InitialLivingCount = FMath::Max(Group->InitialLivingCount, LivingCount);
			const int32 Lost = Group->LastLivingCount - LivingCount;
			if (Lost > 0 && Group->InitialLivingCount > 0 && Group->CollectiveConfig.bEnableMorale)
			{
				Group->Morale = FMath::Max(0.0f, Group->Morale
					- (static_cast<float>(Lost) / static_cast<float>(Group->InitialLivingCount))
						* Group->CollectiveConfig.CasualtyMoraleWeight);
			}
			Group->LastLivingCount = LivingCount;
		}

		if (Group->CollectiveConfig.bEnableMorale)
		{
			UpdateGroupMorale(*Group, CurrentTime, DeltaTime);
			Group = Groups.Find(GroupHandle);
			if (!Group)
			{
				continue;
			}
		}

		if (Group->CollectiveConfig.bEnableThreatTargeting && Group->bEngaged)
		{
			UpdateGroupThreat(*Group, CurrentTime, DeltaTime);
			Group = Groups.Find(GroupHandle);
			if (!Group)
			{
				continue;
			}
		}

		const FMassUnitCrowdCollectiveConfig& Collective = Group->CollectiveConfig;
		if (Group->bEngaged
			&& !Group->bRouting
			&& Group->TargetActor.IsValid()
			&& Collective.bBroadcastAlerts
			&& Collective.AlertRebroadcastInterval > 0.0f
			&& Group->EngagementRelayDepth <= Collective.MaxAlertRelayDepth
			&& CurrentTime >= Group->NextAlertBroadcastTime)
		{
			Group->NextAlertBroadcastTime = CurrentTime + Collective.AlertRebroadcastInterval;
			EmitCrowdSignal(
				*Group,
				ECrowdSignalType::Alert,
				Collective.AlertRadius,
				Collective.AlertStrength * FMath::Pow(Collective.AlertRelayFalloff, static_cast<float>(Group->EngagementRelayDepth)),
				Group->TargetActor.Get(),
				Group->EngagementRelayDepth);
		}
	}

	// ---- Deliver what was emitted LAST update (each hop costs one crowd tick).
	DeliverCrowdSignals(CurrentTime);

	// ---- Recruitment: groups whose reaction delay has elapsed now join the fight.
	for (const int32 GroupHandle : GroupHandles)
	{
		FCrowdGroup* Group = Groups.Find(GroupHandle);
		if (!Group || !Group->bCollectiveEnabled
			|| Group->PendingAlertActivationTime < 0.0f
			|| CurrentTime < Group->PendingAlertActivationTime)
		{
			continue;
		}
		AActor* RecruitTarget = Group->PendingAlertTarget.Get();
		const int32 SourceGroup = Group->PendingAlertSourceGroup;
		const int32 RecruitDepth = Group->PendingRecruitRelayDepth;
		Group->PendingAlertActivationTime = -1.0f;
		Group->PendingAlertTarget.Reset();
		Group->PendingAlertSourceGroup = INDEX_NONE;
		Group->PendingRecruitRelayDepth = 0;
		if (Group->bEngaged || Group->bPaused || Group->bRouting || !Group->bEngagementEnabled || !IsValid(RecruitTarget))
		{
			Group->Awareness = 0.0f;
			continue;
		}

		Group->PendingAlertRelayDepth = RecruitDepth;
		if (!ActivateCrowdGroupForActor(GroupHandle, RecruitTarget))
		{
			if (FCrowdGroup* FailedGroup = Groups.Find(GroupHandle))
			{
				FailedGroup->PendingAlertRelayDepth = 0;
			}
			continue;
		}
		++LastStats.GroupsAlerted;
		if (FCrowdGroup* AlertedGroup = Groups.Find(GroupHandle))
		{
			RequestPresentationCue(*AlertedGroup, EMassUnitCrowdCue::Alerted, GetCollectiveAnchor(*AlertedGroup));
		}
		OnCrowdGroupAlerted.Broadcast(GroupHandle, RecruitTarget, SourceGroup);
	}
}

void UMassUnitCrowdSystem::DeliverCrowdSignals(float CurrentTime)
{
	if (PendingSignals.IsEmpty())
	{
		return;
	}
	// Swap out first: anything emitted while delivering (recruits relaying) waits for the next update.
	TArray<FCrowdSignal> Signals = MoveTemp(PendingSignals);
	PendingSignals.Reset();

	TArray<int32> GroupHandles;
	Groups.GetKeys(GroupHandles);
	for (const FCrowdSignal& Signal : Signals)
	{
		if (Signal.Radius <= 0.0f || Signal.Strength <= 0.0f)
		{
			continue;
		}
		const float RadiusSquared = FMath::Square(Signal.Radius);
		for (const int32 GroupHandle : GroupHandles)
		{
			if (GroupHandle == Signal.SourceGroup)
			{
				continue;
			}
			FCrowdGroup* Receiver = Groups.Find(GroupHandle);
			if (!Receiver || !Receiver->bCollectiveEnabled || Receiver->bPaused || !DoesSignalReachTeam(Signal, *Receiver))
			{
				continue;
			}
			const FMassUnitCrowdCollectiveConfig& Collective = Receiver->CollectiveConfig;
			if (Signal.Type == ECrowdSignalType::Alert)
			{
				if (!Collective.bReceiveAlerts || !Receiver->bEngagementEnabled || Receiver->bEngaged || Receiver->bRouting)
				{
					continue;
				}
			}
			else if (!Collective.bEnableMorale || Collective.FearSusceptibility <= 0.0f)
			{
				continue;
			}

			const FVector Anchor = GetCollectiveAnchor(*Receiver);
			const bool bUse3DMovement = Receiver->Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
			const float DistanceSquared = bUse3DMovement
				? FVector::DistSquared(Signal.Location, Anchor)
				: FVector::DistSquared2D(Signal.Location, Anchor);
			if (DistanceSquared >= RadiusSquared)
			{
				continue;
			}
			const float Falloff = 1.0f - FMath::Sqrt(DistanceSquared) / Signal.Radius;
			++LastStats.CollectiveSignalsProcessed;

			if (Signal.Type == ECrowdSignalType::Fear)
			{
				Receiver->Morale = FMath::Clamp(Receiver->Morale - Signal.Strength * Falloff * Collective.FearSusceptibility, 0.0f, 1.0f);
				continue;
			}

			Receiver->Awareness += Signal.Strength * Falloff * Collective.AlertSensitivity;
			if (Receiver->PendingAlertActivationTime < 0.0f
				&& Receiver->Awareness >= Collective.AlertThreshold
				&& Signal.Target.IsValid())
			{
				// Deterministic per group and per alert, like every other crowd random choice.
				uint32 Seed = HashCombine(::GetTypeHash(Receiver->Config.RandomSeed), ::GetTypeHash(GroupHandle));
				Seed = HashCombine(Seed, ::GetTypeHash(Receiver->AlertSequence++));
				FRandomStream DelayStream(static_cast<int32>(Seed));
				Receiver->PendingAlertActivationTime = CurrentTime
					+ DelayStream.FRandRange(Collective.MinAlertReactionDelay, Collective.MaxAlertReactionDelay);
				Receiver->PendingAlertTarget = Signal.Target;
				Receiver->PendingAlertSourceGroup = Signal.SourceGroup;
				Receiver->PendingRecruitRelayDepth = Signal.RelayDepth + 1;
			}
		}
	}
}

void UMassUnitCrowdSystem::UpdateGroupMorale(FCrowdGroup& Group, float CurrentTime, float DeltaTime)
{
	const FMassUnitCrowdCollectiveConfig& Collective = Group.CollectiveConfig;
	Group.Morale = FMath::Min(1.0f, Group.Morale + Collective.MoraleRecoveryPerSecond * DeltaTime);

	if (!Group.bRouting)
	{
		if (Group.bEngaged
			&& Collective.RoutThreshold > 0.0f
			&& Group.Morale <= Collective.RoutThreshold
			&& Group.LastLivingCount > 0)
		{
			BeginRout(Group, CurrentTime);
		}
	}
	else if (Group.Morale >= Collective.RallyThreshold
		&& CurrentTime >= Group.RoutStartTime + Collective.MinRoutDuration)
	{
		EndRout(Group, CurrentTime);
	}
}

void UMassUnitCrowdSystem::UpdateGroupThreat(FCrowdGroup& Group, float CurrentTime, float DeltaTime)
{
	const FMassUnitCrowdCollectiveConfig& Collective = Group.CollectiveConfig;
	AActor* CurrentTarget = Group.TargetActor.Get();

	// Exponential decay by half-life, so threat fades at the same rate whatever the update interval.
	const float DecayFactor = DeltaTime > 0.0f ? FMath::Pow(0.5f, DeltaTime / Collective.ThreatHalfLife) : 1.0f;
	for (auto It = Group.Threat.CreateIterator(); It; ++It)
	{
		AActor* ThreatActor = It->Key.Get();
		if (!IsValid(ThreatActor) || !PassesTargetFilter(ThreatActor, Group.TeamID))
		{
			It.RemoveCurrent();
			continue;
		}
		It->Value *= DecayFactor;
		if (It->Value < 0.01f && ThreatActor != CurrentTarget)
		{
			It.RemoveCurrent();
		}
	}

	// Proximity threat - at most one entry per player, so this loop is a handful of iterations.
	if (Collective.ProximityThreatRadius > 0.0f && Collective.ProximityThreatPerSecond > 0.0f && DeltaTime > 0.0f)
	{
		const FVector Anchor = GetCollectiveAnchor(Group);
		const float RadiusSquared = FMath::Square(Collective.ProximityThreatRadius);
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (!IsValid(Pawn) || !PassesTargetFilter(Pawn, Group.TeamID))
			{
				continue;
			}
			const float DistanceSquared = FVector::DistSquared2D(Anchor, Pawn->GetActorLocation());
			if (DistanceSquared >= RadiusSquared)
			{
				continue;
			}
			const float Falloff = 1.0f - FMath::Sqrt(DistanceSquared) / Collective.ProximityThreatRadius;
			Group.Threat.FindOrAdd(TWeakObjectPtr<AActor>(Pawn)) += Collective.ProximityThreatPerSecond * Falloff * DeltaTime;
		}
	}

	AActor* BestActor = nullptr;
	float BestThreat = 0.0f;
	for (const TPair<TWeakObjectPtr<AActor>, float>& Pair : Group.Threat)
	{
		AActor* Candidate = Pair.Key.Get();
		if (IsValid(Candidate) && Candidate != CurrentTarget && Pair.Value > BestThreat)
		{
			BestActor = Candidate;
			BestThreat = Pair.Value;
		}
	}
	if (!BestActor)
	{
		return;
	}

	// Lost target: hand the fight to the next most threatening actor instead of disengaging.
	if (!IsValid(CurrentTarget))
	{
		RetargetCrowdGroup(Group, BestActor, CurrentTime);
		return;
	}
	if (CurrentTime - Group.LastTargetSwitchTime < Collective.MinTargetSwitchInterval)
	{
		return;
	}
	// A floor of one threat point stops vanishingly small values from flipping a group whose current
	// target has no recorded threat yet.
	const float CurrentThreat = Group.Threat.FindRef(TWeakObjectPtr<AActor>(CurrentTarget));
	if (BestThreat > 1.0f && BestThreat > CurrentThreat * (1.0f + Collective.TargetSwitchMargin))
	{
		RetargetCrowdGroup(Group, BestActor, CurrentTime);
	}
}

bool UMassUnitCrowdSystem::RetargetCrowdGroup(FCrowdGroup& Group, AActor* NewTarget, float CurrentTime)
{
	if (!IsValid(NewTarget) || Group.TargetActor.Get() == NewTarget)
	{
		return false;
	}
	Group.TargetActor = NewTarget;
	Group.LastTargetLocation = NewTarget->GetActorLocation();
	Group.NextTargetRefreshTime = CurrentTime + Group.EngagementConfig.TargetRefreshInterval;
	Group.SharedPathPoints.Reset();
	Group.bSharedPathUsesNavmesh = false;
	Group.SharedPathTargetLocation = FVector::ZeroVector;
	Group.NextSharedPathUpdateTime = 0.0f;
	// Tokens were granted against the old target's range; start the new fight with a clean pool.
	Group.AttackTokenExpiry.Reset();
	Group.LastTargetSwitchTime = CurrentTime;

	if (EntitySubsystem)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		for (const FMassUnitEntityHandle Entity : Group.Units)
		{
			if (!IsEntityValid(Entity))
			{
				continue;
			}
			if (FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle()))
			{
				Crowd->NextFollowUpdateTime = 0.0f;
				Crowd->LastFollowTargetLocation = FVector::ZeroVector;
			}
		}
	}

	++LastStats.TargetSwitches;
	const int32 GroupHandle = Group.Handle;
	RequestPresentationCue(Group, EMassUnitCrowdCue::TargetChanged, GetCollectiveAnchor(Group));
	OnCrowdGroupTargetChanged.Broadcast(GroupHandle, NewTarget);
	return true;
}

void UMassUnitCrowdSystem::BeginRout(FCrowdGroup& Group, float CurrentTime)
{
	Group.bRouting = true;
	Group.RoutStartTime = CurrentTime;
	Group.AttackTokenExpiry.Reset();
	Group.PendingAlertActivationTime = -1.0f;
	Group.PendingAlertTarget.Reset();

	// Each unit picks a flee destination on its first routed visit rather than finishing its approach.
	if (EntitySubsystem)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		for (const FMassUnitEntityHandle Entity : Group.Units)
		{
			if (!IsEntityValid(Entity))
			{
				continue;
			}
			if (FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle()))
			{
				Crowd->NextFollowUpdateTime = 0.0f;
			}
		}
	}

	const FMassUnitCrowdCollectiveConfig& Collective = Group.CollectiveConfig;
	if (Collective.bBroadcastFear && Collective.FearStrength > 0.0f)
	{
		EmitCrowdSignal(Group, ECrowdSignalType::Fear, Collective.FearRadius, Collective.FearStrength, Group.TargetActor.Get(), 0);
	}
	const int32 GroupHandle = Group.Handle;
	AActor* FleeingFrom = Group.TargetActor.Get();
	RequestPresentationCue(Group, EMassUnitCrowdCue::Routed, GetCollectiveAnchor(Group));
	OnCrowdGroupRouted.Broadcast(GroupHandle, FleeingFrom);
}

void UMassUnitCrowdSystem::EndRout(FCrowdGroup& Group, float CurrentTime)
{
	Group.bRouting = false;
	// Force a fresh engaged destination; otherwise the repath-distance gate keeps the flee point.
	if (EntitySubsystem)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		for (const FMassUnitEntityHandle Entity : Group.Units)
		{
			if (!IsEntityValid(Entity))
			{
				continue;
			}
			if (FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Entity.ToMassEntityHandle()))
			{
				Crowd->NextFollowUpdateTime = 0.0f;
				Crowd->LastFollowTargetLocation = FVector::ZeroVector;
				Crowd->LastDestination = FVector::ZeroVector;
			}
		}
	}
	const int32 GroupHandle = Group.Handle;
	AActor* Target = Group.TargetActor.Get();
	RequestPresentationCue(Group, EMassUnitCrowdCue::Rallied, GetCollectiveAnchor(Group));
	OnCrowdGroupRallied.Broadcast(GroupHandle, Target);
}

bool UMassUnitCrowdSystem::HasRoutReachedUnit(
	FMassUnitEntityHandle Entity,
	const FCrowdGroup& Group,
	float CurrentTime) const
{
	const float StaggerTime = Group.CollectiveConfig.RoutStaggerTime;
	if (StaggerTime <= 0.0f)
	{
		return true;
	}
	uint32 Seed = HashCombine(::GetTypeHash(Group.Config.RandomSeed), GetTypeHash(Entity));
	Seed = HashCombine(Seed, 0x0F1EE000u);
	const float UnitOffset = (static_cast<float>(Seed % 1024u) / 1023.0f) * StaggerTime;
	return CurrentTime >= Group.RoutStartTime + UnitOffset;
}

bool UMassUnitCrowdSystem::UpdateRoutingUnit(
	const FSpatialEntry& Entry,
	FCrowdGroup& Group,
	float CurrentTime,
	bool bForceDecision)
{
	if (!EntitySubsystem || !UnitManager || !IsEntityValid(Entry.Entity))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = Entry.Entity.ToMassEntityHandle();
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(NativeHandle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(NativeHandle);
	FMassUnitLookAtFragment* LookAt = EntityManager.GetFragmentDataPtr<FMassUnitLookAtFragment>(NativeHandle);
	if (!Crowd || !State || !Navigation)
	{
		return false;
	}
	const FMassUnitCrowdCollectiveConfig& Collective = Group.CollectiveConfig;

	// A fleeing unit never holds a token, so the pool is free the moment the group rallies.
	Group.AttackTokenExpiry.Remove(Entry.Entity);
	State->PendingAttackDamage = 0.0f;
	if (State->CurrentState == EMassUnitState::Attacking)
	{
		State->CurrentState = EMassUnitState::Idle;
		State->StateTime = 0.0f;
	}
	State->MoveSpeed = Crowd->BaseMoveSpeed * Collective.FleeSpeedMultiplier;

	if (!bForceDecision && CurrentTime < Crowd->NextFollowUpdateTime)
	{
		return true;
	}
	Crowd->NextFollowUpdateTime = CurrentTime + Collective.FleeRepathInterval;
	Crowd->NextDecisionTime = TNumericLimits<float>::Max();

	const bool bUse3DMovement = Group.Config.MovementMode == EMassUnitCrowdMovementMode::Free3D;
	const FVector FromTarget = Entry.Location - Group.LastTargetLocation;
	FVector AwayDirection = bUse3DMovement ? FromTarget.GetSafeNormal() : FromTarget.GetSafeNormal2D();
	FRandomStream FleeStream = MakeRandomStream(Entry.Entity, Group, Crowd->DecisionSequence++);
	if (AwayDirection.IsNearlyZero())
	{
		const float Angle = FleeStream.FRandRange(0.0f, 2.0f * UE_PI);
		AwayDirection = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
	}
	else if (!bUse3DMovement)
	{
		// Fan the rout out a little so units do not stream away in one file.
		AwayDirection = AwayDirection.RotateAngleAxis(FleeStream.FRandRange(-25.0f, 25.0f), FVector::UpVector);
	}

	FVector Destination = Entry.Location + AwayDirection * Collective.FleeDistance;
	bool bDestinationOnNavmesh = false;
	if (!bUse3DMovement)
	{
		Destination.Z = Entry.Location.Z;
		FVector Projected;
		if (Group.bUseNavigation && NavigationSystem && NavigationSystem->ProjectPointToNavigation(Destination, Projected))
		{
			Destination.X = Projected.X;
			Destination.Y = Projected.Y;
			if (Group.Config.bConformToNavmeshHeight)
			{
				Destination.Z = Projected.Z;
				bDestinationOnNavmesh = true;
			}
		}
	}

	if (!UnitManager->SetUnitDestination(FMassUnitHandle(Entry.Entity), Destination, Group.AcceptanceRadius))
	{
		Crowd->NextFollowUpdateTime = CurrentTime + UpdateInterval;
		return false;
	}
	// Same provenance rule as the shared corridors: the movement processor adds the pivot offset once.
	Navigation->bPathUsesNavmesh = bDestinationOnNavmesh;
	Crowd->LastDestination = Destination;
	if (LookAt)
	{
		LookAt->Direction = AwayDirection;
	}
	++LastStats.DestinationsAssigned;
	return true;
}

void UMassUnitCrowdSystem::EmitCrowdSignal(
	FCrowdGroup& Group,
	ECrowdSignalType Type,
	float Radius,
	float Strength,
	AActor* Target,
	int32 RelayDepth)
{
	if (Radius <= 0.0f || Strength <= 0.0f)
	{
		return;
	}
	FCrowdSignal Signal;
	Signal.Type = Type;
	Signal.SourceGroup = Group.Handle;
	Signal.TeamID = Group.TeamID;
	Signal.Location = GetCollectiveAnchor(Group);
	Signal.Radius = Radius;
	Signal.Strength = Strength;
	Signal.Target = Target;
	Signal.RelayDepth = RelayDepth;
	QueueCrowdSignal(Signal);

#if ENABLE_DRAW_DEBUG
	const FMassUnitCrowdCollectiveConfig& Collective = Group.CollectiveConfig;
	if (World && Collective.bEnableVisualDebug && Collective.bDebugDrawSignals)
	{
		const FColor Color = Type == ECrowdSignalType::Alert ? FColor::Orange : FColor::Purple;
		DrawDebugCircle(World, Signal.Location + FVector(0.0f, 0.0f, 20.0f), Radius, 64, Color, false, 1.0f, 0, 8.0f,
			FVector::ForwardVector, FVector::RightVector, false);
	}
#endif
}

void UMassUnitCrowdSystem::QueueCrowdSignal(const FCrowdSignal& Signal)
{
	if (PendingSignals.Num() >= MaxPendingSignals)
	{
		UE_LOG(LogMassUnitSystem, Warning,
			TEXT("Crowd collective: signal queue full (%d); dropping a signal. Check alert relay depth and rebroadcast intervals."),
			MaxPendingSignals);
		return;
	}
	PendingSignals.Add(Signal);
}

int32 UMassUnitCrowdSystem::CountLivingUnits(const FCrowdGroup& Group, FVector* OutAnchor) const
{
	if (!EntitySubsystem)
	{
		return 0;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	FVector Sum = FVector::ZeroVector;
	int32 LivingCount = 0;
	for (const FMassUnitEntityHandle Entity : Group.Units)
	{
		if (!IsEntityValid(Entity))
		{
			continue;
		}
		const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		if (State && State->CurrentState == EMassUnitState::Dead)
		{
			continue;
		}
		if (const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle))
		{
			Sum += Transform->GetTransform().GetLocation();
		}
		++LivingCount;
	}
	if (OutAnchor && LivingCount > 0)
	{
		*OutAnchor = Sum / static_cast<float>(LivingCount);
	}
	return LivingCount;
}

FVector UMassUnitCrowdSystem::GetCollectiveAnchor(const FCrowdGroup& Group) const
{
	return Group.bHasCachedAnchor ? Group.CachedAnchor : CalculateGroupAnchor(Group);
}

int32 UMassUnitCrowdSystem::ResolveGroupTeamID(const FCrowdGroup& Group) const
{
	if (!EntitySubsystem)
	{
		return INDEX_NONE;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const FMassUnitEntityHandle Entity : Group.Units)
	{
		if (!IsEntityValid(Entity))
		{
			continue;
		}
		if (const FMassUnitTeamFragment* Team = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(Entity.ToMassEntityHandle()))
		{
			return Team->TeamID;
		}
	}
	return INDEX_NONE;
}

AActor* UMassUnitCrowdSystem::ResolveThreatActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}
	if (Actor->IsA<APawn>())
	{
		return Actor;
	}
	// Projectiles, grenades, and beams report themselves as the causer. A crowd must chase whoever
	// fired them - a pooled projectile stays "valid" while hidden and would otherwise be chased forever.
	if (APawn* InstigatorPawn = Actor->GetInstigator())
	{
		return InstigatorPawn;
	}
	if (APawn* OwnerPawn = Cast<APawn>(Actor->GetOwner()))
	{
		return OwnerPawn;
	}
	// A causer with no instigator and no owner is a stand-alone actor (turret, trap, test dummy) and is
	// itself the attacker. Anything that HAS an owner but no resolvable pawn is a sub-object of
	// something we cannot chase meaningfully, so it is ignored.
	return Actor->GetOwner() == nullptr ? Actor : nullptr;
}

bool UMassUnitCrowdSystem::DoesSignalReachTeam(const FCrowdSignal& Signal, const FCrowdGroup& Receiver)
{
	if (!Receiver.CollectiveConfig.bSignalsSameTeamOnly || Signal.TeamID < 0 || Receiver.TeamID < 0)
	{
		return true;
	}
	return Signal.TeamID == Receiver.TeamID;
}

void UMassUnitCrowdSystem::DrawCollectiveDebug(float CurrentTime)
{
#if ENABLE_DRAW_DEBUG
	if (!World)
	{
		return;
	}
	const float Duration = UpdateInterval * 1.5f;
	bool bDrawHUD = false;
	int32 DebugGroups = 0;
	for (const TPair<int32, FCrowdGroup>& Pair : Groups)
	{
		const FCrowdGroup& Group = Pair.Value;
		const FMassUnitCrowdCollectiveConfig& Collective = Group.CollectiveConfig;
		if (!Group.bCollectiveEnabled || !Collective.bEnableVisualDebug)
		{
			continue;
		}
		++DebugGroups;
		bDrawHUD |= Collective.bDebugDrawScreenHUD;
		const FVector Anchor = Group.bHasCachedAnchor ? Group.CachedAnchor : Group.Center;
		const bool bRecruiting = Group.PendingAlertActivationTime >= 0.0f;

		if (Collective.bDebugDrawGroupStatus)
		{
			const FColor StatusColor = Group.bRouting ? FColor::Red
				: Group.bEngaged ? FColor::Green
				: bRecruiting ? FColor::Orange
				: FColor::White;
			const TCHAR* StatusText = Group.bRouting ? TEXT("ROUTED")
				: Group.bEngaged ? TEXT("ENGAGED")
				: bRecruiting ? TEXT("RECRUITING")
				: TEXT("AMBIENT");
			DrawDebugString(World, Anchor + FVector(0.0f, 0.0f, 260.0f),
				FString::Printf(TEXT("Crowd %d  %s\nMorale %.2f  Aware %.2f  Relay %d"),
					Pair.Key, StatusText, Group.Morale, Group.Awareness, Group.EngagementRelayDepth),
				nullptr, StatusColor, Duration, true);
			// Morale bar: full width is 1.0, red/green threshold ticks at rout and rally.
			const FVector BarStart = Anchor + FVector(0.0f, -100.0f, 230.0f);
			DrawDebugLine(World, BarStart, BarStart + FVector(0.0f, 200.0f, 0.0f), FColor(60, 60, 60), false, Duration, 0, 6.0f);
			DrawDebugLine(World, BarStart, BarStart + FVector(0.0f, 200.0f * Group.Morale, 0.0f), StatusColor, false, Duration, 1, 6.0f);
			if (Collective.bEnableMorale)
			{
				const FVector RoutTick = BarStart + FVector(0.0f, 200.0f * Collective.RoutThreshold, 0.0f);
				const FVector RallyTick = BarStart + FVector(0.0f, 200.0f * Collective.RallyThreshold, 0.0f);
				DrawDebugLine(World, RoutTick - FVector(0.0f, 0.0f, 12.0f), RoutTick + FVector(0.0f, 0.0f, 12.0f), FColor::Red, false, Duration, 2, 2.0f);
				DrawDebugLine(World, RallyTick - FVector(0.0f, 0.0f, 12.0f), RallyTick + FVector(0.0f, 0.0f, 12.0f), FColor::Green, false, Duration, 2, 2.0f);
			}
		}

		if (Collective.bDebugDrawThreat && Group.bEngaged)
		{
			if (const AActor* Target = Group.TargetActor.Get())
			{
				const FVector TargetLocation = Target->GetActorLocation();
				DrawDebugLine(World, Anchor, TargetLocation, FColor::Magenta, false, Duration, 0, 2.0f);
				DrawDebugString(World, (Anchor + TargetLocation) * 0.5f,
					FString::Printf(TEXT("Threat %.0f (%d tracked)"), Group.Threat.FindRef(Group.TargetActor), Group.Threat.Num()),
					nullptr, FColor::Magenta, Duration, true);
			}
		}
	}

	if (bDrawHUD && GEngine)
	{
		int32 Key = 7300;  // Crowd collective HUD key range: 7300-7309
		GEngine->AddOnScreenDebugMessage(Key++, Duration, FColor::Cyan, TEXT("=== CROWD COLLECTIVE ==="));
		GEngine->AddOnScreenDebugMessage(Key++, Duration, FColor::White,
			FString::Printf(TEXT("Debug groups %d | Routing %d | Alerted %d | Signals %d (queued %d) | Target switches %d"),
				DebugGroups, LastStats.RoutingGroups, LastStats.GroupsAlerted,
				LastStats.CollectiveSignalsProcessed, PendingSignals.Num(), LastStats.TargetSwitches));
	}
#endif
}

FMassUnitCrowdCollectiveConfig UMassUnitCrowdSystem::SanitizeCollectiveConfig(const FMassUnitCrowdCollectiveConfig& Config)
{
	FMassUnitCrowdCollectiveConfig Result = Config;
	Result.AlertRadius = FMath::Max(1.0f, Result.AlertRadius);
	Result.AlertStrength = FMath::Max(0.0f, Result.AlertStrength);
	Result.AlertRebroadcastInterval = FMath::Max(0.0f, Result.AlertRebroadcastInterval);
	Result.MaxAlertRelayDepth = FMath::Clamp(Result.MaxAlertRelayDepth, 0, 8);
	Result.AlertRelayFalloff = FMath::Clamp(Result.AlertRelayFalloff, 0.0f, 1.0f);
	Result.AlertSensitivity = FMath::Max(0.0f, Result.AlertSensitivity);
	Result.AlertThreshold = FMath::Max(0.01f, Result.AlertThreshold);
	Result.AwarenessDecayPerSecond = FMath::Max(0.0f, Result.AwarenessDecayPerSecond);
	Result.MinAlertReactionDelay = FMath::Max(0.0f, Result.MinAlertReactionDelay);
	Result.MaxAlertReactionDelay = FMath::Max(Result.MinAlertReactionDelay, Result.MaxAlertReactionDelay);
	Result.CasualtyMoraleWeight = FMath::Max(0.0f, Result.CasualtyMoraleWeight);
	Result.MoraleRecoveryPerSecond = FMath::Max(0.0f, Result.MoraleRecoveryPerSecond);
	Result.RoutThreshold = FMath::Clamp(Result.RoutThreshold, 0.0f, 1.0f);
	// Rally must sit above rout or a group would rally and re-rout on the same evaluation.
	Result.RallyThreshold = FMath::Clamp(Result.RallyThreshold, FMath::Min(1.0f, Result.RoutThreshold + 0.05f), 1.0f);
	Result.MinRoutDuration = FMath::Max(0.0f, Result.MinRoutDuration);
	Result.RoutStaggerTime = FMath::Max(0.0f, Result.RoutStaggerTime);
	Result.FleeDistance = FMath::Max(1.0f, Result.FleeDistance);
	Result.FleeSpeedMultiplier = FMath::Clamp(Result.FleeSpeedMultiplier, 0.05f, 4.0f);
	Result.FleeRepathInterval = FMath::Max(0.1f, Result.FleeRepathInterval);
	Result.MoraleEvaluationInterval = FMath::Max(0.05f, Result.MoraleEvaluationInterval);
	Result.FearRadius = FMath::Max(1.0f, Result.FearRadius);
	Result.FearStrength = FMath::Clamp(Result.FearStrength, 0.0f, 1.0f);
	Result.FearSusceptibility = FMath::Max(0.0f, Result.FearSusceptibility);
	Result.DamageThreatScale = FMath::Max(0.0f, Result.DamageThreatScale);
	Result.ProximityThreatRadius = FMath::Max(0.0f, Result.ProximityThreatRadius);
	Result.ProximityThreatPerSecond = FMath::Max(0.0f, Result.ProximityThreatPerSecond);
	Result.ThreatHalfLife = FMath::Max(0.1f, Result.ThreatHalfLife);
	Result.TargetSwitchMargin = FMath::Max(0.0f, Result.TargetSwitchMargin);
	Result.MinTargetSwitchInterval = FMath::Max(0.0f, Result.MinTargetSwitchInterval);
	return Result;
}

bool UMassUnitCrowdSystem::IsEntityValid(FMassUnitEntityHandle Entity) const
{
	return EntitySubsystem && Entity.IsValid()
		&& EntitySubsystem->GetEntityManager().IsEntityValid(Entity.ToMassEntityHandle());
}
