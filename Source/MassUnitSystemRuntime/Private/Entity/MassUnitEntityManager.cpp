// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Entity/MassUnitEntityManager.h"
#include "Core/MassUnitStats.h"

DECLARE_CYCLE_STAT(TEXT("Launches (airborne units)"), STAT_MassUnit_TickLaunches, STATGROUP_MassUnitSystem);
DECLARE_CYCLE_STAT(TEXT("Timed Statuses"), STAT_MassUnit_TickStatuses, STATGROUP_MassUnitSystem);
DECLARE_CYCLE_STAT(TEXT("Prune Invalid Units"), STAT_MassUnit_PruneInvalidUnits, STATGROUP_MassUnitSystem);

#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Core/MassUnitSubsystem.h"
#include "Gameplay/MassUnitCommandSystem.h"
#include "Core/MassUnitSystemRuntime.h"
#include "Entity/UnitTemplate.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassUnitCommonFragments.h"
#include "TimerManager.h"

namespace
{
	/** Status timers advance at 10 Hz; DoT periods shorter than this are quantised to it. */
	constexpr float StatusTickInterval = 0.1f;
}

void UMassUnitEntityManager::Initialize(UMassEntitySubsystem* InEntitySubsystem)
{
	EntitySubsystem = InEntitySubsystem;
	UE_LOG(LogMassUnitSystem, Log, TEXT("Unit entity manager initialized"));
}

void UMassUnitEntityManager::Deinitialize()
{
	// The world-owned Mass entity manager destroys its entities during world teardown.
	// It can already be unavailable when dependent world subsystems are deinitialized,
	// so touching it here would be both redundant and unsafe.
	AllUnits.Reset();
	UnitTypeMap.Reset();
	TeamMap.Reset();
	UnitsWithStatus.Reset();
	NextStatusTickTime = 0.0f;
	ActiveLaunches.Reset();
	PendingRagdollRequests.Reset();
	UnitArchetype = FMassArchetypeHandle();
	RuntimeDefaultTemplate = nullptr;
	EntitySubsystem = nullptr;
}

FMassUnitHandle UMassUnitEntityManager::CreateUnitFromTemplate(UUnitTemplate* Template, const FTransform& SpawnTransform)
{
	return FMassUnitHandle(CreateUnitFromTemplateInternal(Template, SpawnTransform));
}

FMassUnitHandle UMassUnitEntityManager::CreateDefaultUnit(const FTransform& SpawnTransform)
{
	return CreateUnitFromTemplate(GetOrCreateDefaultTemplate(), SpawnTransform);
}

UUnitTemplate* UMassUnitEntityManager::GetOrCreateDefaultTemplate()
{
	if (!RuntimeDefaultTemplate)
	{
		RuntimeDefaultTemplate = NewObject<UUnitTemplate>(this, TEXT("RuntimeDefaultUnitTemplate"), RF_Transient);
	}
	return RuntimeDefaultTemplate;
}

TArray<FMassUnitHandle> UMassUnitEntityManager::CreateUnitsFromTemplate(
	UUnitTemplate* Template,
	const TArray<FTransform>& SpawnTransforms,
	const FMassUnitBatchSpawnOptions& Options)
{
	TArray<FMassUnitHandle> NewUnits;
	if (SpawnTransforms.IsEmpty())
	{
		return NewUnits;
	}
	UUnitTemplate* ResolvedTemplate = Template ? Template : GetOrCreateDefaultTemplate();

	// Load the template's soft references once; every unit after the first then resolves them from memory.
	ResolvedTemplate->SkeletalMesh.LoadSynchronous();
	ResolvedTemplate->AnimationBlueprintClass.LoadSynchronous();
	ResolvedTemplate->StaticMesh.LoadSynchronous();
	ResolvedTemplate->VertexAnimationTexture.LoadSynchronous();
	ResolvedTemplate->NormalMapTexture.LoadSynchronous();

	int32 Budget = SpawnTransforms.Num();
	if (const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>())
	{
		Budget = FMath::Clamp(Settings->MaxUnits - AllUnits.Num(), 0, Budget);
		if (Budget < SpawnTransforms.Num())
		{
			UE_LOG(LogMassUnitSystem, Warning, TEXT("Batch spawn clipped to %d of %d units by the unit limit (%d)"),
				Budget, SpawnTransforms.Num(), Settings->MaxUnits);
		}
	}
	NewUnits.Reserve(Budget);
	AllUnits.Reserve(AllUnits.Num() + Budget);

	FRandomStream VariantStream(Options.RandomSeed);
	for (int32 Index = 0; Index < Budget; ++Index)
	{
		const FMassUnitEntityHandle Handle = CreateUnitInternal(
			ResolvedTemplate, SpawnTransforms[Index], &Options, Options.bSeedVariants ? &VariantStream : nullptr);
		if (!Handle.IsValid())
		{
			break;
		}
		NewUnits.Add(FMassUnitHandle(Handle));
	}
	return NewUnits;
}

FMassUnitEntityHandle UMassUnitEntityManager::CreateUnitFromTemplateInternal(UUnitTemplate* Template, const FTransform& SpawnTransform)
{
	return CreateUnitInternal(Template, SpawnTransform, nullptr, nullptr);
}

