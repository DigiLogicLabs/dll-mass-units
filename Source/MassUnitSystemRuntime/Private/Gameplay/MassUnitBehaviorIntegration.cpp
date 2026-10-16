// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Gameplay/MassUnitBehaviorIntegration.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "Gameplay/GASUnitIntegration.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"

void UMassUnitBehaviorIntegration::Initialize(UGASUnitIntegration* InGASIntegration)
{
	GASIntegration = InGASIntegration;
}

void UMassUnitBehaviorIntegration::Deinitialize()
{
	const TArray<FMassUnitEntityHandle> Entities = [&]()
	{
		TArray<FMassUnitEntityHandle> Result;
		EntityBTMap.GetKeys(Result);
		return Result;
	}();
	for (const FMassUnitEntityHandle Entity : Entities)
	{
		RemoveBehaviorTreeForEntity(Entity);
	}
	EntityBTMap.Reset();
	EntityBBMap.Reset();
	GASIntegration = nullptr;
}

void UMassUnitBehaviorIntegration::Tick(float DeltaTime)
{
	TArray<FMassUnitEntityHandle> InvalidEntities;
	for (const TPair<FMassUnitEntityHandle, TObjectPtr<UBehaviorTreeComponent>>& Pair : EntityBTMap)
	{
		if (!IsEntityValid(Pair.Key))
		{
			InvalidEntities.Add(Pair.Key);
			continue;
		}
		UpdateEntityFromBlackboard(Pair.Key);
		UpdateBlackboardFromEntity(Pair.Key);
	}
	for (const FMassUnitEntityHandle Entity : InvalidEntities)
	{
		RemoveBehaviorTreeForEntity(Entity);
	}
}

bool UMassUnitBehaviorIntegration::SetBehaviorTree(FMassUnitHandle UnitHandle, UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardData)
{
	return SetBehaviorTreeInternal(UnitHandle.EntityHandle, BehaviorTree, BlackboardData);
}

bool UMassUnitBehaviorIntegration::SetBehaviorTreeInternal(FMassUnitEntityHandle Entity, UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardData)
{
	if (!IsEntityValid(Entity) || !BehaviorTree || !BlackboardData)
	{
		return false;
	}
	RemoveBehaviorTreeForEntity(Entity);
	UBehaviorTreeComponent* Component = CreateBehaviorTreeForEntity(Entity, BehaviorTree, BlackboardData);
	if (!Component)
	{
		return false;
	}
	Component->StartTree(*BehaviorTree);
	return true;
}

bool UMassUnitBehaviorIntegration::ExecuteBTTask(FMassUnitHandle UnitHandle, FGameplayTag TaskTag)
{
	return ExecuteBTTaskInternal(UnitHandle.EntityHandle, TaskTag);
}

bool UMassUnitBehaviorIntegration::ExecuteBTTaskInternal(FMassUnitEntityHandle Entity, FGameplayTag TaskTag)
{
	UBlackboardComponent* Blackboard = EntityBBMap.FindRef(Entity);
	if (!Blackboard || !TaskTag.IsValid() || Blackboard->GetKeyID(TEXT("RequestedTask")) == FBlackboard::InvalidKey)
	{
		return false;
	}
	Blackboard->SetValueAsName(TEXT("RequestedTask"), TaskTag.GetTagName());
	return true;
}

UBehaviorTreeComponent* UMassUnitBehaviorIntegration::CreateBehaviorTreeForEntity(FMassUnitEntityHandle Entity, UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardData)
{
	UMassEntitySubsystem* EntitySubsystem = GASIntegration ? GASIntegration->GetEntitySubsystem() : nullptr;
	UWorld* World = EntitySubsystem ? EntitySubsystem->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	UBlackboardComponent* Blackboard = NewObject<UBlackboardComponent>(World);
	if (!Blackboard)
	{
		return nullptr;
	}
	Blackboard->RegisterComponentWithWorld(World);
	if (!Blackboard->InitializeBlackboard(*BlackboardData))
	{
		Blackboard->DestroyComponent();
		return nullptr;
	}

	UBehaviorTreeComponent* BehaviorComponent = NewObject<UBehaviorTreeComponent>(World);
	if (!BehaviorComponent)
	{
		Blackboard->DestroyComponent();
		return nullptr;
	}
	BehaviorComponent->RegisterComponentWithWorld(World);
	BehaviorComponent->CacheBlackboardComponent(Blackboard);
	EntityBBMap.Add(Entity, Blackboard);
	EntityBTMap.Add(Entity, BehaviorComponent);
	UpdateBlackboardFromEntity(Entity);
	return BehaviorComponent;
}

void UMassUnitBehaviorIntegration::RemoveBehaviorTreeForEntity(FMassUnitEntityHandle Entity)
{
	if (UBehaviorTreeComponent* BehaviorComponent = EntityBTMap.FindRef(Entity))
	{
		BehaviorComponent->StopTree(EBTStopMode::Safe);
		BehaviorComponent->DestroyComponent();
	}
	if (UBlackboardComponent* Blackboard = EntityBBMap.FindRef(Entity))
	{
		Blackboard->DestroyComponent();
	}
	EntityBTMap.Remove(Entity);
	EntityBBMap.Remove(Entity);
}

