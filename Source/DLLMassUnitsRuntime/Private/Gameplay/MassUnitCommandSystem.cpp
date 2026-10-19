// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Gameplay/MassUnitCommandSystem.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitStats.h"
#include "Core/MassUnitSubsystem.h"
#include "Core/MassUnitSystemRuntime.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/MassUnitStackSystem.h"
#include "HAL/IConsoleManager.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassUnitVelocityFragment.h"
#include "Navigation/FormationSystem.h"
#include "Navigation/MassUnitNavigationSystem.h"
#include "NavigationSystem.h"

DECLARE_CYCLE_STAT(TEXT("Commands (orders)"), STAT_MassUnit_TickCommands, STATGROUP_MassUnitSystem);

namespace MassUnitCommand
{
	static int32 GCommandDebug = 0;
	static FAutoConsoleVariableRef CVarCommandDebug(
		TEXT("dll.MassUnits.CommandDebug"),
		GCommandDebug,
		TEXT("Draw the command table: yellow line + ring = walking to its slot, green ring = arrived / idle, white box = ")
		TEXT("holding, plus a HUD line with the command stats. 0 = off."),
		ECVF_Cheat);

	static UMassUnitCommandSystem* GetCommands(UWorld* World)
	{
		const UMassUnitSubsystem* Subsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
		return Subsystem ? Subsystem->GetCommandSystem() : nullptr;
	}

	static TArray<FMassUnitHandle> GetTeamUnits(UWorld* World, int32 TeamID)
	{
		const UMassUnitSubsystem* Subsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
		const UMassUnitEntityManager* Manager = Subsystem ? Subsystem->GetUnitManager() : nullptr;
		return Manager ? Manager->GetUnitsByTeam(TeamID) : TArray<FMassUnitHandle>();
	}

	static EMassUnitFormationShape ParseShape(const FString& Text)
	{
		if (Text.Equals(TEXT("Line"), ESearchCase::IgnoreCase)) { return EMassUnitFormationShape::Line; }
		if (Text.Equals(TEXT("Column"), ESearchCase::IgnoreCase)) { return EMassUnitFormationShape::Column; }
		if (Text.Equals(TEXT("Wedge"), ESearchCase::IgnoreCase)) { return EMassUnitFormationShape::Wedge; }
		if (Text.Equals(TEXT("Circle"), ESearchCase::IgnoreCase)) { return EMassUnitFormationShape::Circle; }
		return EMassUnitFormationShape::Box;
	}

	static FAutoConsoleCommandWithWorldAndArgs MoveTeamCmd(
		TEXT("dll.MassUnits.CommandMoveTeam"),
		TEXT("Order every unit of a team into a formation 800 cm in front of the first player pawn. ")
		TEXT("Args: <TeamID> [Box|Line|Column|Wedge|Circle] [Distance cm]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UMassUnitCommandSystem* Commands = GetCommands(World);
			const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (!Commands || !Pawn || Args.IsEmpty())
			{
				UE_LOG(LogMassUnitSystem, Display, TEXT("dll.MassUnits.CommandMoveTeam <TeamID> [Shape] [Distance]: needs a world, a player pawn and a team"));
				return;
			}
			const float Distance = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 800.0f;
			FMassUnitMoveOrder Order;
			Order.Destination = Pawn->GetActorLocation() + Pawn->GetActorForwardVector().GetSafeNormal2D() * Distance;
			Order.Facing = Pawn->GetActorForwardVector().GetSafeNormal2D();
			Order.Shape = Args.Num() > 1 ? ParseShape(Args[1]) : EMassUnitFormationShape::Box;
			const TArray<FMassUnitHandle> Units = GetTeamUnits(World, FCString::Atoi(*Args[0]));
			const int32 OrderId = Commands->IssueMoveOrder(Units, Order);
			UE_LOG(LogMassUnitSystem, Display, TEXT("dll.MassUnits.CommandMoveTeam: order %d for %d unit(s)"), OrderId, Units.Num());
		}),
		ECVF_Cheat);

	static FAutoConsoleCommandWithWorldAndArgs StopTeamCmd(
		TEXT("dll.MassUnits.CommandStopTeam"),
		TEXT("Stop (or with arg 2 = Hold) every unit of a team. Args: <TeamID> [Hold]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UMassUnitCommandSystem* Commands = GetCommands(World);
			if (!Commands || Args.IsEmpty())
			{
				return;
			}
			const TArray<FMassUnitHandle> Units = GetTeamUnits(World, FCString::Atoi(*Args[0]));
			const bool bHold = Args.Num() > 1 && Args[1].Equals(TEXT("Hold"), ESearchCase::IgnoreCase);
			const int32 Count = bHold ? Commands->IssueHoldOrder(Units) : Commands->IssueStopOrder(Units);
			UE_LOG(LogMassUnitSystem, Display, TEXT("dll.MassUnits.CommandStopTeam: %d unit(s) %s"), Count, bHold ? TEXT("holding") : TEXT("stopped"));
		}),
		ECVF_Cheat);

	static FAutoConsoleCommandWithWorldAndArgs ReleaseAllCmd(
		TEXT("dll.MassUnits.CommandReleaseAll"),
		TEXT("Take every commanded unit off orders (back to its crowd group)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UMassUnitCommandSystem* Commands = GetCommands(World))
			{
				UE_LOG(LogMassUnitSystem, Display, TEXT("dll.MassUnits.CommandReleaseAll: %d unit(s) released"), Commands->ReleaseAllUnits());
			}
		}),
		ECVF_Cheat);
}