FMassUnitEntityHandle UMassUnitEntityManager::CreateUnitInternal(
	UUnitTemplate* Template,
	const FTransform& SpawnTransform,
	const FMassUnitBatchSpawnOptions* Options,
	FRandomStream* VariantStream)
{
	if (!Template || !EntitySubsystem)
	{
		UE_LOG(LogMassUnitSystem, Warning, TEXT("CreateUnitFromTemplate rejected an invalid template or uninitialized manager"));
		return {};
	}

	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (Settings && AllUnits.Num() >= Settings->MaxUnits)
	{
		UE_LOG(LogMassUnitSystem, Warning, TEXT("Unit limit (%d) reached"), Settings->MaxUnits);
		return {};
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	if (!UnitArchetype.IsValid())
	{
		UnitArchetype = EntityManager.CreateArchetype(Template->GetRequiredFragments());
	}

	const FMassEntityHandle NativeHandle = EntityManager.CreateEntity(UnitArchetype);
	if (!EntityManager.IsEntityValid(NativeHandle))
	{
		UE_LOG(LogMassUnitSystem, Error, TEXT("Mass failed to create a unit entity"));
		return {};
	}

	FMassEntityView EntityView(EntityManager, NativeHandle);
	EntityView.GetFragmentData<FMassUnitTransformFragment>().SetTransform(SpawnTransform);

	FMassUnitStateFragment& State = EntityView.GetFragmentData<FMassUnitStateFragment>();
	State.CurrentState = EMassUnitState::Idle;
	State.UnitType = Template->UnitType.IsValid() ? Template->UnitType : UE::MassUnitSystem::Tags::UnitTypeDefault();
	State.UnitClass = Template->UnitClass;
	State.DefaultBehavior = Template->DefaultBehavior;
	State.UnitLevel = FMath::Max(1, Template->BaseLevel);
	State.MaxHealth = FMath::Max(0.0f, static_cast<float>(Template->BaseHealth));
	State.Health = State.MaxHealth;
	State.BaseDamage = FMath::Max(0.0f, static_cast<float>(Template->BaseDamage));
	State.MoveSpeed = FMath::Max(0.0f, Template->MoveSpeed);
	State.AttackRange = FMath::Max(0.0f, Template->AttackRange);
	State.AttackCooldown = FMath::Max(0.0f, Template->AttackCooldown);
	State.AttackImpactDelay = FMath::Max(0.0f, Template->AttackImpactDelay);

	FMassUnitTeamFragment& Team = EntityView.GetFragmentData<FMassUnitTeamFragment>();
	Team.TeamID = Template->TeamID;
	Team.TeamColor = Template->TeamColor;
	Team.TeamFaction = Template->TeamFaction;
	if (Options && Options->bOverrideTeam)
	{
		Team.TeamID = Options->TeamID;
		if (Options->bOverrideTeamColor)
		{
			Team.TeamColor = Options->TeamColor;
		}
	}

	FMassUnitAbilityFragment& Ability = EntityView.GetFragmentData<FMassUnitAbilityFragment>();
	Ability.DefaultAbilityTags = Template->DefaultAbilities;
	Ability.LooseTags = Template->GrantedTags;
	Ability.StatusImmunityTags = Template->StatusImmunityTags;
	Ability.RebuildTags();

	FMassUnitVisualFragment& Visual = EntityView.GetFragmentData<FMassUnitVisualFragment>();
	Visual.SkeletalMesh = Template->SkeletalMesh.LoadSynchronous();
	Visual.AnimationBlueprintClass = Template->AnimationBlueprintClass.LoadSynchronous();
	Visual.IdleAnimation = Template->IdleAnimation.LoadSynchronous();
	Visual.MoveAnimation = Template->MoveAnimation.LoadSynchronous();
	Visual.AttackAnimation = Template->AttackAnimation.LoadSynchronous();
	Visual.DeathAnimation = Template->DeathAnimation.LoadSynchronous();
	Visual.StunAnimation = Template->StunAnimation.LoadSynchronous();
	Visual.StaticMesh = Template->StaticMesh.LoadSynchronous();
	Visual.VertexAnimationTexture = Template->VertexAnimationTexture.LoadSynchronous();
	Visual.NormalMapTexture = Template->NormalMapTexture.LoadSynchronous();
	Visual.AnimationTags = Template->AnimationTags;
	Visual.SourceTemplate = Template;
	Visual.RunSpeedThreshold = FMath::Max(0.0f, Template->RunSpeedThreshold);
	// Same golden-ratio value the renderer always derived from the entity index, now stored so it can be replicated.
	Visual.VariantSeed = VariantStream
		? VariantStream->FRand()
		: FMath::Frac(static_cast<float>(NativeHandle.Index) * 0.6180339887f);
	// The look rolls from the seed, so a client puppet that later receives the server's seed re-rolls the same look.
	Visual.LookProfile = Options && Options->LookProfile ? Options->LookProfile.Get() : Template->LookProfile.LoadSynchronous();
	ApplyLook(Visual);
	Visual.AttitudeProfile = Options && Options->AttitudeProfile ? Options->AttitudeProfile.Get() : Template->AttitudeProfile.LoadSynchronous();
	ApplyAttitude(NativeHandle, State, Visual, nullptr);
	Visual.CurrentAnimation = UE::MassUnitSystem::Tags::AnimationIdle();
	Visual.TargetAnimation = Visual.CurrentAnimation;
	Visual.bUseSkeletalMesh = false;
	Visual.bWantsSkeletalMesh = false;
	Visual.bIsVisible = true;

	FMassUnitFormationFragment& Formation = EntityView.GetFragmentData<FMassUnitFormationFragment>();
	Formation.DefaultFormation = Template->DefaultFormation;

	const FMassUnitEntityHandle Handle(NativeHandle);
	AllUnits.Add(Handle);
	UnitTypeMap.FindOrAdd(State.UnitType).Add(Handle);
	TeamMap.FindOrAdd(Team.TeamID).Add(Handle);

	UE_LOG(LogMassUnitSystem, Verbose, TEXT("Created %s (%s, team %d)"), *Handle.ToString(), *State.UnitType.ToString(), Team.TeamID);
	return Handle;
}

void UMassUnitEntityManager::DestroyUnit(FMassUnitHandle UnitHandle)
{
	DestroyUnitInternal(UnitHandle.EntityHandle);
}

void UMassUnitEntityManager::DestroyUnitInternal(FMassUnitEntityHandle EntityHandle)
{
	if (!EntitySubsystem || !EntityHandle.IsValid())
	{
		return;
	}

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = EntityHandle.ToMassEntityHandle();
	if (!EntityManager.IsEntityValid(NativeHandle))
	{
		RemoveHandleFromIndexes(EntityHandle);
		return;
	}

	const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	const FMassUnitTeamFragment* Team = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(NativeHandle);
	if (State)
	{
		if (TArray<FMassUnitEntityHandle>* Units = UnitTypeMap.Find(State->UnitType))
		{
			Units->Remove(EntityHandle);
			if (Units->IsEmpty())
			{
				UnitTypeMap.Remove(State->UnitType);
			}
		}
	}
	if (Team)
	{
		if (TArray<FMassUnitEntityHandle>* Units = TeamMap.Find(Team->TeamID))
		{
			Units->Remove(EntityHandle);
			if (Units->IsEmpty())
			{
				TeamMap.Remove(Team->TeamID);
			}
		}
	}

	AllUnits.Remove(EntityHandle);
	EntityManager.DestroyEntity(NativeHandle);
	UE_LOG(LogMassUnitSystem, Verbose, TEXT("Destroyed %s"), *EntityHandle.ToString());
}

bool UMassUnitEntityManager::IsUnitValid(FMassUnitHandle UnitHandle) const
{
	return EntitySubsystem && UnitHandle.IsValid()
		&& EntitySubsystem->GetEntityManager().IsEntityValid(UnitHandle.EntityHandle.ToMassEntityHandle());
}

int32 UMassUnitEntityManager::GetUnitCount() const
{
	if (!EntitySubsystem)
	{
		return 0;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	int32 ValidUnitCount = 0;
	for (const FMassUnitEntityHandle Handle : AllUnits)
	{
		ValidUnitCount += Handle.IsValid() && EntityManager.IsEntityValid(Handle.ToMassEntityHandle()) ? 1 : 0;
	}
	return ValidUnitCount;
}

TArray<FMassUnitHandle> UMassUnitEntityManager::GetAllUnits() const
{
	TArray<FMassUnitHandle> Result;
	Result.Reserve(AllUnits.Num());
	for (const FMassUnitEntityHandle Handle : AllUnits)
	{
		if (EntitySubsystem && EntitySubsystem->GetEntityManager().IsEntityValid(Handle.ToMassEntityHandle()))
		{
			Result.Emplace(Handle);
		}
	}
	return Result;
}

TArray<FMassUnitHandle> UMassUnitEntityManager::GetUnitsByType(FGameplayTag UnitType) const
{
	TArray<FMassUnitHandle> Result;
	for (const FMassUnitEntityHandle Handle : GetUnitsByTypeInternal(UnitType))
	{
		Result.Emplace(Handle);
	}
	return Result;
}

TArray<FMassUnitHandle> UMassUnitEntityManager::GetUnitsByTeam(int32 TeamID) const
{
	TArray<FMassUnitHandle> Result;
	for (const FMassUnitEntityHandle Handle : GetUnitsByTeamInternal(TeamID))
	{
		Result.Emplace(Handle);
	}
	return Result;
}

TArray<FMassUnitEntityHandle> UMassUnitEntityManager::GetUnitsByTypeInternal(FGameplayTag UnitType) const
{
	if (const TArray<FMassUnitEntityHandle>* Units = UnitTypeMap.Find(UnitType))
	{
		TArray<FMassUnitEntityHandle> Result = *Units;
		Result.RemoveAll([this](const FMassUnitEntityHandle Handle)
		{
			return !EntitySubsystem || !EntitySubsystem->GetEntityManager().IsEntityValid(Handle.ToMassEntityHandle());
		});
		return Result;
	}
	return {};
}

TArray<FMassUnitEntityHandle> UMassUnitEntityManager::GetUnitsByTeamInternal(int32 TeamID) const
{
	if (const TArray<FMassUnitEntityHandle>* Units = TeamMap.Find(TeamID))
	{
		TArray<FMassUnitEntityHandle> Result = *Units;
		Result.RemoveAll([this](const FMassUnitEntityHandle Handle)
		{
			return !EntitySubsystem || !EntitySubsystem->GetEntityManager().IsEntityValid(Handle.ToMassEntityHandle());
		});
		return Result;
	}
	return {};
}

bool UMassUnitEntityManager::GetUnitTransform(FMassUnitHandle UnitHandle, FTransform& OutTransform) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	const FMassUnitTransformFragment* Transform = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitTransformFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Transform)
	{
		return false;
	}
	OutTransform = Transform->GetTransform();
	return true;
}

