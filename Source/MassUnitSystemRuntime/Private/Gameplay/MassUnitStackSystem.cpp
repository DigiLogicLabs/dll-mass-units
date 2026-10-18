// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Gameplay/MassUnitStackSystem.h"

#include "Core/MassUnitSubsystem.h"
#include "Core/MassUnitSystemRuntime.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Core/MassUnitStats.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Entity/MassUnitFragments.h"
#include "Entity/UnitTemplate.h"
#include "GameFramework/Actor.h"
#include "Gameplay/MassUnitCrowdSystem.h"
#include "HAL/IConsoleManager.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassUnitCommonFragments.h"

DECLARE_CYCLE_STAT(TEXT("Stacks (climbers + supports)"), STAT_MassUnit_TickStacks, STATGROUP_MassUnitSystem);
DECLARE_CYCLE_STAT(TEXT("Pile Planner"), STAT_MassUnit_PilePlanner, STATGROUP_MassUnitSystem);

namespace MassUnitStack
{
	static int32 GStackDebug = 0;
	static FAutoConsoleVariableRef CVarStackDebug(
		TEXT("dll.MassUnits.StackDebug"),
		GStackDebug,
		TEXT("Draw the stack table: green = climbing, cyan = standing on its support, white ring = ground unit holding one up, ")
		TEXT("plus a HUD line with the stack stats. 0 = off."),
		ECVF_Cheat);

	/** Defaults for a unit with no template (CreateDefaultUnit): may be climbed, never climbs. */
	static const FMassUnitStackingConfig& GetConfig(const UUnitTemplate* Template)
	{
		static const FMassUnitStackingConfig Defaults;
		return Template ? Template->Stacking : Defaults;
	}

	static bool HasClip(const UUnitTemplate* Template, const FGameplayTag& Tag)
	{
		return Template && Template->VertexAnimationClips.ContainsByPredicate(
			[&Tag](const FMassUnitVATClip& Clip) { return Clip.AnimationTag == Tag; });
	}

	static constexpr float ArriveTolerance = 6.0f;

	static FAutoConsoleCommandWithWorldAndArgs CollapseStacksCmd(
		TEXT("dll.MassUnits.CollapseStacks"),
		TEXT("Throw every climber off its pile (a shake-off test). Optional arg: radius in cm around the first player pawn."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const UMassUnitSubsystem* Subsystem = World ? World->GetSubsystem<UMassUnitSubsystem>() : nullptr;
			UMassUnitStackSystem* Stacks = Subsystem ? Subsystem->GetStackSystem() : nullptr;
			if (!Stacks)
			{
				return;
			}
			const float Radius = Args.Num() > 0 ? FCString::Atof(*Args[0]) : -1.0f;
			const APlayerController* Controller = World->GetFirstPlayerController();
			const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			const int32 Thrown = Radius > 0.0f && Pawn
				? Stacks->CollapseStacksInRadius(Pawn->GetActorLocation(), Radius)
				: Stacks->CollapseAllStacks();
			UE_LOG(LogMassUnitSystem, Display, TEXT("dll.MassUnits.CollapseStacks: %d climber(s) thrown off"), Thrown);
		}),
		ECVF_Cheat);
}

void UMassUnitStackSystem::Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, UMassUnitEntityManager* InUnitManager, UMassUnitCrowdSystem* InCrowdSystem)
{
	World = InWorld;
	EntitySubsystem = InEntitySubsystem;
	UnitManager = InUnitManager;
	CrowdSystem = InCrowdSystem;
}

void UMassUnitStackSystem::Deinitialize()
{
	// World teardown: the Mass entity manager may already be gone, so fragments are not touched here.
	Entries.Reset();
	NextPlanTime.Reset();
	World = nullptr;
	EntitySubsystem = nullptr;
	UnitManager = nullptr;
	CrowdSystem = nullptr;
}

// ------------------------------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------------------------------

bool UMassUnitStackSystem::StackUnitOn(FMassUnitHandle Climber, FMassUnitHandle Support, AActor* FaceTarget)
{
	return AddClimber(Climber.EntityHandle.ToMassEntityHandle(), Support.EntityHandle.ToMassEntityHandle(), FaceTarget, INDEX_NONE);
}

bool UMassUnitStackSystem::ReleaseStackedUnit(FMassUnitHandle Unit, bool bFall)
{
	const FMassEntityHandle Handle = Unit.EntityHandle.ToMassEntityHandle();
	if (!Entries.Contains(Handle))
	{
		return false;
	}
	RemoveEntry(Handle, bFall, bFall);
	return true;
}

int32 UMassUnitStackSystem::CollapseAllStacks()
{
	return CollapseStacksInRadius(FVector::ZeroVector, -1.0f);
}