void UMassUnitCommandSystem::Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, UMassUnitEntityManager* InUnitManager, UMassUnitNavigationSystem* InNavigationSystem, UMassUnitStackSystem* InStackSystem)
{
	World = InWorld;
	EntitySubsystem = InEntitySubsystem;
	UnitManager = InUnitManager;
	NavigationSystem = InNavigationSystem;
	StackSystem = InStackSystem;
}

void UMassUnitCommandSystem::Deinitialize()
{
	// World teardown: the Mass entity manager may already be gone, so fragments are not touched here.
	Entries.Reset();
	Orders.Reset();
	PendingFinished.Reset();
	World = nullptr;
	EntitySubsystem = nullptr;
	UnitManager = nullptr;
	NavigationSystem = nullptr;
	StackSystem = nullptr;
}

float UMassUnitCommandSystem::GetCurrentTime() const
{
	return World ? World->GetTimeSeconds() : 0.0f;
}

// ------------------------------------------------------------------------------------------------
// Formation layout
// ------------------------------------------------------------------------------------------------

TArray<FVector> UMassUnitCommandSystem::ComputeFormationSlots(int32 Count, FVector Center, FVector Facing, EMassUnitFormationShape Shape, float Spacing)
{
	TArray<FVector> Slots;
	if (Count <= 0)
	{
		return Slots;
	}
	Slots.Reserve(Count);
	const FVector Forward = Facing.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Facing.GetSafeNormal2D();
	const FVector Right(-Forward.Y, Forward.X, 0.0f);
	const float S = FMath::Max(1.0f, Spacing);

	// Rows of Width units, front row first, each row centred; the whole block is centred on Center.
	auto AddRows = [&](int32 Width)
	{
		Width = FMath::Clamp(Width, 1, Count);
		const int32 Rows = FMath::DivideAndRoundUp(Count, Width);
		const float FrontOffset = (Rows - 1) * 0.5f * S;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Row = Index / Width;
			const int32 InRow = Row < Rows - 1 ? Width : Count - Row * Width;
			const int32 Column = Index % Width;
			const float Across = (Column - (InRow - 1) * 0.5f) * S;
			Slots.Add(Center + Forward * (FrontOffset - Row * S) + Right * Across);
		}
	};

	switch (Shape)
	{
	case EMassUnitFormationShape::Line:
		AddRows(Count);
		break;
	case EMassUnitFormationShape::Column:
		AddRows(3);
		break;
	case EMassUnitFormationShape::Wedge:
	{
		// Row k holds k + 1 units; the point is at the front.
		int32 Rows = 0;
		while ((Rows + 1) * (Rows + 2) / 2 < Count)
		{
			++Rows;
		}
		const float FrontOffset = Rows * 0.5f * S;
		int32 Placed = 0;
		for (int32 Row = 0; Placed < Count; ++Row)
		{
			const int32 InRow = FMath::Min(Row + 1, Count - Placed);
			for (int32 Column = 0; Column < InRow; ++Column, ++Placed)
			{
				const float Across = (Column - (InRow - 1) * 0.5f) * S;
				Slots.Add(Center + Forward * (FrontOffset - Row * S) + Right * Across);
			}
		}
		break;
	}
	case EMassUnitFormationShape::Circle:
	{
		// One unit in the middle, then rings Spacing apart, each holding as many as fit around it.
		Slots.Add(Center);
		for (int32 Ring = 1; Slots.Num() < Count; ++Ring)
		{
			const float Radius = Ring * S;
			const int32 Capacity = FMath::Max(6, FMath::FloorToInt(2.0f * UE_PI * Radius / S));
			const int32 InRing = FMath::Min(Capacity, Count - Slots.Num());
			for (int32 Index = 0; Index < InRing; ++Index)
			{
				// Start at the front so a partial ring faces the move.
				const float Angle = 2.0f * UE_PI * Index / InRing;
				Slots.Add(Center + (Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * Radius);
			}
		}
		break;
	}
	case EMassUnitFormationShape::Box:
	default:
		AddRows(FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count))));
		break;
	}
	return Slots;
}

