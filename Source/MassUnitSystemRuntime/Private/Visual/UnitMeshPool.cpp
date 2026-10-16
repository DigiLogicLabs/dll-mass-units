// Copyright Digi Logic Labs LLC. All Rights Reserved.

#include "Visual/UnitMeshPool.h"

#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Config/MassUnitSystemSettings.h"
#include "Core/MassUnitGameplayTags.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Entity/MassUnitFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassUnitCommonFragments.h"
#include "Visual/MassUnitAnimInstance.h"

void UUnitMeshPool::Initialize(UWorld* InWorld, UMassEntitySubsystem* InEntitySubsystem, int32 PoolSize)
{
	World = InWorld;
	EntitySubsystem = InEntitySubsystem;
	MaxPoolSize = FMath::Max(0, PoolSize);
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	UpdateInterval = Settings ? FMath::Max(0.0f, Settings->VisualUpdateInterval) : 0.033f;
	const int32 PreCreateCount = FMath::Min(MaxPoolSize, 25);
	for (int32 Index = 0; Index < PreCreateCount; ++Index)
	{
		if (USkeletalMeshComponent* Component = CreateMeshComponent())
		{
			AvailableMeshes.Add(Component);
		}
	}
}

void UUnitMeshPool::Deinitialize()
{
	TSet<USkeletalMeshComponent*> Components;
	for (const TPair<FMassUnitEntityHandle, TObjectPtr<USkeletalMeshComponent>>& Pair : EntityMeshMap)
	{
		Components.Add(Pair.Value);
	}
	for (USkeletalMeshComponent* Component : AvailableMeshes)
	{
		Components.Add(Component);
	}
	for (USkeletalMeshComponent* Component : Components)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	AvailableMeshes.Reset();
	EntityMeshMap.Reset();
	MeshEntityMap.Reset();
	MeshAnimationTags.Reset();
	MeshAttackSerials.Reset();
	EntitySubsystem = nullptr;
	World = nullptr;
}

void UUnitMeshPool::UpdateUnitMeshes(const TArray<FMassUnitEntityHandle>& Entities)
{
	if (!World || World->GetTimeSeconds() - LastUpdateTime < UpdateInterval)
	{
		return;
	}
	LastUpdateTime = World->GetTimeSeconds();

	TArray<USkeletalMeshComponent*> MeshesToRelease;
	for (const TPair<FMassUnitEntityHandle, TObjectPtr<USkeletalMeshComponent>>& Pair : EntityMeshMap)
	{
		if (!IsEntityValid(Pair.Key))
		{
			MeshesToRelease.Add(Pair.Value);
			continue;
		}
		if (RagdollMeshes.Contains(Pair.Value))
		{
			continue;   // a ragdoll stays until its unit is destroyed, whatever the camera does
		}
		const FMassUnitVisualFragment* Visual = EntitySubsystem->GetEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(
			Pair.Key.ToMassEntityHandle());
		if (!Visual || !Visual->bWantsSkeletalMesh || !Visual->bIsVisible || !Visual->SkeletalMesh)
		{
			MeshesToRelease.Add(Pair.Value);
		}
	}
	for (USkeletalMeshComponent* Mesh : MeshesToRelease)
	{
		ReleaseMesh(Mesh);
	}

	if (!EntitySubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
	struct FCandidate
	{
		FMassUnitEntityHandle Entity;
		float DistanceSquared = 0.0f;
	};
	TArray<FCandidate> Candidates;
	for (const FMassUnitEntityHandle Entity : Entities)
	{
		if (!IsEntityValid(Entity))
		{
			continue;
		}
		FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(Entity.ToMassEntityHandle());
		if (!Visual)
		{
			continue;
		}
		if (USkeletalMeshComponent* Existing = EntityMeshMap.FindRef(Entity))
		{
			Visual->bUseSkeletalMesh = true;
			if (!RagdollMeshes.Contains(Existing))
			{
				UpdateMeshFromEntity(Existing, Entity);
			}
		}
		else if (Visual->bWantsSkeletalMesh && Visual->bIsVisible && Visual->SkeletalMesh)
		{
			Candidates.Add({Entity, Visual->ViewerDistanceSquared});
			Visual->bUseSkeletalMesh = false;
		}
		else
		{
			Visual->bUseSkeletalMesh = false;
		}
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.DistanceSquared < B.DistanceSquared;
	});
	for (const FCandidate& Candidate : Candidates)
	{
		if (EntityMeshMap.Num() >= MaxPoolSize)
		{
			break;
		}
		GetMeshForUnitInternal(Candidate.Entity);
	}
}

