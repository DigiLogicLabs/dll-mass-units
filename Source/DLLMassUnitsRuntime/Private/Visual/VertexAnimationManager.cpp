// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Visual/VertexAnimationManager.h"

#include "Core/MassUnitGameplayTags.h"
#include "Engine/Texture2D.h"

UVertexAnimationManager::UVertexAnimationManager() = default;
UVertexAnimationManager::~UVertexAnimationManager() = default;

void UVertexAnimationManager::Initialize()
{
	LoadAnimationTextures();
}

void UVertexAnimationManager::Deinitialize()
{
	AnimationTextureMap.Reset();
	AnimationIndexMap.Reset();
	BlendedAnimationMap.Reset();
	NextAnimationIndex = 0;
}

UTexture2D* UVertexAnimationManager::GetAnimationTexture(FGameplayTag AnimationTag)
{
	return AnimationTextureMap.FindRef(AnimationTag);
}

UTexture2D* UVertexAnimationManager::BlendAnimations(FGameplayTag FromAnim, FGameplayTag ToAnim, float BlendAlpha)
{
	// VAT frames are sampled and blended by the material/Niagara renderer. Returning
	// the dominant source texture keeps this UObject API deterministic and allocation-free.
	return GetAnimationTexture(FMath::Clamp(BlendAlpha, 0.0f, 1.0f) < 0.5f ? FromAnim : ToAnim);
}

int32 UVertexAnimationManager::GetAnimationIndex(FGameplayTag AnimationTag)
{
	if (const int32* Index = AnimationIndexMap.Find(AnimationTag))
	{
		return *Index;
	}
	return INDEX_NONE;
}

int32 UVertexAnimationManager::RegisterAnimationTexture(FGameplayTag AnimationTag, UTexture2D* Texture)
{
	if (!AnimationTag.IsValid() || !Texture)
	{
		return INDEX_NONE;
	}
	int32& Index = AnimationIndexMap.FindOrAdd(AnimationTag, NextAnimationIndex);
	if (Index == NextAnimationIndex)
	{
		++NextAnimationIndex;
	}
	AnimationTextureMap.Add(AnimationTag, Texture);
	return Index;
}

void UVertexAnimationManager::UnregisterAnimationTexture(FGameplayTag AnimationTag)
{
	AnimationTextureMap.Remove(AnimationTag);
}

void UVertexAnimationManager::LoadAnimationTextures()
{
	const TArray<FGameplayTag> BuiltInAnimations = {
		UE::MassUnitSystem::Tags::AnimationIdle(),
		UE::MassUnitSystem::Tags::AnimationWalk(),
		UE::MassUnitSystem::Tags::AnimationRun(),
		UE::MassUnitSystem::Tags::AnimationAttack(),
		UE::MassUnitSystem::Tags::AnimationDeath(),
		UE::MassUnitSystem::Tags::AnimationStun()
	};
	for (const FGameplayTag Tag : BuiltInAnimations)
	{
		AnimationIndexMap.Add(Tag, NextAnimationIndex++);
	}
}
