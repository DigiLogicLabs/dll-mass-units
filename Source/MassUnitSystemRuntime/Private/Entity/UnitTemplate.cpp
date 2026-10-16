// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Entity/UnitTemplate.h"

#include "Core/MassUnitGameplayTags.h"
#include "Entity/MassUnitFragments.h"
#include "MassUnitCommonFragments.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UUnitTemplate::UUnitTemplate()
{
	using namespace UE::MassUnitSystem;
	UnitType = Tags::UnitTypeDefault();
	UnitClass = Tags::UnitClassSoldier();
	DefaultBehavior = Tags::BehaviorIdle();
	DefaultFormation = Tags::FormationLine();
	TeamFaction = Tags::FactionNeutral();
	AnimationTags = {
		Tags::AnimationIdle(),
		Tags::AnimationWalk(),
		Tags::AnimationRun(),
		Tags::AnimationAttack(),
		Tags::AnimationDeath(),
		Tags::AnimationStun()
	};
}

TArray<const UScriptStruct*> UUnitTemplate::GetRequiredFragments() const
{
	return {
		FMassUnitTransformFragment::StaticStruct(),
		FMassUnitVelocityFragment::StaticStruct(),
		FMassUnitForceFragment::StaticStruct(),
		FMassUnitLookAtFragment::StaticStruct(),
		FMassUnitStateFragment::StaticStruct(),
		FMassUnitTargetFragment::StaticStruct(),
		FMassUnitAbilityFragment::StaticStruct(),
		FMassUnitTeamFragment::StaticStruct(),
		FMassUnitVisualFragment::StaticStruct(),
		FMassUnitFormationFragment::StaticStruct(),
		FMassUnitNavigationFragment::StaticStruct(),
		FMassUnitCrowdFragment::StaticStruct(),
		FMassUnitLODFragment::StaticStruct(),
		FMassUnitVisualizationLODFragment::StaticStruct()
	};
}

const FMassUnitVATClip* UUnitTemplate::FindVertexAnimationClip(const FGameplayTag& AnimationTag) const
{
	if (VertexAnimationClips.IsEmpty())
	{
		return nullptr;
	}

	auto FindExact = [this](const FGameplayTag& Tag) -> const FMassUnitVATClip*
	{
		if (!Tag.IsValid())
		{
			return nullptr;
		}
		return VertexAnimationClips.FindByPredicate([&Tag](const FMassUnitVATClip& Clip) { return Clip.AnimationTag == Tag; });
	};

	using namespace UE::MassUnitSystem;
	if (const FMassUnitVATClip* Clip = FindExact(AnimationTag))
	{
		return Clip;
	}
	// The docs ship one Move clip shared by Walk and Run, so either tag satisfies the other.
	if (AnimationTag == Tags::AnimationRun())
	{
		if (const FMassUnitVATClip* Clip = FindExact(Tags::AnimationWalk()))
		{
			return Clip;
		}
	}
	else if (AnimationTag == Tags::AnimationWalk())
	{
		if (const FMassUnitVATClip* Clip = FindExact(Tags::AnimationRun()))
		{
			return Clip;
		}
	}
	if (const FMassUnitVATClip* Idle = FindExact(Tags::AnimationIdle()))
	{
		return Idle;
	}
	return &VertexAnimationClips[0];
}

#if WITH_EDITOR
EDataValidationResult UUnitTemplate::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Fail = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};
	auto Warn = [&Context](const FString& Message)
	{
		Context.AddWarning(FText::FromString(Message));
	};

	if (AttackCooldown > 0.0f && AttackImpactDelay >= AttackCooldown)
	{
		Fail(FString::Printf(TEXT("AttackImpactDelay (%.2fs) must be shorter than AttackCooldown (%.2fs), or swings overlap and damage is lost."),
			AttackImpactDelay, AttackCooldown));
	}

	if (VertexAnimationClips.IsEmpty())
	{
		return Result;
	}

	using namespace UE::MassUnitSystem;
	TSet<FGameplayTag> SeenTags;
	bool bHasIdle = false;
	for (int32 Index = 0; Index < VertexAnimationClips.Num(); ++Index)
	{
		const FMassUnitVATClip& Clip = VertexAnimationClips[Index];
		if (!Clip.AnimationTag.IsValid())
		{
			Fail(FString::Printf(TEXT("VertexAnimationClips[%d] has no AnimationTag."), Index));
			continue;
		}
		bool bAlreadySeen = false;
		SeenTags.Add(Clip.AnimationTag, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Fail(FString::Printf(TEXT("VertexAnimationClips has two rows for %s; only the first is ever played."), *Clip.AnimationTag.ToString()));
		}
		if (Clip.NumFrames < 1 || Clip.FramesPerSecond <= 0.0f)
		{
			Fail(FString::Printf(TEXT("VertexAnimationClips[%d] (%s) needs NumFrames >= 1 and FramesPerSecond > 0."), Index, *Clip.AnimationTag.ToString()));
		}
		bHasIdle |= Clip.AnimationTag == Tags::AnimationIdle();
		if (Clip.AnimationTag == Tags::AnimationDeath() && Clip.bLoop)
		{
			Warn(TEXT("The Death clip loops; corpses will keep dying. Untick bLoop so it holds its last frame."));
		}
		if (Clip.AnimationTag == Tags::AnimationAttack())
		{
			if (Clip.bLoop)
			{
				Warn(TEXT("The Attack clip loops. Each swing already restarts it, so it should play once."));
			}
			if (AttackImpactDelay > Clip.GetDuration())
			{
				Warn(FString::Printf(TEXT("AttackImpactDelay (%.2fs) is past the end of the Attack clip (%.2fs)."), AttackImpactDelay, Clip.GetDuration()));
			}
			if (AttackCooldown > 0.0f && Clip.GetDuration() > AttackCooldown)
			{
				Warn(FString::Printf(TEXT("The Attack clip (%.2fs) is longer than AttackCooldown (%.2fs); the next swing cuts it off."), Clip.GetDuration(), AttackCooldown));
			}
		}
	}
	if (!bHasIdle)
	{
		Fail(TEXT("VertexAnimationClips has no MassUnit.Animation.Idle row; every missing clip falls back to Idle."));
	}

	// Overlapping frame ranges almost always mean the table was typed by hand from a stale bake.
	for (int32 A = 0; A < VertexAnimationClips.Num(); ++A)
	{
		for (int32 B = A + 1; B < VertexAnimationClips.Num(); ++B)
		{
			const FMassUnitVATClip& ClipA = VertexAnimationClips[A];
			const FMassUnitVATClip& ClipB = VertexAnimationClips[B];
			const bool bOverlap = ClipA.StartFrame < ClipB.StartFrame + ClipB.NumFrames && ClipB.StartFrame < ClipA.StartFrame + ClipA.NumFrames;
			if (bOverlap && ClipA.StartFrame != ClipB.StartFrame)
			{
				Warn(FString::Printf(TEXT("Clips %s and %s overlap in the baked texture; re-run the bake script to refresh the table."),
					*ClipA.AnimationTag.ToString(), *ClipB.AnimationTag.ToString()));
			}
		}
	}

	if (StaticMesh.IsNull())
	{
		Fail(TEXT("VertexAnimationClips is set but StaticMesh is empty; the crowd band has nothing to animate."));
	}
	return Result;
}
#endif
