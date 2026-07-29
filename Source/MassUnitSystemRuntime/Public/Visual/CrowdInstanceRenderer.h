// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "MassEntityTypes.h"

#include "CrowdInstanceRenderer.generated.h"

class UInstancedStaticMeshComponent;
class UMassEntitySubsystem;
class UStaticMesh;

/**
 * Per-instance custom-data channel layout uploaded to instanced static meshes.
 *
 * Materials read these through the Per Instance Custom Data node, so the indices are
 * a published contract: never renumber an existing channel, only append at the end and
 * bump NumCustomDataFloats.
 *
 * All 10 channels are written every update, but M_SwarmBot_Instanced currently READS only
 * 4-8 (team RGB, health, seed). Channels 0-2 and 9 - AnimationIndex, AnimationStartTime,
 * LODLevel and Speed - are populated and unread until the vertex-animation material lands;
 * they are the inputs it will consume, so keep writing them. TeamID (3) is an alternative
 * to the RGB channels for palette-indexing materials and is unread by design.
 */
namespace UE::MassUnitSystem::CrowdCustomData
{
	/** Index into the vertex-animation atlas for the unit's current animation. */
	inline constexpr int32 AnimationIndex = 0;
	/** World seconds at which the current animation started; the material derives phase from it. */
	inline constexpr int32 AnimationStartTime = 1;
	/** Distance LOD band from the visibility processor. Lets the material drop detail before the mesh LOD does. */
	inline constexpr int32 LODLevel = 2;
	/** Numeric team id, for materials that index a team palette instead of using the RGB channels. */
	inline constexpr int32 TeamID = 3;
	inline constexpr int32 TeamColorR = 4;
	inline constexpr int32 TeamColorG = 5;
	inline constexpr int32 TeamColorB = 6;
	/** Health in the 0-1 range. Drives damage decals, emissive falloff, and health pips. */
	inline constexpr int32 HealthPercent = 7;
	/** Stable per-entity 0-1 value used to de-synchronise animation phase and colour variation. */
	inline constexpr int32 RandomSeed = 8;
	/** Horizontal speed in cm/s, so the material can cross-fade walk and run cycles. */
	inline constexpr int32 Speed = 9;

	/** Total floats reserved per instance. Append-only; see the channel contract above. */
	inline constexpr int32 NumFloats = 10;
}

/**
 * One instanced-static-mesh component plus the stable slot bookkeeping for the units drawn by it.
 *
 * Slots are owned by an entity for its whole lifetime. Released slots are parked at zero scale and
 * recycled rather than removed, because mid-array instance removal reindexes every later instance
 * and would swap one unit's per-instance data onto another - visible as colour and health popping.
 */
USTRUCT()
struct FCrowdInstanceBucket
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Component = nullptr;

	/** Entity to instance slot. Only ever removed when the entity stops being rendered by this mesh. */
	TMap<FMassEntityHandle, int32> EntityToSlot;

	/** Reverse lookup. An invalid handle marks a parked slot. Parallel to the component's instance array. */
	TArray<FMassEntityHandle> SlotToEntity;

	/** Parked slots available for reuse, newest first. */
	TArray<int32> FreeSlots;

	/** Full-length world transform mirror. Uploaded in a single contiguous batch per update. */
	TArray<FTransform> SlotTransforms;

	/** Full-length custom-data mirror, NumFloats entries per slot. */
	TArray<float> SlotCustomData;

	/** Slots written during the current update. Anything else is released at the end of the pass. */
	TBitArray<> SlotTouched;

	/** Transforms appended to the component at the end of the current update. */
	TArray<FTransform> PendingAddTransforms;

	/** Units currently occupying a slot in this bucket. */
	int32 LiveCount = 0;

	/** Set when any custom-data float actually changed, so unchanged frames skip the upload entirely. */
	bool bCustomDataDirty = false;
};

