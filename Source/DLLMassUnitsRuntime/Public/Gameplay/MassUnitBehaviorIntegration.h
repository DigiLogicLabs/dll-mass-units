// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "MassUnitBehaviorIntegration.generated.h"

class UBehaviorTree;
class UBehaviorTreeComponent;
class UBlackboardComponent;
class UBlackboardData;
class UGASUnitIntegration;

/** Optional behavior-tree bridge for the small subset of Mass units that need UObject AI. */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UMassUnitBehaviorIntegration : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UGASUnitIntegration* InGASIntegration);
	void Deinitialize();
	void Tick(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Behavior")
	bool SetBehaviorTree(FMassUnitHandle UnitHandle, UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardData);

	/** Writes TaskTag to an optional RequestedTask name key for the running tree to consume. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Behavior")
	bool ExecuteBTTask(FMassUnitHandle UnitHandle, FGameplayTag TaskTag);

	bool SetBehaviorTreeInternal(FMassUnitEntityHandle Entity, UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardData);
	bool ExecuteBTTaskInternal(FMassUnitEntityHandle Entity, FGameplayTag TaskTag);

private:
	UPROPERTY(Transient)
	TObjectPtr<UGASUnitIntegration> GASIntegration = nullptr;

	UPROPERTY(Transient)
	TMap<FMassUnitEntityHandle, TObjectPtr<UBehaviorTreeComponent>> EntityBTMap;

	UPROPERTY(Transient)
	TMap<FMassUnitEntityHandle, TObjectPtr<UBlackboardComponent>> EntityBBMap;

	UBehaviorTreeComponent* CreateBehaviorTreeForEntity(FMassUnitEntityHandle Entity, UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardData);
	void RemoveBehaviorTreeForEntity(FMassUnitEntityHandle Entity);
	void UpdateBlackboardFromEntity(FMassUnitEntityHandle Entity);
	void UpdateEntityFromBlackboard(FMassUnitEntityHandle Entity);
	bool IsEntityValid(FMassUnitEntityHandle Entity) const;
};