bool UMassUnitEntityManager::SetUnitTransform(FMassUnitHandle UnitHandle, const FTransform& NewTransform)
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassUnitTransformFragment* Transform = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitTransformFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Transform)
	{
		return false;
	}
	Transform->SetTransform(NewTransform);
	return true;
}

bool UMassUnitEntityManager::SetUnitDestination(FMassUnitHandle UnitHandle, const FVector& Destination, float AcceptanceRadius)
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = UnitHandle.EntityHandle.ToMassEntityHandle();
	FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle);
	FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(NativeHandle);
	if (!Target || !Navigation)
	{
		return false;
	}

	Target->TargetEntity.Invalidate();
	Target->TargetLocation = Destination;
	Target->bHasTargetLocation = true;
	Navigation->DestinationLocation = Destination;
	Navigation->PathPoints = {Destination};
	Navigation->CurrentPathIndex = 0;
	Navigation->AcceptanceRadius = FMath::Max(1.0f, AcceptanceRadius);
	Navigation->bPathRequested = false;
	Navigation->bPathValid = true;
	Navigation->bPathUsesNavmesh = false;
	return true;
}

bool UMassUnitEntityManager::SetUnitTarget(FMassUnitHandle UnitHandle, FMassUnitHandle TargetUnit, float Priority)
{
	if (!IsUnitValid(UnitHandle) || !IsUnitValid(TargetUnit) || UnitHandle.EntityHandle == TargetUnit.EntityHandle)
	{
		return false;
	}
	FMassUnitTargetFragment* Target = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitTargetFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Target)
	{
		return false;
	}
	Target->TargetEntity = TargetUnit.EntityHandle;
	Target->TargetLocation = FVector::ZeroVector;
	Target->bHasTargetLocation = false;
	Target->TargetPriority = Priority;
	if (FMassUnitNavigationFragment* Navigation = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitNavigationFragment>(UnitHandle.EntityHandle.ToMassEntityHandle()))
	{
		Navigation->ResetPath();
	}
	return true;
}

void UMassUnitEntityManager::PruneInvalidUnits()
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_PruneInvalidUnits);
	if (!EntitySubsystem)
	{
		AllUnits.Reset();
		UnitTypeMap.Reset();
		TeamMap.Reset();
		return;
	}

	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	TArray<FMassUnitEntityHandle> InvalidHandles;
	for (const FMassUnitEntityHandle Handle : AllUnits)
	{
		if (!Handle.IsValid() || !EntityManager.IsEntityValid(Handle.ToMassEntityHandle()))
		{
			InvalidHandles.Add(Handle);
		}
	}
	for (const FMassUnitEntityHandle Handle : InvalidHandles)
	{
		RemoveHandleFromIndexes(Handle);
	}
}

void UMassUnitEntityManager::RemoveHandleFromIndexes(FMassUnitEntityHandle EntityHandle)
{
	AllUnits.Remove(EntityHandle);
	UnitsWithStatus.Remove(EntityHandle.ToMassEntityHandle());
	for (auto It = UnitTypeMap.CreateIterator(); It; ++It)
	{
		It.Value().Remove(EntityHandle);
		if (It.Value().IsEmpty())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = TeamMap.CreateIterator(); It; ++It)
	{
		It.Value().Remove(EntityHandle);
		if (It.Value().IsEmpty())
		{
			It.RemoveCurrent();
		}
	}
}

bool UMassUnitEntityManager::ClearUnitTarget(FMassUnitHandle UnitHandle)
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = UnitHandle.EntityHandle.ToMassEntityHandle();
	if (FMassUnitTargetFragment* Target = EntityManager.GetFragmentDataPtr<FMassUnitTargetFragment>(NativeHandle))
	{
		Target->Clear();
	}
	if (FMassUnitNavigationFragment* Navigation = EntityManager.GetFragmentDataPtr<FMassUnitNavigationFragment>(NativeHandle))
	{
		Navigation->ResetPath();
	}
	return true;
}

bool UMassUnitEntityManager::GetUnitState(FMassUnitHandle UnitHandle, FMassUnitStateFragment& OutState) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	const FMassUnitStateFragment* State = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitStateFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!State)
	{
		return false;
	}
	OutState = *State;
	return true;
}

bool UMassUnitEntityManager::GetUnitHealth(
	FMassUnitHandle UnitHandle,
	float& OutHealth,
	float& OutMaxHealth) const
{
	OutHealth = 0.0f;
	OutMaxHealth = 0.0f;
	FMassUnitStateFragment State;
	if (!GetUnitState(UnitHandle, State))
	{
		return false;
	}
	OutHealth = State.Health;
	OutMaxHealth = State.MaxHealth;
	return true;
}

float UMassUnitEntityManager::GetUnitHealthPercent(FMassUnitHandle UnitHandle) const
{
	float Health = 0.0f;
	float MaxHealth = 0.0f;
	return GetUnitHealth(UnitHandle, Health, MaxHealth) && MaxHealth > UE_SMALL_NUMBER
		? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f)
		: 0.0f;
}

bool UMassUnitEntityManager::ApplyDamage(
	FMassUnitHandle UnitHandle,
	float Damage,
	AActor* DamageInstigator)
{
	return ApplyDamageInternal(UnitHandle.EntityHandle, Damage, DamageInstigator);
}

bool UMassUnitEntityManager::ApplyDamageInternal(
	FMassUnitEntityHandle EntityHandle,
	float Damage,
	AActor* DamageInstigator,
	bool bDeferEvents)
{
	const FMassUnitHandle UnitHandle(EntityHandle);
	if (!IsUnitValid(UnitHandle) || Damage <= 0.0f)
	{
		return false;
	}
	FMassUnitStateFragment* State = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitStateFragment>(EntityHandle.ToMassEntityHandle());
	if (!State || State->CurrentState == EMassUnitState::Dead)
	{
		return false;
	}

	const float PreviousHealth = State->Health;
	const float NewHealth = FMath::Max(0.0f, PreviousHealth - Damage);
	if (FMath::IsNearlyEqual(PreviousHealth, NewHealth))
	{
		return false;
	}
	State->Health = NewHealth;
	const bool bDied = NewHealth <= 0.0f;
	if (bDied)
	{
		State->CurrentState = EMassUnitState::Dead;
		State->StateTime = 0.0f;
	}

	if (bDeferEvents && EntitySubsystem && EntitySubsystem->GetWorld())
	{
		TWeakObjectPtr<UMassUnitEntityManager> WeakThis(this);
		TWeakObjectPtr<AActor> WeakInstigator(DamageInstigator);
		EntitySubsystem->GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[WeakThis, WeakInstigator, UnitHandle, PreviousHealth, NewHealth, bDied]()
			{
				if (UMassUnitEntityManager* Manager = WeakThis.Get())
				{
					AActor* InstigatorActor = WeakInstigator.Get();
					Manager->OnUnitHealthChanged.Broadcast(UnitHandle, PreviousHealth, NewHealth, InstigatorActor);
					if (bDied)
					{
						Manager->OnUnitDied.Broadcast(UnitHandle, InstigatorActor);
					}
				}
			}));
	}
	else
	{
		OnUnitHealthChanged.Broadcast(UnitHandle, PreviousHealth, NewHealth, DamageInstigator);
		if (bDied)
		{
			OnUnitDied.Broadcast(UnitHandle, DamageInstigator);
		}
	}
	return true;
}

bool UMassUnitEntityManager::HealUnit(
	FMassUnitHandle UnitHandle,
	float Amount,
	AActor* HealInstigator)
{
	if (!IsUnitValid(UnitHandle) || Amount <= 0.0f)
	{
		return false;
	}
	FMassUnitStateFragment* State = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitStateFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!State || State->CurrentState == EMassUnitState::Dead)
	{
		return false;
	}
	const float PreviousHealth = State->Health;
	const float NewHealth = FMath::Min(State->MaxHealth, PreviousHealth + Amount);
	if (FMath::IsNearlyEqual(PreviousHealth, NewHealth))
	{
		return false;
	}
	State->Health = NewHealth;
	OnUnitHealthChanged.Broadcast(UnitHandle, PreviousHealth, NewHealth, HealInstigator);
	return true;
}