/**
 * Instanced crowd renderer for units that are too distant or too numerous for the skeletal mesh pool.
 *
 * Design notes that are easy to get wrong and expensive to rediscover:
 *
 * - Plain UInstancedStaticMeshComponent is used deliberately, not HISM. Under GPUScene (UE5 default on
 *   SM5+) ISM instances are frustum-culled and LOD-selected per instance on the GPU. HISM adds a CPU
 *   cluster tree that assumes mostly-static instances and has to be rebuilt as they move, which is a
 *   net loss for a crowd where every unit moves every update.
 * - Transforms are pushed with bMarkRenderStateDirty = false on purpose. Since 5.5 the batch update
 *   records dirty instances through the primitive instance data manager and streams deltas to GPUScene;
 *   marking the whole render state dirty would rebuild the proxy each update instead.
 * - Detail beyond the mesh LOD chain belongs in the material via the custom-data channels above, not in
 *   extra components. One component per unique static mesh keeps draw calls flat as unit counts grow.
 */
UCLASS()
class MASSUNITSYSTEMRUNTIME_API UCrowdInstanceRenderer : public UObject
{
	GENERATED_BODY()

public:
	/** FallbackMesh is used for any unit whose visual fragment has no static mesh assigned. */
	void Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, UStaticMesh* InFallbackMesh);
	void Deinitialize();

	/**
	 * Reconciles the instance buckets with the supplied entities. Safe to call with an empty array.
	 *
	 * AnimationIndexResolver maps a unit's current animation tag onto its index in the vertex-animation
	 * atlas. It is injected rather than looked up here so the renderer stays independent of how a project
	 * registers its animation textures.
	 */
	void UpdateInstances(
		const TArray<FMassUnitEntityHandle>& Entities,
		TFunctionRef<int32(const struct FMassUnitVisualFragment&, const struct FMassUnitStateFragment&)> AnimationIndexResolver);

	void SetFallbackMesh(UStaticMesh* InFallbackMesh) { FallbackMesh = InFallbackMesh; }
	void SetMaxUnits(int32 InMaxUnits) { MaxUnits = FMath::Max(1, InMaxUnits); }

	/** Number of ISM components allocated, which equals the number of distinct unit meshes in view. */
	int32 GetComponentCount() const { return Buckets.Num(); }

	/** Units currently drawn. Excludes parked slots retained for reuse. */
	int32 GetLiveInstanceCount() const;

	/** Instances allocated on the components, including parked slots. */
	int32 GetAllocatedSlotCount() const;

	/** Increments only when slots are added or reclaimed, never while units move. */
	int32 GetTopologyRevision() const { return TopologyRevision; }

	static constexpr int32 GetCustomDataFloatCount() { return UE::MassUnitSystem::CrowdCustomData::NumFloats; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FallbackMesh = nullptr;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMesh>, FCrowdInstanceBucket> Buckets;

	/** Which mesh currently owns each entity's slot, so a mesh swap releases the old bucket's slot. */
	TMap<FMassEntityHandle, TObjectPtr<UStaticMesh>> EntityBucketMesh;

	int32 MaxUnits = 10000;
	int32 TopologyRevision = 0;

	FCrowdInstanceBucket* FindOrAddBucket(UStaticMesh* Mesh);
	UInstancedStaticMeshComponent* CreateComponent(UStaticMesh* Mesh) const;

	/** Returns an existing, recycled, or freshly appended slot for the entity. */
	int32 AcquireSlot(FCrowdInstanceBucket& Bucket, FMassEntityHandle Entity);
	void ReleaseSlot(FCrowdInstanceBucket& Bucket, int32 Slot);

	/** Releases every slot in the bucket that was not written during this update. */
	void ReleaseUntouchedSlots(FCrowdInstanceBucket& Bucket);

	/** Appends pending instances, uploads transforms, and uploads custom data only when it changed. */
	void FlushBucket(FCrowdInstanceBucket& Bucket);

	/** Trims parked slots off the tail once they dominate the bucket. Never reindexes a live slot. */
	void CompactBucket(FCrowdInstanceBucket& Bucket);

	/** Writes a custom-data float and reports whether it differed from the value already uploaded. */
	void WriteCustomData(FCrowdInstanceBucket& Bucket, int32 Slot, int32 Channel, float Value, float Tolerance = UE_KINDA_SMALL_NUMBER);
};
