// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Animation/AnimInstance.h"
#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "GameplayTagContainer.h"
#include "MassUnitAnimInstance.generated.h"

/**
 * Optional parent class for a unit's close-range Animation Blueprint.
 *
 * The skeletal mesh pool has no Actor or Pawn to read from, so a regular ABP sees nothing. Reparent the
 * unit's ABP to this class and the pool pushes the unit's simulation state into the variables below on
 * every visual update; build the state machine on them exactly as on a character ABP.
 *
 * Works with any skeleton, including the UE5 Mannequin skeleton the goblins are rigged on. Assign it as
 * UUnitTemplate::AnimationBlueprintClass and leave the template's state clips empty, because the pool
 * prefers single-node clips when they are set.
 */
UCLASS(Blueprintable)
class MASSUNITSYSTEMRUNTIME_API UMassUnitAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Unit this instance currently presents. Pooled meshes are reused, so it changes over the instance's life. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	FMassUnitHandle Unit;

	/** MassUnit.Animation.* tag the simulation wants: Idle, Walk, Run, Attack, Death or Stun. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	FGameplayTag AnimationTag;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	EMassUnitState UnitState = EMassUnitState::Idle;

	/** Seconds in the current state. Matches the vertex-animation phase, so a unit promoted mid-clip does not pop. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	float StateTime = 0.0f;

	/** Horizontal speed in cm/s, for locomotion blend spaces. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	float Speed = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	float HealthPercent = 1.0f;

	/** Increments once per swing. Play the attack montage when it changes (see OnAttackSwing). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	int32 AttackSerial = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	bool bIsDead = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	bool bIsStunned = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	bool bIsAttacking = false;

	/** The unit's gameplay tags, statuses included (Status.Burning, Status.Frozen...), for additive reaction layers. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	FGameplayTagContainer OwnedTags;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	int32 TeamID = 0;

	/** Called by the mesh pool. Fires OnAttackSwing / OnUnitAssigned as the values change. */
	void PushUnitState(
		const FMassUnitHandle& InUnit,
		const FMassUnitStateFragment& State,
		const FMassUnitVisualFragment& Visual,
		const FMassUnitAbilityFragment* Ability,
		int32 InTeamID,
		float InSpeed);

	/** The pooled mesh started presenting a different unit; reset any per-unit animation state here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Mass Unit")
	void OnUnitAssigned(FMassUnitHandle NewUnit);

	/** A new swing started. Play the attack montage here; its impact pose should sit at the template's AttackImpactDelay. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Mass Unit")
	void OnAttackSwing(int32 NewAttackSerial);
};