// ------------------------------------------------------------------------------------------------
// Orders
// ------------------------------------------------------------------------------------------------

UMassUnitCommandSystem::FCommandEntry* UMassUnitCommandSystem::TakeCommand(const FMassEntityHandle& Unit)
{
	if (!EntitySubsystem || !UnitManager)
	{
		return nullptr;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Unit))
	{
		return nullptr;
	}
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit);
	FCommandEntry* Entry = Entries.Find(Unit);
	const bool bAirborne = UnitManager->IsUnitAirborne(FMassUnitHandle(Unit));
	// A unit already under orders takes a new one mid-air (it applies once it lands); a new one must be on the ground.
	if (!Crowd || !State || State->CurrentState == EMassUnitState::Dead || (bAirborne && !Entry))
	{
		return nullptr;
	}
	// Off the pile first: a climber is set down, a ground support lets its riders fall.
	if (!bAirborne && StackSystem && StackSystem->IsUnitStacked(FMassUnitHandle(Unit)))
	{
		StackSystem->ReleaseStackedUnit(FMassUnitHandle(Unit), false);
	}
	// A UFormationSystem formation rewrites its members' targets every frame: orders take the unit out of it.
	if (const UMassUnitSubsystem* Subsystem = GetTypedOuter<UMassUnitSubsystem>())
	{
		if (UFormationSystem* Formations = Subsystem->GetFormationSystem())
		{
			Formations->RemoveEntityFromFormationInternal(FMassUnitEntityHandle(Unit));
		}
	}

	if (!Entry)
	{
		Entry = &Entries.Add(Unit);
		// The crowd's base speed when it has one (State->MoveSpeed may carry a wander speed roll).
		Entry->SavedMoveSpeed = Crowd->BaseMoveSpeed > 0.0f ? Crowd->BaseMoveSpeed : State->MoveSpeed;
	}
	Crowd->bCommanded = true;
	Crowd->bSleeping = false;
	Crowd->InteractionEndTime = 0.0f;
	Crowd->InteractionPartner.Invalidate();
	if (State->CurrentState == EMassUnitState::Interacting)
	{
		State->CurrentState = EMassUnitState::Idle;
		State->StateTime = 0.0f;
	}
	return Entry;
}

