// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "UnitConfigDataAsset.generated.h"

class USkeletalMesh;
class UStaticMesh;
class UNiagaraSystem;
class UTexture2D;
class UBehaviorTree;
class UBlackboardData;

/**
 * Optional project-facing catalog metadata for a unit type.
 * Runtime entity creation uses UUnitTemplate; projects may use this richer
 * asset to build their own template/content selection workflow.
 */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UUnitConfigDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UUnitConfigDataAsset();

    /** Unit type identifier */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Config")
    FGameplayTag UnitType;
    
    /** Display name of the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Config")
    FText DisplayName;
    
    /** Description of the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Config", meta = (MultiLine = true))
    FText Description;
    
    /** Skeletal mesh for detailed representation */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
    
    /** Static mesh for simplified representation */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    TSoftObjectPtr<UStaticMesh> StaticMesh;
    
    /** Niagara system for particle representation */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    TSoftObjectPtr<UNiagaraSystem> NiagaraSystem;
    
    /** Vertex animation textures for different animations */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
    TMap<FGameplayTag, TSoftObjectPtr<UTexture2D>> VertexAnimationTextures;
    
    /** Behavior tree for AI control */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    TSoftObjectPtr<UBehaviorTree> BehaviorTree;
    
    /** Blackboard data for AI control */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
    TSoftObjectPtr<UBlackboardData> BlackboardData;
    
    /** Default formation type for this unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation")
    FName DefaultFormationType;
    
    /** Unit spacing in formation */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formation")
    float FormationSpacing;
    
    /** LOD distance thresholds */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD")
    TArray<float> LODDistanceThresholds;
    
    /** Distance for skeletal mesh transition */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD")
    float SkeletalMeshDistance;
    
    /** Maximum visible distance */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LOD")
    float MaxVisibleDistance;
};