int32 UMassUnitStackSystem::CollapseStacksInRadius(FVector Location, float Radius)
{
	if (!EntitySubsystem || Entries.IsEmpty())
	{
		return 0;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	TArray<FMassEntityHandle> ToCollapse;
	for (const TPair<FMassEntityHandle, FStackEntry>& Pair : Entries)
	{
		if (Pair.Value.Level < 1)
		{
			continue;
		}
		if (Radius >= 0.0f)
		{
			const FMassUnitTransformFragment* Transform = EntityManager.IsEntityValid(Pair.Key)
				? EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Pair.Key)
				: nullptr;
			if (!Transform || FVector::DistSquared2D(Transform->GetTransform().GetLocation(), Location) > FMath::Square(Radius))
			{
				continue;
			}
		}
		ToCollapse.Add(Pair.Key);
	}
	for (const FMassEntityHandle& Handle : ToCollapse)
	{
		RemoveEntry(Handle, true, true);
	}
	return ToCollapse.Num();
}

bool UMassUnitStackSystem::IsUnitStacked(FMassUnitHandle Unit) const
{
	return Unit.IsValid() && Entries.Contains(Unit.EntityHandle.ToMassEntityHandle());
}

int32 UMassUnitStackSystem::GetUnitStackLevel(FMassUnitHandle Unit) const
{
	const FStackEntry* Entry = Unit.IsValid() ? Entries.Find(Unit.EntityHandle.ToMassEntityHandle()) : nullptr;
	return Entry ? Entry->Level : 0;
}

EMassUnitStackRole UMassUnitStackSystem::GetUnitStackRole(FMassUnitHandle Unit) const
{
	const FStackEntry* Entry = Unit.IsValid() ? Entries.Find(Unit.EntityHandle.ToMassEntityHandle()) : nullptr;
	if (!Entry)
	{
		return EMassUnitStackRole::None;
	}
	if (Entry->Level == 0)
	{
		return EMassUnitStackRole::Holding;
	}
	return Entry->bArrived ? EMassUnitStackRole::Standing : EMassUnitStackRole::Climbing;
}

FMassUnitHandle UMassUnitStackSystem::GetUnitStackSupport(FMassUnitHandle Unit) const
{
	const FStackEntry* Entry = Unit.IsValid() ? Entries.Find(Unit.EntityHandle.ToMassEntityHandle()) : nullptr;
	return Entry && Entry->Level > 0 ? FMassUnitHandle(Entry->Support) : FMassUnitHandle();
}

bool UMassUnitStackSystem::ApplyPresentationStackRole(FMassUnitHandle Unit, EMassUnitStackRole Role)
{
	if (!EntitySubsystem || !Unit.IsValid())
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle Handle = Unit.EntityHandle.ToMassEntityHandle();
	if (!EntityManager.IsEntityValid(Handle))
	{
		return false;
	}
	FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Handle);
	if (!Crowd)
	{
		return false;
	}
	Crowd->PresentationStackRole = static_cast<uint8>(Role);
	return true;
}

EMassUnitStackRole UMassUnitStackSystem::GetPresentationStackRole(FMassUnitHandle Unit) const
{
	if (!EntitySubsystem || !Unit.IsValid())
	{
		return EMassUnitStackRole::None;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const FMassEntityHandle Handle = Unit.EntityHandle.ToMassEntityHandle();
	const FMassUnitCrowdFragment* Crowd = EntityManager.IsEntityValid(Handle) ? EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Handle) : nullptr;
	return Crowd ? static_cast<EMassUnitStackRole>(Crowd->PresentationStackRole) : EMassUnitStackRole::None;
}

FMassUnitStackStats UMassUnitStackSystem::GetStackStats() const
{
	FMassUnitStackStats Stats;
	Stats.StackedUnits = Entries.Num();
	Stats.TotalCollapses = TotalCollapses;
	Stats.TotalClimbsStarted = TotalClimbsStarted;
	TSet<int32> Piles;
	for (const TPair<FMassEntityHandle, FStackEntry>& Pair : Entries)
	{
		if (Pair.Value.Level == 0)
		{
			++Stats.GroundSupports;
			continue;
		}
		++Stats.Climbers;
		Stats.HighestLevel = FMath::Max(Stats.HighestLevel, Pair.Value.Level);
		if (Pair.Value.PileKey != INDEX_NONE)
		{
			Piles.Add(Pair.Value.PileKey);
		}
	}
	Stats.ActivePiles = Piles.Num();
	return Stats;
}

// ------------------------------------------------------------------------------------------------
// Table bookkeeping
// ------------------------------------------------------------------------------------------------

const UUnitTemplate* UMassUnitStackSystem::GetTemplate(const FMassEntityHandle& Unit) const
{
	const FMassUnitVisualFragment* Visual = EntitySubsystem
		? EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(Unit)
		: nullptr;
	return Visual ? Visual->SourceTemplate.Get() : nullptr;
}