USkeletalMeshComponent* UUnitMeshPool::GetMeshForUnit(FMassUnitHandle UnitHandle)
{
	return GetMeshForUnitInternal(UnitHandle.EntityHandle);
}

USkeletalMeshComponent* UUnitMeshPool::GetMeshForUnitInternal(FMassUnitEntityHandle Entity)
{
	if (!IsEntityValid(Entity) || MaxPoolSize <= 0)
	{
		return nullptr;
	}
	if (USkeletalMeshComponent* Existing = EntityMeshMap.FindRef(Entity))
	{
		return Existing;
	}
	if (EntityMeshMap.Num() >= MaxPoolSize)
	{
		return nullptr;
	}

	USkeletalMeshComponent* Mesh = nullptr;
	if (AvailableMeshes.IsEmpty())
	{
		Mesh = CreateMeshComponent();
	}
	else
	{
		Mesh = AvailableMeshes.Pop(EAllowShrinking::No).Get();
	}
	if (!Mesh)
	{
		return nullptr;
	}
	EntityMeshMap.Add(Entity, Mesh);
	MeshEntityMap.Add(Mesh, Entity);
	if (FMassUnitVisualFragment* Visual = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(Entity.ToMassEntityHandle()))
	{
		Visual->bUseSkeletalMesh = true;
	}
	UpdateMeshFromEntity(Mesh, Entity);
	return Mesh;
}

void UUnitMeshPool::ReleaseMesh(USkeletalMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return;
	}
	FMassUnitEntityHandle Entity;
	if (!MeshEntityMap.RemoveAndCopyValue(Mesh, Entity))
	{
		return;
	}
	EntityMeshMap.Remove(Entity);
	if (IsEntityValid(Entity))
	{
		if (FMassUnitVisualFragment* Visual = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(Entity.ToMassEntityHandle()))
		{
			Visual->bUseSkeletalMesh = false;
		}
	}
	MeshAnimationTags.Remove(Mesh);
	MeshAttackSerials.Remove(Mesh);
	if (RagdollMeshes.Remove(Mesh) > 0)
	{
		// Back to an animated, collision-free pool mesh.
		Mesh->SetSimulatePhysics(false);
		Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	}
	Mesh->SetVisibility(false);
	Mesh->SetAnimInstanceClass(nullptr);
	Mesh->SetSkeletalMeshAsset(nullptr);
	AvailableMeshes.AddUnique(Mesh);
}

bool UUnitMeshPool::TransitionToSkeletal(FMassUnitHandle UnitHandle)
{
	return TransitionToSkeletalInternal(UnitHandle.EntityHandle);
}

bool UUnitMeshPool::TransitionToSkeletalInternal(FMassUnitEntityHandle Entity)
{
	if (!IsEntityValid(Entity))
	{
		return false;
	}
	FMassUnitVisualFragment* Visual = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(Entity.ToMassEntityHandle());
	if (!Visual || !Visual->SkeletalMesh)
	{
		return false;
	}
	Visual->bUseSkeletalMesh = true;
	Visual->bWantsSkeletalMesh = true;
	if (USkeletalMeshComponent* Mesh = GetMeshForUnitInternal(Entity))
	{
		Mesh->SetVisibility(true);
		return true;
	}
	Visual->bUseSkeletalMesh = false;
	return false;
}

bool UUnitMeshPool::TransitionToVertex(FMassUnitHandle UnitHandle)
{
	return TransitionToVertexInternal(UnitHandle.EntityHandle);
}

bool UUnitMeshPool::TransitionToVertexInternal(FMassUnitEntityHandle Entity)
{
	if (!IsEntityValid(Entity))
	{
		return false;
	}
	if (FMassUnitVisualFragment* Visual = EntitySubsystem->GetMutableEntityManager().GetFragmentDataPtr<FMassUnitVisualFragment>(Entity.ToMassEntityHandle()))
	{
		Visual->bUseSkeletalMesh = false;
		Visual->bWantsSkeletalMesh = false;
	}
	if (USkeletalMeshComponent* Mesh = EntityMeshMap.FindRef(Entity))
	{
		ReleaseMesh(Mesh);
	}
	return true;
}

