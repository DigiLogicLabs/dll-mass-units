// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Entity/MassUnitEntityManager.h"
#include "MassUnitStackSystem.generated.h"

class AActor;
class UMassEntitySubsystem;
class UMassUnitCrowdSystem;
class UUnitTemplate;

/** What a unit in the stack table is doing. */
UENUM(BlueprintType)
enum class EMassUnitStackRole : uint8
{
	/** Not in the stack table. */
	None,
	/** Moving up to its place on its support. */
	Climbing,
	/** Standing in its place on its support (and possibly holding others up). */
	Standing,
	/** A ground unit frozen in place because something stands on it. */
	Holding
};

/** Snapshot of the stack table (diagnostics, stress HUDs). */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitStackStats
{
	GENERATED_BODY()

	/** Units the stack system drives right now: climbers, standers and the ground units holding them. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stacking")
	int32 StackedUnits = 0;

	/** Units standing on another unit (level 1 and up), climbing or in place. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stacking")
	int32 Climbers = 0;

	/** Ground units frozen in place under a climber. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stacking")
	int32 GroundSupports = 0;

	/** Highest level anyone stands on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stacking")
	int32 HighestLevel = 0;

	/** Piles (engaged group + target) with at least one climber. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stacking")
	int32 ActivePiles = 0;

	/** Units that fell off a pile since the world started (support lost, target broke away). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stacking")
	int32 TotalCollapses = 0;

	/** Climbers the planner started since the world started. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stacking")
	int32 TotalClimbsStarted = 0;
};

/** A unit joined (bStacked) or left the stack table. Support is invalid for ground units and when leaving. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FMassUnitStackChangedSignature,
	FMassUnitHandle, UnitHandle,
	FMassUnitHandle, SupportHandle,
	int32, Level,
	bool, bStacked);

/** A stacked unit fell off (its support went away, or the target broke away). Fires before its fall launch. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMassUnitStackCollapsedSignature,
	FMassUnitHandle, UnitHandle,
	int32, Level);

/**
 * Stacking and piling: units climbing onto other units. See UUnitTemplate::Stacking.
 *
 * A sparse table of stacked units, ticked once per frame by the subsystem (free when empty). A climber stands on a
 * support: it moves to the support's position (nudged toward the target) one StackHeight up, and the movement processor
 * skips it (FMassUnitCrowdFragment::bStackDriven). A ground unit with something on it is frozen in place (Holding).
 * When a support dies, is launched, is destroyed or leaves the table, each unit on it falls off with a small
 * LaunchUnit, and the launch path lands it, so a pile collapses in a cascade at no extra cost.
 *
 * The pile planner (server only, every PilePlanInterval per engaged crowd group) starts climbers once enough
 * stack-capable units crowd the group's target: outer units climb onto units nearer the target, so the pile grows into a
 * cone around it. Heights reach clients through whatever replicates unit locations; the plugin replicates nothing.
 */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UMassUnitStackSystem : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, UMassUnitEntityManager* InUnitManager, UMassUnitCrowdSystem* InCrowdSystem);
	void Deinitialize();

	/**
	 * Puts Climber on Support: it climbs to Support's position one StackHeight up and stays there until Support goes away
	 * (then it falls off) or ReleaseStackedUnit is called. Optional FaceTarget: the climber leans toward and faces it, and
	 * falls off when it gets farther than the climber's PileRadius + PileBreakSlack. Fails when stacking is off, either
	 * template forbids it, the climber is not a crowd unit, Support has no free rider place, the level would exceed the
	 * climber's MaxStackLevel, or MaxStackedUnits is reached. Runs on whichever machine calls it (gameplay: the server).
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Stacking")
	bool StackUnitOn(FMassUnitHandle Climber, FMassUnitHandle Support, AActor* FaceTarget = nullptr);

	/** Takes a unit out of the stack table. bFall throws it off (a collapse); otherwise it is set down at its ground height. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Stacking")
	bool ReleaseStackedUnit(FMassUnitHandle Unit, bool bFall = true);

	/** Throws every climber off (an explosion, a shake-off ability). Ground supports are freed. Returns climbers thrown. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Stacking")
	int32 CollapseAllStacks();

	/** Throws off every climber within Radius of Location (planar) - a shockwave or a stomp that topples piles. */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Stacking")
	int32 CollapseStacksInRadius(FVector Location, float Radius);

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Stacking")
	bool IsUnitStacked(FMassUnitHandle Unit) const;

	/** 0 for ground units holding someone up (and units not in the table), 1+ for climbers. */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Stacking")
	int32 GetUnitStackLevel(FMassUnitHandle Unit) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Stacking")
	EMassUnitStackRole GetUnitStackRole(FMassUnitHandle Unit) const;

	/** The unit this one stands on (invalid for ground units and units not in the table). */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Stacking")
	FMassUnitHandle GetUnitStackSupport(FMassUnitHandle Unit) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Stacking")
	int32 GetStackedUnitCount() const { return Entries.Num(); }

	/**
	 * Client puppet side (the adopter replicates GetUnitStackRole): shows a stacked pose without simulating the stack -
	 * Climbing plays Climb (else Run), Holding plays Hold (else Idle), and any role but None eases the puppet's height to
	 * its destination height (PresentationClimbSpeed) instead of snapping. Presentation only: no stack entry, no falls,
	 * no events, so calling it on a real unit cannot fake gameplay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Stacking")
	bool ApplyPresentationStackRole(FMassUnitHandle Unit, EMassUnitStackRole Role);

	/** The puppet role last applied by ApplyPresentationStackRole (None for real units and unknown handles). */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Stacking")
	EMassUnitStackRole GetPresentationStackRole(FMassUnitHandle Unit) const;

	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Stacking")
	FMassUnitStackStats GetStackStats() const;

	/** Advances climbers, validates supports, collapses what lost its footing. Called by the subsystem each frame. */
	void TickStacks(float DeltaTime);

	/** Runs the pile planner for every engaged crowd group that is due. Called by the subsystem; server only. */
	void UpdatePiles(float CurrentTime);

	/**
	 * One pile plan around TargetLocation from Candidates (the planner's core, public for tests and custom drivers):
	 * when at least the climber template's PileMinUnits stack-capable units are inside its PileRadius, the outer ones
	 * climb onto units nearer the target, up to MaxClimbersPerPilePass. Returns climbers started.
	 */
	int32 PlanPile(const TArray<FMassUnitEntityHandle>& Candidates, const FVector& TargetLocation, AActor* Target, int32 PileKey);

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Stacking|Events")
	FMassUnitStackChangedSignature OnUnitStackChanged;

	UPROPERTY(BlueprintAssignable, Category = "Mass Unit System|Stacking|Events")
	FMassUnitStackCollapsedSignature OnUnitStackCollapsed;