bool UMassUnitStackSystem::IsUnitUsable(const FMassEntityHandle& Unit) const
{
	if (!EntitySubsystem || !UnitManager)
	{
		return false;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	if (!EntityManager.IsEntityValid(Unit))
	{
		return false;
	}
	const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit);
	const FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit);
	return State && Crowd
		&& State->CurrentState != EMassUnitState::Dead
		&& State->CurrentState != EMassUnitState::Stunned
		&& !Crowd->bSleeping
		&& !Crowd->bCommanded   // under player/AI orders (UMassUnitCommandSystem): never piled automatically
		&& !UnitManager->IsUnitAirborne(FMassUnitHandle(Unit));
}

void UMassUnitStackSystem::SetStackDriven(const FMassEntityHandle& Unit, bool bDriven) const
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
	if (FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit))
	{
		Crowd->bStackDriven = bDriven;
		Crowd->SteeringDirection = FVector::ZeroVector;
	}
	if (bDriven)
	{
		if (FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit))
		{
			if (State->CurrentState == EMassUnitState::Moving)
			{
				State->CurrentState = EMassUnitState::Idle;
				State->StateTime = 0.0f;
			}
		}
		if (FMassUnitVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(Unit))
		{
			Velocity->Value = FVector::ZeroVector;
		}
	}
	if (UnitManager)
	{
		FGameplayTagContainer Tags(UE::MassUnitSystem::Tags::Stacked());
		if (bDriven)
		{
			UnitManager->AddUnitTags(FMassUnitHandle(Unit), Tags);
		}
		else
		{
			UnitManager->RemoveUnitTags(FMassUnitHandle(Unit), Tags);
		}
	}
}

bool UMassUnitStackSystem::AddClimber(const FMassEntityHandle& Climber, const FMassEntityHandle& Support, AActor* Target, int32 PileKey)
{
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (!Settings || !Settings->bEnableStacking || !EntitySubsystem || Climber == Support || Entries.Contains(Climber))
	{
		return false;
	}
	if (!IsUnitUsable(Climber))
	{
		return false;
	}
	FStackEntry* SupportEntry = Entries.Find(Support);
	// A support already in the table may be mid-fall bookkeeping-wise only if dead; usable covers that. Stunned
	// supports in the table keep holding (a stun does not topple a pile), so only check new ground supports fully.
	if (SupportEntry)
	{
		const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
		const FMassUnitStateFragment* SupportState = EntityManager.IsEntityValid(Support)
			? EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Support)
			: nullptr;
		if (!SupportState || SupportState->CurrentState == EMassUnitState::Dead || UnitManager->IsUnitAirborne(FMassUnitHandle(Support)))
		{
			return false;
		}
	}
	else if (!IsUnitUsable(Support))
	{
		return false;
	}

	const FMassUnitStackingConfig& ClimberConfig = MassUnitStack::GetConfig(GetTemplate(Climber));
	const FMassUnitStackingConfig& SupportConfig = MassUnitStack::GetConfig(GetTemplate(Support));
	if (!ClimberConfig.bCanStack || !SupportConfig.bCanBeClimbed)
	{
		return false;
	}
	const int32 SupportLevel = SupportEntry ? SupportEntry->Level : 0;
	const int32 RiderCount = SupportEntry ? SupportEntry->RiderCount : 0;
	if (SupportLevel + 1 > ClimberConfig.MaxStackLevel || RiderCount >= FMath::Max(1, SupportConfig.MaxRiders))
	{
		return false;
	}
	const int32 NewEntries = SupportEntry ? 1 : 2;
	if (Entries.Num() + NewEntries > Settings->MaxStackedUnits)
	{
		return false;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const FMassUnitTransformFragment* ClimberTransform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Climber);
	const FMassUnitTransformFragment* SupportTransform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Support);
	if (!ClimberTransform || !SupportTransform)
	{
		return false;
	}

	if (!SupportEntry)
	{
		FStackEntry Ground;
		Ground.BaseZ = SupportTransform->GetTransform().GetLocation().Z;
		Ground.Level = 0;
		Ground.PileKey = PileKey;
		Ground.Target = Target;
		Ground.bArrived = true;
		Ground.Serial = NextSerial++;
		Entries.Add(Support, Ground);
		SetStackDriven(Support, true);
		OnUnitStackChanged.Broadcast(FMassUnitHandle(Support), FMassUnitHandle(), 0, true);
		SupportEntry = Entries.Find(Support);
	}
	FStackEntry Entry;
	Entry.Support = Support;
	Entry.SupportSerial = SupportEntry->Serial;
	Entry.Serial = NextSerial++;
	Entry.RiderSlot = SupportEntry->RiderCount;
	SupportEntry->RiderCount += 1;
	Entry.Target = Target;
	// A climber standing on a climber keeps the ground height of the pile's base, so a fall never lands mid-air.
	Entry.BaseZ = FMath::Min(ClimberTransform->GetTransform().GetLocation().Z, SupportEntry->BaseZ);
	Entry.Level = SupportLevel + 1;
	Entry.PileKey = PileKey;
	Entries.Add(Climber, Entry);
	SetStackDriven(Climber, true);
	++TotalClimbsStarted;
	OnUnitStackChanged.Broadcast(FMassUnitHandle(Climber), FMassUnitHandle(Support), Entry.Level, true);
	return true;
}

