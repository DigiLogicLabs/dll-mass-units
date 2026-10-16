// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "UnitMeshPool.generated.h"

class UMassEntitySubsystem;
class USkeletalMeshComponent;

/** Bounded pool for close-range skeletal representations. */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UUnitMeshPool : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, int32 PoolSize);
	void Deinitialize();
	void UpdateUnitMeshes(const TArray<FMassUnitEntityHandle>& Entities);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Rendering")
	USkeletalMeshComponent* GetMeshForUnit(FMassUnitHandle UnitHandle);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Rendering")
	void ReleaseMesh(USkeletalMeshComponent* Mesh);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Rendering")
	bool TransitionToSkeletal(FMassUnitHandle UnitHandle);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Rendering")
	bool TransitionToVertex(FMassUnitHandle UnitHandle);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetActiveSkeletalMeshCount() const { return EntityMeshMap.Num(); }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetAvailableSkeletalMeshCount() const { return AvailableMeshes.Num(); }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetSkeletalMeshCapacity() const { return MaxPoolSize; }

	/**
	 * Near-band kill ragdolls: each (dead unit, launch velocity) whose unit currently has a pooled skeletal mesh with a
	 * Physics Asset becomes a simulating ragdoll, up to UMassUnitSystemSettings::MaxActiveRagdolls at once. The mesh
	 * stays with its unit (and stops following the entity transform) until the unit is destroyed, then is reset and
	 * returned to the pool. Units that do not qualify keep the cheap tumbling fling. Returns how many started.
	 */
	int32 StartRagdolls(const TArray<TPair<FMassUnitEntityHandle, FVector>>& Requests);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetActiveRagdollCount() const { return RagdollMeshes.Num(); }

	USkeletalMeshComponent* GetMeshForUnitInternal(FMassUnitEntityHandle Entity);
	bool TransitionToSkeletalInternal(FMassUnitEntityHandle Entity);
	bool TransitionToVertexInternal(FMassUnitEntityHandle Entity);

private:
	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> AvailableMeshes;

	UPROPERTY(Transient)
	TMap<FMassUnitEntityHandle, TObjectPtr<USkeletalMeshComponent>> EntityMeshMap;

	UPROPERTY(Transient)
	TMap<TObjectPtr<USkeletalMeshComponent>, FMassUnitEntityHandle> MeshEntityMap;

	TMap<TObjectPtr<USkeletalMeshComponent>, FGameplayTag> MeshAnimationTags;

	/** Attack serial each mesh last played, so every swing restarts the attack clip. */
	TMap<TObjectPtr<USkeletalMeshComponent>, int32> MeshAttackSerials;

	/** Pooled meshes currently simulating as ragdolls (not driven by their entity until released). */
	TSet<TObjectPtr<USkeletalMeshComponent>> RagdollMeshes;

	int32 MaxPoolSize = 100;
	float UpdateInterval = 0.033f;
	float LastUpdateTime = -BIG_NUMBER;

	USkeletalMeshComponent* CreateMeshComponent();
	void UpdateMeshFromEntity(USkeletalMeshComponent* Mesh, FMassUnitEntityHandle Entity);
	class UAnimationAsset* ResolveAnimationAsset(const struct FMassUnitVisualFragment& Visual) const;
	static bool ShouldLoopAnimation(const FGameplayTag& AnimationTag);
	bool IsEntityValid(FMassUnitEntityHandle Entity) const;
};
