// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Navigation/MassUnitNavigationSystem.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitSystemRuntime.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

void UMassUnitNavigationSystem::Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem)
{
	World = InWorld;
	EntitySubsystem = InEntitySubsystem;
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	MaxPathRequestsPerFrame = Settings ? FMath::Max(1, Settings->MaxPathRequestsPerFrame) : 100;
	UpdateNavigationData(InWorld);
}

void UMassUnitNavigationSystem::Deinitialize()
{
	TArray<uint32> PendingPathIds;
	PendingPathOwners.GenerateKeyArray(PendingPathIds);
	PendingPathOwners.Reset();
	if (NavigationSystem)
	{
		for (const uint32 PathId : PendingPathIds)
		{
			NavigationSystem->AbortAsyncFindPathRequest(PathId);
		}
	}
	PathRequestQueue.Reset();
	NavigationData = nullptr;
	NavigationSystem = nullptr;
	EntitySubsystem = nullptr;
	World = nullptr;
}

void UMassUnitNavigationSystem::UpdateNavigationData(UWorld* InWorld)
{
	World = InWorld;
	NavigationSystem = InWorld ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(InWorld) : nullptr;
	NavigationData = NavigationSystem ? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
}

bool UMassUnitNavigationSystem::RequestPath(FMassUnitHandle UnitHandle, const FVector& Destination, float AcceptanceRadius)
{
	return RequestPathInternal(UnitHandle.EntityHandle, Destination, AcceptanceRadius);
}

bool UMassUnitNavigationSystem::CancelPath(FMassUnitHandle UnitHandle)
{
	return CancelPathInternal(UnitHandle.EntityHandle);
}

bool UMassUnitNavigationSystem::CancelPathInternal(FMassUnitEntityHandle Entity)
{
	if (!IsEntityValid(Entity))
	{
		return false;
	}

	PathRequestQueue.RemoveAll([Entity](const FPathRequest& Existing) { return Existing.Entity == Entity; });
	TArray<uint32> PendingPathIds;
	for (const TPair<uint32, FMassUnitEntityHandle>& Pair : PendingPathOwners)
	{
		if (Pair.Value == Entity)
		{
			PendingPathIds.Add(Pair.Key);
		}
	}
	for (const uint32 PathId : PendingPathIds)
	{
		PendingPathOwners.Remove(PathId);
		if (NavigationSystem)
		{
			NavigationSystem->AbortAsyncFindPathRequest(PathId);
		}
	}

	if (FMassUnitNavigationFragment* Navigation = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitNavigationFragment>(Entity.ToMassEntityHandle()))
	{
		Navigation->ResetPath();
	}
	return true;
}

bool UMassUnitNavigationSystem::RequestPathInternal(FMassUnitEntityHandle Entity, const FVector& Destination, float AcceptanceRadius)
{
	if (!IsEntityValid(Entity))
	{
		return false;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(Entity.ToMassEntityHandle());
	if (!Navigation)
	{
		return false;
	}

	Navigation->DestinationLocation = Destination;
	Navigation->AcceptanceRadius = FMath::Max(1.0f, AcceptanceRadius);
	Navigation->PathPoints.Reset();
	Navigation->CurrentPathIndex = INDEX_NONE;
	Navigation->bPathRequested = true;
	Navigation->bPathValid = false;
	Navigation->bPathUsesNavmesh = false;
	if (FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Entity.ToMassEntityHandle()))
	{
		Target->TargetEntity.Invalidate();
		Target->TargetLocation = Destination;
		Target->bHasTargetLocation = true;
	}

	PathRequestQueue.RemoveAll([Entity](const FPathRequest& Existing) { return Existing.Entity == Entity; });
	TArray<uint32> SupersededPathIds;
	for (const TPair<uint32, FMassUnitEntityHandle>& Pair : PendingPathOwners)
	{
		if (Pair.Value == Entity)
		{
			SupersededPathIds.Add(Pair.Key);
		}
	}
	for (const uint32 PathId : SupersededPathIds)
	{
		PendingPathOwners.Remove(PathId);
		if (NavigationSystem)
		{
			NavigationSystem->AbortAsyncFindPathRequest(PathId);
		}
	}
	PathRequestQueue.Add({Entity, Destination, Navigation->AcceptanceRadius});
	return true;
}