int32 UMassUnitCommandSystem::IssueMoveOrder(const TArray<FMassUnitHandle>& Units, const FMassUnitMoveOrder& Order)
{
	if (!EntitySubsystem || !UnitManager || Units.IsEmpty())
	{
		return INDEX_NONE;
	}
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	// Who can move: living, grounded units with the fragments orders need. Duplicates are dropped.
	struct FMover
	{
		FMassEntityHandle Handle;
		FVector Location;
	};
	TArray<FMover> Movers;
	Movers.Reserve(Units.Num());
	TSet<FMassEntityHandle> Seen;
	FVector Centroid = FVector::ZeroVector;
	for (const FMassUnitHandle& Unit : Units)
	{
		const FMassEntityHandle Handle = Unit.EntityHandle.ToMassEntityHandle();
		if (Seen.Contains(Handle) || !EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		Seen.Add(Handle);
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Handle);
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Handle);
		if (!Transform || !State || State->CurrentState == EMassUnitState::Dead
			|| (UnitManager->IsUnitAirborne(Unit) && !Entries.Contains(Handle))
			|| !EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Handle)
			|| !EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(Handle))
		{
			continue;
		}
		Movers.Add({ Handle, Transform->GetTransform().GetLocation() });
		Centroid += Movers.Last().Location;
	}
	if (Movers.IsEmpty())
	{
		return INDEX_NONE;
	}
	Centroid /= Movers.Num();

	FVector Facing = Order.Facing.GetSafeNormal2D();
	if (Facing.IsNearlyZero())
	{
		Facing = (Order.Destination - Centroid).GetSafeNormal2D();
	}
	if (Facing.IsNearlyZero())
	{
		Facing = FVector::ForwardVector;
	}
	const float Spacing = Order.Spacing > 0.0f ? Order.Spacing : (Settings ? Settings->DefaultFormationSpacing : 120.0f);
	TArray<FVector> Slots = ComputeFormationSlots(Movers.Num(), Order.Destination, Facing, Order.Shape, Spacing);

	// Slots onto the navmesh where there is one (a slot over a ledge or inside a wall moves to the nearest walkable point).
	UNavigationSystemV1* NavSystem = Order.bUseNavigation && NavigationSystem ? NavigationSystem->GetNavigationSystem() : nullptr;
	if (NavSystem)
	{
		const FVector Extent(Spacing, Spacing, 250.0f);
		for (FVector& Slot : Slots)
		{
			FNavLocation Projected;
			if (NavSystem->ProjectPointToNavigation(Slot, Projected, Extent))
			{
				Slot = Projected.Location;
			}
		}
	}

	// Slot assignment: nearest free slot (front slots first) for normal selections; sorted matching for huge ones.
	TArray<int32> SlotForMover;
	SlotForMover.Init(INDEX_NONE, Movers.Num());
	const int32 MaxNearest = Settings ? Settings->MaxNearestSlotAssignment : 256;
	if (Movers.Num() <= MaxNearest)
	{
		TArray<bool> Taken;
		Taken.Init(false, Movers.Num());
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			int32 Best = INDEX_NONE;
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 MoverIndex = 0; MoverIndex < Movers.Num(); ++MoverIndex)
			{
				if (Taken[MoverIndex])
				{
					continue;
				}
				const float Distance = FVector::DistSquared2D(Movers[MoverIndex].Location, Slots[SlotIndex]);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = MoverIndex;
				}
			}
			Taken[Best] = true;
			SlotForMover[Best] = SlotIndex;
		}
	}
	else
	{
		const FVector Right(-Facing.Y, Facing.X, 0.0f);
		auto Key = [&Facing, &Right](const FVector& Point)
		{
			// Coarse across bands, then along: keeps columns together without an O(n^2) match.
			return FVector2D(FMath::RoundToFloat(FVector::DotProduct(Point, Right) / 200.0f), FVector::DotProduct(Point, Facing));
		};
		TArray<int32> MoverOrder;
		TArray<int32> SlotOrder;
		for (int32 Index = 0; Index < Movers.Num(); ++Index)
		{
			MoverOrder.Add(Index);
			SlotOrder.Add(Index);
		}
		auto Less = [](const FVector2D& A, const FVector2D& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; };
		MoverOrder.Sort([&](int32 A, int32 B) { return Less(Key(Movers[A].Location), Key(Movers[B].Location)); });
		SlotOrder.Sort([&](int32 A, int32 B) { return Less(Key(Slots[A]), Key(Slots[B])); });
		for (int32 Index = 0; Index < Movers.Num(); ++Index)
		{
			SlotForMover[MoverOrder[Index]] = SlotOrder[Index];
		}
	}

	// The slowest unit sets the pace when the formation should arrive together.
	float SharedSpeed = TNumericLimits<float>::Max();
	if (Order.bMatchSpeed)
	{
		for (const FMover& Mover : Movers)
		{
			const FCommandEntry* Existing = Entries.Find(Mover.Handle);
			const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Mover.Handle);
			const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Mover.Handle);
			const float Speed = Existing ? Existing->SavedMoveSpeed
				: (Crowd && Crowd->BaseMoveSpeed > 0.0f ? Crowd->BaseMoveSpeed : State->MoveSpeed);
			if (Speed > 0.0f)
			{
				SharedSpeed = FMath::Min(SharedSpeed, Speed);
			}
		}
	}

	const int32 OrderId = NextOrderId++;
	Orders.Add(OrderId);
	const float Now = GetCurrentTime();
	int32 Issued = 0;
	for (int32 MoverIndex = 0; MoverIndex < Movers.Num(); ++MoverIndex)
	{
		const FMassEntityHandle Handle = Movers[MoverIndex].Handle;
		FCommandEntry* Entry = TakeCommand(Handle);
		if (!Entry)
		{
			continue;
		}
		FinishMove(*Entry, EMoveOutcome::Superseded);
		Entry->Order = EMassUnitOrder::Move;
		Entry->OrderId = OrderId;
		Entry->Slot = Slots[SlotForMover[MoverIndex]];
		Entry->AcceptanceRadius = FMath::Max(1.0f, Order.AcceptanceRadius);
		Entry->bUseNavigation = Order.bUseNavigation;
		Entry->BestDistance = TNumericLimits<float>::Max();
		Entry->LastProgressTime = Now;
		Entry->Repaths = 0;
		if (FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Handle))
		{
			State->MoveSpeed = Order.bMatchSpeed && SharedSpeed < TNumericLimits<float>::Max() ? SharedSpeed : Entry->SavedMoveSpeed;
		}
		SendToSlot(Handle, *Entry);
		++Issued;
	}
	if (Issued == 0)
	{
		Orders.Remove(OrderId);
		FlushFinishedOrders();
		return INDEX_NONE;
	}
	Orders.FindChecked(OrderId).Remaining = Issued;
	++TotalOrdersIssued;
	NextTickTime = 0.0f;
	FlushFinishedOrders();
	OnOrderIssued.Broadcast(OrderId, EMassUnitOrder::Move, Issued);
	return OrderId;
}