FMassUnitHandle UMassUnitEntityManager::FindClosestUnit(
	FVector WorldLocation,
	float MaxDistance,
	bool bUse3DDistance,
	bool bIncludeDead) const
{
	if (!EntitySubsystem || MaxDistance < 0.0f)
	{
		return {};
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	float BestDistanceSquared = FMath::Square(MaxDistance);
	FMassUnitEntityHandle BestEntity;
	for (const FMassUnitEntityHandle Entity : AllUnits)
	{
		if (!Entity.IsValid() || !EntityManager.IsEntityValid(Entity.ToMassEntityHandle()))
		{
			continue;
		}
		const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
		if (!Transform || (!bIncludeDead && State && State->CurrentState == EMassUnitState::Dead))
		{
			continue;
		}
		const FVector Delta = Transform->GetTransform().GetLocation() - WorldLocation;
		const float DistanceSquared = bUse3DDistance ? Delta.SizeSquared() : Delta.SizeSquared2D();
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestEntity = Entity;
		}
	}
	return FMassUnitHandle(BestEntity);
}

TArray<FMassUnitHandle> UMassUnitEntityManager::GetUnitsInRadius(
	FVector WorldLocation,
	float Radius,
	bool bUse3DDistance,
	bool bIncludeDead) const
{
	TArray<FMassUnitHandle> Result;
	if (!EntitySubsystem || Radius < 0.0f)
	{
		return Result;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const float RadiusSquared = FMath::Square(Radius);
	for (const FMassUnitEntityHandle Entity : AllUnits)
	{
		if (!Entity.IsValid() || !EntityManager.IsEntityValid(Entity.ToMassEntityHandle()))
		{
			continue;
		}
		const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
		if (!Transform || (!bIncludeDead && State && State->CurrentState == EMassUnitState::Dead))
		{
			continue;
		}
		const FVector Delta = Transform->GetTransform().GetLocation() - WorldLocation;
		const float DistanceSquared = bUse3DDistance ? Delta.SizeSquared() : Delta.SizeSquared2D();
		if (DistanceSquared <= RadiusSquared)
		{
			Result.Emplace(Entity);
		}
	}
	return Result;
}

// ============================================================================
// Gameplay tags and statuses
// ============================================================================

float UMassUnitEntityManager::GetStatusClockTime() const
{
	const UWorld* World = EntitySubsystem ? EntitySubsystem->GetWorld() : nullptr;
	return World ? World->GetTimeSeconds() : 0.0f;
}

void UMassUnitEntityManager::RefreshStatusEffects(
	FMassUnitAbilityFragment& Ability,
	FMassUnitStateFragment& State,
	FMassUnitVisualFragment* Visual) const
{
	float SpeedMultiplier = 1.0f;
	bool bStunned = false;
	for (const FMassUnitStatusEffect& Status : Ability.ActiveStatuses)
	{
		SpeedMultiplier *= FMath::Max(0.0f, Status.MoveSpeedMultiplier);
		bStunned |= Status.bStuns;
	}
	State.StatusMoveSpeedMultiplier = SpeedMultiplier;
	Ability.RebuildTags();

	if (State.CurrentState == EMassUnitState::Dead)
	{
		return;
	}
	if (bStunned && State.CurrentState != EMassUnitState::Stunned)
	{
		// A stun interrupts a swing in progress; its damage never lands.
		State.CurrentState = EMassUnitState::Stunned;
		State.StateTime = 0.0f;
		State.PendingAttackDamage = 0.0f;
		if (Visual)
		{
			Visual->CurrentAnimation = UE::MassUnitSystem::Tags::AnimationStun();
		}
	}
	else if (!bStunned && State.CurrentState == EMassUnitState::Stunned)
	{
		State.CurrentState = EMassUnitState::Idle;
		State.StateTime = 0.0f;
		if (Visual)
		{
			Visual->CurrentAnimation = UE::MassUnitSystem::Tags::AnimationIdle();
		}
	}
}

bool UMassUnitEntityManager::ApplyUnitStatus(FMassUnitHandle UnitHandle, const FMassUnitStatusEffect& Status, AActor* StatusInstigator)
{
	if (!IsUnitValid(UnitHandle) || !Status.StatusTag.IsValid())
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = UnitHandle.EntityHandle.ToMassEntityHandle();
	FMassUnitAbilityFragment* Ability = EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(NativeHandle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	if (!Ability || !State || State->CurrentState == EMassUnitState::Dead)
	{
		return false;
	}
	if (Status.StatusTag.MatchesAny(Ability->StatusImmunityTags))
	{
		return false;
	}

	const float Now = GetStatusClockTime();
	FMassUnitStatusEffect* Existing = Ability->ActiveStatuses.FindByPredicate(
		[&Status](const FMassUnitStatusEffect& Active) { return Active.StatusTag == Status.StatusTag; });
	const bool bIsNew = Existing == nullptr;
	// Refresh never SHORTENS: a weaker or briefer re-application (burning ground re-arming every pulse over a 6 s
	// flamethrower burn) keeps the later expiry, as two GE handles would coexist on an actor. Permanent stays permanent.
	const bool bWasPermanent = Existing && Existing->IsPermanent();
	const float PreviousExpireTime = Existing ? Existing->ExpireTime : 0.0f;
	const float PreviousDuration = Existing ? Existing->Duration : 0.0f;
	if (Existing)
	{
		const int32 PreviousStacks = Existing->Stacks;
		const float PreviousNextTick = Existing->NextTickTime;
		const bool bWasTicking = Existing->DamagePerPeriod > 0.0f;
		*Existing = Status;
		Existing->Stacks = Status.bStacks ? FMath::Min(FMath::Max(1, Status.MaxStacks), PreviousStacks + 1) : 1;
		// Refreshing must not reset the tick phase, or re-applying faster than the period would never tick.
		// A status that was NOT ticking never advanced its phase, so it starts a fresh one instead of
		// paying out every period since it was first applied.
		Existing->NextTickTime = bWasTicking
			? PreviousNextTick
			: Now + FMath::Max(0.05f, Status.Period);
	}
	else
	{
		Existing = &Ability->ActiveStatuses.Add_GetRef(Status);
		Existing->Stacks = 1;
		Existing->NextTickTime = Now + FMath::Max(0.05f, Status.Period);
	}
	Existing->Instigator = StatusInstigator;
	if (!bIsNew && bWasPermanent)
	{
		Existing->Duration = PreviousDuration;
		Existing->ExpireTime = 0.0f;
	}
	else
	{
		Existing->ExpireTime = Status.IsPermanent() ? 0.0f : FMath::Max(PreviousExpireTime, Now + Status.Duration);
	}

	RefreshStatusEffects(*Ability, *State, EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle));
	UnitsWithStatus.Add(NativeHandle);
	if (bIsNew)
	{
		OnUnitStatusChanged.Broadcast(UnitHandle, Status.StatusTag, true);
	}
	return true;
}

bool UMassUnitEntityManager::RemoveUnitStatus(FMassUnitHandle UnitHandle, FGameplayTag StatusTag)
{
	if (!StatusTag.IsValid() || !IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = UnitHandle.EntityHandle.ToMassEntityHandle();
	FMassUnitAbilityFragment* Ability = EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(NativeHandle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	if (!Ability || !State)
	{
		return false;
	}
	const int32 RemovedCount = Ability->ActiveStatuses.RemoveAll(
		[&StatusTag](const FMassUnitStatusEffect& Status) { return Status.StatusTag == StatusTag; });
	if (RemovedCount == 0)
	{
		return false;
	}
	RefreshStatusEffects(*Ability, *State, EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle));
	if (Ability->ActiveStatuses.IsEmpty())
	{
		UnitsWithStatus.Remove(NativeHandle);
	}
	OnUnitStatusChanged.Broadcast(UnitHandle, StatusTag, false);
	return true;
}

int32 UMassUnitEntityManager::RemoveUnitStatusesMatching(FMassUnitHandle UnitHandle, FGameplayTagContainer StatusTags)
{
	if (!IsUnitValid(UnitHandle) || StatusTags.IsEmpty())
	{
		return 0;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = UnitHandle.EntityHandle.ToMassEntityHandle();
	FMassUnitAbilityFragment* Ability = EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(NativeHandle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	if (!Ability || !State)
	{
		return 0;
	}

	TArray<FGameplayTag> Removed;
	Ability->ActiveStatuses.RemoveAll([&StatusTags, &Removed](const FMassUnitStatusEffect& Status)
	{
		if (Status.StatusTag.MatchesAny(StatusTags))
		{
			Removed.Add(Status.StatusTag);
			return true;
		}
		return false;
	});
	if (Removed.IsEmpty())
	{
		return 0;
	}
	RefreshStatusEffects(*Ability, *State, EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle));
	if (Ability->ActiveStatuses.IsEmpty())
	{
		UnitsWithStatus.Remove(NativeHandle);
	}
	for (const FGameplayTag& Tag : Removed)
	{
		OnUnitStatusChanged.Broadcast(UnitHandle, Tag, false);
	}
	return Removed.Num();
}

bool UMassUnitEntityManager::StunUnit(FMassUnitHandle UnitHandle, float Duration, AActor* StunInstigator)
{
	if (Duration <= 0.0f)
	{
		return false;
	}
	FMassUnitStatusEffect Stun;
	Stun.StatusTag = UE::MassUnitSystem::Tags::StatusStunned();
	Stun.Duration = Duration;
	Stun.bStuns = true;
	Stun.MoveSpeedMultiplier = 0.0f;
	return ApplyUnitStatus(UnitHandle, Stun, StunInstigator);
}

bool UMassUnitEntityManager::GetUnitPresentation(FMassUnitHandle UnitHandle, EMassUnitState& OutState, uint8& OutSwingCounter) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	const FMassUnitStateFragment* State = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitStateFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!State)
	{
		return false;
	}
	OutState = State->CurrentState;
	OutSwingCounter = static_cast<uint8>(State->AttackSerial & 0x1F);
	return true;
}

bool UMassUnitEntityManager::ApplyPresentationState(FMassUnitHandle UnitHandle, EMassUnitState NewState, uint8 SwingCounter)
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle = UnitHandle.EntityHandle.ToMassEntityHandle();
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle);
	if (!State || !Visual || State->CurrentState == EMassUnitState::Dead || NewState == EMassUnitState::Dead)
	{
		return false;   // death arrives through health; a corpse is never revived by presentation
	}
	SwingCounter &= 0x1F;
	switch (NewState)
	{
	case EMassUnitState::Attacking:
		if (State->CurrentState != EMassUnitState::Attacking || State->PresentationSwingCounter != SwingCounter)
		{
			// A new swing: restart the clip (renderer start stamp + pooled skeletal mesh serial). No damage is queued.
			State->CurrentState = EMassUnitState::Attacking;
			State->StateTime = 0.0f;
			++State->AttackSerial;
			State->PendingAttackDamage = 0.0f;
		}
		Visual->CurrentAnimation = UE::MassUnitSystem::Tags::AnimationAttack();
		break;
	case EMassUnitState::Stunned:
		if (State->CurrentState != EMassUnitState::Stunned)
		{
			State->CurrentState = EMassUnitState::Stunned;
			State->StateTime = 0.0f;
			State->PendingAttackDamage = 0.0f;
		}
		Visual->CurrentAnimation = UE::MassUnitSystem::Tags::AnimationStun();
		break;
	default:
		// Locomotion and anything else: leave a replicated attack/stun and let the movement processor choose
		// Walk/Run/Idle from the puppet's own velocity.
		if (State->CurrentState == EMassUnitState::Attacking || State->CurrentState == EMassUnitState::Stunned)
		{
			State->CurrentState = EMassUnitState::Idle;
			State->StateTime = 0.0f;
			Visual->CurrentAnimation = UE::MassUnitSystem::Tags::AnimationIdle();
		}
		break;
	}
	State->PresentationSwingCounter = SwingCounter;
	return true;
}