private:
	struct FStackEntry
	{
		FMassEntityHandle Support;
		/** Serial of the support's entry when this unit climbed on: a recycled support entry (new serial) is not its support. */
		uint32 SupportSerial = 0;
		/** This entry's own serial, unique per table insertion. */
		uint32 Serial = 0;
		/** Place on the support (0 = centre); riders past the first stand beside it. */
		int32 RiderSlot = 0;
		TWeakObjectPtr<AActor> Target;
		/** Ground height when the unit joined: where it is set down, and the most a fall may land at. */
		float BaseZ = 0.0f;
		int32 Level = 0;
		int32 RiderCount = 0;
		/** Pile it belongs to (crowd group handle), INDEX_NONE for StackUnitOn calls without a pile. */
		int32 PileKey = INDEX_NONE;
		/** Seconds since this unit died with riders on it (the corpse holds them up for SupportCorpseHoldSeconds). */
		float DeadSeconds = 0.0f;
		bool bArrived = false;
	};

	UPROPERTY(Transient)
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassEntitySubsystem> EntitySubsystem = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitEntityManager> UnitManager = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMassUnitCrowdSystem> CrowdSystem = nullptr;

	TMap<FMassEntityHandle, FStackEntry> Entries;
	/** Next plan time per crowd group (pile key). */
	TMap<int32, float> NextPlanTime;
	uint32 NextSerial = 1;
	int32 TotalCollapses = 0;
	int32 TotalClimbsStarted = 0;

	bool AddClimber(const FMassEntityHandle& Climber, const FMassEntityHandle& Support, AActor* Target, int32 PileKey);
	/** Removes an entry and its bookkeeping. bFall throws a living climber off; dead units always drop. */
	void RemoveEntry(const FMassEntityHandle& Unit, bool bFall, bool bCountCollapse);
	void SetStackDriven(const FMassEntityHandle& Unit, bool bDriven) const;
	void DropUnit(const FMassEntityHandle& Unit, const FStackEntry& Entry, bool bFall) const;
	const UUnitTemplate* GetTemplate(const FMassEntityHandle& Unit) const;
	bool IsUnitUsable(const FMassEntityHandle& Unit) const;
	int32 CountPileClimbers(int32 PileKey) const;
	/** Throws off this pile's climbers whose target is not KeepTarget (null = all of them). Returns climbers thrown. */
	int32 CollapsePile(int32 PileKey, const AActor* KeepTarget);
	/** Collapses piles whose crowd group is gone (pile keys with no registered group). */
	void CollapseOrphanPiles(const TArray<int32>& LiveGroupHandles);
	/** True when Entry's support is the entry it climbed onto (not a recycled one). */
	const FStackEntry* FindLiveSupport(const FStackEntry& Entry) const;
	void DrawStackDebug() const;
};