void UMassUnitStackSystem::RemoveEntry(const FMassEntityHandle& Unit, bool bFall, bool bCountCollapse)
{
	FStackEntry Entry;
	if (!Entries.RemoveAndCopyValue(Unit, Entry))
	{
		return;
	}
	if (Entry.Level > 0)
	{
		FStackEntry* SupportEntry = Entries.Find(Entry.Support);
		if (SupportEntry && SupportEntry->Serial == Entry.SupportSerial)
		{
			SupportEntry->RiderCount = FMath::Max(0, SupportEntry->RiderCount - 1);
		}
	}
	SetStackDriven(Unit, false);

	const bool bValid = EntitySubsystem && EntitySubsystem->GetEntityManager().IsEntityValid(Unit);
	if (bCountCollapse && Entry.Level > 0)
	{
		++TotalCollapses;
		if (bValid)
		{
			OnUnitStackCollapsed.Broadcast(FMassUnitHandle(Unit), Entry.Level);
		}
	}
	if (bValid && Entry.Level > 0)
	{
		DropUnit(Unit, Entry, bFall);
	}
	// Riders of this unit notice their missing support on the next tick and fall in turn (the cascade).
	OnUnitStackChanged.Broadcast(FMassUnitHandle(Unit), FMassUnitHandle(), Entry.Level, false);
}

void UMassUnitStackSystem::DropUnit(const FMassEntityHandle& Unit, const FStackEntry& Entry, bool bFall) const
{
	if (!EntitySubsystem || !UnitManager)
	{
		return;
	}
	const FMassUnitHandle UnitHandle(Unit);
	if (UnitManager->IsUnitAirborne(UnitHandle))
	{
		UnitManager->CapLaunchLandingHeight(UnitHandle, Entry.BaseZ);
		return;   // already thrown (a blast, a kill fling): the launch lands it
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Unit);
	const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit);
	if (!Transform || !State)
	{
		return;
	}
	const FVector Location = Transform->GetTransform().GetLocation();
	const bool bDead = State->CurrentState == EMassUnitState::Dead;
	if (bDead || bFall)
	{
		const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
		FVector Outward = FVector::ZeroVector;
		if (const AActor* Target = Entry.Target.Get())
		{
			Outward = (Location - Target->GetActorLocation()).GetSafeNormal2D();
		}
		if (Outward.IsNearlyZero())
		{
			Outward = -Transform->GetTransform().GetRotation().GetForwardVector().GetSafeNormal2D();
		}
		// A corpse just slides off; a living climber is thrown clear. Template scale is skipped so even a heavy falls.
		const FVector Velocity = bDead
			? Outward * 60.0f + FVector(0.0f, 0.0f, 40.0f)
			: Outward * (Settings ? Settings->CollapseOutwardSpeed : 260.0f) + FVector(0.0f, 0.0f, Settings ? Settings->CollapseUpSpeed : 220.0f);
		if (UnitManager->LaunchUnit(UnitHandle, Velocity, nullptr, /*bApplyTemplateScale*/ false))
		{
			UnitManager->CapLaunchLandingHeight(UnitHandle, Entry.BaseZ);
			return;
		}
	}
	// Set down (or the launch was refused: launches off, airborne cap): put it back on its ground height.
	FTransform& Mutable = Transform->GetMutableTransform();
	Mutable.SetLocation(FVector(Location.X, Location.Y, Entry.BaseZ));
}

const UMassUnitStackSystem::FStackEntry* UMassUnitStackSystem::FindLiveSupport(const FStackEntry& Entry) const
{
	const FStackEntry* SupportEntry = Entries.Find(Entry.Support);
	return SupportEntry && SupportEntry->Serial == Entry.SupportSerial ? SupportEntry : nullptr;
}

int32 UMassUnitStackSystem::CollapsePile(int32 PileKey, const AActor* KeepTarget)
{
	TArray<FMassEntityHandle> ToCollapse;
	for (const TPair<FMassEntityHandle, FStackEntry>& Pair : Entries)
	{
		if (Pair.Value.PileKey == PileKey && Pair.Value.Level > 0 && (!KeepTarget || Pair.Value.Target.Get() != KeepTarget))
		{
			ToCollapse.Add(Pair.Key);
		}
	}
	for (const FMassEntityHandle& Handle : ToCollapse)
	{
		RemoveEntry(Handle, true, true);
	}
	return ToCollapse.Num();
}

