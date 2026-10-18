// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Visual/CrowdInstanceRenderer.h"
#include "Core/MassUnitStats.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Entity/UnitTemplate.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassUnitCommonFragments.h"
#include "Materials/MaterialInterface.h"

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

	/** Clip playback data for one instance; all zero when the template has no clip table. */
	struct FResolvedClip
	{
		float StartTime = 0.0f;
		float StartFrame = 0.0f;
		float FrameCount = 0.0f;
		float FramesPerSecond = 0.0f;
	};

	/**
	 * Picks the clip row for the unit's animation tag. A finished one-shot clip (an attack, a hit) hands
	 * over to Idle with the start stamp moved to the clip's end, so the material needs no state of its own.
	 * Death and Stun never hand over: corpses hold their last frame, and a stun lasts as long as the status.
	 */
	FResolvedClip ResolveClip(const FMassUnitVisualFragment& Visual, const FMassUnitStateFragment& State, float CurrentTime)
	{
		FResolvedClip Result;
		Result.StartTime = CurrentTime - State.StateTime;
		const UUnitTemplate* Template = Visual.SourceTemplate.Get();
		const FMassUnitVATClip* Clip = Template ? Template->FindVertexAnimationClip(Visual.CurrentAnimation) : nullptr;
		if (!Clip)
		{
			return Result;
		}
		if (!Clip->bLoop
			&& Clip->bReturnToIdleWhenDone
			&& Visual.CurrentAnimation != Tags::AnimationDeath()
			&& Visual.CurrentAnimation != Tags::AnimationStun()
			&& State.StateTime >= Clip->GetDuration())
		{
			if (const FMassUnitVATClip* Idle = Template->FindVertexAnimationClip(Tags::AnimationIdle()))
			{
				Result.StartTime += Clip->GetDuration();
				Clip = Idle;
			}
		}
		Result.StartFrame = static_cast<float>(Clip->StartFrame);
		Result.FrameCount = static_cast<float>(FMath::Max(1, Clip->NumFrames));
		// Attitude tempo on looping locomotion and idle only: one-shots keep the timing their impact delays match, and
		// climb / hold / stun keep theirs (climb tempo must match the unscaled ClimbSpeed).
		const bool bTempoClip = Clip->bLoop
			&& (Visual.CurrentAnimation == Tags::AnimationIdle()
				|| Visual.CurrentAnimation == Tags::AnimationWalk()
				|| Visual.CurrentAnimation == Tags::AnimationRun());
		const float Tempo = bTempoClip ? FMath::Max(0.05f, Visual.AnimationRate) : 1.0f;
		Result.FramesPerSecond = Clip->bLoop ? Clip->FramesPerSecond * Tempo : -Clip->FramesPerSecond;
		return Result;
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
	for (TPair<FCrowdBucketKey, FCrowdInstanceBucket>& Pair : Buckets)
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
	for (const TPair<FCrowdBucketKey, FCrowdInstanceBucket>& Pair : Buckets)
	{
		Count += Pair.Value.LiveCount;
	}
	return Count;
}

int32 UCrowdInstanceRenderer::GetAllocatedSlotCount() const
{
	int32 Count = 0;
	for (const TPair<FCrowdBucketKey, FCrowdInstanceBucket>& Pair : Buckets)
	{
		Count += Pair.Value.SlotToEntity.Num();
	}
	return Count;
}

DECLARE_CYCLE_STAT(TEXT("Instance Renderer Update"), STAT_MassUnit_RendererUpdate, STATGROUP_MassUnitSystem);