int32 UMassUnitCommandSystem::IssueStopOrder(const TArray<FMassUnitHandle>& Units)
{
	int32 Stopped = 0;
	for (const FMassUnitHandle& Unit : Units)
	{
		const FMassEntityHandle Handle = Unit.EntityHandle.ToMassEntityHandle();
		if (FCommandEntry* Entry = TakeCommand(Handle))
		{
			FinishMove(*Entry, EMoveOutcome::Superseded);
			Entry->Order = EMassUnitOrder::Idle;
			Entry->OrderId = INDEX_NONE;
			StopUnit(Handle);
			if (const FMassUnitTransformFragment* Transform = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitTransformFragment>(Handle))
			{
				Entry->Slot = Transform->GetTransform().GetLocation();
			}
			++Stopped;
		}
	}
	FlushFinishedOrders();
	if (Stopped > 0)
	{
		++TotalOrdersIssued;
		OnOrderIssued.Broadcast(INDEX_NONE, EMassUnitOrder::Idle, Stopped);
	}
	return Stopped;
}

int32 UMassUnitCommandSystem::IssueHoldOrder(const TArray<FMassUnitHandle>& Units)
{
	int32 Holding = 0;
	for (const FMassUnitHandle& Unit : Units)
	{
		const FMassEntityHandle Handle = Unit.EntityHandle.ToMassEntityHandle();
		if (FCommandEntry* Entry = TakeCommand(Handle))
		{
			FinishMove(*Entry, EMoveOutcome::Superseded);
			Entry->Order = EMassUnitOrder::Hold;
			Entry->OrderId = INDEX_NONE;
			StopUnit(Handle);
			if (const FMassUnitTransformFragment* Transform = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitTransformFragment>(Handle))
			{
				Entry->Slot = Transform->GetTransform().GetLocation();
			}
			++Holding;
		}
	}
	FlushFinishedOrders();
	if (Holding > 0)
	{
		++TotalOrdersIssued;
		OnOrderIssued.Broadcast(INDEX_NONE, EMassUnitOrder::Hold, Holding);
	}
	return Holding;
}