void UMassUnitStackSystem::CollapseOrphanPiles(const TArray<int32>& LiveGroupHandles)
{
	TSet<int32> Orphans;
	for (const TPair<FMassEntityHandle, FStackEntry>& Pair : Entries)
	{
		if (Pair.Value.PileKey != INDEX_NONE && Pair.Value.Level > 0 && !LiveGroupHandles.Contains(Pair.Value.PileKey))
		{
			Orphans.Add(Pair.Value.PileKey);
		}
	}
	for (const int32 PileKey : Orphans)
	{
		CollapsePile(PileKey, nullptr);
	}
}

int32 UMassUnitStackSystem::CountPileClimbers(int32 PileKey) const
{
	int32 Count = 0;
	for (const TPair<FMassEntityHandle, FStackEntry>& Pair : Entries)
	{
		Count += (Pair.Value.PileKey == PileKey && Pair.Value.Level > 0) ? 1 : 0;
	}
	return Count;
}

// ------------------------------------------------------------------------------------------------
// Tick
// ------------------------------------------------------------------------------------------------

void UMassUnitStackSystem::TickStacks(float DeltaTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_TickStacks);
	if (Entries.IsEmpty() || !EntitySubsystem || !UnitManager)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaTime, 0.1f);
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const float CorpseHoldSeconds = Settings ? Settings->SupportCorpseHoldSeconds : 0.0f;

	struct FPendingRemoval
	{
		FMassEntityHandle Unit;
		bool bFall = false;
		bool bCollapse = false;
	};
	TArray<FPendingRemoval> Removals;

	for (TPair<FMassEntityHandle, FStackEntry>& Pair : Entries)
	{
		const FMassEntityHandle Unit = Pair.Key;
		FStackEntry& Entry = Pair.Value;
		if (!EntityManager.IsEntityValid(Unit))
		{
			Removals.Add({ Unit, false, false });
			continue;
		}
		FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Unit);
		FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Unit);
		FMassUnitCrowdFragment* Crowd = EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Unit);
		FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Unit);
		if (!Transform || !State || !Crowd || !Visual)
		{
			Removals.Add({ Unit, false, false });
			continue;
		}
		// Someone else took the unit back (its crowd group was unregistered and reset the fragment).
		if (!Crowd->bStackDriven)
		{
			Removals.Add({ Unit, true, Entry.Level > 0 });
			continue;
		}
		if (State->CurrentState == EMassUnitState::Dead)
		{
			// A corpse killed in place holds its riders up a moment, then drops and the pile above it slumps. One
			// thrown by its kill (airborne) or with nothing on it leaves at once.
			if (Entry.RiderCount > 0 && Entry.DeadSeconds < CorpseHoldSeconds && !UnitManager->IsUnitAirborne(FMassUnitHandle(Unit)))
			{
				Entry.DeadSeconds += Dt;
				continue;
			}
			Removals.Add({ Unit, false, false });   // the corpse drops; its riders collapse next tick
			continue;
		}
		if (UnitManager->IsUnitAirborne(FMassUnitHandle(Unit)))
		{
			Removals.Add({ Unit, false, Entry.Level > 0 });   // blasted off the pile: the launch already owns it
			continue;
		}

		const UUnitTemplate* Template = Visual->SourceTemplate.Get();
		const FMassUnitStackingConfig& Config = MassUnitStack::GetConfig(Template);

		if (Entry.Level == 0)
		{
			if (Entry.RiderCount <= 0)
			{
				Removals.Add({ Unit, false, false });   // nobody on it any more: back to the crowd
				continue;
			}
			if (State->CurrentState != EMassUnitState::Attacking && State->CurrentState != EMassUnitState::Stunned)
			{
				Visual->CurrentAnimation = MassUnitStack::HasClip(Template, UE::MassUnitSystem::Tags::AnimationHold())
					? UE::MassUnitSystem::Tags::AnimationHold()
					: UE::MassUnitSystem::Tags::AnimationIdle();
			}
			continue;
		}

		// Climber: its support must still be in the table (a dead one stays there only while its corpse holds) and not
		// thrown off.
		const FStackEntry* SupportEntry = FindLiveSupport(Entry);
		const FMassUnitTransformFragment* SupportTransform = SupportEntry && EntityManager.IsEntityValid(Entry.Support)
			? EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Entry.Support)
			: nullptr;
		if (!SupportTransform || UnitManager->IsUnitAirborne(FMassUnitHandle(Entry.Support)))
		{
			Removals.Add({ Unit, true, true });
			continue;
		}

		const FVector Current = Transform->GetTransform().GetLocation();
		const AActor* Target = Entry.Target.Get();
		const bool bHadTarget = !Entry.Target.IsExplicitlyNull();
		if (bHadTarget && !IsValid(Target))
		{
			Removals.Add({ Unit, true, true });
			continue;
		}
		if (Target && FVector::DistSquared2D(Current, Target->GetActorLocation())
			> FMath::Square(Config.PileRadius + Config.PileBreakSlack))
		{
			Removals.Add({ Unit, true, true });   // the target broke away from the pile
			continue;
		}

		const FTransform& SupportWorld = SupportTransform->GetTransform();
		FVector Forward = Target
			? (Target->GetActorLocation() - SupportWorld.GetLocation()).GetSafeNormal2D()
			: SupportWorld.GetRotation().GetForwardVector().GetSafeNormal2D();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}
		FVector Desired = SupportWorld.GetLocation() + Forward * Config.StackForwardOffset;
		if (Entry.RiderSlot > 0)
		{
			// Extra riders (MaxRiders > 1) stand beside the first: alternate right/left, one StackHeight apart.
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
			const float Step = static_cast<float>((Entry.RiderSlot + 1) / 2) * (Entry.RiderSlot % 2 == 1 ? 1.0f : -1.0f);
			Desired += Side * Step * Config.StackHeight;
		}
		Desired.Z = SupportWorld.GetLocation().Z + Config.StackHeight;
		const FVector NewLocation = FMath::VInterpConstantTo(Current, Desired, Dt, Config.ClimbSpeed);
		Entry.bArrived = FVector::DistSquared(NewLocation, Desired) <= FMath::Square(MassUnitStack::ArriveTolerance);

		FTransform& Mutable = Transform->GetMutableTransform();
		Mutable.SetLocation(NewLocation);
		Mutable.SetRotation(FQuat(FRotator(0.0f, Forward.Rotation().Yaw, 0.0f)));

		if (State->CurrentState == EMassUnitState::Moving)
		{
			State->CurrentState = EMassUnitState::Idle;
			State->StateTime = 0.0f;
		}
		if (State->CurrentState == EMassUnitState::Attacking)
		{
			// Multi-role: a climber in reach swings even before it reaches its place; the crowd set the attack clip.
		}
		else if (!Entry.bArrived)
		{
			Visual->CurrentAnimation = MassUnitStack::HasClip(Template, UE::MassUnitSystem::Tags::AnimationClimb())
				? UE::MassUnitSystem::Tags::AnimationClimb()
				: UE::MassUnitSystem::Tags::AnimationRun();
		}
		else if (State->CurrentState != EMassUnitState::Attacking && State->CurrentState != EMassUnitState::Stunned)
		{
			Visual->CurrentAnimation = Entry.RiderCount > 0 && MassUnitStack::HasClip(Template, UE::MassUnitSystem::Tags::AnimationHold())
				? UE::MassUnitSystem::Tags::AnimationHold()
				: UE::MassUnitSystem::Tags::AnimationIdle();
		}
	}

	for (const FPendingRemoval& Removal : Removals)
	{
		RemoveEntry(Removal.Unit, Removal.bFall, Removal.bCollapse);
	}

	DrawStackDebug();
}