float UMassUnitEntityManager::GetUnitVariantSeed(FMassUnitHandle UnitHandle) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return -1.0f;
	}
	const FMassUnitVisualFragment* Visual = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	return Visual ? Visual->VariantSeed : -1.0f;
}

bool UMassUnitEntityManager::SetUnitVariantSeed(FMassUnitHandle UnitHandle, float Seed)
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassUnitVisualFragment* Visual = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Visual)
	{
		return false;
	}
	Visual->VariantSeed = FMath::Clamp(Seed, 0.0f, 1.0f);
	ApplyLook(*Visual);
	if (Visual->AttitudeProfile)
	{
		FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
		const FMassEntityHandle Handle = UnitHandle.EntityHandle.ToMassEntityHandle();
		if (FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Handle))
		{
			ApplyAttitude(Handle, *State, *Visual, EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Handle));
		}
	}
	return true;
}

void UMassUnitEntityManager::RetainProfile(UObject* Profile)
{
	if (!Profile || RetainedAssets.Contains(Profile))
	{
		return;
	}
	RetainedAssets.Add(Profile);
	const UMassUnitLookProfile* Look = Cast<UMassUnitLookProfile>(Profile);
	const UWorld* World = EntitySubsystem ? EntitySubsystem->GetWorld() : nullptr;
	// A dedicated server draws nothing: it never loads look meshes or materials.
	if (Look && !(World && World->GetNetMode() == NM_DedicatedServer))
	{
		TArray<UObject*> Assets;
		Look->LoadAllOptionAssets(Assets);
		for (UObject* Asset : Assets)
		{
			RetainedAssets.Add(Asset);
		}
	}
}

void UMassUnitEntityManager::ApplyAttitude(const FMassEntityHandle& Handle, FMassUnitStateFragment& State, FMassUnitVisualFragment& Visual, FMassUnitCrowdFragment* Crowd)
{
	RetainProfile(Visual.AttitudeProfile);
	const UUnitTemplate* Template = Visual.SourceTemplate.Get();
	const FMassUnitRolledAttitude Attitude = Visual.AttitudeProfile ? Visual.AttitudeProfile->RollAttitude(Visual.VariantSeed) : FMassUnitRolledAttitude();
	if (Template)
	{
		const float NewSpeed = FMath::Max(0.0f, Template->MoveSpeed) * Attitude.MoveSpeedMultiplier;
		// Under orders: the order owns the live speed (matched formation speed); its saved speed takes the new value so
		// release restores the new temperament.
		UMassUnitCommandSystem* Commands = nullptr;
		if (Crowd && Crowd->bCommanded)
		{
			const UMassUnitSubsystem* Subsystem = GetTypedOuter<UMassUnitSubsystem>();
			Commands = Subsystem ? Subsystem->GetCommandSystem() : nullptr;
		}
		if (!(Commands && Commands->UpdateSavedMoveSpeed(FMassUnitHandle(Handle), NewSpeed)))
		{
			State.MoveSpeed = NewSpeed;
		}
		const float TemplateCooldown = FMath::Max(0.0f, Template->AttackCooldown);
		State.AttackCooldown = TemplateCooldown;
		// Never shorter than the swing's impact delay - unless the template itself is authored that way (left as is).
		if (Visual.AttitudeProfile && TemplateCooldown > 0.0f && TemplateCooldown >= State.AttackImpactDelay + 0.05f)
		{
			State.AttackCooldown = FMath::Max(TemplateCooldown * Attitude.AttackCooldownMultiplier, State.AttackImpactDelay + 0.05f);
		}
	}
	Visual.AnimationRate = Attitude.AnimationRate;
	State.Courage = Attitude.Courage;
	State.bAvoidsClimbing = !Attitude.bWillClimb;
	// A grouped unit the crowd drives keeps its crowd speed in step (the crowd scales wander and chase from it).
	if (Crowd && Crowd->BaseMoveSpeed > 0.0f && !Crowd->bCommanded)
	{
		Crowd->BaseMoveSpeed = State.MoveSpeed;
	}
}

bool UMassUnitEntityManager::SetUnitAttitudeProfile(FMassUnitHandle UnitHandle, UMassUnitAttitudeProfile* Profile)
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle Handle = UnitHandle.EntityHandle.ToMassEntityHandle();
	FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Handle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Handle);
	if (!Visual || !State)
	{
		return false;
	}
	Visual->AttitudeProfile = Profile;
	ApplyAttitude(Handle, *State, *Visual, EntityManager.GetFragmentDataPtr<FMassUnitCrowdFragment>(Handle));
	return true;
}