int32 UMassUnitCommandSystem::ReleaseUnits(const TArray<FMassUnitHandle>& Units)
{
	if (!EntitySubsystem)
	{
		return 0;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	int32 Released = 0;
	for (const FMassUnitHandle& Unit : Units)
	{
		const FMassEntityHandle Handle = Unit.EntityHandle.ToMassEntityHandle();
		FCommandEntry Entry;
		if (!Entries.RemoveAndCopyValue(Handle, Entry))
		{
			continue;
		}
		FinishMove(Entry, EMoveOutcome::Superseded);
		++Released;
		if (!EntityManager.IsEntityValid(Handle))
		{
			continue;
		}
		FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Handle);
		if (Crowd)
		{
			Crowd->bCommanded = false;
			Crowd->NextDecisionTime = 0.0f;   // its crowd group decides for it on the next update
			Crowd->SteeringDirection = FVector::ZeroVector;
			// A group joined while under orders took the order's (matched) speed as its base: put the unit's own back.
			if (Crowd->GroupHandle != INDEX_NONE && Entry.SavedMoveSpeed > 0.0f)
			{
				Crowd->BaseMoveSpeed = Entry.SavedMoveSpeed;
			}
		}
		FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Handle);
		if (State && State->CurrentState != EMassUnitState::Dead)
		{
			if (Entry.SavedMoveSpeed > 0.0f)
			{
				State->MoveSpeed = Entry.SavedMoveSpeed;
			}
			StopUnit(Handle);
		}
	}
	FlushFinishedOrders();
	return Released;
}

int32 UMassUnitCommandSystem::ReleaseAllUnits()
{
	TArray<FMassUnitHandle> Units;
	Units.Reserve(Entries.Num());
	for (const TPair<FMassEntityHandle, FCommandEntry>& Pair : Entries)
	{
		Units.Add(FMassUnitHandle(Pair.Key));
	}
	return ReleaseUnits(Units);
}

void UMassUnitCommandSystem::FinishMove(FCommandEntry& Entry, EMoveOutcome Outcome)
{
	if (Entry.Order != EMassUnitOrder::Move || Entry.OrderId == INDEX_NONE)
	{
		return;
	}
	const int32 OrderId = Entry.OrderId;
	Entry.OrderId = INDEX_NONE;
	FOrderRecord* Record = Orders.Find(OrderId);
	if (!Record)
	{
		return;
	}
	switch (Outcome)
	{
	case EMoveOutcome::Arrived: ++Record->Arrived; ++TotalArrivals; break;
	case EMoveOutcome::Failed: ++Record->Failed; ++TotalFailures; break;
	case EMoveOutcome::Superseded: ++Record->Superseded; break;
	}
	if (--Record->Remaining <= 0)
	{
		PendingFinished.Emplace(OrderId, *Record);
		Orders.Remove(OrderId);
	}
}

void UMassUnitCommandSystem::FlushFinishedOrders()
{
	if (PendingFinished.IsEmpty())
	{
		return;
	}
	// Listeners may issue, stop or release (waypoint queues, "release on arrival"): the table is consistent by now, and
	// anything they finish queues again and is flushed by their own call.
	TArray<TPair<int32, FOrderRecord>> Finished = MoveTemp(PendingFinished);
	PendingFinished.Reset();
	for (const TPair<int32, FOrderRecord>& Pair : Finished)
	{
		OnMoveOrderFinished.Broadcast(Pair.Key, Pair.Value.Arrived, Pair.Value.Failed, Pair.Value.Superseded);
	}
}

bool UMassUnitCommandSystem::SendToSlot(const FMassEntityHandle& Unit, const FCommandEntry& Entry) const
{
	if (!UnitManager)
	{
		return false;
	}
	// A navmesh request when asked for (the navigation system falls back to a direct line per the project settings).
	if (Entry.bUseNavigation && NavigationSystem && NavigationSystem->GetNavigationSystem())
	{
		return NavigationSystem->RequestPathInternal(FMassUnitEntityHandle(Unit), Entry.Slot, Entry.AcceptanceRadius);
	}
	return UnitManager->SetUnitDestination(FMassUnitHandle(Unit), Entry.Slot, Entry.AcceptanceRadius);
}

