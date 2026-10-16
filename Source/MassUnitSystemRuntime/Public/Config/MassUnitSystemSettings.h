// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MassUnitSystemSettings.generated.h"

class UNiagaraSystem;
class UStaticMesh;
class UUnitConfigDataAsset;

/** Project-wide defaults for the Mass Unit System. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Mass Unit System"))
class MASSUNITSYSTEMRUNTIME_API UMassUnitSystemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMassUnitSystemSettings();

	/** Hard cap enforced by the Blueprint-facing unit manager. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxUnits = 10000;

	/** Maximum number of units represented by individual skeletal mesh components. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxSkeletalMeshUnits = 100;

	/** Maximum queued navigation requests submitted per world tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxPathRequestsPerFrame = 100;

	/** Maximum new shared crowd/subgroup navmesh corridors built during one crowd update. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxSharedPathBuildsPerCrowdUpdate = 8;

	/** Minimum interval between GPU/instanced representation uploads. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float VisualUpdateInterval = 0.033f;

	/** Base interval for budgeted crowd decisions and spatial-hash steering. Smooth Mass movement still runs independently. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "0.02", ForceUnits = "s"))
	float CrowdUpdateInterval = 0.1f;

	/** Maximum crowd units that can receive a decision/steering update during one crowd timer callback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxCrowdUnitsPerUpdate = 500;

	/** Two-dimensional spatial-hash cell size used for separation and interaction neighbor searches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Performance", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float CrowdSpatialCellSize = 200.0f;

	/** Distances, in centimeters, at which a unit advances to the next visual LOD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "LOD", meta = (ForceUnits = "cm"))
	TArray<float> LODDistanceThresholds;

	/** Per-LOD intervals for recomputing distance visibility. The final entry is used beyond the last visual threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "LOD", meta = (ForceUnits = "s"))
	TArray<float> VisibilityLODUpdateIntervals;

	/** Observer distances used by behavior LOD. Decisions become less frequent after each threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Crowd LOD", meta = (ForceUnits = "cm"))
	TArray<float> CrowdSimulationLODDistances;

	/** Multipliers applied to the base crowd interval for successive behavior LODs. Values below one are clamped to one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Crowd LOD")
	TArray<float> CrowdSimulationLODIntervalMultipliers;

	/** Units with a skeletal mesh use it inside this distance when pool capacity permits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "LOD", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float SkeletalMeshDistance = 300.0f;

	/** Exit-distance multiplier that prevents skeletal/instanced representation thrashing near the boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "LOD", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float SkeletalMeshHysteresis = 1.2f;

	/** Units beyond this distance are excluded from visual representation. Zero disables distance culling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "LOD", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MaxVisibleDistance = 10000.0f;

	/** Optional Niagara system that consumes the documented Unit* array parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Rendering")
	TSoftObjectPtr<UNiagaraSystem> DefaultNiagaraSystem;

	/** Use instanced static meshes when no Niagara system is configured or can be loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Rendering")
	bool bEnableInstancedMeshFallback = true;

	/** Optional final fallback mesh. If unset, the engine cube is used so new installs remain visible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Rendering")
	TSoftObjectPtr<UStaticMesh> FallbackStaticMesh;

	/**
	 * Distance at which instanced units begin fading out. Zero starts the fade at the end distance,
	 * producing a hard pop. Only takes effect when InstanceEndCullDistance is above zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Rendering|Instancing", meta = (ClampMin = "0", ForceUnits = "cm"))
	int32 InstanceStartCullDistance = 0;

	/**
	 * Distance at which instanced units stop drawing entirely, culled per instance on the GPU.
	 * Zero disables per-instance cull distance and leaves MaxVisibleDistance as the only limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Rendering|Instancing", meta = (ClampMin = "0", ForceUnits = "cm"))
	int32 InstanceEndCullDistance = 0;

	/**
	 * Whether instanced crowd units cast dynamic shadows.
	 * A crowd is the worst case for shadow cost - thousands of small movable casters re-rendered per
	 * cascade - so this defaults to off. Turn it on only for small, close-range unit counts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Rendering|Instancing")
	bool bCrowdInstancesCastShadows = false;

	/**
	 * Distance past which world position offset stops being evaluated on crowd instances.
	 * The shipped SwarmBot material uses no WPO, so this costs nothing today. It exists for unit
	 * materials that add camera-facing or wind-style vertex motion: past this range a unit covers a few
	 * pixels and the per-vertex evaluation is pure cost. Zero never disables WPO.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Rendering|Instancing", meta = (ClampMin = "0", ForceUnits = "cm"))
	int32 WorldPositionOffsetDisableDistance = 5000;

	/** If no nav data exists, use a direct two-point path instead of rejecting movement requests. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Navigation")
	bool bFallbackToDirectPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Debug")
	bool bEnableDebugVisualization = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Debug", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DebugVisualizationDuration = 0.0f;

	// ------------------------------------------------------------------------------------------------
	// LAUNCH (knockback, kill flings, near ragdolls) - UMassUnitEntityManager::LaunchUnit
	// ------------------------------------------------------------------------------------------------

	/** Master switch for LaunchUnit. Off = launches are ignored and units die in place, as before. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch")
	bool bEnableUnitLaunch = true;

	/** Gravity for airborne units (cm/s^2). Twice real gravity reads as a snappy action-game arc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "100.0"))
	float LaunchGravity = 1960.0f;

	/** Launch speed cap (cm/s), applied after the template's LaunchVelocityScale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "0.0"))
	float MaxLaunchSpeed = 3000.0f;

	/**
	 * Most units airborne at once. A launch past the cap is refused (the unit is damaged or dies in place), so a
	 * screen-clearing blast costs a bounded amount. Each airborne unit is one entry in a sparse table.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "0"))
	int32 MaxAirborneUnits = 512;

	/** Seconds a LIVING unit stays down after it lands before it can move or attack again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float KnockdownSeconds = 0.8f;

	/** Tumble rate of a launched CORPSE (degrees per second, randomised per unit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "0.0"))
	float CorpseTumbleDegreesPerSecond = 540.0f;

	/** Vertical speed kept by a corpse's single bounce (0 = no bounce). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CorpseBounceRestitution = 0.3f;

	/**
	 * One line trace per launch, at the predicted landing point, so a unit knocked off a ledge lands on the ground
	 * below instead of at its launch height. Off = land at launch height (no traces at all).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch")
	bool bTraceLandingGround = true;

	/** Landing traces allowed per frame; launches past it land at their launch height (fine on flat ground). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "0", EditCondition = "bTraceLandingGround"))
	int32 MaxLandingTracesPerFrame = 48;

	/**
	 * Kills launched while their unit is in the skeletal (near) band become real physics ragdolls, at most this many
	 * at once. Needs a Physics Asset on the template's skeletal mesh. 0 = never; everything else uses the cheap
	 * tumbling fling, which is what most of a wave gets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Launch", meta = (ClampMin = "0"))
	int32 MaxActiveRagdolls = 12;

	/** Optional project catalog. These assets are not spawned automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Units")
	TArray<TSoftObjectPtr<UUnitConfigDataAsset>> UnitConfigurations;

	/** Optional default catalog entry for project-owned selection workflows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Units")
	TSoftObjectPtr<UUnitConfigDataAsset> DefaultUnitConfiguration;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System", meta = (DisplayName = "Get Mass Unit System Settings", Keywords = "Mass Entity performance optimization LOD"))
	static UMassUnitSystemSettings* Get();
};