UMassUnitAttitudeProfile* UMassUnitEntityManager::GetUnitAttitudeProfile(FMassUnitHandle UnitHandle) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return nullptr;
	}
	const FMassUnitVisualFragment* Visual = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	return Visual ? Visual->AttitudeProfile.Get() : nullptr;
}

bool UMassUnitEntityManager::GetUnitAttitude(FMassUnitHandle UnitHandle, FMassUnitRolledAttitude& OutAttitude) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	const FMassUnitVisualFragment* Visual = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Visual)
	{
		return false;
	}
	OutAttitude = Visual->AttitudeProfile ? Visual->AttitudeProfile->RollAttitude(Visual->VariantSeed) : FMassUnitRolledAttitude();
	return true;
}

void UMassUnitEntityManager::ApplyLook(FMassUnitVisualFragment& Visual)
{
	RetainProfile(Visual.LookProfile);
	const UUnitTemplate* Template = Visual.SourceTemplate.Get();
	UStaticMesh* TemplateMesh = Template ? Template->StaticMesh.LoadSynchronous() : Visual.StaticMesh.Get();
	const UWorld* World = EntitySubsystem ? EntitySubsystem->GetWorld() : nullptr;
	const bool bDedicatedServer = World && World->GetNetMode() == NM_DedicatedServer;
	// A dedicated server draws nothing: it keeps the profile (for queries) but never rolls or loads look assets.
	if (!Visual.LookProfile || bDedicatedServer)
	{
		Visual.StaticMesh = TemplateMesh;
		Visual.MaterialOverride = nullptr;
		Visual.LookParts.Reset();
		Visual.LookScale = 1.0f;
		Visual.LookTint = FLinearColor::White;
		return;
	}
	const FMassUnitRolledLook Look = Visual.LookProfile->RollLook(Visual.VariantSeed);
	Visual.StaticMesh = Look.Body.Mesh ? Look.Body.Mesh.Get() : TemplateMesh;
	Visual.MaterialOverride = Look.Body.Material;
	Visual.LookParts = Look.Parts;
	Visual.LookScale = Look.Scale;
	Visual.LookTint = Look.Tint;
}

bool UMassUnitEntityManager::SetUnitLookProfile(FMassUnitHandle UnitHandle, UMassUnitLookProfile* Profile)
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	FMassUnitVisualFragment* Visual = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Visual)
	{
		return false;
	}
	Visual->LookProfile = Profile;
	ApplyLook(*Visual);
	return true;
}

UMassUnitLookProfile* UMassUnitEntityManager::GetUnitLookProfile(FMassUnitHandle UnitHandle) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return nullptr;
	}
	const FMassUnitVisualFragment* Visual = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	return Visual ? Visual->LookProfile.Get() : nullptr;
}

bool UMassUnitEntityManager::GetUnitLook(FMassUnitHandle UnitHandle, FMassUnitRolledLook& OutLook) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	const FMassUnitVisualFragment* Visual = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
		UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Visual)
	{
		return false;
	}
	OutLook.Body.Mesh = Visual->StaticMesh;
	OutLook.Body.Material = Visual->MaterialOverride;
	OutLook.Parts = Visual->LookParts;
	OutLook.Scale = Visual->LookScale;
	OutLook.Tint = Visual->LookTint;
	return true;
}

TArray<FMassUnitStatusEffect> UMassUnitEntityManager::GetUnitStatuses(FMassUnitHandle UnitHandle) const
{
	if (!IsUnitValid(UnitHandle))
	{
		return {};
	}
	const FMassUnitAbilityFragment* Ability = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitAbilityFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	return Ability ? Ability->ActiveStatuses : TArray<FMassUnitStatusEffect>();
}

bool UMassUnitEntityManager::AddUnitTags(FMassUnitHandle UnitHandle, FGameplayTagContainer Tags)
{
	if (!IsUnitValid(UnitHandle) || Tags.IsEmpty())
	{
		return false;
	}
	FMassUnitAbilityFragment* Ability = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitAbilityFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Ability)
	{
		return false;
	}
	Ability->LooseTags.AppendTags(Tags);
	Ability->RebuildTags();
	return true;
}

bool UMassUnitEntityManager::RemoveUnitTags(FMassUnitHandle UnitHandle, FGameplayTagContainer Tags)
{
	if (!IsUnitValid(UnitHandle) || Tags.IsEmpty())
	{
		return false;
	}
	FMassUnitAbilityFragment* Ability = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitAbilityFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Ability)
	{
		return false;
	}
	Ability->LooseTags.RemoveTags(Tags);
	Ability->RebuildTags();
	return true;
}

bool UMassUnitEntityManager::GetUnitTags(FMassUnitHandle UnitHandle, FGameplayTagContainer& OutTags) const
{
	OutTags.Reset();
	if (!IsUnitValid(UnitHandle))
	{
		return false;
	}
	const FMassUnitAbilityFragment* Ability = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitAbilityFragment>(UnitHandle.EntityHandle.ToMassEntityHandle());
	if (!Ability)
	{
		return false;
	}
	OutTags = Ability->OwnedTags;
	return true;
}

bool UMassUnitEntityManager::UnitHasTag(FMassUnitHandle UnitHandle, FGameplayTag Tag) const
{
	FGameplayTagContainer Owned;
	return Tag.IsValid() && GetUnitTags(UnitHandle, Owned) && Owned.HasTag(Tag);
}

bool UMassUnitEntityManager::UnitHasAnyTags(FMassUnitHandle UnitHandle, FGameplayTagContainer Tags) const
{
	FGameplayTagContainer Owned;
	return !Tags.IsEmpty() && GetUnitTags(UnitHandle, Owned) && Owned.HasAny(Tags);
}

bool UMassUnitEntityManager::UnitMatchesTagQuery(FMassUnitHandle UnitHandle, const FGameplayTagQuery& Query) const
{
	FGameplayTagContainer Owned;
	return !Query.IsEmpty() && GetUnitTags(UnitHandle, Owned) && Query.Matches(Owned);
}

