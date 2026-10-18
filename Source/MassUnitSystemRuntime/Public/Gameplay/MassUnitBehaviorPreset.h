// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "MassUnitBehaviorPreset.generated.h"

/**
 * A crowd group's whole temperament as one shareable asset: how it moves (motion style, wander, avoidance), how it
 * fights (engagement) and how it reacts as a group (alerts, morale, threat). Swap a preset on a live group to turn the
 * same minions from an ambient hive into a hunting swarm or a routing mob; reuse one preset across any template.
 * Individual variety within the group comes from each unit's UMassUnitAttitudeProfile.
 *
 * UMassUnitCrowdSystem::RegisterCrowdGroupFromPreset / ApplyCrowdGroupPreset.
 */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UMassUnitBehaviorPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Movement, motion style, avoidance, interactions, LOD. Managed-subgroup layout applies only when a group is registered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior|Crowd")
	FMassUnitCrowdConfig Crowd;

	/** Path ambient and engaged movement on the navmesh (planar groups). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior|Crowd")
	bool bUseNavigation = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior|Crowd", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float AcceptanceRadius = 50.0f;

	/** Engage players / actors (activation, chase, attack tokens, slots). Off = the group never engages. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior|Engagement")
	bool bEnableEngagement = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior|Engagement", meta = (EditCondition = "bEnableEngagement"))
	FMassUnitPlayerEngagementConfig Engagement;

	/** Group reactions: alerts between groups, morale and rout, threat-based retargeting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior|Collective")
	bool bEnableCollectiveBehavior = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior|Collective", meta = (EditCondition = "bEnableCollectiveBehavior"))
	FMassUnitCrowdCollectiveConfig Collective;
};
