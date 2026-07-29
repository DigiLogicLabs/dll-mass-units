// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Visual/CrowdInstanceRenderer.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Config/MassUnitSystemSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassUnitCommonFragments.h"

namespace
{
	using namespace UE::MassUnitSystem;

	/** Parked slots keep their last position so bucket bounds stay tight, but collapse to nothing. */
	FTransform MakeParkedTransform(const FTransform& LastKnown)
	{
		FTransform Parked = LastKnown;
		Parked.SetScale3D(FVector::ZeroVector);
		return Parked;
	}

	/**
	 * Stable, well-distributed 0-1 value per entity.
	 * The golden-ratio conjugate scatters consecutive entity indices, so units spawned in a batch do not
	 * end up animating in lockstep the way a plain modulo would leave them.
	 */
	float MakeRandomSeed(FMassEntityHandle Entity)
	{
		return FMath::Frac(static_cast<float>(Entity.Index) * 0.6180339887f);
	}

	/** Free slots must outnumber this before a bucket bothers trimming its tail. */
	constexpr int32 MinFreeSlotsBeforeCompaction = 64;
}

void UCrowdInstanceRenderer::Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, UStaticMesh* InFallbackMesh)
{
	World = InWorld;
	EntitySubsystem = InEntitySubsystem;
	FallbackMesh = InFallbackMesh;
	TopologyRevision = 0;
	if (const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>())
	{
		MaxUnits = FMath::Max(1, Settings->MaxUnits);
	}
}

void UCrowdInstanceRenderer::Deinitialize()
{
	for (TPair<TObjectPtr<UStaticMesh>, FCrowdInstanceBucket>& Pair : Buckets)
	{
		if (Pair.Value.Component)
		{
			Pair.Value.Component->DestroyComponent();
		}
	}
	Buckets.Reset();
	EntityBucketMesh.Reset();
	FallbackMesh = nullptr;
	EntitySubsystem = nullptr;
	World = nullptr;
}

int32 UCrowdInstanceRenderer::GetLiveInstanceCount() const
{
	int32 Count = 0;
	for (const TPair<TObjectPtr<UStaticMesh>, FCrowdInstanceBucket>& Pair : Buckets)
	{
		Count += Pair.Value.LiveCount;
	}
	return Count;
}

int32 UCrowdInstanceRenderer::GetAllocatedSlotCount() const
{
	int32 Count = 0;
	for (const TPair<TObjectPtr<UStaticMesh>, FCrowdInstanceBucket>& Pair : Buckets)
	{
		Count += Pair.Value.SlotToEntity.Num();
	}
	return Count;
}

