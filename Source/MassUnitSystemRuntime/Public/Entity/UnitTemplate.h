// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Animation/AnimInstance.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "UnitTemplate.generated.h"

class UScriptStruct;
class UAnimationAsset;
class USkeletalMesh;
class UStaticMesh;
class UTexture2D;

/**
 * One clip inside a baked vertex-animation (AnimToTexture) texture.
 *
 * AnimToTexture packs every clip of a mesh into one texture, one after another. This row says where the
 * clip for an animation tag starts and how to play it. The crowd renderer writes the row of the unit's
 * current clip into per-instance custom data (channels ClipStartFrame, ClipFrameCount and
 * ClipFramesPerSecond), so a single material plays any clip without per-template material switches.
 */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitVATClip
{
	GENERATED_BODY()

	/** MassUnit.Animation.* tag this clip plays for. Walk/Run fall back to each other; anything else falls back to Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex Animation", meta = (Categories = "MassUnit.Animation"))
	FGameplayTag AnimationTag;

	/** First frame of the clip in the baked texture (AnimToTexture "Animations[i].StartFrame"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex Animation", meta = (ClampMin = "0"))
	int32 StartFrame = 0;

	/** Frames in the clip (EndFrame - StartFrame + 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex Animation", meta = (ClampMin = "1"))
	int32 NumFrames = 1;

	/** Bake sample rate times play rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex Animation", meta = (ClampMin = "0.01"))
	float FramesPerSecond = 30.0f;

	/** Loops (idle, move) or plays once and holds its last frame (attack, death). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex Animation")
	bool bLoop = true;

	/** For a non-looping clip other than Death: return to the Idle clip when it finishes instead of holding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex Animation", meta = (EditCondition = "!bLoop"))
	bool bReturnToIdleWhenDone = true;

	float GetDuration() const { return FramesPerSecond > UE_SMALL_NUMBER ? static_cast<float>(FMath::Max(1, NumFrames)) / FramesPerSecond : 0.0f; }
};

/**
 * Template for creating units in the Mass Unit System
 */
UCLASS(BlueprintType)
class MASSUNITSYSTEMRUNTIME_API UUnitTemplate : public UDataAsset
{
    GENERATED_BODY()

public:
    UUnitTemplate();

    /** Unit type tag */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template")
    FGameplayTag UnitType;

    /** Unit class tag */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template")
    FGameplayTag UnitClass;

    /** Base level for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template")
    int32 BaseLevel = 1;

    /** Base health for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template")
    int32 BaseHealth = 100;

    /** Base damage for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template")
    int32 BaseDamage = 10;

    /** Move speed for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template")
    float MoveSpeed = 300.0f;

    /** Maximum distance at which this unit can perform its basic attack. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Combat", meta = (ClampMin = "0.0", ForceUnits = "cm"))
    float AttackRange = 200.0f;

    /** Time between basic attacks. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Combat", meta = (ClampMin = "0.0", ForceUnits = "s"))
    float AttackCooldown = 1.0f;

    /**
     * Seconds from the start of a swing to the frame its damage lands; match it to the attack clip's
     * impact pose. Zero applies damage the moment the swing starts (the original behaviour). Keep it
     * below AttackCooldown. A unit that leaves range or loses its attack token before impact whiffs.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Combat", meta = (ClampMin = "0.0", ForceUnits = "s"))
    float AttackImpactDelay = 0.0f;

    /**
     * How far hits throw this unit: launch velocity is multiplied by this. 1 = normal, above 1 = a light goblin that
     * flies further, 0 = never launched (heavies, bosses) - it is damaged and dies in place.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Combat", meta = (ClampMin = "0.0", ClampMax = "4.0"))
    float LaunchVelocityScale = 1.0f;

    /** Default abilities for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template")
    TArray<FGameplayTag> DefaultAbilities;

    /**
     * Gameplay tags every unit of this template carries from spawn, for example Enemy.Goblin or
     * Creature.Small. Gameplay code queries them with UnitHasTag / FindUnitsWithTags the same way it
     * would query an actor's ASC, and statuses add to them at runtime.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Tags")
    FGameplayTagContainer GrantedTags;

    /**
     * Statuses this template cannot receive, matched hierarchically. Status.Stunned here makes a boss
     * unit stun-immune; Status here makes it immune to every status.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Tags")
    FGameplayTagContainer StatusImmunityTags;

    /**
     * What the unit's body is made of, for the host project's material-keyed systems (gore pools and splats, impact
     * effects): a flesh tag that bleeds, a metal tag that sparks or leaks oil. The tag vocabulary is the project's;
     * the plugin never reads it. Empty = the project's crowd default.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Tags")
    FGameplayTag BodyMaterialTag;

    /** Skeletal mesh for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual")
    TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Optional close-range Animation Blueprint. State clips below take precedence when assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Skeletal Animation")
	TSoftClassPtr<UAnimInstance> AnimationBlueprintClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Skeletal Animation")
	TSoftObjectPtr<UAnimationAsset> IdleAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Skeletal Animation")
	TSoftObjectPtr<UAnimationAsset> MoveAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Skeletal Animation")
	TSoftObjectPtr<UAnimationAsset> AttackAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Skeletal Animation")
	TSoftObjectPtr<UAnimationAsset> DeathAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Skeletal Animation")
	TSoftObjectPtr<UAnimationAsset> StunAnimation;

    /** Static mesh for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual")
    TSoftObjectPtr<UStaticMesh> StaticMesh;

    /** Vertex animation texture for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual")
    TSoftObjectPtr<UTexture2D> VertexAnimationTexture;

    /** Normal map texture for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual")
    TSoftObjectPtr<UTexture2D> NormalMapTexture;

    /** Animation tags for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual")
    TArray<FGameplayTag> AnimationTags;

    /**
     * Where each animation lives inside the baked vertex-animation texture. Fill it from the
     * AnimToTexture data asset (Tools/Python/mass_unit_vat_bake.py does this for you). Empty keeps the
     * legacy behaviour: only the global animation index is written and no clip is played.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Vertex Animation", meta = (TitleProperty = "AnimationTag"))
    TArray<FMassUnitVATClip> VertexAnimationClips;

    /** Horizontal speed above which a moving unit plays Run instead of Walk. Zero always plays Walk. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Visual|Vertex Animation", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
    float RunSpeedThreshold = 0.0f;

    /** Clip for an animation tag, with Walk/Run falling back to each other and everything else to Idle. Null when the table is empty. */
    const FMassUnitVATClip* FindVertexAnimationClip(const FGameplayTag& AnimationTag) const;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

    /** Default behavior tag for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Behavior")
    FGameplayTag DefaultBehavior;

    /** Default formation tag for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Behavior")
    FGameplayTag DefaultFormation;

    /** Team ID for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Team")
    int32 TeamID = 0;

    /** Team color for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Team")
    FLinearColor TeamColor = FLinearColor::White;

    /** Team faction tag for the unit */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unit Template|Team")
    FGameplayTag TeamFaction;

    /** Native Mass fragments used by every unit archetype created from this template. */
    TArray<const UScriptStruct*> GetRequiredFragments() const;
};