USkeletalMeshComponent* UUnitMeshPool::CreateMeshComponent()
{
	if (!World)
	{
		return nullptr;
	}
	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(World);
	if (!Component)
	{
		return nullptr;
	}
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetVisibility(false);
	Component->RegisterComponentWithWorld(World);
	return Component;
}

void UUnitMeshPool::UpdateMeshFromEntity(USkeletalMeshComponent* Mesh, FMassUnitEntityHandle Entity)
{
	if (!Mesh || !IsEntityValid(Entity))
	{
		return;
	}
	const FMassEntityManager& EntityManager = EntitySubsystem->GetEntityManager();
	const FMassEntityHandle NativeHandle = Entity.ToMassEntityHandle();
	const FMassUnitTransformFragment* Transform = EntityManager.GetFragmentDataPtr<FMassUnitTransformFragment>(NativeHandle);
	const FMassUnitVisualFragment* Visual = EntityManager.GetFragmentDataPtr<FMassUnitVisualFragment>(NativeHandle);
	if (!Transform || !Visual || !Visual->SkeletalMesh)
	{
		return;
	}
	if (Mesh->GetSkeletalMeshAsset() != Visual->SkeletalMesh)
	{
		Mesh->SetSkeletalMeshAsset(Visual->SkeletalMesh);
		MeshAnimationTags.Remove(Mesh);
	}
	const FMassUnitStateFragment* State = EntityManager.GetFragmentDataPtr<FMassUnitStateFragment>(NativeHandle);
	const float StateTime = State ? State->StateTime : 0.0f;
	const int32 AttackSerial = State ? State->AttackSerial : 0;

	FGameplayTag PlayTag = Visual->CurrentAnimation;
	UAnimationAsset* Animation = ResolveAnimationAsset(*Visual);
	float Elapsed = StateTime;
	// A finished one-shot (an attack between swings) hands over to Idle, mirroring the crowd renderer so
	// promotion and demotion between the two bands do not change what the unit is doing. Death holds.
	if (Animation && !ShouldLoopAnimation(PlayTag)
		&& PlayTag != UE::MassUnitSystem::Tags::AnimationDeath()
		&& PlayTag != UE::MassUnitSystem::Tags::AnimationStun()
		&& Visual->IdleAnimation)
	{
		const float OneShotLength = Animation->GetPlayLength();
		if (OneShotLength > UE_SMALL_NUMBER && StateTime >= OneShotLength)
		{
			PlayTag = UE::MassUnitSystem::Tags::AnimationIdle();
			Animation = Visual->IdleAnimation;
			Elapsed = StateTime - OneShotLength;
		}
	}

	if (Animation)
	{
		const bool bNewSwing = PlayTag == UE::MassUnitSystem::Tags::AnimationAttack()
			&& MeshAttackSerials.FindRef(Mesh) != AttackSerial;
		if (MeshAnimationTags.FindRef(Mesh) != PlayTag || bNewSwing)
		{
			const bool bLoop = ShouldLoopAnimation(PlayTag);
			Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Mesh->PlayAnimation(Animation, bLoop);

			// Start where the vertex-animation band would be, including its per-unit loop phase offset,
			// so a unit crossing the promotion distance continues its cycle instead of snapping to frame 0.
			const float Length = Animation->GetPlayLength();
			if (Length > UE_SMALL_NUMBER)
			{
				const float Seed = FMath::Frac(static_cast<float>(NativeHandle.Index) * 0.6180339887f);
				const float Position = bLoop
					? FMath::Fmod(Elapsed + Seed * Length, Length)
					: FMath::Clamp(Elapsed, 0.0f, Length);
				Mesh->SetPosition(Position, /*bFireNotifies*/ false);
			}
			MeshAnimationTags.Add(Mesh, PlayTag);
		}
		MeshAttackSerials.Add(Mesh, AttackSerial);
	}
	else if (Visual->AnimationBlueprintClass)
	{
		if (Mesh->GetAnimationMode() != EAnimationMode::AnimationBlueprint
			|| Mesh->GetAnimClass() != Visual->AnimationBlueprintClass.Get())
		{
			Mesh->SetAnimInstanceClass(Visual->AnimationBlueprintClass.Get());
			MeshAnimationTags.Remove(Mesh);
		}
		// An ABP has no pawn to read; units with a UMassUnitAnimInstance parent get their state pushed.
		if (State)
		{
			if (UMassUnitAnimInstance* UnitAnim = Cast<UMassUnitAnimInstance>(Mesh->GetAnimInstance()))
			{
				const FMassUnitVelocityFragment* Velocity = EntityManager.GetFragmentDataPtr<FMassUnitVelocityFragment>(NativeHandle);
				const FMassUnitTeamFragment* Team = EntityManager.GetFragmentDataPtr<FMassUnitTeamFragment>(NativeHandle);
				UnitAnim->PushUnitState(
					FMassUnitHandle(Entity),
					*State,
					*Visual,
					EntityManager.GetFragmentDataPtr<FMassUnitAbilityFragment>(NativeHandle),
					Team ? Team->TeamID : 0,
					Velocity ? Velocity->Value.Size2D() : 0.0f);
			}
		}
	}
	Mesh->SetWorldTransform(Transform->GetTransform());
	Mesh->SetVisibility(Visual->bIsVisible, true);
}

