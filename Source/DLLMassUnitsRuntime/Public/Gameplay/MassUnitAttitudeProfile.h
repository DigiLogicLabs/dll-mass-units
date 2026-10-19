// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MassUnitAttitudeProfile.generated.h"

/** The result of rolling an attitude profile for one unit. */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitRolledAttitude
{
	GENERATED_BODY()

	/** Multiplier on the template's move speed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude")
	float MoveSpeedMultiplier = 1.0f;

	/** Multiplier on the template's attack cooldown (below 1 = attacks more often). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude")
	float AttackCooldownMultiplier = 1.0f;

	/** Playback rate of looping vertex-animation clips (idle, walk, run). One-shot clips keep their authored timing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude")
	float AnimationRate = 1.0f;

	/** 0 = first to flee when its group routs, 1 = holds longest. Negative = no courage roll (random rout order). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude")
	float Courage = -1.0f;

	/** Whether the unit is willing to climb piles (it can still be climbed on). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attitude")
	bool bWillClimb = true;
};

/**
 * How a unit behaves as an individual, separate from what it is (template) and how it looks (look profile): small
 * rolled differences in pace, attack rhythm, animation tempo, nerve and willingness to climb, so a crowd of one template
 * reads as individuals and one minion can be reused with a different temperament (a frantic swarm, a sluggish horde, a
 * disciplined squad). Group-wide behaviour (motion style, engagement, morale) stays on the crowd group.
 *
 * Assign per template (UUnitTemplate::AttitudeProfile), per batch (FMassUnitBatchSpawnOptions::AttitudeProfile) or per unit
 * (UMassUnitEntityManager::SetUnitAttitudeProfile). Rolled from the unit's variant seed at byte precision with its own
 * salt (independent of the look roll), so the visible part (animation tempo) matches on clients from the seed alone.
 */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UMassUnitAttitudeProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Move speed multiplier range (min, max) on the template's MoveSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attitude", meta = (ClampMin = "0.05"))
	FVector2D MoveSpeedRange = FVector2D(1.0, 1.0);

	/** Attack cooldown multiplier range (min, max). Never shorter than the template's AttackImpactDelay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attitude", meta = (ClampMin = "0.05"))
	FVector2D AttackCooldownRange = FVector2D(1.0, 1.0);

	/** Looping clip playback rate range (min, max): scurrying vs lumbering at the same speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attitude", meta = (ClampMin = "0.05"))
	FVector2D AnimationRateRange = FVector2D(1.0, 1.0);

	/**
	 * One shared roll drives every range above: a unit at the top of one range is at the top of all of them (fast pace,
	 * quick attacks, quick tempo) - coherent temperaments instead of a fast unit with a lazy walk cycle. Note the cooldown
	 * range then maps its MIN to the energetic end.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attitude")
	bool bLinkRolls = true;

	/** Roll courage: the order units flee in when their group routs (cowards first, brave ones hold). Off = random order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attitude|Courage")
	bool bRollCourage = false;

	/** Courage range (0-1). 0 flees as soon as the rout starts, 1 at the end of the group's RoutStaggerTime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attitude|Courage", meta = (EditCondition = "bRollCourage", ClampMin = "0.0", ClampMax = "1.0"))
	FVector2D CourageRange = FVector2D(0.0, 1.0);

	/** Chance a unit is willing to climb piles (templates that can stack). The rest stay on the ground and hold others up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attitude|Piles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClimbChance = 1.0f;

	/** Rolls the attitude for a 0-1 variant seed. Same seed byte, same attitude, on every machine. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Attitude")
	FMassUnitRolledAttitude RollAttitude(float VariantSeed) const;
};
