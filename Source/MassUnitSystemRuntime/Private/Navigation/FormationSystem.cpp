// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Navigation/FormationSystem.h"

#include "Core/MassUnitSystemRuntime.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"

namespace
{
	bool IsSupportedFormationShape(const FName Shape)
	{
		return Shape == TEXT("Rectangle") || Shape == TEXT("Line") || Shape == TEXT("Column")
			|| Shape == TEXT("Wedge") || Shape == TEXT("Circle");
	}
}

void UFormationSystem::Initialize(UMassEntitySubsystem* InEntitySubsystem)
{
	EntitySubsystem = InEntitySubsystem;
}

void UFormationSystem::Deinitialize()
{
	Formations.Reset();
	EntityFormationMap.Reset();
	EntitySubsystem = nullptr;
}

void UFormationSystem::Tick(float DeltaTime)
{
	UpdateFormationMovement(DeltaTime);
	UpdateFormationPositions();
}

int32 UFormationSystem::CreateFormation(FVector Location, FRotator Rotation, FName FormationType)
{
	FFormationData Data;
	Data.Location = Location;
	Data.Rotation = Rotation;
	Data.TargetLocation = Location;
	Data.FormationType = FormationType;

	if (FormationType == TEXT("Cavalry"))
	{
		Data.FormationShape = TEXT("Wedge");
		Data.FormationWidth = 800.0f;
		Data.FormationDepth = 800.0f;
		Data.UnitSpacing = 200.0f;
	}
	else if (FormationType == TEXT("Archers"))
	{
		Data.FormationShape = TEXT("Line");
		Data.FormationWidth = 1200.0f;
		Data.FormationDepth = 300.0f;
		Data.UnitSpacing = 180.0f;
	}

	const int32 Handle = NextFormationHandle++;
	Formations.Add(Handle, MoveTemp(Data));
	return Handle;
}

bool UFormationSystem::DestroyFormation(int32 FormationHandle)
{
	FFormationData* Formation = Formations.Find(FormationHandle);
	if (!Formation)
	{
		return false;
	}
	const TArray<FMassUnitEntityHandle> Units = Formation->Entities;
	for (const FMassUnitEntityHandle Entity : Units)
	{
		EntityFormationMap.Remove(Entity);
		UpdateEntityFormationData(Entity, INDEX_NONE, INDEX_NONE);
	}
	Formations.Remove(FormationHandle);
	return true;
}

bool UFormationSystem::AddUnitToFormation(FMassUnitHandle UnitHandle, int32 FormationHandle)
{
	return AddEntityToFormation(UnitHandle.EntityHandle, FormationHandle);
}

bool UFormationSystem::RemoveUnitFromFormation(FMassUnitHandle UnitHandle)
{
	return RemoveEntityFromFormation(UnitHandle.EntityHandle);
}

bool UFormationSystem::AddEntityToFormation(FMassUnitEntityHandle Entity, int32 FormationHandle)
{
	if (!IsEntityValid(Entity))
	{
		return false;
	}
	FFormationData* Formation = Formations.Find(FormationHandle);
	if (!Formation)
	{
		return false;
	}
	if (EntityFormationMap.Contains(Entity))
	{
		RemoveEntityFromFormation(Entity);
	}

	const int32 SlotIndex = Formation->Entities.Num();
	Formation->Entities.Add(Entity);
	Formation->EntitySlots.Add(Entity, SlotIndex);
	EntityFormationMap.Add(Entity, FormationHandle);
	UpdateEntityFormationData(Entity, FormationHandle, SlotIndex);
	return true;
}

bool UFormationSystem::RemoveEntityFromFormation(FMassUnitEntityHandle Entity)
{
	const int32* HandlePtr = EntityFormationMap.Find(Entity);
	if (!HandlePtr)
	{
		return false;
	}
	const int32 FormationHandle = *HandlePtr;
	FFormationData* Formation = Formations.Find(FormationHandle);
	EntityFormationMap.Remove(Entity);
	if (!Formation)
	{
		return false;
	}

	Formation->Entities.Remove(Entity);
	Formation->EntitySlots.Reset();
	for (int32 Index = 0; Index < Formation->Entities.Num(); ++Index)
	{
		Formation->EntitySlots.Add(Formation->Entities[Index], Index);
		UpdateEntityFormationData(Formation->Entities[Index], FormationHandle, Index);
	}
	UpdateEntityFormationData(Entity, INDEX_NONE, INDEX_NONE);
	return true;
}

