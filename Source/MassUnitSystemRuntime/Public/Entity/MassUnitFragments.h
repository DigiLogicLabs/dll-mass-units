// Copyright Digi Logic Labs LLC. All Rights Reserved.

#pragma once

#include "Animation/AnimInstance.h"
#include "CoreMinimal.h"
#include "Entity/MassEntityFallback.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "MassEntityTypes.h"
#include "Visual/MassUnitLookProfile.h"
#include "MassUnitFragments.generated.h"

class UMassUnitAttitudeProfile;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;
class UTexture2D;
class UAnimationAsset;
class AActor;

UENUM(BlueprintType)
enum class EMassUnitState : uint8
{
	Idle,
	Moving,
	Attacking,
	Defending,
	Dead,
	Interacting,
	Stunned,
	Custom
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitTransformFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FTransform Transform;

	const FTransform& GetTransform() const { return Transform; }
	FTransform& GetMutableTransform() { return Transform; }
	void SetTransform(const FTransform& InTransform) { Transform = InTransform; }
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitStateFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	EMassUnitState CurrentState = EMassUnitState::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit", meta = (ClampMin = "0.0"))
	float StateTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FGameplayTag UnitType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FGameplayTag UnitClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FGameplayTag DefaultBehavior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit", meta = (ClampMin = "1"))
	int32 UnitLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Combat", meta = (ClampMin = "0.0"))
	float Health = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Combat", meta = (ClampMin = "0.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Combat", meta = (ClampMin = "0.0"))
	float BaseDamage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Combat", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float AttackRange = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Combat", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float AttackCooldown = 1.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Combat")
	float AttackCooldownRemaining = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Movement", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float MoveSpeed = 300.0f;

	/**
	 * Delay from the start of a swing to the moment its damage lands, copied from the template.
	 * Zero applies damage on the frame the swing starts, which was the only behaviour before this field.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Combat", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float AttackImpactDelay = 0.0f;

	/** Increments once per swing. Presentation restarts the attack clip whenever it changes. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Combat")
	int32 AttackSerial = 0;

	/** Damage of a swing whose impact frame has not been reached yet; zero when nothing is pending. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Combat")
	float PendingAttackDamage = 0.0f;

	/** Product of every active status slow. 1 is normal speed, 0 is rooted. Applied by the movement processor. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Status")
	float StatusMoveSpeedMultiplier = 1.0f;

	/** Attitude: 0 flees first when the unit's group routs, 1 holds longest. Negative = random rout order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit|Attitude")
	float Courage = -1.0f;

	/** Attitude: the pile planner never makes this unit climb (it can still hold others up). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit|Attitude")
	bool bAvoidsClimbing = false;

	/** Presentation-only units (client puppets): last replicated swing counter seen, see ApplyPresentationState. */
	uint8 PresentationSwingCounter = 0;

	/** Starts a swing: restarts the attack clip and records the damage to land at the impact delay. */
	void BeginAttackSwing(float Damage)
	{
		StateTime = 0.0f;
		++AttackSerial;
		PendingAttackDamage = FMath::Max(0.0f, Damage);
	}

	/** Returns the pending swing damage once its impact time is reached, clearing it; otherwise zero. */
	float ConsumeAttackImpact()
	{
		if (PendingAttackDamage <= 0.0f || StateTime + UE_KINDA_SMALL_NUMBER < AttackImpactDelay)
		{
			return 0.0f;
		}
		const float Damage = PendingAttackDamage;
		PendingAttackDamage = 0.0f;
		return Damage;
	}
};