void UMassUnitBehaviorIntegration::UpdateBlackboardFromEntity(FMassUnitEntityHandle Entity)
{
	UBlackboardComponent* Blackboard = EntityBBMap.FindRef(Entity);
	UMassEntitySubsystem* EntitySubsystem = GASIntegration ? GASIntegration->GetEntitySubsystem() : nullptr;
	if (!Blackboard || !EntitySubsystem || !IsEntityValid(Entity))
	{
		return;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
	if (const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle))
	{
		if (Blackboard->GetKeyID(TEXT("Position")) != FBlackboard::InvalidKey)
		{
			Blackboard->SetValueAsVector(TEXT("Position"), Transform->GetTransform().GetLocation());
		}
	}
	if (const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle))
	{
		if (Blackboard->GetKeyID(TEXT("State")) != FBlackboard::InvalidKey) Blackboard->SetValueAsEnum(TEXT("State"), static_cast<uint8>(State->CurrentState));
		if (Blackboard->GetKeyID(TEXT("StateTime")) != FBlackboard::InvalidKey) Blackboard->SetValueAsFloat(TEXT("StateTime"), State->StateTime);
		if (Blackboard->GetKeyID(TEXT("UnitLevel")) != FBlackboard::InvalidKey) Blackboard->SetValueAsInt(TEXT("UnitLevel"), State->UnitLevel);
		if (Blackboard->GetKeyID(TEXT("Health")) != FBlackboard::InvalidKey) Blackboard->SetValueAsFloat(TEXT("Health"), State->Health);
	}
	if (const FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle))
	{
		if (Blackboard->GetKeyID(TEXT("TargetLocation")) != FBlackboard::InvalidKey) Blackboard->SetValueAsVector(TEXT("TargetLocation"), Target->TargetLocation);
		if (Blackboard->GetKeyID(TEXT("TargetPriority")) != FBlackboard::InvalidKey) Blackboard->SetValueAsFloat(TEXT("TargetPriority"), Target->TargetPriority);
		if (Blackboard->GetKeyID(TEXT("HasTarget")) != FBlackboard::InvalidKey) Blackboard->SetValueAsBool(TEXT("HasTarget"), Target->HasTarget());
	}
	if (const FMassUnitTeamFragment* Team = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(NativeHandle))
	{
		if (Blackboard->GetKeyID(TEXT("TeamID")) != FBlackboard::InvalidKey) Blackboard->SetValueAsInt(TEXT("TeamID"), Team->TeamID);
	}
}

void UMassUnitBehaviorIntegration::UpdateEntityFromBlackboard(FMassUnitEntityHandle Entity)
{
	UBlackboardComponent* Blackboard = EntityBBMap.FindRef(Entity);
	UMassEntitySubsystem* EntitySubsystem = GASIntegration ? GASIntegration->GetEntitySubsystem() : nullptr;
	if (!Blackboard || !EntitySubsystem || !IsEntityValid(Entity))
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
	if (FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle))
	{
		if (Blackboard->GetKeyID(TEXT("ClearTarget")) != FBlackboard::InvalidKey && Blackboard->GetValueAsBool(TEXT("ClearTarget")))
		{
			Target->Clear();
			Blackboard->SetValueAsBool(TEXT("ClearTarget"), false);
		}
		else if (Blackboard->GetKeyID(TEXT("HasRequestedTarget")) != FBlackboard::InvalidKey
			&& Blackboard->GetValueAsBool(TEXT("HasRequestedTarget"))
			&& Blackboard->GetKeyID(TEXT("RequestedTargetLocation")) != FBlackboard::InvalidKey)
		{
			Target->TargetEntity.Invalidate();
			Target->TargetLocation = Blackboard->GetValueAsVector(TEXT("RequestedTargetLocation"));
			Target->bHasTargetLocation = true;
			if (Blackboard->GetKeyID(TEXT("RequestedTargetPriority")) != FBlackboard::InvalidKey)
			{
				Target->TargetPriority = Blackboard->GetValueAsFloat(TEXT("RequestedTargetPriority"));
			}
		}
	}
	if (FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle))
	{
		if (Blackboard->GetKeyID(TEXT("RequestedState")) != FBlackboard::InvalidKey)
		{
			State->CurrentState = static_cast<EMassUnitState>(Blackboard->GetValueAsEnum(TEXT("RequestedState")));
		}
	}
}

bool UMassUnitBehaviorIntegration::IsEntityValid(FMassUnitEntityHandle Entity) const
{
	const UMassEntitySubsystem* EntitySubsystem = GASIntegration ? GASIntegration->GetEntitySubsystem() : nullptr;
	return EntitySubsystem && Entity.IsValid()
		&& EntitySubsystem->GetEntityManager().IsEntityValid(Entity.ToMassEntityHandle());
}