bool UMassUnitNavigationSystem::FindSharedPath(
	const FVector& Start,
	const FVector& Destination,
	TArray<FVector>& OutPathPoints,
	bool* bOutUsesNavmesh)
{
	OutPathPoints.Reset();
	if (bOutUsesNavmesh)
	{
		*bOutUsesNavmesh = false;
	}
	if (!World)
	{
		return false;
	}
	if (!NavigationSystem || !NavigationData)
	{
		UpdateNavigationData(World);
	}

	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const bool bUseDirectFallback = !Settings || Settings->bFallbackToDirectPath;
	if (!NavigationSystem || !NavigationData)
	{
		if (bUseDirectFallback)
		{
			OutPathPoints.Add(Destination);
			return true;
		}
		return false;
	}

	FNavLocation ProjectedStart;
	FNavLocation ProjectedDestination;
	const FVector QueryExtent = NavigationData->GetDefaultQueryExtent();
	const bool bProjectedStart = NavigationSystem->ProjectPointToNavigation(
		Start,
		ProjectedStart,
		QueryExtent,
		NavigationData);
	const bool bProjectedDestination = NavigationSystem->ProjectPointToNavigation(
		Destination,
		ProjectedDestination,
		QueryExtent,
		NavigationData);
	if (!bProjectedStart || !bProjectedDestination)
	{
		if (bUseDirectFallback)
		{
			OutPathPoints.Add(Destination);
			return true;
		}
		return false;
	}

	FPathFindingQuery Query(nullptr, *NavigationData, ProjectedStart.Location, ProjectedDestination.Location);
	const FPathFindingResult Result = NavigationSystem->FindPathSync(
		FNavAgentProperties::DefaultProperties,
		MoveTemp(Query));
	if (Result.IsSuccessful() && Result.Path.IsValid())
	{
		for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
		{
			OutPathPoints.Add(Point.Location);
		}
		if (!OutPathPoints.IsEmpty())
		{
			if (bOutUsesNavmesh)
			{
				*bOutUsesNavmesh = true;
			}
			return true;
		}
	}

	if (bUseDirectFallback)
	{
		OutPathPoints.Add(Destination);
		return true;
	}
	return false;
}

bool UMassUnitNavigationSystem::ProjectPointToNavigation(
	const FVector& Point,
	FVector& OutProjectedLocation)
{
	if (!World)
	{
		return false;
	}
	if (!NavigationSystem || !NavigationData)
	{
		UpdateNavigationData(World);
	}
	if (!NavigationSystem || !NavigationData)
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavigationSystem->ProjectPointToNavigation(
		Point,
		ProjectedLocation,
		NavigationData->GetDefaultQueryExtent(),
		NavigationData))
	{
		return false;
	}
	OutProjectedLocation = ProjectedLocation.Location;
	return true;
}

void UMassUnitNavigationSystem::ProcessPathRequests()
{
	if (PathRequestQueue.IsEmpty())
	{
		return;
	}
	if (!NavigationSystem || !NavigationData)
	{
		UpdateNavigationData(World);
	}

	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const bool bUseDirectFallback = !Settings || Settings->bFallbackToDirectPath;
	const int32 RequestCount = FMath::Min(MaxPathRequestsPerFrame, PathRequestQueue.Num());
	for (int32 Index = 0; Index < RequestCount; ++Index)
	{
		const FPathRequest Request = PathRequestQueue[0];
		PathRequestQueue.RemoveAt(0, 1, EAllowShrinking::No);
		if (!IsEntityValid(Request.Entity))
		{
			continue;
		}

		if (!NavigationSystem || !NavigationData)
		{
			if (bUseDirectFallback)
			{
				SetDirectPath(Request.Entity, Request.Destination, Request.AcceptanceRadius);
			}
			else
			{
				MarkPathFailed(Request.Entity);
			}
			continue;
		}

		const FMassUnitTransformFragment* Transform = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitTransformFragment>(Request.Entity.ToMassEntityHandle());
		if (!Transform)
		{
			MarkPathFailed(Request.Entity);
			continue;
		}
		FNavLocation ProjectedStart;
		FNavLocation ProjectedDestination;
		const FVector QueryExtent = NavigationData->GetDefaultQueryExtent();
		const bool bProjectedStart = NavigationSystem->ProjectPointToNavigation(
			Transform->GetTransform().GetLocation(),
			ProjectedStart,
			QueryExtent,
			NavigationData);
		const bool bProjectedDestination = NavigationSystem->ProjectPointToNavigation(
			Request.Destination,
			ProjectedDestination,
			QueryExtent,
			NavigationData);
		if (!bProjectedStart || !bProjectedDestination)
		{
			if (bUseDirectFallback)
			{
				SetDirectPath(Request.Entity, Request.Destination, Request.AcceptanceRadius);
			}
			else
			{
				MarkPathFailed(Request.Entity);
			}
			continue;
		}

		if (FMassUnitNavigationFragment* Navigation = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitNavigationFragment>(Request.Entity.ToMassEntityHandle()))
		{
			Navigation->DestinationLocation = ProjectedDestination.Location;
		}
		if (FMassUnitTargetFragment* Target = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitTargetFragment>(Request.Entity.ToMassEntityHandle()))
		{
			Target->TargetLocation = ProjectedDestination.Location;
		}
		FPathFindingQuery Query(nullptr, *NavigationData, ProjectedStart.Location, ProjectedDestination.Location);
		const FNavPathQueryDelegate Delegate = FNavPathQueryDelegate::CreateUObject(this, &UMassUnitNavigationSystem::HandlePathRequestComplete);
		const uint32 PathId = NavigationSystem->FindPathAsync(FNavAgentProperties::DefaultProperties, MoveTemp(Query), Delegate);
		if (PathId != INVALID_NAVQUERYID)
		{
			PendingPathOwners.Add(PathId, Request.Entity);
		}
		else if (bUseDirectFallback)
		{
			SetDirectPath(Request.Entity, Request.Destination, Request.AcceptanceRadius);
		}
		else
		{
			MarkPathFailed(Request.Entity);
		}
	}
}