bool UFormationSystem::AddEntityToFormationInternal(FMassUnitEntityHandle Entity, int32 FormationHandle)
{
	return AddEntityToFormation(Entity, FormationHandle);
}

bool UFormationSystem::RemoveEntityFromFormationInternal(FMassUnitEntityHandle Entity)
{
	return RemoveEntityFromFormation(Entity);
}

bool UFormationSystem::SetFormationTarget(int32 FormationHandle, FVector TargetLocation)
{
	if (FFormationData* Formation = Formations.Find(FormationHandle))
	{
		Formation->TargetLocation = TargetLocation;
		Formation->bIsMoving = !Formation->Location.Equals(TargetLocation, 1.0f);
		return true;
	}
	return false;
}

bool UFormationSystem::SetFormationShape(int32 FormationHandle, FName FormationShape)
{
	if (!IsSupportedFormationShape(FormationShape))
	{
		return false;
	}
	if (FFormationData* Formation = Formations.Find(FormationHandle))
	{
		Formation->FormationShape = FormationShape;
		for (int32 Index = 0; Index < Formation->Entities.Num(); ++Index)
		{
			UpdateEntityFormationData(Formation->Entities[Index], FormationHandle, Index);
		}
		return true;
	}
	return false;
}

FVector UFormationSystem::GetFormationLocation(int32 FormationHandle) const
{
	if (const FFormationData* Formation = Formations.Find(FormationHandle))
	{
		return Formation->Location;
	}
	return FVector::ZeroVector;
}

FRotator UFormationSystem::GetFormationRotation(int32 FormationHandle) const
{
	if (const FFormationData* Formation = Formations.Find(FormationHandle))
	{
		return Formation->Rotation;
	}
	return FRotator::ZeroRotator;
}

TArray<FMassUnitEntityHandle> UFormationSystem::GetEntitiesInFormation(int32 FormationHandle) const
{
	if (const FFormationData* Formation = Formations.Find(FormationHandle))
	{
		return Formation->Entities;
	}
	return {};
}

TArray<FMassUnitEntityHandle> UFormationSystem::GetEntitiesInFormationInternal(int32 FormationHandle) const
{
	return GetEntitiesInFormation(FormationHandle);
}

void UFormationSystem::UpdateFormationMovement(float DeltaTime)
{
	for (TPair<int32, FFormationData>& Pair : Formations)
	{
		FFormationData& Formation = Pair.Value;
		if (!Formation.bIsMoving)
		{
			continue;
		}
		const FVector Previous = Formation.Location;
		Formation.Location = FMath::VInterpConstantTo(Formation.Location, Formation.TargetLocation, DeltaTime, Formation.MoveSpeed);
		const FVector Direction = Formation.Location - Previous;
		if (!Direction.IsNearlyZero())
		{
			Formation.Rotation = Direction.Rotation();
		}
		Formation.bIsMoving = !Formation.Location.Equals(Formation.TargetLocation, 1.0f);
	}
}

void UFormationSystem::UpdateFormationPositions()
{
	for (TPair<int32, FFormationData>& Pair : Formations)
	{
		FFormationData& Formation = Pair.Value;
		for (int32 Index = Formation.Entities.Num() - 1; Index >= 0; --Index)
		{
			const FMassUnitEntityHandle Entity = Formation.Entities[Index];
			if (!IsEntityValid(Entity))
			{
				EntityFormationMap.Remove(Entity);
				Formation.EntitySlots.Remove(Entity);
				Formation.Entities.RemoveAt(Index);
			}
		}
		for (int32 Index = 0; Index < Formation.Entities.Num(); ++Index)
		{
			Formation.EntitySlots.FindOrAdd(Formation.Entities[Index]) = Index;
			UpdateEntityFormationData(Formation.Entities[Index], Pair.Key, Index);
		}
	}
}

