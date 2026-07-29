// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "MassUnitCrowdSystem.generated.h"

class UMassEntitySubsystem;
class UMassUnitNavigationSystem;
class AActor;
class UDamageType;
class UGameplayEffect;

/** Selects whether crowd distance, steering, and random destinations use a plane or full XYZ space. */
UENUM(BlueprintType)
enum class EMassUnitCrowdMovementMode : uint8
{
	Planar2D UMETA(DisplayName = "Planar 2D (Navmesh Compatible)"),
	Free3D UMETA(DisplayName = "Free 3D")
};

/** Defines how an engagement-enabled group acquires its first player target. */
UENUM(BlueprintType)
enum class EMassUnitCrowdActivationMode : uint8
{
	Manual UMETA(DisplayName = "Manual Blueprint Call"),
	OnInteraction UMETA(DisplayName = "Player Interaction or Damage"),
	OnPlayerProximity UMETA(DisplayName = "Player Proximity"),
	Always UMETA(DisplayName = "Always Acquire Player")
};

/** Aggregated group/subgroup moments suitable for pooled audio, Niagara, or other presentation. */
UENUM(BlueprintType)
enum class EMassUnitCrowdCue : uint8
{
	MovementStarted,
	AmbientInteraction,
	EngagementStarted,
	EngagementEnded,
	Attack
};

