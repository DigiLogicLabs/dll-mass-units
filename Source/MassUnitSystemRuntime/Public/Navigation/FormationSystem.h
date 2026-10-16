// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "FormationSystem.generated.h"

class UMassEntitySubsystem;

/** World-local registry that assigns Mass units to deterministic formation slots. */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UFormationSystem : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UMassEntitySubsystem* InEntitySubsystem);
	void Deinitialize();
	void Tick(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	int32 CreateFormation(FVector Location, FRotator Rotation, FName FormationType);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	bool DestroyFormation(int32 FormationHandle);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	bool AddUnitToFormation(FMassUnitHandle UnitHandle, int32 FormationHandle);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	bool RemoveUnitFromFormation(FMassUnitHandle UnitHandle);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	bool AddEntityToFormation(FMassUnitEntityHandle Entity, int32 FormationHandle);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	bool RemoveEntityFromFormation(FMassUnitEntityHandle Entity);

	bool AddEntityToFormationInternal(FMassUnitEntityHandle Entity, int32 FormationHandle);
	bool RemoveEntityFromFormationInternal(FMassUnitEntityHandle Entity);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	bool SetFormationTarget(int32 FormationHandle, FVector TargetLocation);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Formation")
	bool SetFormationShape(int32 FormationHandle, FName FormationShape);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Formation")
	FVector GetFormationLocation(int32 FormationHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Formation")
	FRotator GetFormationRotation(int32 FormationHandle) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Formation")
	TArray<FMassUnitEntityHandle> GetEntitiesInFormation(int32 FormationHandle) const;

	TArray<FMassUnitEntityHandle> GetEntitiesInFormationInternal(int32 FormationHandle) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	struct FFormationData
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector TargetLocation = FVector::ZeroVector;
		FName FormationType = NAME_None;
		FName FormationShape = TEXT("Rectangle");
		TArray<FMassUnitEntityHandle> Entities;
		TMap<FMassUnitEntityHandle, int32> EntitySlots;
		float FormationWidth = 1000.0f;
		float FormationDepth = 1000.0f;
		float UnitSpacing = 150.0f;
		float MoveSpeed = 300.0f;
		bool bIsMoving = false;
	};

	TMap<int32, FFormationData> Formations;
	TMap<FMassUnitEntityHandle, int32> EntityFormationMap;
	int32 NextFormationHandle = 1;

	void UpdateFormationPositions();
	void UpdateFormationMovement(float DeltaTime);
	FVector CalculateSlotPosition(const FFormationData& Formation, int32 SlotIndex) const;
	void UpdateEntityFormationData(FMassUnitEntityHandle Entity, int32 FormationHandle, int32 SlotIndex);
	bool IsEntityValid(FMassUnitEntityHandle Entity) const;
};
