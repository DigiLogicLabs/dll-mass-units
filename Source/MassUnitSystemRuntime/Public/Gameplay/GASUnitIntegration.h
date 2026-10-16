// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "GameplayAbilitySpec.h"
#include "GASUnitIntegration.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;
class UMassEntitySubsystem;

/**
 * Optional bridge from a lightweight Mass unit to an externally owned ASC.
 * The plugin deliberately does not create an actor and ASC for every Mass entity;
 * callers register ASCs only for units that need full GAS behavior.
 */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UGASUnitIntegration : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UMassEntitySubsystem* InEntitySubsystem);
	void Deinitialize();

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|GAS")
	bool RegisterAbilitySystemForUnit(FMassUnitHandle UnitHandle, UAbilitySystemComponent* AbilitySystem);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|GAS")
	void UnregisterAbilitySystemForUnit(FMassUnitHandle UnitHandle);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|GAS")
	UAbilitySystemComponent* GetAbilitySystemForEntity(FMassUnitHandle UnitHandle) const;

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|GAS")
	FGameplayAbilitySpecHandle GrantAbility(FMassUnitHandle UnitHandle, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|GAS")
	bool ActivateAbility(FMassUnitHandle UnitHandle, FGameplayTag AbilityTag);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|GAS")
	bool ApplyGameplayEffect(FMassUnitHandle UnitHandle, TSubclassOf<UGameplayEffect> EffectClass, AActor* Instigator = nullptr);

	UAbilitySystemComponent* GetAbilitySystemForEntityInternal(FMassUnitEntityHandle Entity) const;
	FGameplayAbilitySpecHandle GrantAbilityInternal(FMassUnitEntityHandle Entity, TSubclassOf<UGameplayAbility> AbilityClass, int32 Level = 1);
	bool ActivateAbilityInternal(FMassUnitEntityHandle Entity, FGameplayTag AbilityTag);
	bool ApplyGameplayEffectInternal(FMassUnitEntityHandle Entity, TSubclassOf<UGameplayEffect> EffectClass, AActor* Instigator = nullptr);

	UMassEntitySubsystem* GetEntitySubsystem() const { return EntitySubsystem; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TMap<FMassUnitEntityHandle, TObjectPtr<UAbilitySystemComponent>> EntityASCMap;

	bool IsEntityValid(FMassUnitEntityHandle Entity) const;
};
