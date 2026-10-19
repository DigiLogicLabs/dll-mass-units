// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Visual/NiagaraUnitSystem.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitSystemRuntime.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassUnitCommonFragments.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Visual/CrowdInstanceRenderer.h"
#include "Visual/VertexAnimationManager.h"

void UNiagaraUnitSystem::Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem)
{
	World = InWorld;
	EntitySubsystem = InEntitySubsystem;
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (Settings)
	{
		MaxUnits = FMath::Max(1, Settings->MaxUnits);
		UpdateFrequency = FMath::Max(0.0f, Settings->VisualUpdateInterval);
		bEnableInstancedFallback = Settings->bEnableInstancedMeshFallback;
		NiagaraSystemAsset = Settings->DefaultNiagaraSystem.LoadSynchronous();
		FallbackStaticMesh = Settings->FallbackStaticMesh.LoadSynchronous();
	}
	if (!FallbackStaticMesh && bEnableInstancedFallback)
	{
		FallbackStaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	CreateNiagaraSystem();
	VertexAnimationManager = NewObject<UVertexAnimationManager>(this);
	VertexAnimationManager->Initialize();

	// The instanced renderer is created even when Niagara is active, so a project can switch
	// representation at runtime without a subsystem restart. It costs nothing while unused.
	CrowdInstanceRenderer = NewObject<UCrowdInstanceRenderer>(this);
	CrowdInstanceRenderer->Initialize(World, EntitySubsystem, FallbackStaticMesh);
	CrowdInstanceRenderer->SetMaxUnits(MaxUnits);
}

void UNiagaraUnitSystem::Deinitialize()
{
	if (NiagaraComponent)
	{
		NiagaraComponent->DestroyComponent();
	}
	if (CrowdInstanceRenderer)
	{
		CrowdInstanceRenderer->Deinitialize();
	}
	if (VertexAnimationManager)
	{
		VertexAnimationManager->Deinitialize();
	}
	CrowdInstanceRenderer = nullptr;
	VertexAnimationManager = nullptr;
	NiagaraComponent = nullptr;
	NiagaraSystemAsset = nullptr;
	FallbackStaticMesh = nullptr;
	EntitySubsystem = nullptr;
	World = nullptr;
}

void UNiagaraUnitSystem::UpdateUnitVisualsByHandles(const TArray<FMassUnitHandle>& UnitHandles)
{
	TArray<FMassUnitEntityHandle> Entities;
	Entities.Reserve(UnitHandles.Num());
	for (const FMassUnitHandle& Handle : UnitHandles)
	{
		Entities.Add(Handle.EntityHandle);
	}
	UpdateUnitVisuals(Entities);
}

void UNiagaraUnitSystem::UpdateUnitVisuals(const TArray<FMassUnitEntityHandle>& Entities)
{
	if (!World || !EntitySubsystem)
	{
		return;
	}
	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastUpdateTime < UpdateFrequency)
	{
		return;
	}
	LastUpdateTime = CurrentTime;
	if (NiagaraComponent)
	{
		UpdateNiagaraData(Entities);
	}
	else if (bEnableInstancedFallback)
	{
		UpdateInstancedMeshData(Entities);
	}
}

void UNiagaraUnitSystem::SetLODLevel(int32 LODLevel)
{
	CurrentLODLevel = FMath::Max(0, LODLevel);
	if (NiagaraComponent)
	{
		NiagaraComponent->SetIntParameter(TEXT("LODLevel"), CurrentLODLevel);
	}
}

int32 UNiagaraUnitSystem::GetInstancedMeshComponentCount() const
{
	return CrowdInstanceRenderer ? CrowdInstanceRenderer->GetComponentCount() : 0;
}

int32 UNiagaraUnitSystem::GetInstancedMeshInstanceCount() const
{
	return CrowdInstanceRenderer ? CrowdInstanceRenderer->GetLiveInstanceCount() : 0;
}

int32 UNiagaraUnitSystem::GetInstancedMeshAllocatedSlotCount() const
{
	return CrowdInstanceRenderer ? CrowdInstanceRenderer->GetAllocatedSlotCount() : 0;
}

int32 UNiagaraUnitSystem::GetInstancedMeshTopologyRevision() const
{
	return CrowdInstanceRenderer ? CrowdInstanceRenderer->GetTopologyRevision() : 0;
}

int32 UNiagaraUnitSystem::GetInstancedMeshCustomDataFloatCount() const
{
	return UCrowdInstanceRenderer::GetCustomDataFloatCount();
}

