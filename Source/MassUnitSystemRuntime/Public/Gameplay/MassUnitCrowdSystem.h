// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "MassUnitCrowdSystem.generated.h"

class UMassEntitySubsystem;
class UMassUnitNavigationSystem;
class UMassUnitBehaviorPreset;
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

/**
 * How a group's units move while it is NOT engaged on a target. Engagement (chasing and attacking a player) is the same
 * for every style. Wander is the original behaviour and the default.
 */
UENUM(BlueprintType)
enum class EMassUnitCrowdMotionStyle : uint8
{
	/** Each unit picks random destinations around the group centre (the original behaviour). */
	Wander,
	/** Boids: units steer by separation, alignment and cohesion with their neighbours and follow one roaming flock goal. */
	Flock,
	/** A tight buzzing cloud: units orbit a swarm centre that drifts around the group centre. */
	Swarm,
	/** A colony: units cluster at home (the group centre) and some forage out and come back. Pair with Player Proximity
	 *  engagement around the centre for a hive that defends itself. */
	Hive
};

/** Tuning for the non-Wander motion styles (FMassUnitCrowdConfig::MotionStyle). Only the selected style's values are used. */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitCrowdMotionConfig
{
	GENERATED_BODY()

	/** Seconds before the flock goal or swarm centre picks a new waypoint even if it has not reached the current one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.5", ForceUnits = "s"))
	float WaypointInterval = 6.0f;

	/** Flock: neighbours within this distance count for alignment and cohesion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Flock", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float FlockNeighborRadius = 450.0f;

	/** Flock: steer toward the average heading of neighbours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Flock", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float AlignmentWeight = 0.9f;

	/** Flock: steer toward the centre of neighbours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Flock", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float CohesionWeight = 0.7f;

	/** Flock: speed of the shared flock goal. Keep it at or below the units' move speed or the flock strings out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Flock", meta = (ClampMin = "1.0", ForceUnits = "cm/s"))
	float FlockGoalSpeed = 220.0f;

	/** Flock: the goal waits when it gets this far ahead of the flock's centre, so stragglers keep up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Flock", meta = (ClampMin = "50.0", ForceUnits = "cm"))
	float FlockGoalLeash = 600.0f;

	/** Swarm: radius of the cloud around the swarm centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Swarm", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float SwarmRadius = 300.0f;

	/** Swarm: how fast units circle the centre (half go each way). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Swarm", meta = (ClampMin = "0.0", UIMax = "360.0", ForceUnits = "deg/s"))
	float SwarmOrbitSpeed = 90.0f;

	/** Swarm: speed of the swarm centre as it drifts between waypoints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Swarm", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float SwarmDriftSpeed = 260.0f;

	/** Swarm: random offset per decision, as a fraction of Swarm Radius. Higher reads as more agitated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Swarm", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SwarmJitter = 0.3f;

	/** Swarm: speed multiplier on the units' template speed (swarms move urgently). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Swarm", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float SwarmSpeedMultiplier = 1.25f;

	/** Hive: units at home stay within this distance of the group centre. Foraging goes out to the group's Wander Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Hive", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float HiveRadius = 350.0f;

	/** Hive: chance that a unit at home leaves on a forage trip at each decision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Hive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ForageChance = 0.35f;

	/** Hive: seconds a forager lingers at its find before heading home. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion|Hive", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float ForageLingerTime = 2.0f;
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
	Attack,
	/** Collective behavior: the group was recruited by another group's alert. Appended - existing values keep their order. */
	Alerted,
	/** Collective behavior: the group's morale broke and its units are fleeing. */
	Routed,
	/** Collective behavior: a routed group recovered its nerve. */
	Rallied,
	/** Collective behavior: the group's threat table moved it onto a different target. */
	TargetChanged
};