UAnimationAsset* UUnitMeshPool::ResolveAnimationAsset(const FMassUnitVisualFragment& Visual) const
{
	using namespace UE::MassUnitSystem;
	if (Visual.CurrentAnimation == Tags::AnimationAttack())
	{
		return Visual.AttackAnimation;
	}
	if (Visual.CurrentAnimation == Tags::AnimationDeath())
	{
		return Visual.DeathAnimation;
	}
	if (Visual.CurrentAnimation == Tags::AnimationStun())
	{
		return Visual.StunAnimation;
	}
	if (Visual.CurrentAnimation == Tags::AnimationWalk() || Visual.CurrentAnimation == Tags::AnimationRun())
	{
		return Visual.MoveAnimation;
	}
	return Visual.IdleAnimation;
}

bool UUnitMeshPool::ShouldLoopAnimation(const FGameplayTag& AnimationTag)
{
	using namespace UE::MassUnitSystem;
	return AnimationTag != Tags::AnimationAttack() && AnimationTag != Tags::AnimationDeath();
}

bool UUnitMeshPool::IsEntityValid(FMassUnitEntityHandle Entity) const
{
	return EntitySubsystem && Entity.IsValid()
		&& EntitySubsystem->GetEntityManager().IsEntityValid(Entity.ToMassEntityHandle());
}

int32 UUnitMeshPool::StartRagdolls(const TArray<TPair<FMassUnitEntityHandle, FVector>>& Requests)
{
	const UMassUnitSystemSettings* Settings = GetDefault<UMassUnitSystemSettings>();
	const int32 MaxRagdolls = Settings ? Settings->MaxActiveRagdolls : 0;
	int32 Started = 0;
	for (const TPair<FMassUnitEntityHandle, FVector>& Request : Requests)
	{
		if (RagdollMeshes.Num() >= MaxRagdolls)
		{
			break;
		}
		USkeletalMeshComponent* Mesh = EntityMeshMap.FindRef(Request.Key);
		if (!Mesh || RagdollMeshes.Contains(Mesh) || !Mesh->GetPhysicsAsset())
		{
			continue;   // not in the near band, or no Physics Asset: the tumbling fling carries it
		}
		Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
		Mesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		// Pool meshes are registered with NoCollision, so they may have no bodies yet: build them before simulating.
		if (!Mesh->IsPhysicsStateCreated())
		{
			Mesh->RecreatePhysicsState();
		}
		Mesh->SetSimulatePhysics(true);
		if (!Mesh->IsSimulatingPhysics())
		{
			// No bodies after all: restore the animated mesh and let the tumbling fling carry this corpse.
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			continue;
		}
		Mesh->SetAllPhysicsLinearVelocity(Request.Value);
		Mesh->WakeAllRigidBodies();
		RagdollMeshes.Add(Mesh);
		++Started;
	}
	return Started;
}
