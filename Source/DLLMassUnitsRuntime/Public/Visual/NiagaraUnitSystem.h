// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "NiagaraUnitSystem.generated.h"

class UCrowdInstanceRenderer;
class UInstancedStaticMeshComponent;
class UMassEntitySubsystem;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMesh;
class UVertexAnimationManager;

/** GPU representation uploader with an asset-free instanced-mesh fallback. */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UNiagaraUnitSystem : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem);
	void Deinitialize();
	void UpdateUnitVisuals(const TArray<FMassUnitEntityHandle>& Entities);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Rendering")
	void UpdateUnitVisualsByHandles(const TArray<FMassUnitHandle>& UnitHandles);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Rendering")
	void SetLODLevel(int32 LODLevel);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering")
	UNiagaraComponent* GetNiagaraComponent() const { return NiagaraComponent; }

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering")
	bool IsUsingNiagara() const { return NiagaraComponent != nullptr; }

	/** Number of dynamic ISM components currently allocated by the asset-free/static-mesh fallback. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetInstancedMeshComponentCount() const;

	/** Units currently drawn through the ISM fallback. Returns zero when Niagara is active. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetInstancedMeshInstanceCount() const;

	/**
	 * Instance slots allocated on the ISM components, including slots parked for reuse.
	 * The gap between this and GetInstancedMeshInstanceCount is the recycling headroom kept for
	 * respawns; it is reclaimed once parked slots dominate a bucket.
	 */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetInstancedMeshAllocatedSlotCount() const;

	/** Increments only when instanced-rendering slots are added or removed, not while units move. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetInstancedMeshTopologyRevision() const;

	/** Per-instance material data: see the channel contract in CrowdInstanceRenderer.h. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Rendering|Diagnostics")
	int32 GetInstancedMeshCustomDataFloatCount() const;

	UVertexAnimationManager* GetVertexAnimationManager() const { return VertexAnimationManager; }

	/** Stable-slot instanced renderer backing the non-Niagara path. Null until Initialize runs. */
	UCrowdInstanceRenderer* GetCrowdInstanceRenderer() const { return CrowdInstanceRenderer; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> NiagaraSystemAsset = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> NiagaraComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVertexAnimationManager> VertexAnimationManager = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FallbackStaticMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCrowdInstanceRenderer> CrowdInstanceRenderer = nullptr;

	int32 CurrentLODLevel = 0;
	int32 MaxUnits = 10000;
	float UpdateFrequency = 0.033f;
	float LastUpdateTime = -BIG_NUMBER;
	bool bEnableInstancedFallback = true;

	void CreateNiagaraSystem();
	void UpdateNiagaraData(const TArray<FMassUnitEntityHandle>& Entities);
	/** Forwards the frame's entities to the stable-slot instanced renderer. */
	void UpdateInstancedMeshData(const TArray<FMassUnitEntityHandle>& Entities);
	int32 ResolveAnimationIndex(const struct FMassUnitVisualFragment& Visual, const struct FMassUnitStateFragment& State);
};
