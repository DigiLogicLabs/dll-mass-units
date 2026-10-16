// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "GameFramework/Actor.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "MassUnitSpawner.generated.h"

class UArrowComponent;
class USceneComponent;
class UUnitTemplate;
class AActor;

/** Layout of a spawner's units around the actor. Every pattern is deterministic for the same settings and seed. */
UENUM(BlueprintType)
enum class EMassUnitSpawnPattern : uint8
{
	/** Rows of Columns, UnitSpacing apart (the original layout). */
	Grid,
	/** Filled disc on a sunflower spiral; one unit per UnitSpacing square, no radius needed. */
	Disc,
	/** Evenly around a circle of PatternRadius (0 = just wide enough for UnitSpacing between units). */
	Ring,
	/** Centred line along the actor's right axis, UnitSpacing.Y apart. */
	Line,
	/** Uniform random in a disc of PatternRadius (0 = the Disc pattern's radius), from SpawnOptions.RandomSeed. */
	Scatter
};

/**
 * Placeable, actor-light bootstrap for repeatable Mass Unit spawning.
 *
 * The actor owns only stable Mass handles. The units themselves remain native
 * Mass entities, and the actor performs no per-frame work.
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "Mass Unit Spawner"))
class MASSUNITSYSTEMRUNTIME_API AMassUnitSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMassUnitSpawner();

	/** Optional data-driven template. When empty, the plugin's visible asset-free default is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Units")
	TObjectPtr<UUnitTemplate> UnitTemplate = nullptr;

	/** Number of native Mass entities created by Spawn Units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Units", meta = (ClampMin = "1", UIMin = "1", UIMax = "1000"))
	int32 UnitCount = 25;

	/** Maximum units per grid row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Layout", meta = (ClampMin = "1", UIMin = "1"))
	int32 Columns = 5;

	/** Actor-local distance between grid rows (X) and columns (Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Layout", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	FVector2D UnitSpacing = FVector2D(150.0f, 150.0f);

	/** Actor-local height added to every spawn point. Fifty centimeters centers the default engine cube on a floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Layout", meta = (ForceUnits = "cm"))
	float SpawnHeight = 50.0f;

	/** How units are laid out around the actor. Grid keeps the original Columns / UnitSpacing layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Layout")
	EMassUnitSpawnPattern SpawnPattern = EMassUnitSpawnPattern::Grid;

	/** Ring and Scatter radius. Zero sizes the pattern from UnitSpacing instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Layout", meta = (ClampMin = "0.0", ForceUnits = "cm",
		EditCondition = "SpawnPattern == EMassUnitSpawnPattern::Ring || SpawnPattern == EMassUnitSpawnPattern::Scatter"))
	float PatternRadius = 0.0f;

	/** Team override and the random seed (Scatter positions, optional variant seeds). Defaults change nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Units")
	FMassUnitBatchSpawnOptions SpawnOptions;

	/** Create the grid automatically when gameplay begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Lifecycle")
	bool bSpawnOnBeginPlay = true;

	/** Command every spawned unit immediately after the automatic spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Movement", meta = (EditCondition = "bSpawnOnBeginPlay"))
	bool bMoveOnBeginPlay = true;

	/** Actor-local offset applied to each unit's own spawn point, preserving the grid while it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Movement", meta = (EditCondition = "bMoveOnBeginPlay", ForceUnits = "cm"))
	FVector DestinationOffset = FVector(1500.0f, 0.0f, 0.0f);

	/** Request navmesh paths instead of writing direct destinations. Missing nav data still uses the project fallback setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Movement")
	bool bUseNavigation = false;

	/** Distance from a destination at which movement stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Movement", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float AcceptanceRadius = 50.0f;

	/** Start continuous, budgeted crowd behavior after spawning. This takes precedence over Move On Begin Play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Crowd")
	bool bEnableCrowdSimulation = false;

	/** Wandering, separation, interaction, randomness, behavior LOD, and debug settings for this spawner's crowd. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Crowd", meta = (EditCondition = "bEnableCrowdSimulation", ShowOnlyInnerProperties))
	FMassUnitCrowdConfig CrowdConfig;

	/** Adds opt-in target acquisition, following, and attacks to this crowd. Ambient-only behavior remains the default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Engagement", meta = (EditCondition = "bEnableCrowdSimulation"))
	bool bEnablePlayerEngagement = false;

	/** Activation, follow/repath, native damage, and optional GAS-effect controls for the whole group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Engagement", meta = (EditCondition = "bEnableCrowdSimulation && bEnablePlayerEngagement", ShowOnlyInnerProperties))
	FMassUnitPlayerEngagementConfig PlayerEngagementConfig;

	/**
	 * Opt-in group intelligence: alerts that recruit nearby crowds, morale that routs and rallies the group,
	 * and a threat table for choosing between several players. Requires player engagement. Off by default,
	 * so existing spawners behave exactly as before.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Collective", meta = (EditCondition = "bEnableCrowdSimulation && bEnablePlayerEngagement"))
	bool bEnableCollectiveBehavior = false;

	/** Alert, morale, threat, and debug settings for this spawner's crowd. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Collective", meta = (EditCondition = "bEnableCrowdSimulation && bEnablePlayerEngagement && bEnableCollectiveBehavior", ShowOnlyInnerProperties))
	FMassUnitCrowdCollectiveConfig CollectiveConfig;

	/** Prevent duplicate autonomous simulation on network clients. Standalone and listen-server quick starts still spawn normally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Networking")
	bool bSpawnOnAuthorityOnly = true;

	/** Destroy only the Mass entities created by this actor when the actor or world ends play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Lifecycle")
	bool bDestroySpawnedUnitsOnEndPlay = true;

	/** Draw spawn boxes and command lines when Spawn Units or a command function is called. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Debug")
	bool bEnableVisualDebug = false;

	/** Lifetime of one-shot debug geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit Spawner|Debug", meta = (EditCondition = "bEnableVisualDebug", ClampMin = "0.0", ForceUnits = "s"))
	float DebugDuration = 10.0f;

	/** Handles created and owned by this spawner. Invalid/destroyed handles are filtered by the getter functions. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Mass Unit Spawner|Runtime")
	TArray<FMassUnitHandle> SpawnedUnits;

	/** Appends one configured grid of units and returns the successfully created handles. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner", meta = (ReturnDisplayName = "New Units"))
	TArray<FMassUnitHandle> SpawnUnits();

	/** Moves valid owned units by the same actor-local offset, preserving their current spacing. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner", meta = (ReturnDisplayName = "Commanded Unit Count"))
	int32 MoveSpawnedUnitsByOffset(FVector LocalOffset);

	/** Moves valid owned units around a world-space center, optionally preserving their current spacing. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner", meta = (ReturnDisplayName = "Commanded Unit Count"))
	int32 CommandSpawnedUnitsToLocation(FVector DestinationCenter, bool bPreserveSpacing = true);

	/** Destroys valid Mass entities created by this actor and clears its handles. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner")
	void DestroySpawnedUnits();

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner")
	TArray<FMassUnitHandle> GetValidSpawnedUnits() const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner")
	int32 GetValidSpawnedUnitCount() const;

	/** Registers currently valid owned units with the world crowd service. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner|Crowd", meta = (ReturnDisplayName = "Crowd Group Handle"))
	int32 StartCrowdSimulation();

	/** Unregisters this spawner's crowd group. The Mass entities remain owned by the spawner. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner|Crowd")
	void StopCrowdSimulation(bool bStopUnits = true);

	/** Immediately refreshes this spawner's crowd decisions. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner|Crowd")
	bool ForceCrowdUpdate();

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner|Crowd")
	bool IsCrowdSimulationActive() const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner|Crowd")
	int32 GetCrowdGroupHandle() const { return CrowdGroupHandle; }

	/** Explicitly activates this spawner's engagement-enabled crowd against an Actor. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner|Engagement")
	bool ActivateCrowdAgainstActor(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner|Engagement")
	bool DeactivateCrowdEngagement(bool bReturnToWander = true);

	/** Call from the project's interaction trace/click/overlap after resolving a spawned unit. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner|Engagement")
	bool NotifySpawnedUnitInteracted(FMassUnitHandle UnitHandle, AActor* InteractingActor);

	/** Convenience path for a player hit: native Mass damage plus group activation. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Spawner|Engagement")
	bool DamageSpawnedUnitAndActivate(FMassUnitHandle UnitHandle, float Damage, AActor* DamageInstigator);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner|Engagement")
	FMassUnitHandle FindClosestSpawnedUnit(FVector WorldLocation, float MaxDistance, bool bUse3DDistance = false) const;

	/** Actor-local spawn points for TotalUnits under the current pattern, spacing, radius and seed (SpawnHeight included). */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner")
	TArray<FVector> CalculateLocalSpawnOffsets(int32 TotalUnits) const;

	/** Outer radius of the Disc / Ring / Scatter layouts for TotalUnits; zero for Grid and Line. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner")
	float GetPatternRadius(int32 TotalUnits) const;

	/**
	 * Same layouts without a spawner actor (stress director, tools). Local offsets around an origin facing +X;
	 * Columns is used by Grid only, PatternRadius by Ring/Scatter only, RandomSeed by Scatter only.
	 */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner")
	static TArray<FVector> ComputePatternOffsets(
		EMassUnitSpawnPattern Pattern,
		int32 TotalUnits,
		int32 InColumns,
		FVector2D InUnitSpacing,
		float InPatternRadius,
		int32 RandomSeed,
		float Height);

	/** Outer radius ComputePatternOffsets uses for Disc / Ring / Scatter; zero for Grid and Line. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Spawner")
	static float ComputePatternRadius(
		EMassUnitSpawnPattern Pattern,
		int32 TotalUnits,
		FVector2D InUnitSpacing,
		float InPatternRadius);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Mass Unit Spawner", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	/** Shows the default local movement direction in the editor viewport. */
	UPROPERTY(VisibleAnywhere, Category = "Mass Unit Spawner", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UArrowComponent> DirectionArrow = nullptr;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Mass Unit Spawner|Runtime")
	int32 CrowdGroupHandle = INDEX_NONE;

	bool CommandUnit(FMassUnitHandle UnitHandle, const FVector& Destination) const;
	void DrawSpawnDebug(const FVector& Location) const;
	void DrawPatternDebug(int32 TotalUnits) const;
	void DrawCommandDebug(const FVector& Start, const FVector& Destination) const;
};