FVector UFormationSystem::CalculateSlotPosition(const FFormationData& Formation, int32 SlotIndex) const
{
	const int32 UnitCount = FMath::Max(1, Formation.Entities.Num());
	if (Formation.FormationShape == TEXT("Line"))
	{
		return FVector(0.0f, (SlotIndex - (UnitCount - 1) * 0.5f) * Formation.UnitSpacing, 0.0f);
	}
	if (Formation.FormationShape == TEXT("Column"))
	{
		return FVector(-SlotIndex * Formation.UnitSpacing, 0.0f, 0.0f);
	}
	if (Formation.FormationShape == TEXT("Wedge"))
	{
		const int32 Row = FMath::FloorToInt((FMath::Sqrt(8.0f * SlotIndex + 1.0f) - 1.0f) * 0.5f);
		const int32 RowStart = Row * (Row + 1) / 2;
		const int32 Column = SlotIndex - RowStart;
		return FVector(-Row * Formation.UnitSpacing, (Column - Row * 0.5f) * Formation.UnitSpacing, 0.0f);
	}
	if (Formation.FormationShape == TEXT("Circle"))
	{
		const float Angle = 2.0f * PI * static_cast<float>(SlotIndex) / UnitCount;
		const float Radius = FMath::Max(Formation.UnitSpacing, Formation.UnitSpacing * UnitCount / (2.0f * PI));
		return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
	}

	const int32 UnitsPerRow = FMath::Max(1, FMath::FloorToInt(Formation.FormationWidth / Formation.UnitSpacing));
	const int32 Row = SlotIndex / UnitsPerRow;
	const int32 Column = SlotIndex % UnitsPerRow;
	return FVector(-Row * Formation.UnitSpacing, (Column - (UnitsPerRow - 1) * 0.5f) * Formation.UnitSpacing, 0.0f);
}

void UFormationSystem::UpdateEntityFormationData(FMassUnitEntityHandle Entity, int32 FormationHandle, int32 SlotIndex)
{
	if (!IsEntityValid(Entity))
	{
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
	FMassUnitFormationFragment* FormationFragment = EntityManager.GetFragmentDataPtr<FMassUnitFormationFragment>(NativeHandle);
	FMassUnitTargetFragment* TargetFragment = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle);
	FMassUnitNavigationFragment* NavigationFragment = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(NativeHandle);
	if (!FormationFragment)
	{
		return;
	}

	if (FormationHandle == INDEX_NONE)
	{
		FormationFragment->FormationHandle = INDEX_NONE;
		FormationFragment->FormationSlot = INDEX_NONE;
		FormationFragment->FormationOffset = FVector::ZeroVector;
		if (TargetFragment && !TargetFragment->TargetEntity.IsValid())
		{
			TargetFragment->Clear();
		}
		return;
	}

	const FFormationData* Formation = Formations.Find(FormationHandle);
	if (!Formation)
	{
		return;
	}
	const FVector Offset = CalculateSlotPosition(*Formation, SlotIndex);
	FormationFragment->FormationHandle = FormationHandle;
	FormationFragment->FormationSlot = SlotIndex;
	FormationFragment->FormationOffset = Offset;
	if (TargetFragment)
	{
		TargetFragment->TargetEntity.Invalidate();
		TargetFragment->TargetLocation = Formation->Location + Formation->Rotation.RotateVector(Offset);
		TargetFragment->bHasTargetLocation = true;
	}
	if (NavigationFragment)
	{
		NavigationFragment->ResetPath();
	}
}

bool UFormationSystem::IsEntityValid(FMassUnitEntityHandle Entity) const
{
	return EntitySubsystem && Entity.IsValid()
		&& EntitySubsystem->GetEntityManager().IsEntityValid(Entity.ToMassEntityHandle());
}