void UNiagaraUnitSystem::CreateNiagaraSystem()
{
	if (!World || !NiagaraSystemAsset)
	{
		if (!NiagaraSystemAsset && bEnableInstancedFallback)
		{
			UE_LOG(LogMassUnitSystem, Log, TEXT("No default Niagara asset configured; using instanced static mesh rendering"));
		}
		return;
	}
	NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World,
		NiagaraSystemAsset,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		FVector::OneVector,
		true,
		false,
		ENCPoolMethod::None,
		true);
	if (NiagaraComponent)
	{
		NiagaraComponent->SetIntParameter(TEXT("MaxUnits"), MaxUnits);
		NiagaraComponent->SetIntParameter(TEXT("LODLevel"), CurrentLODLevel);
	}
}

void UNiagaraUnitSystem::UpdateNiagaraData(const TArray<FMassUnitEntityHandle>& Entities)
{
	if (!NiagaraComponent || !EntitySubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	TArray<FVector> Positions;
	TArray<FVector> Velocities;
	TArray<FVector> Scales;
	TArray<FQuat> Rotations;
	TArray<FVector> TeamColors;
	TArray<float> TeamIDs;
	TArray<float> AnimationIndices;
	TArray<float> AnimationTimes;
	TArray<float> LODLevels;
	TArray<float> VisibilityFlags;
	const int32 Count = FMath::Min(MaxUnits, Entities.Num());
	Positions.Reserve(Count);
	Velocities.Reserve(Count);
	Scales.Reserve(Count);
	Rotations.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FMassEntityHandle NativeHandle = Entities[Index].ToMassEntityHandle();
		if (!EntityManager.IsEntityValid(NativeHandle))
		{
			continue;
		}
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
		const FMassUnitVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(NativeHandle);
		const FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle);
		const FMassUnitTeamFragment* Team = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(NativeHandle);
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		if (!Transform || !Velocity || !Visual || !Team || !State || !Visual->bIsVisible || Visual->bUseSkeletalMesh)
		{
			continue;
		}

		const FTransform& UnitTransform = Transform->GetTransform();
		Positions.Add(UnitTransform.GetLocation());
		Velocities.Add(Velocity->Value);
		Scales.Add(UnitTransform.GetScale3D());
		Rotations.Add(UnitTransform.GetRotation());
		TeamColors.Add(FVector(Team->TeamColor.R, Team->TeamColor.G, Team->TeamColor.B));
		TeamIDs.Add(static_cast<float>(Team->TeamID));
		AnimationIndices.Add(static_cast<float>(ResolveAnimationIndex(*Visual, *State)));
		AnimationTimes.Add(State->StateTime);
		LODLevels.Add(static_cast<float>(Visual->LODLevel));
		VisibilityFlags.Add(1.0f);
	}

	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, TEXT("UnitPositions"), Positions);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, TEXT("UnitVelocities"), Velocities);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, TEXT("UnitScales"), Scales);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(NiagaraComponent, TEXT("UnitRotations"), Rotations);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(NiagaraComponent, TEXT("UnitTeamColors"), TeamColors);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(NiagaraComponent, TEXT("UnitTeamIDs"), TeamIDs);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(NiagaraComponent, TEXT("UnitAnimationIndices"), AnimationIndices);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(NiagaraComponent, TEXT("UnitAnimationTimes"), AnimationTimes);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(NiagaraComponent, TEXT("UnitLODLevels"), LODLevels);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(NiagaraComponent, TEXT("UnitVisibilityFlags"), VisibilityFlags);
	NiagaraComponent->SetIntParameter(TEXT("UnitCount"), Positions.Num());
}

void UNiagaraUnitSystem::UpdateInstancedMeshData(const TArray<FMassUnitEntityHandle>& Entities)
{
	if (!CrowdInstanceRenderer)
	{
		return;
	}
	CrowdInstanceRenderer->UpdateInstances(
		Entities,
		[this](const FMassUnitVisualFragment& Visual, const FMassUnitStateFragment& State)
		{
			return ResolveAnimationIndex(Visual, State);
		});
}

int32 UNiagaraUnitSystem::ResolveAnimationIndex(
	const FMassUnitVisualFragment& Visual,
	const FMassUnitStateFragment& State)
{
	if (!VertexAnimationManager)
	{
		return static_cast<int32>(State.CurrentState);
	}
	if (Visual.VertexAnimationTexture && Visual.CurrentAnimation.IsValid())
	{
		VertexAnimationManager->RegisterAnimationTexture(Visual.CurrentAnimation, Visual.VertexAnimationTexture);
	}
	const int32 AnimationIndex = VertexAnimationManager->GetAnimationIndex(Visual.CurrentAnimation);
	return AnimationIndex != INDEX_NONE ? AnimationIndex : static_cast<int32>(State.CurrentState);
}