void UMassUnitNavigationSystem::HandlePathRequestComplete(uint32 PathId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path)
{
	FMassUnitEntityHandle Entity;
	if (!PendingPathOwners.RemoveAndCopyValue(PathId, Entity) || !IsEntityValid(Entity))
	{
		return;
	}
	if (Result == ENavigationQueryResult::Success && Path.IsValid())
	{
		UpdateEntityWithPath(Entity, Path);
		return;
	}

	FMassUnitNavigationFragment* Navigation = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitNavigationFragment>(Entity.ToMassEntityHandle());
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (Navigation && (!Settings || Settings->bFallbackToDirectPath))
	{
		SetDirectPath(Entity, Navigation->DestinationLocation, Navigation->AcceptanceRadius);
	}
	else if (Navigation)
	{
		MarkPathFailed(Entity);
	}
}

void UMassUnitNavigationSystem::UpdateEntityWithPath(FMassUnitEntityHandle Entity, const FNavPathSharedPtr& Path)
{
	if (!IsEntityValid(Entity) || !Path.IsValid())
	{
		return;
	}
	FMassUnitNavigationFragment* Navigation = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitNavigationFragment>(Entity.ToMassEntityHandle());
	if (!Navigation)
	{
		return;
	}

	Navigation->PathPoints.Reset();
	for (const FNavPathPoint& Point : Path->GetPathPoints())
	{
		Navigation->PathPoints.Add(Point.Location);
	}
	Navigation->CurrentPathIndex = Navigation->PathPoints.IsEmpty() ? INDEX_NONE : 0;
	Navigation->bPathRequested = false;
	Navigation->bPathValid = !Navigation->PathPoints.IsEmpty();
	Navigation->bPathUsesNavmesh = Navigation->bPathValid;
	if (Navigation->bPathValid)
	{
		Navigation->DestinationLocation = Navigation->PathPoints.Last();
		if (FMassUnitTargetFragment* Target = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitTargetFragment>(Entity.ToMassEntityHandle()))
		{
			Target->TargetLocation = Navigation->DestinationLocation;
			Target->bHasTargetLocation = true;
		}
	}
}

bool UMassUnitNavigationSystem::SetDirectPath(FMassUnitEntityHandle Entity, const FVector& Destination, float AcceptanceRadius)
{
	if (!IsEntityValid(Entity))
	{
		return false;
	}
	FMassUnitNavigationFragment* Navigation = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitNavigationFragment>(Entity.ToMassEntityHandle());
	if (!Navigation)
	{
		return false;
	}
	Navigation->DestinationLocation = Destination;
	Navigation->AcceptanceRadius = FMath::Max(1.0f, AcceptanceRadius);
	Navigation->PathPoints = {Destination};
	Navigation->CurrentPathIndex = 0;
	Navigation->bPathRequested = false;
	Navigation->bPathValid = true;
	Navigation->bPathUsesNavmesh = false;
	return true;
}

void UMassUnitNavigationSystem::MarkPathFailed(FMassUnitEntityHandle Entity)
{
	if (!IsEntityValid(Entity))
	{
		return;
	}
	if (FMassUnitNavigationFragment* Navigation = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitNavigationFragment>(Entity.ToMassEntityHandle()))
	{
		Navigation->PathPoints.Reset();
		Navigation->CurrentPathIndex = INDEX_NONE;
		Navigation->bPathRequested = false;
		Navigation->bPathValid = false;
		Navigation->bPathUsesNavmesh = false;
	}
}

bool UMassUnitNavigationSystem::IsEntityValid(FMassUnitEntityHandle Entity) const
{
	return EntitySubsystem && Entity.IsValid()
		&& EntitySubsystem->GetEntityManager().IsEntityValid(Entity.ToMassEntityHandle());
}