TArray<FMassUnitHandle> UMassUnitEntityManager::FindUnitsMatchingTagQuery(
	const FGameplayTagQuery& Query,
	FVector WorldLocation,
	float Radius,
	bool bUse3DDistance) const
{
	TArray<FMassUnitHandle> Result;
	if (!EntitySubsystem || Query.IsEmpty())
	{
		return Result;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const float RadiusSquared = FMath::Square(Radius);
	for (const FMassUnitEntityHandle& Handle : AllUnits)
	{
		const FMassEntityHandle NativeHandle = Handle.ToMassEntityHandle();
		if (!EntityManager.IsEntityValid(NativeHandle))
		{
			continue;
		}
		const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
		const FMassUnitAbilityFragment* Ability = EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(NativeHandle);
		if (!State || !Ability || State->CurrentState == EMassUnitState::Dead)
		{
			continue;
		}
		if (Radius > 0.0f)
		{
			const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
			if (!Transform)
			{
				continue;
			}
			const FVector Location = Transform->GetTransform().GetLocation();
			const float DistanceSquared = bUse3DDistance ? FVector::DistSquared(Location, WorldLocation) : FVector::DistSquared2D(Location, WorldLocation);
			if (DistanceSquared > RadiusSquared)
			{
				continue;
			}
		}
		if (Query.Matches(Ability->OwnedTags))
		{
			Result.Add(FMassUnitHandle(Handle));
		}
	}
	return Result;
}

void UMassUnitEntityManager::TickStatuses(float CurrentTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_TickStatuses);
	if (UnitsWithStatus.IsEmpty() || !EntitySubsystem || CurrentTime < NextStatusTickTime)
	{
		return;
	}
	NextStatusTickTime = CurrentTime + StatusTickInterval;

	struct FPendingDamage
	{
		FMassUnitHandle Unit;
		FGameplayTag Tag;
		FGameplayTag DamageType;
		float Damage = 0.0f;
		TWeakObjectPtr<AActor> Instigator;
	};
	struct FPendingRemoval
	{
		FMassUnitHandle Unit;
		FGameplayTag Tag;
	};
	TArray<FPendingDamage> DamageEvents;
	TArray<FPendingRemoval> RemovedEvents;
	TArray<FMassEntityHandle> Finished;

	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	for (const FMassEntityHandle NativeHandle : UnitsWithStatus)
	{
		FMassUnitAbilityFragment* Ability = EntityManager.IsEntityValid(NativeHandle)
			? EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(NativeHandle)
			: nullptr;
		FMassUnitStateFragment* State = Ability ? EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle) : nullptr;
		if (!Ability || !State)
		{
			Finished.Add(NativeHandle);
			continue;
		}
		const FMassUnitHandle Unit(NativeHandle);
		if (State->CurrentState == EMassUnitState::Dead)
		{
			// Statuses do not outlive the unit; clearing them keeps a burning corpse from ticking.
			for (const FMassUnitStatusEffect& Status : Ability->ActiveStatuses)
			{
				RemovedEvents.Add({Unit, Status.StatusTag});
			}
			Ability->ActiveStatuses.Reset();
			Ability->RebuildTags();
			State->StatusMoveSpeedMultiplier = 1.0f;
			Finished.Add(NativeHandle);
			continue;
		}

		bool bChanged = false;
		for (int32 Index = Ability->ActiveStatuses.Num() - 1; Index >= 0; --Index)
		{
			FMassUnitStatusEffect& Status = Ability->ActiveStatuses[Index];
			const bool bExpired = !Status.IsPermanent() && CurrentTime >= Status.ExpireTime;
			if (Status.DamagePerPeriod > 0.0f)
			{
				const float Period = FMath::Max(0.05f, Status.Period);
				// A tick due exactly at expiry still lands; that is how a 3 s burn with a 1 s period deals 3 ticks.
				const float LastTickTime = Status.IsPermanent()
					? CurrentTime
					: FMath::Min(CurrentTime, Status.ExpireTime + UE_KINDA_SMALL_NUMBER);
				while (Status.NextTickTime <= LastTickTime)
				{
					DamageEvents.Add({Unit, Status.StatusTag, Status.DamageTypeTag, Status.DamagePerPeriod * static_cast<float>(FMath::Max(1, Status.Stacks)), Status.Instigator});
					Status.NextTickTime += Period;
				}
			}
			if (bExpired)
			{
				RemovedEvents.Add({Unit, Status.StatusTag});
				Ability->ActiveStatuses.RemoveAtSwap(Index, EAllowShrinking::No);
				bChanged = true;
			}
		}
		if (bChanged)
		{
			RefreshStatusEffects(*Ability, *State, EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle));
		}
		if (Ability->ActiveStatuses.IsEmpty())
		{
			Finished.Add(NativeHandle);
		}
	}
	for (const FMassEntityHandle Handle : Finished)
	{
		UnitsWithStatus.Remove(Handle);
	}

	// Events go out after the fragment pass: listeners may apply damage or new statuses, which would
	// otherwise mutate the set and the arrays being walked.
	for (const FPendingRemoval& Removal : RemovedEvents)
	{
		OnUnitStatusChanged.Broadcast(Removal.Unit, Removal.Tag, false);
	}
	for (const FPendingDamage& Event : DamageEvents)
	{
		if (OnUnitStatusDamageTick.IsBound())
		{
			OnUnitStatusDamageTick.Broadcast(Event.Unit, Event.Tag, Event.DamageType, Event.Damage, Event.Instigator.Get());
		}
		else
		{
			ApplyDamageInternal(Event.Unit.EntityHandle, Event.Damage, Event.Instigator.Get());
		}
	}
}

// ================================================================================================
// LAUNCH
// ================================================================================================

bool UMassUnitEntityManager::LaunchUnit(FMassUnitHandle UnitHandle, FVector Velocity, AActor* LaunchInstigator, bool bApplyTemplateScale)
{
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	if (!Settings || !Settings->bEnableUnitLaunch || !IsUnitValid(UnitHandle) || Velocity.IsNearlyZero())
	{
		return false;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	const FMassEntityHandle Handle = UnitHandle.EntityHandle.ToMassEntityHandle();
	FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Handle);
	FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Handle);
	const FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Handle);
	if (!Transform || !State)
	{
		return false;
	}

	const bool bDead = State->CurrentState == EMassUnitState::Dead;
	if (bApplyTemplateScale)
	{
		const UUnitTemplate* Template = Visual ? Visual->SourceTemplate.Get() : nullptr;
		const float Scale = Template ? Template->LaunchVelocityScale : 1.0f;
		if (Scale <= 0.0f)
		{
			return false;   // heavies and bosses stand their ground
		}
		// A living unit immune to the Launched status (immune to MassUnit.Status, or to it exactly) could not be held
		// down for the flight, so it would walk and attack in the air: it stands its ground instead.
		const FMassUnitAbilityFragment* Ability = EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(Handle);
		if (!bDead && Ability && UE::MassUnitSystem::Tags::StatusLaunched().MatchesAny(Ability->StatusImmunityTags))
		{
			return false;
		}
		Velocity = (Velocity * Scale).GetClampedToMaxSize(Settings->MaxLaunchSpeed);
	}

	FActiveLaunch* Launch = ActiveLaunches.Find(Handle);
	if (!Launch)
	{
		if (ActiveLaunches.Num() >= Settings->MaxAirborneUnits)
		{
			return false;   // bounded cost: a screen-clearing blast past the cap damages in place
		}
		Launch = &ActiveLaunches.Add(Handle);
	}

	const float Gravity = Settings->LaunchGravity;
	FTransform& UnitTransform = Transform->GetMutableTransform();
	Launch->Velocity = Velocity;
	Launch->FlightTime = 0.0f;
	Launch->Bounces = 0;
	Launch->bSettling = false;
	Launch->bDead = bDead;
	Launch->LandZ = ResolveLandingZ(UnitTransform.GetLocation(), Velocity, Gravity, &Launch->bLandZFromTrace);

	if (bDead)
	{
		StartCorpseTumble(*Launch, Handle);
		PendingRagdollRequests.Emplace(UnitHandle.EntityHandle, Velocity);
	}
	else
	{
		// Thrown backwards: face where the hit came from, and stay down through the flight and the knockdown.
		const FVector Back = -Velocity.GetSafeNormal2D();
		if (!Back.IsNearlyZero())
		{
			UnitTransform.SetRotation(FQuat(FRotator(0.0f, Back.Rotation().Yaw, 0.0f)));
		}
		// Held down for the WHOLE flight, however long it really takes (slow server frames, a long fall off a ledge):
		// the landing replaces this with the knockdown, and TickLaunches re-arms it if anything lifts it mid-air.
		FMassUnitStatusEffect Down;
		Down.StatusTag = UE::MassUnitSystem::Tags::StatusLaunched();
		Down.Duration = LaunchFlightHoldSeconds;
		Down.bStuns = true;
		Down.MoveSpeedMultiplier = 0.0f;
		ApplyUnitStatus(UnitHandle, Down, LaunchInstigator);
	}

	OnUnitLaunched.Broadcast(UnitHandle, Velocity, bDead);
	return true;
}

bool UMassUnitEntityManager::IsUnitAirborne(FMassUnitHandle UnitHandle) const
{
	return UnitHandle.IsValid() && ActiveLaunches.Contains(UnitHandle.EntityHandle.ToMassEntityHandle());
}

bool UMassUnitEntityManager::CapLaunchLandingHeight(FMassUnitHandle UnitHandle, float MaxLandZ)
{
	FActiveLaunch* Launch = UnitHandle.IsValid() ? ActiveLaunches.Find(UnitHandle.EntityHandle.ToMassEntityHandle()) : nullptr;
	if (!Launch)
	{
		return false;
	}
	if (!Launch->bLandZFromTrace)
	{
		Launch->LandZ = FMath::Min(Launch->LandZ, MaxLandZ);
	}
	return true;
}

TArray<TPair<FMassUnitEntityHandle, FVector>> UMassUnitEntityManager::ConsumeRagdollRequests()
{
	return MoveTemp(PendingRagdollRequests);
}