void UMassUnitCommandSystem::StopUnit(const FMassEntityHandle& Unit) const
{
	if (!EntitySubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(Unit))
	{
		return;
	}
	if (NavigationSystem)
	{
		NavigationSystem->CancelPathInternal(FMassUnitEntityHandle(Unit));
	}
	if (FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Unit))
	{
		Target->Clear();
	}
	if (FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(Unit))
	{
		Navigation->ResetPath();
	}
	FMassUnitVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(Unit);
	if (Velocity && !(UnitManager && UnitManager->IsUnitAirborne(FMassUnitHandle(Unit))))
	{
		Velocity->Value = FVector::ZeroVector;
	}
	if (FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit))
	{
		if (State->CurrentState == EMassUnitState::Moving)
		{
			State->CurrentState = EMassUnitState::Idle;
			State->StateTime = 0.0f;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Tick
// ------------------------------------------------------------------------------------------------

void UMassUnitCommandSystem::TickCommands(float CurrentTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_TickCommands);
	if (Entries.IsEmpty() || !EntitySubsystem || !UnitManager)
	{
		return;
	}
	if (MassUnitCommand::GCommandDebug != 0)
	{
		DrawCommandDebug();
	}
	if (CurrentTime < NextTickTime)
	{
		return;
	}
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	NextTickTime = CurrentTime + (Settings ? Settings->CommandUpdateInterval : 0.1f);
	const float StuckTime = Settings ? Settings->CommandStuckTime : 2.5f;
	const int32 MaxRepaths = Settings ? Settings->MaxCommandRepaths : 4;

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	TArray<FMassEntityHandle> Gone;
	for (TPair<FMassEntityHandle, FCommandEntry>& Pair : Entries)
	{
		const FMassEntityHandle Unit = Pair.Key;
		FCommandEntry& Entry = Pair.Value;
		if (!EntityManager.IsEntityValid(Unit))
		{
			FinishMove(Entry, EMoveOutcome::Failed);
			Gone.Add(Unit);
			continue;
		}
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit);
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Unit);
		FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit);
		if (!State || !Transform || !Crowd || State->CurrentState == EMassUnitState::Dead)
		{
			if (Crowd)
			{
				Crowd->bCommanded = false;   // a corpse is nobody's to command (and pooling / revive starts clean)
			}
			FinishMove(Entry, EMoveOutcome::Failed);
			Gone.Add(Unit);
			continue;
		}
		// Keep the flag owned: nothing else should clear it, but a fragment reset elsewhere must not drop orders silently.
		Crowd->bCommanded = true;
		if (Entry.Order != EMassUnitOrder::Move)
		{
			continue;
		}
		// In the air (knockback, kill fling) or on a pile: the launch / stack owns it; resume once it is back down.
		if (UnitManager->IsUnitAirborne(FMassUnitHandle(Unit)) || Crowd->bStackDriven || State->CurrentState == EMassUnitState::Stunned)
		{
			Entry.LastProgressTime = CurrentTime;
			continue;
		}

		const FVector Location = Transform->GetTransform().GetLocation();
		const float Distance = FVector::Dist2D(Location, Entry.Slot);
		if (Distance <= Entry.AcceptanceRadius)
		{
			FinishMove(Entry, EMoveOutcome::Arrived);
			Entry.Order = EMassUnitOrder::Idle;
			StopUnit(Unit);
			continue;
		}

		const FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(Unit);
		const FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(Unit);
		const bool bHasMovement = (Navigation && (Navigation->bPathRequested || Navigation->bPathValid)) || (Target && Target->HasTarget());
		if (Distance < Entry.BestDistance - 25.0f)
		{
			// Real progress since the last repath: a long march may jam several times without failing.
			if (Entry.BestDistance < TNumericLimits<float>::Max() && Distance < Entry.BestDistance - 100.0f)
			{
				Entry.Repaths = 0;
			}
			Entry.BestDistance = Distance;
			Entry.LastProgressTime = CurrentTime;
		}
		const bool bStuck = CurrentTime - Entry.LastProgressTime > StuckTime;
		if (bHasMovement && !bStuck)
		{
			continue;
		}
		// Lost its path (a launch or landing cleared it) or made no progress: ask again, up to MaxCommandRepaths.
		if (Entry.Repaths >= MaxRepaths)
		{
			FinishMove(Entry, EMoveOutcome::Failed);
			Entry.Order = EMassUnitOrder::Idle;
			Entry.Slot = Location;
			StopUnit(Unit);
			continue;
		}
		++Entry.Repaths;
		Entry.BestDistance = Distance;
		Entry.LastProgressTime = CurrentTime;
		SendToSlot(Unit, Entry);
	}
	for (const FMassEntityHandle& Unit : Gone)
	{
		Entries.Remove(Unit);
	}
	FlushFinishedOrders();
}

// ------------------------------------------------------------------------------------------------
// Queries
// ------------------------------------------------------------------------------------------------

