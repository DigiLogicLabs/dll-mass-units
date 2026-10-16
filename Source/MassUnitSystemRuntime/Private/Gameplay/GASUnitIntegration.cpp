// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Gameplay/GASUnitIntegration.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"

void UGASUnitIntegration::Initialize(UMassEntitySubsystem* InEntitySubsystem)
{
	EntitySubsystem = InEntitySubsystem;
}

void UGASUnitIntegration::Deinitialize()
{
	EntityASCMap.Reset();
	EntitySubsystem = nullptr;
}

bool UGASUnitIntegration::RegisterAbilitySystemForUnit(FMassUnitHandle UnitHandle, UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem || !IsEntityValid(UnitHandle.EntityHandle))
	{
		return false;
	}
	EntityASCMap.Add(UnitHandle.EntityHandle, AbilitySystem);
	return true;
}

void UGASUnitIntegration::UnregisterAbilitySystemForUnit(FMassUnitHandle UnitHandle)
{
	EntityASCMap.Remove(UnitHandle.EntityHandle);
}

UAbilitySystemComponent* UGASUnitIntegration::GetAbilitySystemForEntity(FMassUnitHandle UnitHandle) const
{
	return GetAbilitySystemForEntityInternal(UnitHandle.EntityHandle);
}

UAbilitySystemComponent* UGASUnitIntegration::GetAbilitySystemForEntityInternal(FMassUnitEntityHandle Entity) const
{
	if (!IsEntityValid(Entity))
	{
		return nullptr;
	}
	return EntityASCMap.FindRef(Entity);
}

FGameplayAbilitySpecHandle UGASUnitIntegration::GrantAbility(FMassUnitHandle UnitHandle, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level)
{
	return GrantAbilityInternal(UnitHandle.EntityHandle, AbilityClass, Level);
}

FGameplayAbilitySpecHandle UGASUnitIntegration::GrantAbilityInternal(FMassUnitEntityHandle Entity, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level)
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemForEntityInternal(Entity);
	if (!AbilitySystem || !AbilityClass)
	{
		return {};
	}
	return AbilitySystem->GiveAbility(FGameplayAbilitySpec(AbilityClass, FMath::Max(1, Level)));
}

bool UGASUnitIntegration::ActivateAbility(FMassUnitHandle UnitHandle, FGameplayTag AbilityTag)
{
	return ActivateAbilityInternal(UnitHandle.EntityHandle, AbilityTag);
}

bool UGASUnitIntegration::ActivateAbilityInternal(FMassUnitEntityHandle Entity, FGameplayTag AbilityTag)
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemForEntityInternal(Entity);
	if (!AbilitySystem || !AbilityTag.IsValid())
	{
		return false;
	}
	FGameplayTagContainer Tags;
	Tags.AddTag(AbilityTag);
	return AbilitySystem->TryActivateAbilitiesByTag(Tags);
}

bool UGASUnitIntegration::ApplyGameplayEffect(FMassUnitHandle UnitHandle, TSubclassOf<UGameplayEffect> EffectClass, AActor* Instigator)
{
	return ApplyGameplayEffectInternal(UnitHandle.EntityHandle, EffectClass, Instigator);
}

bool UGASUnitIntegration::ApplyGameplayEffectInternal(FMassUnitEntityHandle Entity, TSubclassOf<UGameplayEffect> EffectClass, AActor* Instigator)
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemForEntityInternal(Entity);
	if (!AbilitySystem || !EffectClass)
	{
		return false;
	}
	FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
	if (Instigator)
	{
		Context.AddInstigator(Instigator, Instigator);
	}
	const FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(EffectClass, 1.0f, Context);
	return Spec.IsValid() && AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).IsValid();
}

bool UGASUnitIntegration::IsEntityValid(FMassUnitEntityHandle Entity) const
{
	return EntitySubsystem && Entity.IsValid()
		&& EntitySubsystem->GetEntityManager().IsEntityValid(Entity.ToMassEntityHandle());
}
