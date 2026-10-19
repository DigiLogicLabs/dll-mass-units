// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Gameplay/UnitGameplayEventSystem.h"
#include "Core/MassUnitSystemRuntime.h"
#include "GameplayEffectTypes.h"

UUnitGameplayEventSystem::UUnitGameplayEventSystem()
{
}

UUnitGameplayEventSystem::~UUnitGameplayEventSystem()
{
}

void UUnitGameplayEventSystem::Initialize()
{
    UE_LOG(LogMassUnitSystem, Verbose, TEXT("Unit gameplay event system initialized"));
}

void UUnitGameplayEventSystem::Deinitialize()
{
    // Clear all listeners
    EventListeners.Empty();
    
    UE_LOG(LogMassUnitSystem, Verbose, TEXT("Unit gameplay event system deinitialized"));
}

void UUnitGameplayEventSystem::DispatchEvent(FGameplayTag EventTag, FMassUnitHandle UnitHandle, const FGameplayEventData& EventData)
{
    // Convert UnitHandle to FMassUnitEntityHandle using direct member access
    FMassUnitEntityHandle Entity = UnitHandle.EntityHandle;
    DispatchEventInternal(EventTag, Entity, EventData);
}

void UUnitGameplayEventSystem::DispatchEventInternal(FGameplayTag EventTag, FMassUnitEntityHandle Entity, const FGameplayEventData& EventData)
{
    if (!EventTag.IsValid())
    {
        return;
    }
    FOnGameplayEvent* Listeners = EventListeners.Find(EventTag);
    if (Listeners)
    {
        Listeners->Broadcast(EventTag, Entity, EventData);
        UE_LOG(LogMassUnitSystem, Verbose, TEXT("Dispatched %s for %s"), *EventTag.ToString(), *Entity.ToString());
    }
}

FDelegateHandle UUnitGameplayEventSystem::RegisterListener(FGameplayTag EventTag, FOnGameplayEvent::FDelegate Listener)
{
    if (!EventTag.IsValid() || !Listener.IsBound())
    {
        return {};
    }
    FOnGameplayEvent& Listeners = EventListeners.FindOrAdd(EventTag);
    return Listeners.Add(MoveTemp(Listener));
}

void UUnitGameplayEventSystem::UnregisterListener(FGameplayTag EventTag, FDelegateHandle ListenerHandle)
{
    if (!EventTag.IsValid())
    {
        return;
    }
    FOnGameplayEvent* Listeners = EventListeners.Find(EventTag);
    if (Listeners)
    {
        Listeners->Remove(ListenerHandle);
        if (!Listeners->IsBound())
        {
            EventListeners.Remove(EventTag);
        }
    }
}
