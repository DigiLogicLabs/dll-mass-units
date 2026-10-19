// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Entity/MassUnitEntityManager.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "MassUnitSwarmType.generated.h"

class UUnitTemplate;
class UMassUnitLookProfile;
class UMassUnitAttitudeProfile;
class UMassUnitBehaviorPreset;

/** How a swarm type's units occupy space. Appended to, never reordered (assets store the value). */
UENUM(BlueprintType)
enum class EMassUnitLocomotion : uint8
{
	/** Walks: the behaviour preset's crowd config as is (planar, navmesh when the preset navigates). */
	Ground,
	/** Flies: Free 3D movement inside an altitude band over the ground, hovers over a target and dives at it. */
	Air
};

/** What SpawnSwarm made. */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitSwarmSpawnResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Swarm")
	TArray<FMassUnitHandle> Units;

	/** The crowd group the units were registered as (INDEX_NONE when nothing spawned). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Swarm")
	int32 CrowdGroupHandle = INDEX_NONE;
};

/**
 * A kind of swarm as one asset: which minion (template), how each one looks and feels (look and attitude profiles),
 * how the group behaves (behaviour preset) and how it gets around (locomotion). Ankle biters, bats, drones and wasps are
 * swarm types over the same layers; SpawnSwarm makes "a swarm of X" in one call. Nothing here is new per-unit state:
 * the asset only chooses existing layers (see docs/unit_composition.md section 4).
 */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UMassUnitSwarmType : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Name for tools and debug text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm")
	FText DisplayName;

	/** The minion: stats, mesh, clips, abilities. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm")
	TObjectPtr<UUnitTemplate> Template = nullptr;

	/** Dress every unit in this look instead of the template's (optional). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Variety")
	TObjectPtr<UMassUnitLookProfile> LookProfile = nullptr;

	/** Give every unit this temperament instead of the template's (optional). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Variety")
	TObjectPtr<UMassUnitAttitudeProfile> AttitudeProfile = nullptr;

	/** Roll each unit's variant seed from the spawn seed, so a swarm looks the same on every run with the same seed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Variety")
	bool bSeedVariants = true;

	/** The group's temperament: motion style, engagement, collective behaviour. Null = default crowd config, no engagement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Behavior")
	TObjectPtr<UMassUnitBehaviorPreset> BehaviorPreset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Locomotion")
	EMassUnitLocomotion Locomotion = EMassUnitLocomotion::Ground;

	/** Lowest ambient flying height over the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Locomotion", meta = (EditCondition = "Locomotion == EMassUnitLocomotion::Air", EditConditionHides, ClampMin = "0.0", ForceUnits = "cm"))
	float MinAltitude = 150.0f;

	/** Highest ambient flying height over the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Locomotion", meta = (EditCondition = "Locomotion == EMassUnitLocomotion::Air", EditConditionHides, ClampMin = "0.0", ForceUnits = "cm"))
	float MaxAltitude = 600.0f;

	/** Height over the target at which flyers circle while they wait for an attack token. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Locomotion", meta = (EditCondition = "Locomotion == EMassUnitLocomotion::Air", EditConditionHides, ClampMin = "0.0", ForceUnits = "cm"))
	float EngagedHoverHeight = 250.0f;

	/** Killed flyers fall to the ground as corpses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swarm|Locomotion", meta = (EditCondition = "Locomotion == EMassUnitLocomotion::Air", EditConditionHides))
	bool bFallWhenKilled = true;

	/** The crowd config a group of this type gets: the preset's (or the default), made to fly for Air. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Swarm")
	FMassUnitCrowdConfig MakeCrowdConfig() const;

	/**
	 * Spawns Count units of SwarmType scattered over a disc of Radius around Center and registers them as one crowd group
	 * (preset engagement and collective behaviour included). Air types start inside their altitude band over Center.Z.
	 * RandomSeed drives positions and, with Seed Variants, looks and attitudes. TeamOverride >= 0 puts the swarm on that
	 * team. Units stop at the unit cap (fewer than Count come back).
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Swarm", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "RandomSeed,TeamOverride"))
	static FMassUnitSwarmSpawnResult SpawnSwarm(const UObject* WorldContextObject, UMassUnitSwarmType* SwarmType, FVector Center,
		int32 Count, float Radius = 600.0f, int32 RandomSeed = 0, int32 TeamOverride = -1);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