/** Designer-facing controls for one lightweight crowd group. */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitCrowdConfig
{
	GENERATED_BODY()

	/** Planar mode is appropriate for ground crowds and navmesh. Free 3D supports flying, swimming, and 2.5D/3D groups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Movement")
	EMassUnitCrowdMovementMode MovementMode = EMassUnitCrowdMovementMode::Planar2D;

	/**
	 * How units move while the group is not engaged: Wander (random destinations, the default), Flock (boids), Swarm
	 * (orbiting cloud) or Hive (home cluster + foragers). Flock and Swarm steer straight at their goals without per-unit
	 * navmesh paths, so use them in open areas; Hive foraging uses navigation when the group does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Motion")
	EMassUnitCrowdMotionStyle MotionStyle = EMassUnitCrowdMotionStyle::Wander;

	/** Tuning for the selected Motion Style. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd|Motion", meta = (EditCondition = "MotionStyle != EMassUnitCrowdMotionStyle::Wander"))
	FMassUnitCrowdMotionConfig Motion;

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

	/**
	 * Extra attack tokens only units on a pile can take (climbers, and the ground units holding them up -
	 * FMassUnitCrowdFragment::bStackDriven). Without them the ground ring holds every token and a climber that reaches the
	 * target never swings. Counted on top of MaxSimultaneousAttackers; 0 = stacked units share the normal pool. Only
	 * matters for templates that stack (UUnitTemplate::Stacking), so it changes nothing for other groups.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engagement|Combat", meta = (EditCondition = "bEnableAttacks && MaxSimultaneousAttackers > 0", ClampMin = "0", UIMax = "24"))
	int32 ExtraAttackersWhileStacked = 8;

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

/**
 * Opt-in collective behavior: how crowd groups share information and nerve.
 *
 * The design borrows three ideas from social swarm simulation (influence-weighted spread of
 * information, contagious sentiment, staggered reaction delays) and rebuilds them as cheap
 * GROUP-level state on the existing crowd timer. Nothing here runs per unit per frame:
 *   - Alerts:  an engaging group emits a signal; nearby groups accumulate awareness and join after a
 *              reaction delay, optionally relaying a weaker signal onward (bounded by relay depth).
 *   - Morale:  casualties and shocks drain group morale; below a threshold the group routs (units flee
 *              with a staggered start), spreads fear to nearby allies, and rallies once it recovers.
 *   - Threat:  damage and proximity build per-actor threat so a group chooses between several players
 *              with hysteresis instead of locking onto whoever activated it.
 *
 * Requires player engagement to be configured on the group. Groups without this config are unchanged.
 */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitCrowdCollectiveConfig
{
	GENERATED_BODY()

	// ------------------------------------------------------------------ Activation

	/** Any damage reported through ReportCrowdUnitDamaged engages the group, whatever its activation mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Activation")
	bool bActivateOnDamage = true;

	// ------------------------------------------------------------------ Alerts (sending)

	/** Emit an alert when this group engages, so nearby groups can join the fight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts")
	bool bBroadcastAlerts = true;

	/** Reach of this group's alert. Strength falls off linearly to zero at this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bBroadcastAlerts", ClampMin = "1.0", ForceUnits = "cm"))
	float AlertRadius = 4000.0f;

	/** Influence of this group's alerts at the source. A receiver joins once accumulated awareness reaches its threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bBroadcastAlerts", ClampMin = "0.0", UIMax = "4.0"))
	float AlertStrength = 1.0f;

	/** Re-emit the alert on this interval while engaged so late or distant groups are drawn in. Zero emits once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bBroadcastAlerts", ClampMin = "0.0", ForceUnits = "s"))
	float AlertRebroadcastInterval = 3.0f;

	/** How many hops an alert may be relayed by groups it recruited. Zero means only direct alerts recruit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bBroadcastAlerts", ClampMin = "0", ClampMax = "8"))
	int32 MaxAlertRelayDepth = 2;

	/** Strength multiplier per relay hop. Below one, each hop is weaker, so propagation is guaranteed to die out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bBroadcastAlerts", ClampMin = "0.0", ClampMax = "1.0"))
	float AlertRelayFalloff = 0.6f;

	// ------------------------------------------------------------------ Alerts (receiving)

	/** Join other groups' fights when their alerts reach this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts")
	bool bReceiveAlerts = true;

	/** Multiplier on incoming alert strength. Higher values make a twitchier group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bReceiveAlerts", ClampMin = "0.0", UIMax = "4.0"))
	float AlertSensitivity = 1.0f;

	/** Accumulated awareness needed before this group joins. Weak alerts must stack (several groups, or rebroadcasts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bReceiveAlerts", ClampMin = "0.01"))
	float AlertThreshold = 0.5f;

	/** Awareness lost per second, so sub-threshold rumors fade instead of accumulating forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bReceiveAlerts", ClampMin = "0.0"))
	float AwarenessDecayPerSecond = 0.25f;

	/** Shortest delay between crossing the threshold and engaging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bReceiveAlerts", ClampMin = "0.0", ForceUnits = "s"))
	float MinAlertReactionDelay = 0.25f;

	/** Longest delay between crossing the threshold and engaging. A spread reads as a ripple, not a switch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts", meta = (EditCondition = "bReceiveAlerts", ClampMin = "0.0", ForceUnits = "s"))
	float MaxAlertReactionDelay = 1.5f;

	/** Alerts and fear only travel between groups whose units share a Team ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Alerts")
	bool bSignalsSameTeamOnly = true;

	// ------------------------------------------------------------------ Morale

	/** Track group morale; when it breaks, the group routs and later rallies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale")
	bool bEnableMorale = true;

	/** Morale lost per fraction of the group killed. 1.5 means losing a third of the group costs half its morale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.0", UIMax = "5.0"))
	float CasualtyMoraleWeight = 1.5f;

	/** Morale regained per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.0"))
	float MoraleRecoveryPerSecond = 0.05f;

	/** An engaged group routs when morale falls to this value. Zero means never rout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.0", ClampMax = "1.0"))
	float RoutThreshold = 0.35f;

	/** A routed group rallies once morale recovers to this value (and Min Rout Duration has passed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.0", ClampMax = "1.0"))
	float RallyThreshold = 0.7f;

	/** Minimum time a rout lasts, so a group cannot flicker between fleeing and fighting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.0", ForceUnits = "s"))
	float MinRoutDuration = 4.0f;

	/** Units start fleeing at a deterministic offset within this window, so a rout ripples instead of snapping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.0", ForceUnits = "s"))
	float RoutStaggerTime = 1.0f;

	/** How far a fleeing unit runs from the group's target before choosing again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "1.0", ForceUnits = "cm"))
	float FleeDistance = 1500.0f;

	/** Multiplies each unit's template speed while fleeing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.05", ClampMax = "4.0"))
	float FleeSpeedMultiplier = 1.3f;

	/** Seconds between a fleeing unit's destination choices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.1", ForceUnits = "s"))
	float FleeRepathInterval = 1.0f;

	/** Seconds between casualty counts. Counting is O(group size), so this bounds its cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.05", ForceUnits = "s", AdvancedDisplay))
	float MoraleEvaluationInterval = 0.25f;

	/** When this group routs, drain the morale of nearby groups that track morale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale"))
	bool bBroadcastFear = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale && bBroadcastFear", ClampMin = "1.0", ForceUnits = "cm"))
	float FearRadius = 2500.0f;

	/** Morale removed from a receiver at the source of a rout; falls off linearly with distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale && bBroadcastFear", ClampMin = "0.0", ClampMax = "1.0"))
	float FearStrength = 0.3f;

	/** Multiplier on fear this group RECEIVES. Zero makes it fearless (elite or mindless units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Morale", meta = (EditCondition = "bEnableMorale", ClampMin = "0.0", UIMax = "4.0"))
	float FearSusceptibility = 1.0f;

	// ------------------------------------------------------------------ Threat

	/** Keep a threat table and switch targets between actors (multiple players) with hysteresis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Threat")
	bool bEnableThreatTargeting = true;

	/** Threat added per point of damage reported against this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Threat", meta = (EditCondition = "bEnableThreatTargeting", ClampMin = "0.0"))
	float DamageThreatScale = 1.0f;

	/** Players within this distance of the group anchor build threat just by being close. Zero disables. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Threat", meta = (EditCondition = "bEnableThreatTargeting", ClampMin = "0.0", ForceUnits = "cm"))
	float ProximityThreatRadius = 1500.0f;

	/** Threat per second at zero distance; falls off linearly to the proximity radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Threat", meta = (EditCondition = "bEnableThreatTargeting", ClampMin = "0.0"))
	float ProximityThreatPerSecond = 10.0f;

	/** Seconds for an actor's threat to halve once it stops earning more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Threat", meta = (EditCondition = "bEnableThreatTargeting", ClampMin = "0.1", ForceUnits = "s"))
	float ThreatHalfLife = 6.0f;

	/** A challenger must exceed the current target's threat by this fraction to take over (0.3 = 30% more). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Threat", meta = (EditCondition = "bEnableThreatTargeting", ClampMin = "0.0", UIMax = "2.0"))
	float TargetSwitchMargin = 0.3f;

	/** Minimum time between target switches, so the group cannot ping-pong between players. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Threat", meta = (EditCondition = "bEnableThreatTargeting", ClampMin = "0.0", ForceUnits = "s"))
	float MinTargetSwitchInterval = 2.0f;

	// ------------------------------------------------------------------ Debug

	/** Master toggle for collective-behavior debug drawing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Debug")
	bool bEnableVisualDebug = false;

	/** Orange circle per alert, purple circle per fear pulse, drawn at emission. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Debug", meta = (EditCondition = "bEnableVisualDebug"))
	bool bDebugDrawSignals = true;

	/** Morale / awareness / state text above the group anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Debug", meta = (EditCondition = "bEnableVisualDebug"))
	bool bDebugDrawGroupStatus = true;

	/** Magenta line from the group anchor to its current target, with the target's threat value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Debug", meta = (EditCondition = "bEnableVisualDebug"))
	bool bDebugDrawThreat = true;

	/** Collective totals in the on-screen debug HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collective|Debug", meta = (EditCondition = "bEnableVisualDebug"))
	bool bDebugDrawScreenHUD = true;
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

	/** Alert and fear signals delivered to receiving groups by the most recent update. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Collective")
	int32 CollectiveSignalsProcessed = 0;

	/** Groups that engaged because of an alert during the most recent update. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Collective")
	int32 GroupsAlerted = 0;

	/** Groups currently routed (morale broken). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Collective")
	int32 RoutingGroups = 0;

	/** Threat-table target switches during the most recent update. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Collective")
	int32 TargetSwitches = 0;
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMassUnitCrowdAlertedSignature,
	int32, CrowdGroupHandle,
	AActor*, TargetActor,
	int32, SourceCrowdGroupHandle);

/**
 * Project hook deciding whether a crowd of CrowdTeamID may target Candidate. Lets the game apply its own
 * hostility rules (teams, factions, possession, PvP) without the plugin knowing them. Unset means every
 * player pawn is a valid target, the behaviour from before the hook existed.
 */
DECLARE_DELEGATE_RetVal_TwoParams(bool, FMassUnitCrowdTargetFilter, const AActor* /*Candidate*/, int32 /*CrowdTeamID*/);

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

	/**
	 * Adds units to an existing group without touching its current members (refills, reinforcements, churn). New units
	 * get the group's config, engagement and motion on their next decision; units already in another group move over.
	 * Returns how many joined.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd")
	int32 AddUnitsToCrowdGroup(int32 CrowdGroupHandle, const TArray<FMassUnitHandle>& UnitHandles);

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

	/**
	 * Changes a live group's crowd config (motion style, wander, avoidance, interactions, LOD) and navigation. The
	 * managed-subgroup layout (bEnableManagedSubgroups, ManagedSubgroupSize) is kept from registration. Units drop their
	 * ambient destinations and decide again under the new config; units under orders or on a pile are left alone.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd")
	bool SetCrowdGroupConfig(int32 CrowdGroupHandle, const FMassUnitCrowdConfig& Config, bool bUseNavigation, float AcceptanceRadius = 50.0f);

	/** Registers a group with a behaviour preset's crowd config, engagement and collective behaviour in one call. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Presets", meta = (ReturnDisplayName = "Crowd Group Handle"))
	int32 RegisterCrowdGroupFromPreset(const TArray<FMassUnitHandle>& UnitHandles, FVector Center, UMassUnitBehaviorPreset* Preset);

	/**
	 * Swaps a live group's whole temperament to Preset: crowd config (SetCrowdGroupConfig), engagement (an engaged group
	 * whose new preset has engagement off disengages) and collective behaviour. Same handle, same units.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Presets")
	bool ApplyCrowdGroupPreset(int32 CrowdGroupHandle, UMassUnitBehaviorPreset* Preset);

	/** The preset last registered or applied on the group (null when it was configured by hand). */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Presets")
	UMassUnitBehaviorPreset* GetCrowdGroupPreset(int32 CrowdGroupHandle) const;

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

	/**
	 * The point a Flock group follows or a Swarm group orbits (false for other styles, an unknown group, or before the
	 * first update). Engaged groups keep their last goal.
	 */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Motion")
	bool GetCrowdGroupMotionGoal(int32 CrowdGroupHandle, FVector& OutGoal) const;

	/** The group's motion style, or Wander for an unknown group. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Motion")
	EMassUnitCrowdMotionStyle GetCrowdGroupMotionStyle(int32 CrowdGroupHandle) const;

	// ====================================================================================
	// COLLECTIVE BEHAVIOR (opt-in) - alerts, morale/rout, threat targeting. See
	// FMassUnitCrowdCollectiveConfig. Server-authoritative; outcomes reach clients through
	// whatever already replicates unit transforms and states.
	// ====================================================================================

	/** Enables or updates collective behavior for a group. Requires engagement to be configured for alerts/threat to act. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Collective")
	bool ConfigureCrowdGroupCollectiveBehavior(
		int32 CrowdGroupHandle,
		const FMassUnitCrowdCollectiveConfig& Config,
		bool bEnableCollectiveBehavior = true);

	/**
	 * Single entry point for "this crowd unit was hurt by that actor". Safe to call for every hit.
	 * Projectiles and other causers resolve to their instigating pawn. Engages the unit's group like
	 * NotifyUnitInteracted (OnInteraction groups), or for any activation mode when the group's collective
	 * config sets bActivateOnDamage, and feeds the group's threat table. Does NOT apply damage.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Collective")
	bool ReportCrowdUnitDamaged(FMassUnitHandle UnitHandle, float Damage, AActor* DamageInstigator);

	/** Instantly removes morale from a group (leader killed, terrifying ability, big explosion). Negative values boost it. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Collective")
	bool ApplyCrowdGroupMoraleShock(int32 CrowdGroupHandle, float MoraleLoss);

	/**
	 * Raises an alert from the world rather than from a group (alarm, gunshot, explosion). Groups with
	 * collective behavior that receive alerts react exactly as they would to another group's alert.
	 * TeamID below zero reaches every team. Returns false when the alert could not be queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Crowd|Collective")
	bool RaiseCrowdAlertAtLocation(FVector WorldLocation, float Radius, float Strength, AActor* TargetActor, int32 TeamID = -1);

	/** Current morale from zero to one. Groups without morale tracking report one. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Collective")
	float GetCrowdGroupMorale(int32 CrowdGroupHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Collective")
	bool IsCrowdGroupRouting(int32 CrowdGroupHandle) const;

	/** Accumulated alert awareness; the group engages when this reaches its Alert Threshold. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Collective")
	float GetCrowdGroupAwareness(int32 CrowdGroupHandle) const;

	/**
	 * Where the group is, for projects relaying its state to other systems (its living-unit anchor, or its center
	 * when no unit is alive). Returns false for an unknown group.
	 */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Queries")
	bool GetCrowdGroupAnchor(int32 CrowdGroupHandle, FVector& OutLocation) const;

	/** The Team ID of the group's units (the same value its alerts carry), or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Queries")
	int32 GetCrowdGroupTeamID(int32 CrowdGroupHandle) const;

	/** The crowd group a unit belongs to, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Crowd|Queries")
	int32 GetUnitCrowdGroup(FMassUnitHandle UnitHandle) const;

	/**
	 * Installs the project's hostility rule. It is consulted for automatic target acquisition, proximity
	 * threat, and damage reports: a crowd never engages, or builds threat on, an actor the filter rejects
	 * (so friendly fire does not turn a friendly crowd). Explicit ActivateCrowdGroupForActor calls are
	 * not filtered, because a designer asking for a target gets it.
	 */
	void SetCrowdTargetFilter(FMassUnitCrowdTargetFilter InFilter) { TargetFilter = MoveTemp(InFilter); }
	void ClearCrowdTargetFilter() { TargetFilter.Unbind(); }

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

	/** A group engaged because an alert reached it. SourceCrowdGroupHandle is INDEX_NONE for world alerts. */
	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdAlertedSignature OnCrowdGroupAlerted;

	/** A group's morale broke. TargetActor is what it is fleeing from (may be null). */
	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdEngagementSignature OnCrowdGroupRouted;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdEngagementSignature OnCrowdGroupRallied;

	/** The threat table moved an engaged group onto a new target. */
	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Crowd|Events")
	FMassUnitCrowdEngagementSignature OnCrowdGroupTargetChanged;

private:
	enum class ECrowdSignalType : uint8
	{
		Alert,
		Fear
	};

	/** A group-to-group (or world-to-group) message, delivered on the NEXT crowd update so each hop takes a tick. */
	struct FCrowdSignal
	{
		ECrowdSignalType Type = ECrowdSignalType::Alert;
		int32 SourceGroup = INDEX_NONE;
		int32 TeamID = INDEX_NONE;
		FVector Location = FVector::ZeroVector;
		float Radius = 0.0f;
		float Strength = 0.0f;
		TWeakObjectPtr<AActor> Target;
		int32 RelayDepth = 0;
	};

	/** Hard cap on queued signals, so a misconfigured relay chain can never grow without bound. */
	static constexpr int32 MaxPendingSignals = 256;
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
		/** Preset last registered or applied (query only; its values are copied into the configs). */
		TWeakObjectPtr<UMassUnitBehaviorPreset> Preset;
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
		/** Bonus tokens held by stack-driven units (ExtraAttackersWhileStacked), same expiry rules. */
		TMap<FMassUnitEntityHandle, float> StackedAttackTokenExpiry;

		int32 GetActiveAttackerCount() const { return AttackTokenExpiry.Num() + StackedAttackTokenExpiry.Num(); }
		bool HoldsAttackToken(const FMassUnitEntityHandle& Entity) const
		{
			return AttackTokenExpiry.Contains(Entity) || StackedAttackTokenExpiry.Contains(Entity);
		}

		// -------- Collective behavior (inert unless bCollectiveEnabled) --------
		bool bCollectiveEnabled = false;
		FMassUnitCrowdCollectiveConfig CollectiveConfig;
		/** Team of the group's units, captured at configure time. INDEX_NONE when unknown (matches any team). */
		int32 TeamID = INDEX_NONE;
		float LastCollectiveUpdateTime = -1.0f;

		float Awareness = 0.0f;
		/** Time at which a recruited group engages; negative when no recruitment is pending. */
		float PendingAlertActivationTime = -1.0f;
		TWeakObjectPtr<AActor> PendingAlertTarget;
		int32 PendingAlertSourceGroup = INDEX_NONE;
		/** Relay depth a pending recruitment will engage with (incoming signal depth + 1). */
		int32 PendingRecruitRelayDepth = 0;
		/** Relay depth the NEXT activation inherits; set only by the recruitment path, consumed by every activation. */
		int32 PendingAlertRelayDepth = 0;
		/** Relay depth of the current engagement, reused by periodic rebroadcasts so they stay damped. */
		int32 EngagementRelayDepth = 0;
		float NextAlertBroadcastTime = 0.0f;
		int32 AlertSequence = 0;

		float Morale = 1.0f;
		bool bRouting = false;
		float RoutStartTime = 0.0f;
		/** Next O(group size) evaluation: living count for morale and the cached anchor for signals/threat. */
		float NextMoraleEvaluationTime = 0.0f;
		int32 InitialLivingCount = 0;
		int32 LastLivingCount = 0;
		/** Living-unit centroid from the last evaluation, so signals and threat avoid recomputing it. */
		FVector CachedAnchor = FVector::ZeroVector;
		bool bHasCachedAnchor = false;

		TMap<TWeakObjectPtr<AActor>, float> Threat;
		float LastTargetSwitchTime = -1000.0f;

		// -------- Motion styles (Flock / Swarm) --------
		FVector MotionGoal = FVector::ZeroVector;
		FVector MotionWaypoint = FVector::ZeroVector;
		float NextWaypointTime = 0.0f;
		float LastMotionTime = -1.0f;
		int32 WaypointSequence = 0;
		bool bMotionInitialized = false;
	};

	struct FSpatialEntry
	{
		FMassUnitEntityHandle Entity;
		FVector Location = FVector::ZeroVector;
		/** Current velocity (Flock alignment). */
		FVector Velocity = FVector::ZeroVector;
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

public:
	/**
	 * Registered crowd groups, live (profiling counter; see Core/MassUnitStats.h). Use this for leak checks:
	 * GetCrowdStats().RegisteredGroups is a snapshot from the last crowd update and keeps its old value after the
	 * last group is removed, because the update timer stops when idle.
	 */
	int32 GetCrowdGroupCount() const { return Groups.Num(); }

	/** Every registered group handle (C++ services that walk groups, e.g. the stack system's pile planner). */
	void GetCrowdGroupHandles(TArray<int32>& OutHandles) const { Groups.GetKeys(OutHandles); }

	/** The group's unit list, or null for an unknown group. Includes dead units until they are pruned. */
	const TArray<FMassUnitEntityHandle>* GetCrowdGroupUnitsInternal(int32 CrowdGroupHandle) const
	{
		const FCrowdGroup* Group = Groups.Find(CrowdGroupHandle);
		return Group ? &Group->Units : nullptr;
	}

private:
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
	TArray<FCrowdSignal> PendingSignals;

	// -------- Collective behavior --------
	/** Once per update, before engagement: decay, morale, threat, signal delivery, recruitment, rebroadcast. */
	void UpdateCollectiveBehavior(float CurrentTime);
	void DeliverCrowdSignals(float CurrentTime);
	void UpdateGroupMorale(FCrowdGroup& Group, float CurrentTime, float DeltaTime);
	void UpdateGroupThreat(FCrowdGroup& Group, float CurrentTime, float DeltaTime);
	void EmitCrowdSignal(FCrowdGroup& Group, ECrowdSignalType Type, float Radius, float Strength, AActor* Target, int32 RelayDepth);
	void QueueCrowdSignal(const FCrowdSignal& Signal);
	void BeginRout(FCrowdGroup& Group, float CurrentTime);
	void EndRout(FCrowdGroup& Group, float CurrentTime);
	bool RetargetCrowdGroup(FCrowdGroup& Group, AActor* NewTarget, float CurrentTime);
	bool UpdateRoutingUnit(const FSpatialEntry& Entry, FCrowdGroup& Group, float CurrentTime, bool bForceDecision);
	bool HasRoutReachedUnit(FMassUnitEntityHandle Entity, const FCrowdGroup& Group, float CurrentTime) const;
	int32 CountLivingUnits(const FCrowdGroup& Group, FVector* OutAnchor = nullptr) const;
	FVector GetCollectiveAnchor(const FCrowdGroup& Group) const;
	int32 ResolveGroupTeamID(const FCrowdGroup& Group) const;
	static AActor* ResolveThreatActor(AActor* Actor);
	static bool DoesSignalReachTeam(const FCrowdSignal& Signal, const FCrowdGroup& Receiver);
	void DrawCollectiveDebug(float CurrentTime);
	static FMassUnitCrowdCollectiveConfig SanitizeCollectiveConfig(const FMassUnitCrowdCollectiveConfig& Config);

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
	/** True when UMassUnitCommandSystem owns the unit (FMassUnitCrowdFragment::bCommanded). */
	bool IsUnitCommanded(FMassUnitEntityHandle Entity) const;
	void RemoveUnitFromPreviousGroup(FMassUnitEntityHandle Entity);
	void UpdateGroupEngagements(float CurrentTime);

	// -------- Motion styles --------
	/** Once per update: moves each idle Flock / Swarm group's goal toward its waypoint and picks new waypoints. */
	void UpdateGroupMotion(float CurrentTime);
	/** Ambient decision for a unit of a non-Wander group. Separation is already in the unit's steering. */
	void UpdateStyledUnit(
		const FSpatialEntry& Entry,
		FCrowdGroup& Group,
		const TArray<FSpatialEntry>& Entries,
		const TMap<FIntVector, TArray<int32>>& Cells,
		float CurrentTime,
		float LODIntervalMultiplier,
		bool bForceDecision);
	/** Flock alignment + cohesion (weighted) from same-group neighbours in the spatial hash. */
	FVector CalculateFlocking(
		const FSpatialEntry& Entry,
		const FCrowdGroup& Group,
		const TArray<FSpatialEntry>& Entries,
		const TMap<FIntVector, TArray<int32>>& Cells);
	/** Direct (or, when allowed and the group navigates, navmesh) destination for a styled unit. */
	bool AssignMotionDestination(FMassUnitEntityHandle Entity, FCrowdGroup& Group, const FVector& Destination, bool bAllowNavmesh);
	FVector PickMotionWaypoint(FCrowdGroup& Group) const;
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
	AActor* FindClosestPlayerTarget(const FVector& Origin, float MaxDistance, int32 CrowdTeamID = INDEX_NONE) const;

	/** True when no filter is installed or the filter accepts Candidate for this team. */
	bool PassesTargetFilter(const AActor* Candidate, int32 CrowdTeamID) const;

	FMassUnitCrowdTargetFilter TargetFilter;
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
