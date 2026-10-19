// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Visual/MassUnitAnimInstance.h"

void UMassUnitAnimInstance::PushUnitState(
	const FMassUnitHandle& InUnit,
	const FMassUnitStateFragment& State,
	const FMassUnitVisualFragment& Visual,
	const FMassUnitAbilityFragment* Ability,
	int32 InTeamID,
	float InSpeed)
{
	const bool bNewUnit = !(Unit == InUnit);
	const bool bNewSwing = !bNewUnit && State.AttackSerial != AttackSerial;

	Unit = InUnit;
	AnimationTag = Visual.CurrentAnimation;
	UnitState = State.CurrentState;
	StateTime = State.StateTime;
	Speed = InSpeed;
	HealthPercent = State.MaxHealth > UE_SMALL_NUMBER ? FMath::Clamp(State.Health / State.MaxHealth, 0.0f, 1.0f) : 0.0f;
	AttackSerial = State.AttackSerial;
	bIsDead = State.CurrentState == EMassUnitState::Dead;
	bIsStunned = State.CurrentState == EMassUnitState::Stunned;
	bIsAttacking = State.CurrentState == EMassUnitState::Attacking;
	TeamID = InTeamID;
	if (Ability)
	{
		OwnedTags = Ability->OwnedTags;
	}
	else
	{
		OwnedTags.Reset();
	}

	if (bNewUnit)
	{
		OnUnitAssigned(Unit);
	}
	else if (bNewSwing)
	{
		OnAttackSwing(AttackSerial);
	}
}
