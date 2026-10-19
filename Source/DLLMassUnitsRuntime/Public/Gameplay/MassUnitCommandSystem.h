// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "MassUnitCommandSystem.generated.h"

class UMassEntitySubsystem;
class UMassUnitNavigationSystem;
class UMassUnitStackSystem;

/** What a unit's orders tell it to do. */
UENUM(BlueprintType)
enum class EMassUnitOrder : uint8
{
	/** Not under orders: its crowd group (or nothing) drives it. */
	None,
	/** Under orders with nothing to do: it stands where it is (a finished move, a Stop). */
	Idle,
	/** Walking to its formation slot. */
	Move,
	/** Stands its ground. Fighting what comes in range arrives with auto-acquire (RTS phase R2). */
	Hold
};

/** Slot layout of a group move. */
UENUM(BlueprintType)
enum class EMassUnitFormationShape : uint8
{
	/** Rows about as wide as deep. */
	Box,
	/** One row across the facing. */
	Line,
	/** Three abreast, long along the facing. */
	Column,
	/** A point at the front, each row one wider. */
	Wedge,
	/** Rings around the destination. */
	Circle
};

/** One move order for a selection of units. */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitMoveOrder
{
	GENERATED_BODY()

	/** Centre of the formation at the end of the move. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	FVector Destination = FVector::ZeroVector;

	/** Which way the formation faces when it arrives (planar). Zero = from the units' centre toward the destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	FVector Facing = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	EMassUnitFormationShape Shape = EMassUnitFormationShape::Box;

	/** Distance between slots. 0 = the project's DefaultFormationSpacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float Spacing = 0.0f;

	/** How close to its slot a unit must get to count as arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float AcceptanceRadius = 40.0f;

	/** Path on the navmesh (batched requests, direct line when there is none). Off = straight lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	bool bUseNavigation = true;

	/** Every unit moves at the slowest unit's speed so the formation arrives together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	bool bMatchSpeed = true;
};

/** Snapshot of the command table (diagnostics, HUDs). */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitCommandStats
{
	GENERATED_BODY()

	/** Units under orders right now. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commands")
	int32 CommandedUnits = 0;

	/** Units walking to their slot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commands")
	int32 MovingUnits = 0;

	/** Units holding their ground. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commands")
	int32 HoldingUnits = 0;

	/** Move orders still under way. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commands")
	int32 ActiveOrders = 0;

	/** Orders issued since the world started (every Issue* call that reached a unit). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commands")
	int32 TotalOrdersIssued = 0;

	/** Units that reached their slot since the world started. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commands")
	int32 TotalArrivals = 0;

	/** Units whose move failed (died on the way, ran out of new paths) since the world started. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commands")
	int32 TotalFailures = 0;
};

/** An order reached Units units (OrderId identifies it in OnMoveOrderFinished). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMassUnitOrderIssuedSignature,
	int32, OrderId,
	EMassUnitOrder, Order,
	int32, Units);

/** Every unit of a move order arrived, failed, died, or got another order (Superseded). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FMassUnitMoveOrderFinishedSignature,
	int32, OrderId,
	int32, Arrived,
	int32, Failed,
	int32, Superseded);

/**
 * Orders for units: the mechanics an RTS-style control layer needs (any adopter's, no game vocabulary). Selection,
 * input, ownership, UI and replication of orders belong to the adopter; the server calls this, and clients see the
 * result through whatever replicates unit locations.
 *
 * A sparse table of commanded units, checked by the subsystem every CommandUpdateInterval (free when empty). A unit under
 * orders is flagged FMassUnitCrowdFragment::bCommanded: its crowd group stops driving its destination (it only keeps
 * neighbours apart), engagement, interactions and sleep leave it alone, and the pile planner never uses it. Release
 * hands it back to its crowd group, so units nobody commands behave exactly as before.
 *
 * A group move lays out formation slots around the destination facing the move, gives each unit the nearest free slot,
 * paths each unit there (batched navigation), optionally at the slowest unit's speed, and reports when every unit has
 * arrived. A unit knocked away (launch) or stuck asks for a new path; after MaxCommandRepaths it stops and counts as
 * failed. Visual debug: dll.MassUnits.CommandDebug. Test commands: dll.MassUnits.CommandMoveTeam / StopTeam / ReleaseAll.
 */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UMassUnitCommandSystem : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, UMassUnitEntityManager* InUnitManager, UMassUnitNavigationSystem* InNavigationSystem, UMassUnitStackSystem* InStackSystem);
	void Deinitialize();

	/**
	 * Moves Units into a formation at Order.Destination. Living, non-airborne units only; a unit on a pile is set down
	 * first. Returns the order id (INDEX_NONE when no unit could take it). Gameplay: call on the server.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Commands")
	int32 IssueMoveOrder(const TArray<FMassUnitHandle>& Units, const FMassUnitMoveOrder& Order);

	/** Stops Units where they are; they stay under orders (Idle). Returns units stopped. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Commands")
	int32 IssueStopOrder(const TArray<FMassUnitHandle>& Units);

	/** Units stand their ground (Hold). Returns units holding. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Commands")
	int32 IssueHoldOrder(const TArray<FMassUnitHandle>& Units);

	/** Takes Units off orders and hands them back to their crowd group (or leaves them standing). Returns units released. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Commands")
	int32 ReleaseUnits(const TArray<FMassUnitHandle>& Units);

	/** Releases every commanded unit. Returns units released. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Commands")
	int32 ReleaseAllUnits();

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Commands")
	bool IsUnitCommanded(FMassUnitHandle Unit) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Commands")
	EMassUnitOrder GetUnitOrder(FMassUnitHandle Unit) const;

	/** The unit's formation slot for its move (or where it stands). False when it is not under orders. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Commands")
	bool GetUnitOrderSlot(FMassUnitHandle Unit, FVector& OutSlot) const;

	/** The order id the unit is carrying out (INDEX_NONE when none). */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Commands")
	int32 GetUnitOrderId(FMassUnitHandle Unit) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Commands")
	int32 GetCommandedUnitCount() const { return Entries.Num(); }

	/** The speed a commanded unit gets back on release (an attitude re-applied mid-order). False when not commanded. */
	bool UpdateSavedMoveSpeed(FMassUnitHandle Unit, float Speed);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Commands")
	FMassUnitCommandStats GetCommandStats() const;

	/**
	 * Formation slot positions for Count units centred on Center, facing Facing (planar; zero = +X), Spacing apart. Slot 0
	 * is the front centre. Pure layout: no projection to the navmesh. Public for UIs (move previews) and tests.
	 */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Commands")
	static TArray<FVector> ComputeFormationSlots(int32 Count, FVector Center, FVector Facing, EMassUnitFormationShape Shape, float Spacing);

	/** Checks arrivals, stuck and knocked-away units, dead units. Called by the subsystem each frame; interval-gated. */
	void TickCommands(float CurrentTime);

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Commands|Events")
	FMassUnitOrderIssuedSignature OnOrderIssued;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Commands|Events")
	FMassUnitMoveOrderFinishedSignature OnMoveOrderFinished;

private:
	struct FCommandEntry
	{
		EMassUnitOrder Order = EMassUnitOrder::Idle;
		int32 OrderId = INDEX_NONE;
		FVector Slot = FVector::ZeroVector;
		float AcceptanceRadius = 40.0f;
		/** Move speed before the unit came under orders (restored on release). */
		float SavedMoveSpeed = 0.0f;
		/** Closest it has been to its slot since the last progress check (stuck detection). */
		float BestDistance = TNumericLimits<float>::Max();
		float LastProgressTime = 0.0f;
		int32 Repaths = 0;
		bool bUseNavigation = true;
	};

	struct FOrderRecord
	{
		int32 Remaining = 0;
		int32 Arrived = 0;
		int32 Failed = 0;
		int32 Superseded = 0;
	};

	enum class EMoveOutcome : uint8 { Arrived, Failed, Superseded };

	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitEntityManager> UnitManager = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitNavigationSystem> NavigationSystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitStackSystem> StackSystem = nullptr;

	TMap<FMassEntityHandle, FCommandEntry> Entries;
	TMap<int32, FOrderRecord> Orders;
	/** Orders finished during the current call; broadcast at its end so listeners may issue or release safely. */
	TArray<TPair<int32, FOrderRecord>> PendingFinished;
	int32 NextOrderId = 1;
	float NextTickTime = 0.0f;
	int32 TotalOrdersIssued = 0;
	int32 TotalArrivals = 0;
	int32 TotalFailures = 0;

	/** Puts a unit under orders (flag, wake, leave interactions and piles). False when it cannot take orders now. */
	FCommandEntry* TakeCommand(const FMassEntityHandle& Unit);
	/** Ends the unit's current move (if any) with Outcome; queues OnMoveOrderFinished when it was the order's last unit. */
	void FinishMove(FCommandEntry& Entry, EMoveOutcome Outcome);
	/** Broadcasts the queued OnMoveOrderFinished events (after every table change of the current call). */
	void FlushFinishedOrders();
	/** Sends the unit toward its slot (navmesh request or direct destination). */
	bool SendToSlot(const FMassEntityHandle& Unit, const FCommandEntry& Entry) const;
	/** Clears path and target and, unless it is airborne (the launch owns it), velocity; the unit goes Idle. */
	void StopUnit(const FMassEntityHandle& Unit) const;
	float GetCurrentTime() const;
	void DrawCommandDebug() const;
};