// ------------------------------------------------------------------------------------------------
// Pile planner
// ------------------------------------------------------------------------------------------------

void UMassUnitStackSystem::UpdatePiles(float CurrentTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_PilePlanner);
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (!Settings || !Settings->bEnableStacking || !Settings->bEnableAutomaticPiles || !CrowdSystem || !EntitySubsystem
		|| (World && World->GetNetMode() == NM_Client))
	{
		return;
	}

	TArray<int32> GroupHandles;
	CrowdSystem->GetCrowdGroupHandles(GroupHandles);
	if (!Entries.IsEmpty())
	{
		CollapseOrphanPiles(GroupHandles);
	}
	if (GroupHandles.IsEmpty())
	{
		NextPlanTime.Reset();
		return;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const int32 GroupHandle : GroupHandles)
	{
		float& NextTime = NextPlanTime.FindOrAdd(GroupHandle, CurrentTime + static_cast<float>(GroupHandle % 8) * 0.031f);
		if (CurrentTime < NextTime)
		{
			continue;
		}
		NextTime = CurrentTime + Settings->PilePlanInterval;

		AActor* Target = CrowdSystem->IsCrowdGroupEngaged(GroupHandle) ? CrowdSystem->GetCrowdGroupTargetActor(GroupHandle) : nullptr;
		const TArray<FMassUnitEntityHandle>* Units = CrowdSystem->GetCrowdGroupUnitsInternal(GroupHandle);
		// A group that disengaged, routed or lost its target brings its pile down; one that switched target brings down
		// the climbers still piled on the old one.
		if (!Entries.IsEmpty())
		{
			CollapsePile(GroupHandle, IsValid(Target) ? Target : nullptr);
		}
		if (!IsValid(Target) || !Units)
		{
			continue;
		}

		// The group's stack config: the first template that can stack. A group with none backs off for a while, so
		// adopters without stacking templates pay almost nothing.
		const UUnitTemplate* PileTemplate = nullptr;
		const UUnitTemplate* LastTemplate = nullptr;
		for (const FMassUnitEntityHandle& Unit : *Units)
		{
			const FMassEntityHandle Handle = Unit.ToMassEntityHandle();
			const UUnitTemplate* Template = EntityManager.IsEntityValid(Handle) ? GetTemplate(Handle) : nullptr;
			if (Template && Template != LastTemplate)
			{
				LastTemplate = Template;
				if (Template->Stacking.bCanStack)
				{
					PileTemplate = Template;
					break;
				}
			}
		}
		if (!PileTemplate)
		{
			NextTime = CurrentTime + FMath::Max(5.0f, Settings->PilePlanInterval);
			continue;
		}

		TArray<FMassUnitEntityHandle> Candidates;
		const FVector TargetLocation = Target->GetActorLocation();
		const float RadiusSquared = FMath::Square(PileTemplate->Stacking.PileRadius);
		for (const FMassUnitEntityHandle& Unit : *Units)
		{
			const FMassEntityHandle Handle = Unit.ToMassEntityHandle();
			if (!EntityManager.IsEntityValid(Handle))
			{
				continue;
			}
			const UUnitTemplate* Template = GetTemplate(Handle);
			const FMassUnitStackingConfig& Config = MassUnitStack::GetConfig(Template);
			if (!Config.bCanStack && !Config.bCanBeClimbed)
			{
				continue;
			}
			const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Handle);
			if (Transform && FVector::DistSquared2D(Transform->GetTransform().GetLocation(), TargetLocation) <= RadiusSquared)
			{
				Candidates.Add(Unit);
			}
		}
		if (!Candidates.IsEmpty())
		{
			PlanPile(Candidates, TargetLocation, Target, GroupHandle);
		}
	}

	// Forget groups that no longer exist.
	if (NextPlanTime.Num() > GroupHandles.Num())
	{
		TSet<int32> Live(GroupHandles);
		for (auto It = NextPlanTime.CreateIterator(); It; ++It)
		{
			if (!Live.Contains(It->Key))
			{
				It.RemoveCurrent();
			}
		}
	}
}