void UCrowdInstanceRenderer::UpdateInstances(
	const TArray<FMassUnitEntityHandle>& Entities,
	TFunctionRef<int32(const FMassUnitVisualFragment&, const FMassUnitStateFragment&)> AnimationIndexResolver)
{
	if (!World || !EntitySubsystem)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UCrowdInstanceRenderer::UpdateInstances);

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const float CurrentTime = World->GetTimeSeconds();

	for (TPair<TObjectPtr<UStaticMesh>, FCrowdInstanceBucket>& Pair : Buckets)
	{
		FCrowdInstanceBucket& Bucket = Pair.Value;
		Bucket.SlotTouched.Init(false, Bucket.SlotToEntity.Num());
		Bucket.PendingAddTransforms.Reset();
	}

	const int32 Count = FMath::Min(MaxUnits, Entities.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FMassEntityHandle NativeHandle = Entities[Index].ToMassEntityHandle();
		if (!EntityManager.IsEntityValid(NativeHandle))
		{
			continue;
		}
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
		const FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle);
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		const FMassUnitTeamFragment* Team = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(NativeHandle);
		if (!Transform || !Visual || !State || !Team || !Visual->bIsVisible || Visual->bUseSkeletalMesh)
		{
			// Left untouched, so any slot this unit held is released at the end of the pass.
			continue;
		}

		UStaticMesh* Mesh = Visual->StaticMesh ? Visual->StaticMesh.Get() : FallbackMesh.Get();
		if (!Mesh)
		{
			continue;
		}

		// A unit that changed mesh must give up its slot in the previous bucket first, otherwise it
		// would be drawn twice until the stale slot happened to be reclaimed.
		if (const TObjectPtr<UStaticMesh>* PreviousMesh = EntityBucketMesh.Find(NativeHandle))
		{
			if (PreviousMesh->Get() != Mesh)
			{
				if (FCrowdInstanceBucket* PreviousBucket = Buckets.Find(*PreviousMesh))
				{
					if (const int32* PreviousSlot = PreviousBucket->EntityToSlot.Find(NativeHandle))
					{
						ReleaseSlot(*PreviousBucket, *PreviousSlot);
					}
				}
			}
		}

		FCrowdInstanceBucket* Bucket = FindOrAddBucket(Mesh);
		if (!Bucket)
		{
			continue;
		}
		const int32 Slot = AcquireSlot(*Bucket, NativeHandle);
		if (Slot == INDEX_NONE)
		{
			continue;
		}
		EntityBucketMesh.Add(NativeHandle, Mesh);

		Bucket->SlotTransforms[Slot] = Transform->GetTransform();
		Bucket->SlotTouched[Slot] = true;

		const FMassUnitVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(NativeHandle);
		const float Speed = Velocity ? Velocity->Value.Size2D() : 0.0f;

		// StateTime accumulates, so CurrentTime - StateTime is a stable start stamp that only moves when
		// the unit actually changes state. Uploading it instead of the elapsed time keeps the custom-data
		// buffer unchanged on the vast majority of updates.
		const float AnimationStartTime = CurrentTime - State->StateTime;
		const float HealthPercent = State->MaxHealth > UE_SMALL_NUMBER
			? FMath::Clamp(State->Health / State->MaxHealth, 0.0f, 1.0f)
			: 0.0f;

		WriteCustomData(*Bucket, Slot, CrowdCustomData::AnimationIndex, static_cast<float>(AnimationIndexResolver(*Visual, *State)));
		WriteCustomData(*Bucket, Slot, CrowdCustomData::AnimationStartTime, AnimationStartTime, 0.01f);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::LODLevel, static_cast<float>(Visual->LODLevel));
		WriteCustomData(*Bucket, Slot, CrowdCustomData::TeamID, static_cast<float>(Team->TeamID));
		WriteCustomData(*Bucket, Slot, CrowdCustomData::TeamColorR, Team->TeamColor.R);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::TeamColorG, Team->TeamColor.G);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::TeamColorB, Team->TeamColor.B);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::HealthPercent, HealthPercent, 0.01f);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::RandomSeed, MakeRandomSeed(NativeHandle));
		WriteCustomData(*Bucket, Slot, CrowdCustomData::Speed, Speed, 5.0f);
	}

	for (TPair<TObjectPtr<UStaticMesh>, FCrowdInstanceBucket>& Pair : Buckets)
	{
		ReleaseUntouchedSlots(Pair.Value);
		FlushBucket(Pair.Value);
		CompactBucket(Pair.Value);
	}
}

FCrowdInstanceBucket* UCrowdInstanceRenderer::FindOrAddBucket(UStaticMesh* Mesh)
{
	if (!Mesh || !World)
	{
		return nullptr;
	}
	if (FCrowdInstanceBucket* Existing = Buckets.Find(Mesh))
	{
		return Existing;
	}
	UInstancedStaticMeshComponent* Component = CreateComponent(Mesh);
	if (!Component)
	{
		return nullptr;
	}
	FCrowdInstanceBucket& Bucket = Buckets.Add(Mesh);
	Bucket.Component = Component;
	++TopologyRevision;
	return &Bucket;
}

