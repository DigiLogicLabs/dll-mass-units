// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Core/MassUnitGameplayTags.h"

#include "NativeGameplayTags.h"

namespace UE::MassUnitSystem::Tags
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UnitTypeDefault, "MassUnit.Type.Default");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UnitClassSoldier, "MassUnit.Class.Soldier");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BehaviorIdle, "MassUnit.Behavior.Idle");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FormationLine, "MassUnit.Formation.Line");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_FactionNeutral, "MassUnit.Faction.Neutral");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationIdle, "MassUnit.Animation.Idle");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationWalk, "MassUnit.Animation.Walk");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationRun, "MassUnit.Animation.Run");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationAttack, "MassUnit.Animation.Attack");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationDeath, "MassUnit.Animation.Death");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationStun, "MassUnit.Animation.Stun");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StatusStunned, "MassUnit.Status.Stunned");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StatusLaunched, "MassUnit.Status.Launched");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationClimb, "MassUnit.Animation.Climb");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationHold, "MassUnit.Animation.Hold");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AnimationLeap, "MassUnit.Animation.Leap");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Stacked, "MassUnit.Stacked");

	FGameplayTag UnitTypeDefault() { return TAG_UnitTypeDefault.GetTag(); }
	FGameplayTag UnitClassSoldier() { return TAG_UnitClassSoldier.GetTag(); }
	FGameplayTag BehaviorIdle() { return TAG_BehaviorIdle.GetTag(); }
	FGameplayTag FormationLine() { return TAG_FormationLine.GetTag(); }
	FGameplayTag FactionNeutral() { return TAG_FactionNeutral.GetTag(); }
	FGameplayTag AnimationIdle() { return TAG_AnimationIdle.GetTag(); }
	FGameplayTag AnimationWalk() { return TAG_AnimationWalk.GetTag(); }
	FGameplayTag AnimationRun() { return TAG_AnimationRun.GetTag(); }
	FGameplayTag AnimationAttack() { return TAG_AnimationAttack.GetTag(); }
	FGameplayTag AnimationDeath() { return TAG_AnimationDeath.GetTag(); }
	FGameplayTag AnimationStun() { return TAG_AnimationStun.GetTag(); }
	FGameplayTag StatusStunned() { return TAG_StatusStunned.GetTag(); }
	FGameplayTag StatusLaunched() { return TAG_StatusLaunched.GetTag(); }
	FGameplayTag AnimationClimb() { return TAG_AnimationClimb.GetTag(); }
	FGameplayTag AnimationHold() { return TAG_AnimationHold.GetTag(); }
	FGameplayTag AnimationLeap() { return TAG_AnimationLeap.GetTag(); }
	FGameplayTag Stacked() { return TAG_Stacked.GetTag(); }
}
