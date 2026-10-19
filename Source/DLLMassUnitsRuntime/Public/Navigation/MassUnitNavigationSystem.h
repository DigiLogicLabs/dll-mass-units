// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "NavigationPath.h"
#include "MassUnitNavigationSystem.generated.h"

class ANavigationData;
class UMassEntitySubsystem;
class UNavigationSystemV1;

/** Batched asynchronous navmesh path service for Mass units. */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UMassUnitNavigationSystem : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem);
	void Deinitialize();

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Navigation")
	void UpdateNavigationData(UWorld* InWorld);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Navigation")
	bool RequestPath(FMassUnitHandle UnitHandle, const FVector& Destination, float AcceptanceRadius = 50.0f);

	bool RequestPathInternal(FMassUnitEntityHandle Entity, const FVector& Destination, float AcceptanceRadius = 50.0f);

	/**
	 * Calculates one native navmesh corridor for a group anchor. The caller can
	 * share the returned points across many Mass entities instead of submitting
	 * one path query per entity. Uses the configured direct fallback when needed.
	 */
	bool FindSharedPath(
		const FVector& Start,
		const FVector& Destination,
		TArray<FVector>& OutPathPoints,
		bool* bOutUsesNavmesh = nullptr);

	/** Projects one lightweight shared-crowd slot onto the active navmesh without building an individual path. */
	bool ProjectPointToNavigation(const FVector& Point, FVector& OutProjectedLocation);

	/** Cancels queued/in-flight work for one unit and clears its current path. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Navigation")
	bool CancelPath(FMassUnitHandle UnitHandle);

	bool CancelPathInternal(FMassUnitEntityHandle Entity);

	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Navigation")
	void ProcessPathRequests();

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Navigation")
	int32 GetQueuedRequestCount() const { return PathRequestQueue.Num() + PendingPathOwners.Num(); }

	UNavigationSystemV1* GetNavigationSystem() const { return NavigationSystem; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UNavigationSystemV1> NavigationSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ANavigationData> NavigationData = nullptr;

	struct FPathRequest
	{
		FMassUnitEntityHandle Entity;
		FVector Destination = FVector::ZeroVector;
		float AcceptanceRadius = 50.0f;
	};

	TArray<FPathRequest> PathRequestQueue;
	TMap<uint32, FMassUnitEntityHandle> PendingPathOwners;

	UPROPERTY(EditAnywhere, Category = "Navigation", meta = (ClampMin = "1"))
	int32 MaxPathRequestsPerFrame = 100;

	void HandlePathRequestComplete(uint32 PathId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path);
	void UpdateEntityWithPath(FMassUnitEntityHandle Entity, const FNavPathSharedPtr& Path);
	bool SetDirectPath(FMassUnitEntityHandle Entity, const FVector& Destination, float AcceptanceRadius);
	void MarkPathFailed(FMassUnitEntityHandle Entity);
	bool IsEntityValid(FMassUnitEntityHandle Entity) const;
};