bool UMassUnitCommandSystem::IsUnitCommanded(FMassUnitHandle Unit) const
{
	return Entries.Contains(Unit.EntityHandle.ToMassEntityHandle());
}

EMassUnitOrder UMassUnitCommandSystem::GetUnitOrder(FMassUnitHandle Unit) const
{
	const FCommandEntry* Entry = Entries.Find(Unit.EntityHandle.ToMassEntityHandle());
	return Entry ? Entry->Order : EMassUnitOrder::None;
}

bool UMassUnitCommandSystem::GetUnitOrderSlot(FMassUnitHandle Unit, FVector& OutSlot) const
{
	if (const FCommandEntry* Entry = Entries.Find(Unit.EntityHandle.ToMassEntityHandle()))
	{
		OutSlot = Entry->Slot;
		return true;
	}
	return false;
}

int32 UMassUnitCommandSystem::GetUnitOrderId(FMassUnitHandle Unit) const
{
	const FCommandEntry* Entry = Entries.Find(Unit.EntityHandle.ToMassEntityHandle());
	return Entry ? Entry->OrderId : INDEX_NONE;
}

bool UMassUnitCommandSystem::UpdateSavedMoveSpeed(FMassUnitHandle Unit, float Speed)
{
	FCommandEntry* Entry = Entries.Find(Unit.EntityHandle.ToMassEntityHandle());
	if (!Entry)
	{
		return false;
	}
	Entry->SavedMoveSpeed = FMath::Max(0.0f, Speed);
	return true;
}

FMassUnitCommandStats UMassUnitCommandSystem::GetCommandStats() const
{
	FMassUnitCommandStats Stats;
	Stats.CommandedUnits = Entries.Num();
	for (const TPair<FMassEntityHandle, FCommandEntry>& Pair : Entries)
	{
		Stats.MovingUnits += Pair.Value.Order == EMassUnitOrder::Move ? 1 : 0;
		Stats.HoldingUnits += Pair.Value.Order == EMassUnitOrder::Hold ? 1 : 0;
	}
	Stats.ActiveOrders = Orders.Num();
	Stats.TotalOrdersIssued = TotalOrdersIssued;
	Stats.TotalArrivals = TotalArrivals;
	Stats.TotalFailures = TotalFailures;
	return Stats;
}

void UMassUnitCommandSystem::DrawCommandDebug() const
{
#if ENABLE_DRAW_DEBUG
	if (!World || !EntitySubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const TPair<FMassEntityHandle, FCommandEntry>& Pair : Entries)
	{
		const FMassUnitTransformFragment* Transform = EntityManager.IsEntityValid(Pair.Key)
			? EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Pair.Key)
			: nullptr;
		if (!Transform)
		{
			continue;
		}
		const FVector Location = Transform->GetTransform().GetLocation();
		const FCommandEntry& Entry = Pair.Value;
		switch (Entry.Order)
		{
		case EMassUnitOrder::Move:
			DrawDebugLine(World, Location + FVector(0, 0, 20), Entry.Slot + FVector(0, 0, 20), FColor::Yellow, false, -1.0f, 0, 1.0f);
			DrawDebugCircle(World, Entry.Slot + FVector(0, 0, 5), Entry.AcceptanceRadius, 12, FColor::Yellow, false, -1.0f, 0, 1.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			break;
		case EMassUnitOrder::Hold:
			DrawDebugBox(World, Location + FVector(0, 0, 40), FVector(25.0f), FColor::White, false, -1.0f, 0, 1.5f);
			break;
		default:
			DrawDebugCircle(World, Location + FVector(0, 0, 5), 30.0f, 12, FColor::Green, false, -1.0f, 0, 1.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			break;
		}
	}
	if (GEngine)
	{
		const FMassUnitCommandStats Stats = GetCommandStats();
		GEngine->AddOnScreenDebugMessage(9420, 0.0f, FColor::Cyan, FString::Printf(
			TEXT("Mass commands: %d commanded | %d moving | %d holding | %d orders active | issued %d, arrived %d, failed %d"),
			Stats.CommandedUnits, Stats.MovingUnits, Stats.HoldingUnits, Stats.ActiveOrders,
			Stats.TotalOrdersIssued, Stats.TotalArrivals, Stats.TotalFailures));
	}
#endif
}