void UCrowdInstanceRenderer::UpdateInstances(
	const TArray<FMassUnitEntityHandle>& Entities,
	TFunctionRef<int32(const FMassUnitVisualFragment&, const FMassUnitStateFragment&)> AnimationIndexResolver)
{
	if (!World || !EntitySubsystem)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UCrowdInstanceRenderer::UpdateInstances);
	SCOPE_CYCLE_COUNTER(STAT_MassUnit_RendererUpdate);

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const float CurrentTime = World->GetTimeSeconds();

	for (TPair<FCrowdBucketKey, FCrowdInstanceBucket>& Pair : Buckets)
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

		FCrowdBucketKey BodyKey;
		BodyKey.Mesh = Visual->StaticMesh ? Visual->StaticMesh.Get() : FallbackMesh.Get();
		BodyKey.Material = Visual->MaterialOverride;
		if (!BodyKey.Mesh)
		{
			continue;
		}

		// A unit that changed body must give up its slot in the previous bucket first, otherwise it
		// would be drawn twice until the stale slot happened to be reclaimed.
		if (const FCrowdBucketKey* PreviousKey = EntityBucketMesh.Find(NativeHandle))
		{
			if (!(*PreviousKey == BodyKey))
			{
				if (FCrowdInstanceBucket* PreviousBucket = Buckets.Find(*PreviousKey))
				{
					if (const int32* PreviousSlot = PreviousBucket->EntityToSlot.Find(NativeHandle))
					{
						ReleaseSlot(*PreviousBucket, *PreviousSlot);
					}
				}
			}
		}

		FCrowdInstanceBucket* Bucket = FindOrAddBucket(BodyKey);
		if (!Bucket)
		{
			continue;
		}
		const int32 Slot = AcquireSlot(*Bucket, NativeHandle);
		if (Slot == INDEX_NONE)
		{
			continue;
		}
		EntityBucketMesh.Add(NativeHandle, BodyKey);

		FTransform InstanceTransform = Transform->GetTransform();
		if (!FMath::IsNearlyEqual(Visual->LookScale, 1.0f))
		{
			InstanceTransform.SetScale3D(InstanceTransform.GetScale3D() * Visual->LookScale);
		}
		Bucket->SlotTransforms[Slot] = InstanceTransform;
		Bucket->SlotTouched[Slot] = true;

		const FMassUnitVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(NativeHandle);
		const float Speed = Velocity ? Velocity->Value.Size2D() : 0.0f;

		// StateTime accumulates, so CurrentTime - StateTime is a stable start stamp that only moves when
		// the unit actually changes state. Uploading it instead of the elapsed time keeps the custom-data
		// buffer unchanged on the vast majority of updates.
		const FResolvedClip Clip = ResolveClip(*Visual, *State, CurrentTime);
		const float AnimationStartTime = Clip.StartTime;
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
		// The unit's stored variant seed (replicated to client puppets); the index-derived value only if unset.
		WriteCustomData(*Bucket, Slot, CrowdCustomData::RandomSeed,
			Visual->VariantSeed >= 0.0f ? Visual->VariantSeed : MakeRandomSeed(NativeHandle));
		WriteCustomData(*Bucket, Slot, CrowdCustomData::Speed, Speed, 5.0f);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::ClipStartFrame, Clip.StartFrame);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::ClipFrameCount, Clip.FrameCount);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::ClipFramesPerSecond, Clip.FramesPerSecond);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::TintR, Visual->LookTint.R);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::TintG, Visual->LookTint.G);
		WriteCustomData(*Bucket, Slot, CrowdCustomData::TintB, Visual->LookTint.B);

		// Look parts: same transform and the same per-instance values as the body (parts are baked with the body's
		// frame layout). A part that went away is released with every other untouched slot at the end of the pass.
		// From here on `Bucket` may dangle: FindOrAddBucket below can add to (rehash) the bucket map. Copy what is needed first.
		if (!Visual->LookParts.IsEmpty())
		{
			const float* BodyData = Bucket->SlotCustomData.GetData() + Slot * GetCustomDataFloatCount();
			TArray<float, TInlineAllocator<CrowdCustomData::NumFloats>> Values;
			Values.Append(BodyData, GetCustomDataFloatCount());
			for (const FMassUnitLookPiece& Part : Visual->LookParts)
			{
				FCrowdBucketKey PartKey;
				PartKey.Mesh = Part.Mesh;
				PartKey.Material = Part.Material;
				FCrowdInstanceBucket* PartBucket = PartKey.Mesh ? FindOrAddBucket(PartKey) : nullptr;
				const int32 PartSlot = PartBucket ? AcquireSlot(*PartBucket, NativeHandle) : INDEX_NONE;
				if (PartSlot == INDEX_NONE)
				{
					continue;
				}
				PartBucket->SlotTransforms[PartSlot] = InstanceTransform;
				PartBucket->SlotTouched[PartSlot] = true;
				for (int32 Channel = 0; Channel < Values.Num(); ++Channel)
				{
					WriteCustomData(*PartBucket, PartSlot, Channel, Values[Channel]);
				}
			}
		}
	}

	for (TPair<FCrowdBucketKey, FCrowdInstanceBucket>& Pair : Buckets)
	{
		ReleaseUntouchedSlots(Pair.Value);
		FlushBucket(Pair.Value);
		CompactBucket(Pair.Value);
	}
}

FCrowdInstanceBucket* UCrowdInstanceRenderer::FindOrAddBucket(const FCrowdBucketKey& Key)
{
	if (!Key.Mesh || !World)
	{
		return nullptr;
	}
	if (FCrowdInstanceBucket* Existing = Buckets.Find(Key))
	{
		return Existing;
	}
	UInstancedStaticMeshComponent* Component = CreateComponent(Key);
	if (!Component)
	{
		return nullptr;
	}
	// Buckets added while a pass is running (a new look part came into view) start with an empty touched set.
	FCrowdInstanceBucket& Bucket = Buckets.Add(Key);
	Bucket.Component = Component;
	++TopologyRevision;
	return &Bucket;
}

UInstancedStaticMeshComponent* UCrowdInstanceRenderer::CreateComponent(const FCrowdBucketKey& Key) const
{
	UStaticMesh* Mesh = Key.Mesh;
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
	if (Key.Material)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < FMath::Max(1, Mesh->GetStaticMaterials().Num()); ++MaterialIndex)
		{
			Component->SetMaterial(MaterialIndex, Key.Material);
		}
	}

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