/** Designer-facing controls for one lightweight crowd group. */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitCrowdConfig
{
	GENERATED_BODY()

	/** Planar mode is appropriate for ground crowds and navmesh. Free 3D supports flying, swimming, and 2.5D/3D groups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Movement")
	EMassUnitCrowdMovementMode MovementMode = EMassUnitCrowdMovementMode::Planar2D;

	/** Follow native navmesh elevation in Planar2D navigation mode without per-unit ground traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Movement", meta = (EditCondition = "MovementMode == EMassUnitCrowdMovementMode::Planar2D"))
	bool bConformToNavmeshHeight = true;

	/** Vertical offset from the navmesh surface to the unit mesh pivot (50 cm centers the asset-free cube). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Movement", meta = (EditCondition = "MovementMode == EMassUnitCrowdMovementMode::Planar2D && bConformToNavmeshHeight", ForceUnits = "cm"))
	float NavigationHeightOffset = 50.0f;

	/** Deterministically partitions a large group for spatial variation and aggregated presentation cues. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Subgroups")
	bool bEnableManagedSubgroups = true;

	/** Maximum entities in one managed subgroup. No Actors or UObjects are created per subgroup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Subgroups", meta = (EditCondition = "bEnableManagedSubgroups", ClampMin = "1", UIMin = "1"))
	int32 ManagedSubgroupSize = 32;

	/** Fraction of the parent wander radius assigned around each deterministic subgroup center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Subgroups", meta = (EditCondition = "bEnableManagedSubgroups", ClampMin = "0.1", ClampMax = "1.0"))
	float SubgroupWanderRadiusScale = 0.65f;

	/** Distance members look ahead on their subgroup's shared ambient corridor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Subgroups", meta = (EditCondition = "bEnableManagedSubgroups", ClampMin = "1.0", ForceUnits = "cm"))
	float SubgroupPathLookAheadDistance = 350.0f;

	/** Emits coalesced Blueprint cues for pooled sound/VFX rather than per-entity components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Presentation")
	bool bEnableGroupCueEvents = true;

	/** Minimum time between presentation cues from the same managed subgroup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Presentation", meta = (EditCondition = "bEnableGroupCueEvents", ClampMin = "0.0", ForceUnits = "s"))
	float GroupCueCooldown = 0.35f;

	/** Maximum world-space radius used when choosing random destinations around the group center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Wander", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float WanderRadius = 1500.0f;

	/** Minimum distance of a newly selected destination from the unit's current location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Wander", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MinWanderDistance = 300.0f;

	/** Minimum pause after reaching a destination before selecting another one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Wander", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float MinIdleTime = 0.5f;

	/** Maximum pause after reaching a destination before selecting another one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Wander", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float MaxIdleTime = 2.0f;

	/** Re-evaluate a destination after this time so blocked units cannot remain stuck forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Wander", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float MaxMoveTime = 8.0f;

	/** Lowest random movement-speed multiplier applied when a new destination is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Wander", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float MinMoveSpeedMultiplier = 0.8f;

	/** Highest random movement-speed multiplier applied when a new destination is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Wander", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float MaxMoveSpeedMultiplier = 1.2f;

	/** Enables spatial-hash separation steering without adding collision or an Actor per unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Avoidance")
	bool bEnableSeparation = true;

	/** Neighbor radius used by lightweight separation steering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Avoidance", meta = (EditCondition = "bEnableSeparation", ClampMin = "1.0", ForceUnits = "cm"))
	float SeparationRadius = 140.0f;

	/** Separation influence relative to the desired travel direction. Zero disables steering; one gives equal influence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Avoidance", meta = (EditCondition = "bEnableSeparation", ClampMin = "0.0", ClampMax = "4.0"))
	float SeparationWeight = 1.25f;

	/** Enables occasional paired social pauses. Interactions broadcast a Blueprint event and do not imply combat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Interaction")
	bool bEnableInteractions = true;

	/** Chance from zero to one that an idle decision becomes an interaction when a neighbor is available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Interaction", meta = (EditCondition = "bEnableInteractions", ClampMin = "0.0", ClampMax = "1.0"))
	float InteractionChance = 0.15f;

	/** Maximum distance at which two units can begin a lightweight interaction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Interaction", meta = (EditCondition = "bEnableInteractions", ClampMin = "1.0", ForceUnits = "cm"))
	float InteractionRadius = 275.0f;

	/** Minimum duration of a paired interaction pause. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Interaction", meta = (EditCondition = "bEnableInteractions", ClampMin = "0.0", ForceUnits = "s"))
	float MinInteractionTime = 0.75f;

	/** Maximum duration of a paired interaction pause. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Interaction", meta = (EditCondition = "bEnableInteractions", ClampMin = "0.0", ForceUnits = "s"))
	float MaxInteractionTime = 2.0f;

	/** Units farther than this from every player observer sleep until a player returns. Zero disables simulation sleeping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|LOD", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MaxSimulationDistance = 15000.0f;

	/** Stable seed used with each native entity handle so the same setup produces repeatable choices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Randomness", meta = (ClampMin = "0"))
	int32 RandomSeed = 1337;

	/** Draw group bounds, destination arrows, and interaction links as decisions occur. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Debug")
	bool bEnableVisualDebug = false;
};

/**
 * Opt-in player engagement controls. One target is cached per group while movement,
 * cooldowns, health, and representation remain lightweight per-entity fragments.
 */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitPlayerEngagementConfig
{
	GENERATED_BODY()

	/** How this group first acquires a player target. Manual activation always remains available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Activation")
	EMassUnitCrowdActivationMode ActivationMode = EMassUnitCrowdActivationMode::OnInteraction;

	/** Acquisition distance for Player Proximity mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Activation", meta = (EditCondition = "ActivationMode == EMassUnitCrowdActivationMode::OnPlayerProximity", ClampMin = "1.0", ForceUnits = "cm"))
	float ActivationRadius = 1200.0f;

	/** Release the target when no living group member remains within Deactivation Distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Activation")
	bool bAutoDeactivate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Activation", meta = (EditCondition = "bAutoDeactivate", ClampMin = "1.0", ForceUnits = "cm"))
	float DeactivationDistance = 5000.0f;

	/** Group-level interval for sampling a moving target. Lower values react faster but cause more destination work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Following", meta = (ClampMin = "0.02", ForceUnits = "s"))
	float TargetRefreshInterval = 0.2f;

	/** Do not rebuild destinations until the sampled target moved at least this far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Following", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float RepathDistance = 125.0f;

	/**
	 * When the crowd group uses navigation, calculate one navmesh corridor from
	 * the group anchor and share it across all followers. Disable only when every
	 * unit genuinely requires an independent navmesh path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Navigation")
	bool bUseSharedNavigationPath = true;

	/** Minimum time between shared group path queries. Target sampling remains independent and cheaper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Navigation", meta = (EditCondition = "bUseSharedNavigationPath", ClampMin = "0.05", ForceUnits = "s"))
	float SharedPathRepathInterval = 0.5f;

	/** Target movement required before rebuilding the shared navmesh corridor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Navigation", meta = (EditCondition = "bUseSharedNavigationPath", ClampMin = "1.0", ForceUnits = "cm"))
	float SharedPathRepathDistance = 250.0f;

	/** Distance units look ahead along the shared corridor before local separation is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Navigation", meta = (EditCondition = "bUseSharedNavigationPath", ClampMin = "1.0", ForceUnits = "cm"))
	float SharedPathLookAheadDistance = 350.0f;

	/** Preferred stand-off radius around the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Following", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float FollowDistance = 150.0f;

	/** Deterministic extra spread around the follow radius, preventing every unit from selecting one point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Following", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float FollowSpread = 125.0f;

	/** Multiplies each unit's original template speed while the group is engaged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Following", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float EngagedMoveSpeedMultiplier = 1.1f;

	/** Allow native cooldown-based attack requests when a unit reaches range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat")
	bool bEnableAttacks = true;

	/** Zero uses each unit template's Attack Range. A positive value overrides the group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks", ClampMin = "0.0", ForceUnits = "cm"))
	float AttackRangeOverride = 0.0f;

	/** Multiplies Base Damage and Unit Level when producing an attack request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks", ClampMin = "0.0"))
	float DamageMultiplier = 1.0f;

	/** Also route attack damage through Unreal's AnyDamage pipeline. Disable when GAS alone owns target health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks"))
	bool bApplyActorDamage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks && bApplyActorDamage"))
	TSubclassOf<UDamageType> DamageTypeClass;

	/** Optional GAS effect applied to a target Actor that exposes an Ability System Component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks"))
	TSubclassOf<UGameplayEffect> GameplayEffectToTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks && GameplayEffectToTarget", ClampMin = "0.0"))
	float GameplayEffectLevel = 1.0f;

	/**
	 * Maximum units allowed to be mid-attack at the same time. Zero is unlimited.
	 *
	 * A unit must take a token from the group's pool to commit an attack and holds it for one attack
	 * cooldown, so peak incoming damage is bounded by MaxSimultaneousAttackers / AttackCooldown rather
	 * than by however many units happened to path into range. Units that cannot get a token wait on the
	 * standoff ring instead of piling into the target.
	 *
	 * Leave at zero to keep the pre-token behaviour. Six to ten reads as an aggressive swarm while
	 * keeping incoming damage designed rather than emergent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks", ClampMin = "0", UIMin = "0", UIMax = "24"))
	int32 MaxSimultaneousAttackers = 0;

	/** Where token-less units wait, as a multiple of attack range. Must exceed one or they attack anyway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks && MaxSimultaneousAttackers > 0", ClampMin = "1.05", ClampMax = "4.0"))
	float StandoffDistanceMultiplier = 1.35f;

	/**
	 * Spread engaged units around the target on fixed angular slots instead of random follow offsets.
	 *
	 * Without this, every engaged unit steers at the same point and compacts into a solid ring, which
	 * reads as units clipping through each other rather than as a coordinated attack. Slots are derived
	 * deterministically from the entity index, so they cost no bookkeeping and survive respawns.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Following")
	bool bUseEngagementSlots = false;

	/** Number of angular positions around the target. Units sharing a slot are separated by the usual steering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Following", meta = (EditCondition = "bUseEngagementSlots", ClampMin = "3", ClampMax = "64"))
	int32 EngagementSlotCount = 12;

	/** Resume ambient wandering after the target is released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Lifecycle")
	bool bReturnToWanderOnDeactivate = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Debug")
	bool bEnableVisualDebug = false;

	/** Draw every unit's slot destination. Off by default; group-only debug avoids thousands of lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Debug", meta = (EditCondition = "bEnableVisualDebug", AdvancedDisplay))
	bool bDrawUnitDestinations = false;
};

/** Snapshot of the most recent budgeted crowd update. */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitCrowdStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 RegisteredGroups = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 RegisteredUnits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 ManagedSubgroups = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 ActiveUnits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 SleepingUnits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 UnitsUpdated = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 DestinationsAssigned = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 InteractionsStarted = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd")
	int32 NeighborChecks = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Engagement")
	int32 EngagedGroups = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Engagement")
	int32 EngagedUnits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Engagement")
	int32 AttacksRequested = 0;

	/** Units currently holding an attack token. Bounded by MaxSimultaneousAttackers when that is non-zero. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Engagement")
	int32 ActiveAttackers = 0;

	/** Units in attack range but waiting on the standoff ring because the token pool was full. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Engagement")
	int32 UnitsAwaitingAttackToken = 0;

	/** Group-level navmesh corridor queries issued by the most recent update. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Engagement")
	int32 SharedPathsBuilt = 0;

	/** Managed ambient subgroup corridors built by the most recent update. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Navigation")
	int32 AmbientSharedPathsBuilt = 0;

	/** Per-entity navmesh requests issued when shared navigation is explicitly disabled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Engagement")
	int32 PerUnitPathsRequested = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMassUnitCrowdInteractionSignature,
	FMassUnitHandle, UnitA,
	FMassUnitHandle, UnitB);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMassUnitCrowdEngagementSignature,
	int32, CrowdGroupHandle,
	AActor*, TargetActor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMassUnitCrowdAttackSignature,
	FMassUnitHandle, AttackingUnit,
	AActor*, TargetActor,
	float, Damage);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FMassUnitCrowdCueSignature,
	int32, CrowdGroupHandle,
	int32, SubgroupIndex,
	EMassUnitCrowdCue, Cue,
	FVector, WorldLocation);

/**
 * World-owned, timer-driven behavior LOD for lightweight ambient crowds.
 * Smooth movement remains in the native Mass processor; this service only
 * performs budgeted decisions, spatial-hash steering, and interaction events.
 */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UMassUnitCrowdSystem : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(
		UWorld* InWorld,
		UMassEntitySubsystem* InEntitySubsystem,
		UMassUnitEntityManager* InUnitManager,
		UMassUnitNavigationSystem* InNavigationSystem);
	void Deinitialize();

	/** Registers valid handles as one independently configurable crowd group. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd", meta = (ReturnDisplayName = "Crowd Group Handle"))
	int32 RegisterCrowdGroup(
		const TArray<FMassUnitHandle>& UnitHandles,
		FVector Center,
		const FMassUnitCrowdConfig& Config,
		bool bUseNavigation = false,
		float AcceptanceRadius = 50.0f);

	/** Removes a crowd group. Optionally clears movement and restores each unit's original speed. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd")
	bool UnregisterCrowdGroup(int32 CrowdGroupHandle, bool bStopUnits = true);

	/** Pauses or resumes low-frequency behavior for a group without destroying its units. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd")
	bool SetCrowdGroupPaused(int32 CrowdGroupHandle, bool bPaused, bool bStopUnits = true);

	/** Moves the center used for future random destinations. Existing destinations are not changed. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd")
	bool SetCrowdGroupCenter(int32 CrowdGroupHandle, FVector NewCenter);

	/** Immediately gives active units in one group a fresh decision, bypassing the normal per-update budget. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd")
	bool ForceCrowdGroupUpdate(int32 CrowdGroupHandle);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd")
	bool IsCrowdGroupRegistered(int32 CrowdGroupHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd")
	int32 GetCrowdGroupUnitCount(int32 CrowdGroupHandle) const;

	/** Number of deterministic managed subgroups currently represented by this parent crowd. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Subgroups")
	int32 GetCrowdGroupSubgroupCount(int32 CrowdGroupHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Subgroups")
	int32 GetUnitSubgroupIndex(FMassUnitHandle UnitHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Subgroups")
	TArray<FMassUnitHandle> GetCrowdSubgroupUnits(int32 CrowdGroupHandle, int32 SubgroupIndex) const;

	/** Enables or updates player engagement for an existing group. Ambient behavior remains unchanged until configured. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Engagement")
	bool ConfigureCrowdGroupEngagement(
		int32 CrowdGroupHandle,
		const FMassUnitPlayerEngagementConfig& Config,
		bool bEnableEngagement = true);

	/** Explicitly engages any enabled group, independent of its automatic activation mode. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Engagement")
	bool ActivateCrowdGroupForActor(int32 CrowdGroupHandle, AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Engagement")
	bool DeactivateCrowdGroupEngagement(int32 CrowdGroupHandle, bool bReturnToWander = true);

	/** Entry point for line traces, overlap interactions, clicks, or other project interaction systems. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Engagement")
	bool NotifyUnitInteracted(FMassUnitHandle UnitHandle, AActor* InteractingActor);

	/** Applies native Mass health damage and activates the damaged unit's group against the instigator. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Engagement")
	bool DamageUnitAndActivate(FMassUnitHandle UnitHandle, float Damage, AActor* DamageInstigator);

	/** Resolves a lightweight representation hit/location back to the nearest registered crowd entity. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Queries")
	FMassUnitHandle FindClosestCrowdUnit(
		FVector WorldLocation,
		float MaxDistance,
		bool bUse3DDistance = false,
		bool bIncludeDead = false) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Engagement")
	bool IsCrowdGroupEngaged(int32 CrowdGroupHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Engagement")
	AActor* GetCrowdGroupTargetActor(int32 CrowdGroupHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Diagnostics")
	FMassUnitCrowdStats GetCrowdStats() const { return LastStats; }

	/** Fired for non-combat ambient interactions; projects can attach audio, animation, or gameplay reactions. */
	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdInteractionSignature OnCrowdInteractionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdEngagementSignature OnCrowdEngagementStarted;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdEngagementSignature OnCrowdEngagementEnded;

	/** Always fires for a valid cooldown attack; actor damage and GAS effects are independently configurable. */
	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdAttackSignature OnCrowdAttackRequested;

	/** Rate-limited group/subgroup hook intended for pooled MetaSound, audio, Niagara, or decals. */
	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdCueSignature OnCrowdCueRequested;

private:
	struct FCrowdSubgroupState
	{
		FVector Destination = FVector::ZeroVector;
		TArray<FVector> SharedPathPoints;
		float NextPathRefreshTime = 0.0f;
		int32 DecisionSequence = 0;
		int32 Revision = 0;
		bool bUsesNavmesh = false;
	};

	struct FCrowdGroup
	{
		int32 Handle = INDEX_NONE;
		FVector Center = FVector::ZeroVector;
		FMassUnitCrowdConfig Config;
		TArray<FMassUnitEntityHandle> Units;
		int32 SubgroupCount = 1;
		TArray<float> NextSubgroupCueTimes;
		TArray<FCrowdSubgroupState> Subgroups;
		float NextGroupCueTime = 0.0f;
		float AcceptanceRadius = 50.0f;
		bool bUseNavigation = false;
		bool bPaused = false;
		bool bEngagementEnabled = false;
		bool bEngaged = false;
		bool bHoldAfterEngagement = false;
		FMassUnitPlayerEngagementConfig EngagementConfig;
		TWeakObjectPtr<AActor> TargetActor;
		FVector LastTargetLocation = FVector::ZeroVector;
		FVector SharedPathTargetLocation = FVector::ZeroVector;
		TArray<FVector> SharedPathPoints;
		bool bSharedPathUsesNavmesh = false;
		float NextTargetRefreshTime = 0.0f;
		float NextSharedPathUpdateTime = 0.0f;

		/**
		 * Units currently holding an attack token, mapped to the world time their token frees.
		 *
		 * Expiry-based rather than explicitly released, so a unit that dies, despawns, or leaves range
		 * mid-attack cannot leak a token and starve the pool. Pruned lazily on acquisition.
		 */
		TMap<FMassUnitEntityHandle, float> AttackTokenExpiry;
	};

	struct FSpatialEntry
	{
		FMassUnitEntityHandle Entity;
		FVector Location = FVector::ZeroVector;
		int32 GroupHandle = INDEX_NONE;
	};

	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitEntityManager> UnitManager = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitNavigationSystem> NavigationSystem = nullptr;

	TMap<int32, FCrowdGroup> Groups;
	TMap<FMassUnitEntityHandle, int32> UnitToGroup;
	FTimerHandle UpdateTimer;
	FMassUnitCrowdStats LastStats;
	int32 NextGroupHandle = 1;
	int32 UpdateCursor = 0;
	int32 MaxUnitsPerUpdate = 500;
	int32 MaxSharedPathBuildsPerUpdate = 8;
	float UpdateInterval = 0.1f;
	float SpatialCellSize = 200.0f;
	bool bUpdatingCrowds = false;
	TArray<float> SimulationLODDistances;
	TArray<float> SimulationLODIntervalMultipliers;

	/**
	 * Takes an attack token for the entity if the pool has room, or renews one it already holds.
	 * Returns false when the pool is full, which is the caller's signal to hold at standoff instead.
	 * A zero MaxSimultaneousAttackers always succeeds and skips the bookkeeping entirely.
	 */
	bool TryAcquireAttackToken(FCrowdGroup& Group, FMassUnitEntityHandle Entity, float CurrentTime, float HoldDuration);

	/** Drops expired tokens. Cheap enough to run per acquisition because the map is bounded by the pool size. */
	void PruneAttackTokens(FCrowdGroup& Group, float CurrentTime);

	void StartTimerIfNeeded();
	void StopTimerIfIdle();
	void UpdateCrowds();
	void PruneInvalidUnits();
	void BuildSpatialData(
		TArray<FSpatialEntry>& OutEntries,
		TMap<FIntVector, TArray<int32>>& OutCells,
		TMap<FMassUnitEntityHandle, int32>& OutEntryByEntity) const;
	void BuildObserverLocations(TArray<FVector>& OutObserverLocations) const;
	void RefreshPopulationStats(const TArray<FSpatialEntry>& Entries);
	void RefreshManagedSubgroupPaths(
		float CurrentTime,
		const TArray<FSpatialEntry>& Entries,
		int32 OnlyGroupHandle = INDEX_NONE,
		bool bForceRefresh = false);
	bool UpdateManagedSubgroupUnit(
		const FSpatialEntry& Entry,
		FCrowdGroup& Group,
		float CurrentTime,
		float LODIntervalMultiplier);
	void UpdateUnit(
		const FSpatialEntry& Entry,
		FCrowdGroup& Group,
		const TArray<FSpatialEntry>& Entries,
		const TMap<FIntVector, TArray<int32>>& Cells,
		const TMap<FMassUnitEntityHandle, int32>& EntryByEntity,
		const TArray<FVector>& ObserverLocations,
		float CurrentTime,
		bool bForceDecision);
	FVector CalculateSeparation(
		const FSpatialEntry& Entry,
		const FCrowdGroup& Group,
		const TArray<FSpatialEntry>& Entries,
		const TMap<FIntVector, TArray<int32>>& Cells);
	FMassUnitEntityHandle FindInteractionPartner(
		const FSpatialEntry& Entry,
		const FCrowdGroup& Group,
		const TArray<FSpatialEntry>& Entries,
		const TMap<FIntVector, TArray<int32>>& Cells,
		float CurrentTime);
	void BeginInteraction(
		FMassUnitEntityHandle UnitA,
		FMassUnitEntityHandle UnitB,
		FCrowdGroup& Group,
		float CurrentTime,
		FRandomStream& RandomStream);
	bool AssignRandomDestination(
		FMassUnitEntityHandle Entity,
		FCrowdGroup& Group,
		float CurrentTime,
		float LODIntervalMultiplier,
		FRandomStream& RandomStream);
	void ClearMovement(FMassUnitEntityHandle Entity, bool bSetIdle) const;
	void SetUnitSleeping(FMassUnitEntityHandle Entity, bool bSleeping, float CurrentTime) const;
	void ResetCrowdFragment(FMassUnitEntityHandle Entity, bool bStopUnit) const;
	void RemoveUnitFromPreviousGroup(FMassUnitEntityHandle Entity);
	void UpdateGroupEngagements(float CurrentTime);
	bool UpdateEngagedUnit(const FSpatialEntry& Entry, FCrowdGroup& Group, float CurrentTime, bool bForceDecision);
	bool RefreshSharedNavigationPath(FCrowdGroup& Group, float CurrentTime, bool bForceRefresh = false);
	FVector CalculateSharedPathDestination(const FSpatialEntry& Entry, const FCrowdGroup& Group) const;
	static FVector CalculatePathLookAheadDestination(
		const FVector& Location,
		const TArray<FVector>& PathPoints,
		float LookAheadDistance);
	FVector CalculateGroupAnchor(const FCrowdGroup& Group) const;
	void RequestActorAttack(FMassUnitEntityHandle Entity, FCrowdGroup& Group, AActor* TargetActor, float Damage);
	void RequestPresentationCue(
		FCrowdGroup& Group,
		EMassUnitCrowdCue Cue,
		const FVector& WorldLocation,
		int32 SubgroupIndex = INDEX_NONE);
	AActor* FindClosestPlayerTarget(const FVector& Origin, float MaxDistance) const;
	float CalculateClosestLivingUnitDistanceSquared(const FCrowdGroup& Group, const FVector& Location) const;
	FVector CalculateFollowOffset(FMassUnitEntityHandle Entity, const FCrowdGroup& Group) const;
	FVector CalculateSubgroupWanderCenter(FMassUnitEntityHandle Entity, const FCrowdGroup& Group) const;
	FVector CalculateSubgroupWanderCenter(int32 SubgroupIndex, const FCrowdGroup& Group) const;
	FRandomStream MakeRandomStream(FMassUnitEntityHandle Entity, const FCrowdGroup& Group, int32 DecisionSequence) const;
	int32 CalculateSimulationLOD(const FVector& Location, const TArray<FVector>& ObserverLocations, float& OutDistanceSquared) const;
	float GetSimulationIntervalMultiplier(int32 LODLevel) const;
	FIntVector ToSpatialCell(const FVector& Location, bool bUse3DMovement) const;
	static FMassUnitCrowdConfig SanitizeConfig(const FMassUnitCrowdConfig& Config);
	static FMassUnitPlayerEngagementConfig SanitizeEngagementConfig(const FMassUnitPlayerEngagementConfig& Config);
	bool IsEntityValid(FMassUnitEntityHandle Entity) const;
};