int32 UMassUnitStackSystem::PlanPile(const TArray<FMassUnitEntityHandle>& Candidates, const FVector& TargetLocation, AActor* Target, int32 PileKey)
{
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (!Settings || !Settings->bEnableStacking || !EntitySubsystem || Candidates.IsEmpty())
	{
		return 0;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();

	struct FCandidate
	{
		FMassEntityHandle Handle;
		FVector Location = FVector::ZeroVector;
		float Distance = 0.0f;
		const UUnitTemplate* Template = nullptr;
	};
	TArray<FCandidate> Pool;
	Pool.Reserve(Candidates.Num());
	const UUnitTemplate* PileTemplate = nullptr;
	for (const FMassUnitEntityHandle& Unit : Candidates)
	{
		const FMassEntityHandle Handle = Unit.ToMassEntityHandle();
		const FStackEntry* Entry = Entries.Find(Handle);
		// Units in another pile (or a manual stack) are not this pile's to use.
		if (Entry ? Entry->PileKey != PileKey : !IsUnitUsable(Handle))
		{
			continue;
		}
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Handle);
		if (!Transform)
		{
			continue;
		}
		FCandidate& Candidate = Pool.AddDefaulted_GetRef();
		Candidate.Handle = Handle;
		Candidate.Location = Transform->GetTransform().GetLocation();
		Candidate.Distance = FVector::Dist2D(Candidate.Location, TargetLocation);
		Candidate.Template = GetTemplate(Handle);
		if (!PileTemplate && Candidate.Template && Candidate.Template->Stacking.bCanStack)
		{
			PileTemplate = Candidate.Template;
		}
	}
	if (!PileTemplate)
	{
		return 0;
	}
	const FMassUnitStackingConfig& PileConfig = PileTemplate->Stacking;
	Pool.Sort([](const FCandidate& A, const FCandidate& B) { return A.Distance < B.Distance; });
	if (Pool.Num() > Settings->MaxPileCandidates)
	{
		Pool.SetNum(Settings->MaxPileCandidates);
	}
	if (Pool.Num() < PileConfig.PileMinUnits)
	{
		return 0;
	}

	const int32 Existing = CountPileClimbers(PileKey);
	int32 Budget = FMath::Min(Settings->MaxClimbersPerPilePass, Settings->MaxStackersPerTarget - Existing);
	int32 Started = 0;
	const float MinCloser = 10.0f;

	// Outer units climb first, onto the best support nearer the target: the highest level, then the closest in.
	for (int32 ClimberIndex = Pool.Num() - 1; ClimberIndex >= 0 && Budget > 0; --ClimberIndex)
	{
		const FCandidate& Climber = Pool[ClimberIndex];
		if (Entries.Contains(Climber.Handle) || !Climber.Template || !Climber.Template->Stacking.bCanStack)
		{
			continue;
		}
		// Multi-role: a unit already swinging at the target keeps attacking instead of breaking off to climb.
		const FMassUnitStateFragment* ClimberState = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Climber.Handle);
		if (ClimberState && ClimberState->CurrentState == EMassUnitState::Attacking)
		{
			continue;
		}
		// Attitude: some units never climb (they still hold others up).
		if (ClimberState && ClimberState->bAvoidsClimbing)
		{
			continue;
		}
		const FMassUnitStackingConfig& ClimberConfig = Climber.Template->Stacking;
		const float ReachSquared = FMath::Square(ClimberConfig.SupportReach);
		int32 BestIndex = INDEX_NONE;
		float BestScore = -TNumericLimits<float>::Max();
		for (int32 SupportIndex = 0; SupportIndex < ClimberIndex; ++SupportIndex)
		{
			const FCandidate& Support = Pool[SupportIndex];
			if (Support.Distance > Climber.Distance - MinCloser)
			{
				break;   // sorted: everything after is no nearer the target
			}
			if (FVector::DistSquared2D(Support.Location, Climber.Location) > ReachSquared)
			{
				continue;
			}
			const FMassUnitStackingConfig& SupportConfig = MassUnitStack::GetConfig(Support.Template);
			if (!SupportConfig.bCanBeClimbed)
			{
				continue;
			}
			const FStackEntry* SupportEntry = Entries.Find(Support.Handle);
			const int32 Level = SupportEntry ? SupportEntry->Level : 0;
			const int32 Riders = SupportEntry ? SupportEntry->RiderCount : 0;
			if (Level + 1 > ClimberConfig.MaxStackLevel || Riders >= FMath::Max(1, SupportConfig.MaxRiders))
			{
				continue;
			}
			const float Score = static_cast<float>(Level) * 10000.0f - Support.Distance;
			if (Score > BestScore)
			{
				BestScore = Score;
				BestIndex = SupportIndex;
			}
		}
		if (BestIndex != INDEX_NONE && AddClimber(Climber.Handle, Pool[BestIndex].Handle, Target, PileKey))
		{
			++Started;
			--Budget;
		}
	}
	return Started;
}

