// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"

namespace UE::MassUnitSystem::Tags
{
	MASSUNITSYSTEMRUNTIME_API FGameplayTag UnitTypeDefault();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag UnitClassSoldier();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag BehaviorIdle();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag FormationLine();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag FactionNeutral();

	MASSUNITSYSTEMRUNTIME_API FGameplayTag AnimationIdle();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag AnimationWalk();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag AnimationRun();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag AnimationAttack();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag AnimationDeath();
	MASSUNITSYSTEMRUNTIME_API FGameplayTag AnimationStun();

	/** Granted by StunUnit. Project code can stun with its own tag (e.g. Status.Stunned) via a bStuns status instead. */
	MASSUNITSYSTEMRUNTIME_API FGameplayTag StatusStunned();
	/** Airborne or knocked down after LaunchUnit: a stun the launch owns (no moving, no attacking). */
	MASSUNITSYSTEMRUNTIME_API FGameplayTag StatusLaunched();
}
