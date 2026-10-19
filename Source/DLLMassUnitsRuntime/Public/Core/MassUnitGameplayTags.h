// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "GameplayTagContainer.h"

namespace UE::MassUnitSystem::Tags
{
	DLLMASSUNITSRUNTIME_API FGameplayTag UnitTypeDefault();
	DLLMASSUNITSRUNTIME_API FGameplayTag UnitClassSoldier();
	DLLMASSUNITSRUNTIME_API FGameplayTag BehaviorIdle();
	DLLMASSUNITSRUNTIME_API FGameplayTag FormationLine();
	DLLMASSUNITSRUNTIME_API FGameplayTag FactionNeutral();

	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationIdle();
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationWalk();
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationRun();
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationAttack();
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationDeath();
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationStun();

	/** Granted by StunUnit. Project code can stun with its own tag (e.g. Status.Stunned) via a bStuns status instead. */
	DLLMASSUNITSRUNTIME_API FGameplayTag StatusStunned();
	/** Airborne or knocked down after LaunchUnit: a stun the launch owns (no moving, no attacking). */
	DLLMASSUNITSRUNTIME_API FGameplayTag StatusLaunched();

	/** Climbing up a pile. Templates without a Climb clip play Run instead. */
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationClimb();
	/** Bracing under a unit that stands on it. Templates without a Hold clip play Idle instead. */
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationHold();
	/** In a LeapUnit jump (a pounce). Templates without a Leap clip play Attack instead. */
	DLLMASSUNITSRUNTIME_API FGameplayTag AnimationLeap();
	/** Loose tag on every unit the stack system drives (climbing, standing on a unit, or holding one up). */
	DLLMASSUNITSRUNTIME_API FGameplayTag Stacked();
}