// ------------------------------------------------------------------------------------------------
// Debug
// ------------------------------------------------------------------------------------------------

void UMassUnitStackSystem::DrawStackDebug() const
{
#if ENABLE_DRAW_DEBUG
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const bool bDraw = MassUnitStack::GStackDebug != 0 || (Settings && Settings->bEnableDebugVisualization);
	if (!bDraw || !World || !EntitySubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	for (const TPair<FMassEntityHandle, FStackEntry>& Pair : Entries)
	{
		const FMassUnitTransformFragment* Transform = EntityManager.IsEntityValid(Pair.Key)
			? EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Pair.Key)
			: nullptr;
		if (!Transform)
		{
			continue;
		}
		const FVector Location = Transform->GetTransform().GetLocation();
		if (Pair.Value.Level == 0)
		{
			DrawDebugCircle(World, Location + FVector(0.0f, 0.0f, 4.0f), 35.0f, 12, FColor::White, false, -1.0f, 0, 2.0f,
				FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), false);
			continue;
		}
		const FMassUnitTransformFragment* SupportTransform = EntityManager.IsEntityValid(Pair.Value.Support)
			? EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Pair.Value.Support)
			: nullptr;
		const FColor Color = Pair.Value.bArrived ? FColor::Cyan : FColor::Green;
		if (SupportTransform)
		{
			DrawDebugLine(World, Location, SupportTransform->GetTransform().GetLocation(), Color, false, -1.0f, 0, 2.0f);
		}
		DrawDebugString(World, Location + FVector(0.0f, 0.0f, 40.0f), FString::Printf(TEXT("L%d"), Pair.Value.Level), nullptr, Color, -1.0f, true);
	}
	if (GEngine)
	{
		const FMassUnitStackStats Stats = GetStackStats();
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x4D55537400ull), 0.0f, FColor::Cyan,
			FString::Printf(TEXT("Mass stacks: %d units (%d climbers, %d holding) | piles %d | top L%d | climbs %d | collapses %d"),
				Stats.StackedUnits, Stats.Climbers, Stats.GroundSupports, Stats.ActivePiles, Stats.HighestLevel,
				Stats.TotalClimbsStarted, Stats.TotalCollapses));
	}
#endif
}