UInstancedStaticMeshComponent* UCrowdInstanceRenderer::CreateComponent(UStaticMesh* Mesh) const
{
	if (!World || !Mesh)
	{
		return nullptr;
	}
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(World);
	if (!Component)
	{
		return nullptr;
	}

	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();

	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetNumCustomDataFloats(GetCustomDataFloatCount());
	Component->SetStaticMesh(Mesh);

	// A crowd is the single worst case for shadow cost: thousands of small movable casters each
	// re-rendered per cascade. Defaults here are off, and the settings expose the override.
	const bool bCastShadows = Settings ? Settings->bCrowdInstancesCastShadows : false;
	Component->SetCastShadow(bCastShadows);
	Component->bCastDynamicShadow = bCastShadows;
	Component->bCastContactShadow = false;
	Component->bCastFarShadow = false;

	// Crowd units are lit, but they should not participate in the expensive global lighting passes.
	Component->bAffectDistanceFieldLighting = false;
	Component->bAffectDynamicIndirectLighting = false;
	Component->SetReceivesDecals(false);

	// Per-instance distance culling, applied on the GPU alongside frustum culling.
	if (Settings && Settings->InstanceEndCullDistance > 0)
	{
		Component->SetCullDistances(Settings->InstanceStartCullDistance, Settings->InstanceEndCullDistance);
	}

	// Applies only to unit materials that use world position offset; the shipped SwarmBot material does
	// not. Past this range a unit is a few pixels tall and per-vertex WPO evaluation is pure cost.
	if (Settings)
	{
		Component->SetWorldPositionOffsetDisableDistance(FMath::Max(0, Settings->WorldPositionOffsetDisableDistance));
	}

	Component->RegisterComponentWithWorld(World);
	return Component;
}

int32 UCrowdInstanceRenderer::AcquireSlot(FCrowdInstanceBucket& Bucket, FMassEntityHandle Entity)
{
	if (const int32* Existing = Bucket.EntityToSlot.Find(Entity))
	{
		return *Existing;
	}

	int32 Slot = INDEX_NONE;
	if (Bucket.FreeSlots.Num() > 0)
	{
		Slot = Bucket.FreeSlots.Pop(EAllowShrinking::No);
	}
	else
	{
		Slot = Bucket.SlotToEntity.Num();
		Bucket.SlotToEntity.AddDefaulted();
		Bucket.SlotTransforms.AddDefaulted();
		Bucket.SlotCustomData.AddZeroed(GetCustomDataFloatCount());
		Bucket.SlotTouched.Add(false);
		// Appended slots are contiguous at the tail, so one AddInstances call covers them all in FlushBucket.
		Bucket.PendingAddTransforms.AddDefaulted();
		++TopologyRevision;
	}

	Bucket.SlotToEntity[Slot] = Entity;
	Bucket.EntityToSlot.Add(Entity, Slot);
	++Bucket.LiveCount;
	// A recycled slot still holds the previous unit's values; force the next writes through.
	Bucket.bCustomDataDirty = true;
	return Slot;
}

void UCrowdInstanceRenderer::ReleaseSlot(FCrowdInstanceBucket& Bucket, int32 Slot)
{
	if (!Bucket.SlotToEntity.IsValidIndex(Slot) || !Bucket.SlotToEntity[Slot].IsSet())
	{
		return;
	}

	const FMassEntityHandle Entity = Bucket.SlotToEntity[Slot];
	Bucket.EntityToSlot.Remove(Entity);
	EntityBucketMesh.Remove(Entity);
	Bucket.SlotToEntity[Slot] = FMassEntityHandle();
	Bucket.SlotTransforms[Slot] = MakeParkedTransform(Bucket.SlotTransforms[Slot]);
	Bucket.FreeSlots.Add(Slot);
	--Bucket.LiveCount;

	FMemory::Memzero(
		Bucket.SlotCustomData.GetData() + Slot * GetCustomDataFloatCount(),
		GetCustomDataFloatCount() * sizeof(float));
	Bucket.bCustomDataDirty = true;
}

void UCrowdInstanceRenderer::ReleaseUntouchedSlots(FCrowdInstanceBucket& Bucket)
{
	if (Bucket.LiveCount <= 0)
	{
		return;
	}
	for (int32 Slot = 0; Slot < Bucket.SlotToEntity.Num(); ++Slot)
	{
		if (!Bucket.SlotTouched[Slot] && Bucket.SlotToEntity[Slot].IsSet())
		{
			ReleaseSlot(Bucket, Slot);
		}
	}
}