/**
 * One timed status on a unit, the crowd-side stand-in for an active Gameplay Effect.
 * Tag-driven: anything that grants Status.Burning to an actor can grant it to a unit here.
 */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitStatusEffect
{
	GENERATED_BODY()

	/** Tag granted while the status is active, for example Status.Burning or Status.Stunned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status")
	FGameplayTag StatusTag;

	/** Seconds the status lasts. Zero or less makes it permanent until removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status", meta = (ForceUnits = "s"))
	float Duration = 0.0f;

	/** Damage reported each period through OnUnitStatusDamageTick. Zero for non-damaging statuses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status", meta = (ClampMin = "0.0"))
	float DamagePerPeriod = 0.0f;

	/** Damage type reported with each tick (e.g. Damage.Thermal for a burn), so the project can colour and resist it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status")
	FGameplayTag DamageTypeTag;

	/** Seconds between damage ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float Period = 1.0f;

	/** Movement multiplier while active. 1 leaves speed alone, 0 roots the unit. Multiplies with other statuses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float MoveSpeedMultiplier = 1.0f;

	/** Puts the unit in the Stunned state for the duration: no movement, no attacks, stun animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status")
	bool bStuns = false;

	/** Re-applying the same tag refreshes the timer; with this set it also adds a stack (damage scales by stacks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status")
	bool bStacks = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit|Status", meta = (ClampMin = "1"))
	int32 MaxStacks = 1;

	/** Who applied it. Damage ticks are credited to this actor. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Status")
	TWeakObjectPtr<AActor> Instigator;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Status")
	int32 Stacks = 1;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Status")
	float ExpireTime = 0.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Status")
	float NextTickTime = 0.0f;

	bool IsPermanent() const { return Duration <= 0.0f; }
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FMassUnitEntityHandle TargetEntity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FVector TargetLocation = FVector::ZeroVector;

	/** Explicit flag so the world origin remains a valid destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	bool bHasTargetLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	float TargetPriority = 0.0f;

	bool HasTarget() const { return TargetEntity.IsValid() || bHasTargetLocation; }
	void Clear() { TargetEntity.Invalidate(); TargetLocation = FVector::ZeroVector; bHasTargetLocation = false; TargetPriority = 0.0f; }
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitAbilityFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	TArray<FGameplayAbilitySpecHandle> AbilityHandles;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit")
	TArray<FGameplayTag> ActiveEffectTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	TArray<FGameplayTag> DefaultAbilityTags;

	/**
	 * Every gameplay tag the unit currently carries: the template's identity tags plus loose tags plus
	 * the tags of active timed statuses. This is what tag queries read; rebuild it with RebuildTags().
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Tags")
	FGameplayTagContainer OwnedTags;

	/** Tags set from the template or by gameplay with no duration. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Tags")
	FGameplayTagContainer LooseTags;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Status")
	TArray<FMassUnitStatusEffect> ActiveStatuses;

	/** Statuses this unit refuses, copied from the template. Matched hierarchically. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Mass Unit|Status")
	FGameplayTagContainer StatusImmunityTags;

	void RebuildTags()
	{
		OwnedTags = LooseTags;
		for (const FMassUnitStatusEffect& Status : ActiveStatuses)
		{
			OwnedTags.AddTag(Status.StatusTag);
		}
	}
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitTeamFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	int32 TeamID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FLinearColor TeamColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FGameplayTag TeamFaction;
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitVisualFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FGameplayTag CurrentAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FGameplayTag TargetAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlendAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	int32 LODLevel = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	bool bIsVisible = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	bool bUseSkeletalMesh = false;

	/** Distance-LOD request; the bounded pool independently decides whether capacity is available. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	bool bWantsSkeletalMesh = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	float ViewerDistanceSquared = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> AnimationBlueprintClass;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> IdleAnimation = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> MoveAnimation = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> AttackAnimation = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> DeathAnimation = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> StunAnimation = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	/** Look profile the unit's look was rolled from (null = template mesh as is). See UMassUnitLookProfile. */
	UPROPERTY(Transient)
	TObjectPtr<UMassUnitLookProfile> LookProfile = nullptr;

	/** Body material from the rolled look (null = the mesh's own). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MaterialOverride = nullptr;

	/** Modular parts from the rolled look, drawn with the body. */
	UPROPERTY(Transient)
	TArray<FMassUnitLookPiece> LookParts;

	/** Uniform instance scale from the rolled look. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	float LookScale = 1.0f;

	/** Tint from the rolled look (custom data 13-15). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	FLinearColor LookTint = FLinearColor::White;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> VertexAnimationTexture = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> NormalMapTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	TArray<FGameplayTag> AnimationTags;

	/** Template the unit was built from. Supplies the vertex-animation clip table to the renderer. */
	TWeakObjectPtr<const class UUnitTemplate> SourceTemplate;

	/** Speed above which the move clip switches from Walk to Run. Zero never uses Run. */
	float RunSpeedThreshold = 0.0f;

	/** Attitude profile the unit's temperament was rolled from (null = template values as is). */
	UPROPERTY(Transient)
	TObjectPtr<UMassUnitAttitudeProfile> AttitudeProfile = nullptr;

	/** Attitude: playback rate of looping vertex-animation clips (one-shots keep their timing). */
	float AnimationRate = 1.0f;

	/**
	 * Deterministic 0-1 seed for everything random about how this unit LOOKS (animation phase today; variant parts,
	 * tint and scale jitter later). Set at spawn from the entity index (the value the renderer always used) and
	 * overwritten on client puppets with the server's value, so every machine draws the same unit. Negative = unset.
	 */
	float VariantSeed = -1.0f;
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitFormationFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	int32 FormationHandle = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	int32 FormationSlot = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	FVector FormationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit")
	FGameplayTag DefaultFormation;

	bool IsInFormation() const { return FormationHandle != INDEX_NONE; }
};

USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitNavigationFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	FVector DestinationLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	TArray<FVector> PathPoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	int32 CurrentPathIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	bool bPathRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	bool bPathValid = false;

	/** True only when PathPoints came from native navigation data rather than direct fallback. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	bool bPathUsesNavmesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mass Unit", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float AcceptanceRadius = 50.0f;

	bool HasReachedDestination() const
	{
		return bPathValid && PathPoints.IsValidIndex(CurrentPathIndex) == false;
	}

	void ResetPath()
	{
		PathPoints.Reset();
		CurrentPathIndex = INDEX_NONE;
		bPathRequested = false;
		bPathValid = false;
		bPathUsesNavmesh = false;
	}
};

USTRUCT()
struct MASSUNITSYSTEMRUNTIME_API FMassUnitLODFragment : public FMassFragment
{
	GENERATED_BODY()
	int32 Level = 0;
	float NextUpdateTime = 0.0f;
};

USTRUCT()
struct MASSUNITSYSTEMRUNTIME_API FMassUnitVisualizationLODFragment : public FMassFragment
{
	GENERATED_BODY()
	int32 LODLevel = 0;
};

/** Lightweight state used by the timer-driven crowd service and movement processor. */
USTRUCT()
struct MASSUNITSYSTEMRUNTIME_API FMassUnitCrowdFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector SteeringDirection = FVector::ZeroVector;
	FVector LastDestination = FVector::ZeroVector;
	FVector LastFollowTargetLocation = FVector::ZeroVector;
	FMassUnitEntityHandle InteractionPartner;
	float BaseMoveSpeed = 0.0f;
	float NextDecisionTime = 0.0f;
	float NextSteeringUpdateTime = 0.0f;
	float NextFollowUpdateTime = 0.0f;
	float InteractionEndTime = 0.0f;
	int32 GroupHandle = INDEX_NONE;
	int32 SubgroupIndex = 0;
	int32 SharedPathRevision = INDEX_NONE;
	int32 DecisionSequence = 0;
	int32 SimulationLOD = 0;
	float NavigationHeightOffset = 0.0f;
	bool bEnabled = false;
	bool bSleeping = false;
	bool bUse3DMovement = false;
	bool bConformToNavmeshHeight = false;
	/** UMassUnitStackSystem owns this unit's position (climbing or standing on another unit); the movement processor skips it. */
	bool bStackDriven = false;
	/** Crowd motion style bookkeeping (Hive: 0 home, 1 foraging out, 2 lingering before the trip home). */
	uint8 MotionPhase = 0;
	/**
	 * Presentation only (client puppets of stacked units): EMassUnitStackRole replicated by the adopter. Non-zero makes the
	 * movement processor ease the unit's height toward its destination height and play the Climb / Hold clip. The real
	 * (server) stacked units never use it - they are bStackDriven.
	 */
	uint8 PresentationStackRole = 0;
	/**
	 * UMassUnitCommandSystem owns this unit's orders (move, stop, hold): the crowd system leaves its destination alone
	 * (no wander, motion styles, engagement, interactions or sleep) and the pile planner never uses it. Owned by the
	 * command system, so Reset() keeps it (a crowd group re-registering does not drop the unit's orders).
	 */
	bool bCommanded = false;

	void Reset()
	{
		SteeringDirection = FVector::ZeroVector;
		LastDestination = FVector::ZeroVector;
		LastFollowTargetLocation = FVector::ZeroVector;
		InteractionPartner.Invalidate();
		BaseMoveSpeed = 0.0f;
		NextDecisionTime = 0.0f;
		NextSteeringUpdateTime = 0.0f;
		NextFollowUpdateTime = 0.0f;
		InteractionEndTime = 0.0f;
		GroupHandle = INDEX_NONE;
		SubgroupIndex = 0;
		SharedPathRevision = INDEX_NONE;
		DecisionSequence = 0;
		SimulationLOD = 0;
		NavigationHeightOffset = 0.0f;
		bEnabled = false;
		bSleeping = false;
		bUse3DMovement = false;
		bConformToNavmeshHeight = false;
		bStackDriven = false;
		MotionPhase = 0;
		PresentationStackRole = 0;
	}
};

// These fragments intentionally own dynamic/reflected data. Mass supports such
// fragments, but requires authors to acknowledge their non-trivial copy cost.
template<>
struct TMassFragmentTraits<FMassUnitAbilityFragment>
{
	enum { AuthorAcceptsItsNotTriviallyCopyable = true };
};

template<>
struct TMassFragmentTraits<FMassUnitVisualFragment>
{
	enum { AuthorAcceptsItsNotTriviallyCopyable = true };
};

template<>
struct TMassFragmentTraits<FMassUnitNavigationFragment>
{
	enum { AuthorAcceptsItsNotTriviallyCopyable = true };
};