float UMassUnitEntityManager::ResolveLandingZ(const FVector& Start, const FVector& Velocity, float Gravity, bool* bOutHitGround) const
{
	if (bOutHitGround)
	{
		*bOutHitGround = false;
	}
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	UWorld* World = EntitySubsystem ? EntitySubsystem->GetWorld() : nullptr;
	if (!World || !Settings || !Settings->bTraceLandingGround)
	{
		return Start.Z;
	}
	// Per-frame budget: a screen-clearing blast must not stall the frame on hundreds of synchronous traces. Past the
	// budget a unit lands at its launch height, which is right on flat ground.
	const uint64 Frame = GFrameCounter;
	if (Frame != LandingTraceFrame)
	{
		LandingTraceFrame = Frame;
		LandingTracesThisFrame = 0;
	}
	if (LandingTracesThisFrame >= Settings->MaxLandingTracesPerFrame)
	{
		return Start.Z;
	}
	++LandingTracesThisFrame;
	// One trace per launch, where the arc returns to launch height: a unit knocked off a ledge lands below it.
	const float FlightTime = Velocity.Z > 0.0f ? 2.0f * Velocity.Z / Gravity : 0.25f;
	const FVector Landing = Start + FVector(Velocity.X, Velocity.Y, 0.0f) * FlightTime;
	FHitResult Hit;
	const FCollisionObjectQueryParams StaticWorld(ECC_WorldStatic);
	if (World->LineTraceSingleByObjectType(Hit, Landing + FVector(0.0f, 0.0f, 200.0f), Landing - FVector(0.0f, 0.0f, 1500.0f),
		StaticWorld, FCollisionQueryParams(SCENE_QUERY_STAT(MassUnitLaunchLanding), false)))
	{
		if (bOutHitGround)
		{
			*bOutHitGround = true;
		}
		return Hit.ImpactPoint.Z;
	}
	return Start.Z;
}

void UMassUnitEntityManager::StartCorpseTumble(FActiveLaunch& Launch, const FMassEntityHandle& Handle) const
{
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const float Rate = Settings ? Settings->CorpseTumbleDegreesPerSecond : 540.0f;
	// Deterministic per entity (a corpse always tumbles the same way on this machine; client puppets have their own
	// entity indices, so their spin differs slightly from the server's): mostly a back-flip, a little roll.
	const float SeedA = FMath::Frac(static_cast<float>(Handle.Index) * 0.6180339887f);
	const float SeedB = FMath::Frac(static_cast<float>(Handle.Index) * 0.7548776662f);
	Launch.PitchRate = Rate * (0.6f + 0.4f * SeedA);
	Launch.RollRate = Rate * 0.35f * (SeedB * 2.0f - 1.0f);
}

void UMassUnitEntityManager::TickLaunches(float DeltaTime)
{
	MASSUNIT_SCOPE_CYCLE(STAT_MassUnit_TickLaunches);
	if (ActiveLaunches.IsEmpty() || !EntitySubsystem)
	{
		return;
	}
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const float Gravity = Settings ? Settings->LaunchGravity : 1960.0f;
	const float Restitution = Settings ? Settings->CorpseBounceRestitution : 0.3f;
	const float Knockdown = Settings ? Settings->KnockdownSeconds : 0.8f;
	const float Dt = FMath::Min(DeltaTime, 0.05f);
	constexpr float SettleSeconds = 0.2f;
	constexpr float MaxFlightSeconds = 6.0f;
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();

	for (auto It = ActiveLaunches.CreateIterator(); It; ++It)
	{
		const FMassEntityHandle Handle = It->Key;
		FActiveLaunch& Launch = It->Value;
		FMassUnitTransformFragment* Transform = EntityManager.IsEntityValid(Handle)
			? EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(Handle) : nullptr;
		const FMassUnitStateFragment* State = Transform ? EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(Handle) : nullptr;
		if (!Transform || !State)
		{
			It.RemoveCurrent();
			continue;
		}
		FTransform& UnitTransform = Transform->GetMutableTransform();

		// Killed in mid-air (a juggle finisher): from here it flies as a corpse.
		if (!Launch.bDead && State->CurrentState == EMassUnitState::Dead)
		{
			Launch.bDead = true;
			StartCorpseTumble(Launch, Handle);
			PendingRagdollRequests.Emplace(FMassUnitEntityHandle(Handle), Launch.Velocity);
			OnUnitLaunched.Broadcast(FMassUnitHandle(FMassUnitEntityHandle(Handle)), Launch.Velocity, true);
		}

		// A living unit must stay stunned for its whole flight; a cleanse or an expiry mid-air re-arms the hold, or the
		// movement and combat processors would walk and attack it in the air.
		if (!Launch.bDead && State->CurrentState != EMassUnitState::Stunned)
		{
			FMassUnitStatusEffect Hold;
			Hold.StatusTag = UE::MassUnitSystem::Tags::StatusLaunched();
			Hold.Duration = LaunchFlightHoldSeconds;
			Hold.bStuns = true;
			Hold.MoveSpeedMultiplier = 0.0f;
			ApplyUnitStatus(FMassUnitHandle(FMassUnitEntityHandle(Handle)), Hold);
		}

		if (Launch.bSettling)
		{
			// The corpse rolls flat onto the ground (yaw only), where the held death pose lies.
			Launch.SettleTime += Dt;
			const float Alpha = FMath::Clamp(Launch.SettleTime / SettleSeconds, 0.0f, 1.0f);
			UnitTransform.SetRotation(FQuat::Slerp(Launch.SettleFrom, FQuat(FRotator(0.0f, Launch.Yaw, 0.0f)), Alpha).GetNormalized());
			if (Alpha >= 1.0f)
			{
				It.RemoveCurrent();
			}
			continue;
		}

		Launch.FlightTime += Dt;
		Launch.Velocity.Z -= Gravity * Dt;
		FVector Location = UnitTransform.GetLocation() + Launch.Velocity * Dt;
		if (Launch.bDead)
		{
			const FQuat Spin(FRotator(-Launch.PitchRate * Dt, 0.0f, Launch.RollRate * Dt));
			UnitTransform.SetRotation((UnitTransform.GetRotation() * Spin).GetNormalized());
		}

		const bool bTouchedDown = Location.Z <= Launch.LandZ && Launch.Velocity.Z < 0.0f;
		if (!bTouchedDown && Launch.FlightTime < MaxFlightSeconds)
		{
			UnitTransform.SetLocation(Location);
			continue;
		}

		Location.Z = Launch.LandZ;
		UnitTransform.SetLocation(Location);
		if (Launch.bDead && Launch.Bounces == 0 && Restitution > 0.0f && -Launch.Velocity.Z > 250.0f)
		{
			// One bounce sells the weight; more reads as rubber.
			Launch.Velocity = FVector(Launch.Velocity.X * 0.5f, Launch.Velocity.Y * 0.5f, -Launch.Velocity.Z * Restitution);
			Launch.PitchRate *= 0.5f;
			Launch.RollRate *= 0.5f;
			++Launch.Bounces;
			continue;
		}

		const FMassUnitHandle Unit{FMassUnitEntityHandle(Handle)};
		OnUnitLanded.Broadcast(Unit, Location, Launch.bDead);
		if (Launch.bDead)
		{
			Launch.bSettling = true;
			Launch.SettleTime = 0.0f;
			Launch.SettleFrom = UnitTransform.GetRotation();
			Launch.Yaw = UnitTransform.Rotator().Yaw;
			continue;
		}

		// A living unit lands upright and stays down for exactly the knockdown: the flight hold is removed first,
		// because a refresh never shortens a status.
		UnitTransform.SetRotation(FQuat(FRotator(0.0f, UnitTransform.Rotator().Yaw, 0.0f)));
		It.RemoveCurrent();
		RemoveUnitStatus(Unit, UE::MassUnitSystem::Tags::StatusLaunched());
		FMassUnitStatusEffect Down;
		Down.StatusTag = UE::MassUnitSystem::Tags::StatusLaunched();
		Down.Duration = Knockdown;
		Down.bStuns = true;
		Down.MoveSpeedMultiplier = 0.0f;
		ApplyUnitStatus(Unit, Down);
	}
}