void UCrowdInstanceRenderer::FlushBucket(FCrowdInstanceBucket& Bucket)
{
	UInstancedStaticMeshComponent* Component = Bucket.Component;
	if (!Component)
	{
		return;
	}

	if (Bucket.PendingAddTransforms.Num() > 0)
	{
		const int32 FirstNewSlot = Bucket.SlotToEntity.Num() - Bucket.PendingAddTransforms.Num();
		for (int32 Offset = 0; Offset < Bucket.PendingAddTransforms.Num(); ++Offset)
		{
			Bucket.PendingAddTransforms[Offset] = Bucket.SlotTransforms[FirstNewSlot + Offset];
		}
		Component->AddInstances(
			Bucket.PendingAddTransforms,
			/*bShouldReturnIndices*/ false,
			/*bWorldSpace*/ true,
			/*bUpdateNavigation*/ false);
		Bucket.PendingAddTransforms.Reset();
	}

	const int32 SlotCount = Bucket.SlotToEntity.Num();
	if (SlotCount == 0 || Component->GetInstanceCount() != SlotCount)
	{
		// The mirror and the component disagree, which means something outside this renderer edited the
		// component. Skip the upload rather than write past the end of the instance array.
		return;
	}

	// bMarkRenderStateDirty stays false: the instance data manager streams the changed transforms to
	// GPUScene by itself, whereas marking the render state dirty would rebuild the whole scene proxy.
	Component->BatchUpdateInstancesTransforms(
		0,
		Bucket.SlotTransforms,
		/*bWorldSpace*/ true,
		/*bMarkRenderStateDirty*/ false,
		/*bTeleport*/ true);

	if (Bucket.bCustomDataDirty)
	{
		Component->SetCustomData(0, SlotCount - 1, Bucket.SlotCustomData, /*bMarkRenderStateDirty*/ false);
		Bucket.bCustomDataDirty = false;
	}

	const bool bShouldBeVisible = Bucket.LiveCount > 0;
	if (Component->IsVisible() != bShouldBeVisible)
	{
		Component->SetVisibility(bShouldBeVisible);
	}
}

void UCrowdInstanceRenderer::CompactBucket(FCrowdInstanceBucket& Bucket)
{
	UInstancedStaticMeshComponent* Component = Bucket.Component;
	if (!Component || Bucket.FreeSlots.Num() < MinFreeSlotsBeforeCompaction)
	{
		return;
	}
	if (Bucket.FreeSlots.Num() * 2 < Bucket.SlotToEntity.Num())
	{
		return;
	}

	// Only trailing parked slots are removed. Trimming from the middle would reindex live slots and
	// hand one unit's per-instance data to another, which is exactly what stable slots exist to prevent.
	int32 NewSlotCount = Bucket.SlotToEntity.Num();
	while (NewSlotCount > 0 && !Bucket.SlotToEntity[NewSlotCount - 1].IsSet())
	{
		--NewSlotCount;
	}
	const int32 RemovedCount = Bucket.SlotToEntity.Num() - NewSlotCount;
	if (RemovedCount <= 0)
	{
		return;
	}

	TArray<int32> RemovedIndices;
	RemovedIndices.Reserve(RemovedCount);
	for (int32 Slot = Bucket.SlotToEntity.Num() - 1; Slot >= NewSlotCount; --Slot)
	{
		RemovedIndices.Add(Slot);
	}
	Component->RemoveInstances(RemovedIndices, /*bInstanceArrayAlreadySortedInReverseOrder*/ true);

	Bucket.SlotToEntity.SetNum(NewSlotCount, EAllowShrinking::No);
	Bucket.SlotTransforms.SetNum(NewSlotCount, EAllowShrinking::No);
	Bucket.SlotCustomData.SetNum(NewSlotCount * GetCustomDataFloatCount(), EAllowShrinking::No);
	Bucket.SlotTouched.SetNumUninitialized(NewSlotCount);
	Bucket.FreeSlots.RemoveAll([NewSlotCount](int32 Slot) { return Slot >= NewSlotCount; });
	++TopologyRevision;
}

void UCrowdInstanceRenderer::WriteCustomData(FCrowdInstanceBucket& Bucket, int32 Slot, int32 Channel, float Value, float Tolerance)
{
	const int32 FloatIndex = Slot * GetCustomDataFloatCount() + Channel;
	if (!Bucket.SlotCustomData.IsValidIndex(FloatIndex))
	{
		return;
	}
	float& Destination = Bucket.SlotCustomData[FloatIndex];
	if (!FMath::IsNearlyEqual(Destination, Value, Tolerance))
	{
		Destination = Value;
		Bucket.bCustomDataDirty = true;
	}
}
