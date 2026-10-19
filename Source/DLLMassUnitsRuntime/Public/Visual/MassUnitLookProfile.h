// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MassUnitLookProfile.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** One weighted choice for the body or a part. */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitLookOption
{
	GENERATED_BODY()

	/** Instanced (vertex-animated) mesh. For a part, empty means "this choice shows no part". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Optional material for every slot of the mesh (a different skin, palette or faction texture). Empty = the mesh's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	TSoftObjectPtr<UMaterialInterface> Material;

	/** Relative chance against the other choices of the same list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;
};

/**
 * A modular piece drawn with the body: a head, a hat, a weapon, armour. It shares the body's transform and every
 * per-instance value (animation clip, frame, team, health, seed, tint), so a vertex-animated part must be baked against
 * the same skeleton with the same frame layout as the body (one AnimToTexture pass for the whole kit).
 */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitLookPart
{
	GENERATED_BODY()

	/** Designer label (Head, Hat, Weapon). Not read at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FName PartName;

	/** Chance the unit gets this part at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (TitleProperty = "Mesh"))
	TArray<FMassUnitLookOption> Options;
};

/** One drawn piece of a rolled look. */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitLookPiece
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Look")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** Null = the mesh's own materials. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Look")
	TObjectPtr<UMaterialInterface> Material = nullptr;
};

/** The result of rolling a look profile for one unit. */
USTRUCT(BlueprintType)
struct DLLMASSUNITSRUNTIME_API FMassUnitRolledLook
{
	GENERATED_BODY()

	/** Body mesh (null = keep the template's StaticMesh) and its material override. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Look")
	FMassUnitLookPiece Body;

	/** Parts that came up, in profile order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Look")
	TArray<FMassUnitLookPiece> Parts;

	/** Uniform scale on the unit's instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Look")
	float Scale = 1.0f;

	/** Colour from the tint palette (white with no palette). Materials read it from custom data 13-15. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Look")
	FLinearColor Tint = FLinearColor::White;
};

/**
 * How a unit looks, separate from what it is: weighted body variants, modular parts, a scale range and a tint palette.
 * Assign it on a template (UUnitTemplate::LookProfile), per spawn batch (FMassUnitBatchSpawnOptions::LookProfile) or per
 * unit (UMassUnitEntityManager::SetUnitLookProfile), so one minion's stats and behaviour can wear many looks and one look
 * can dress many minions.
 *
 * The roll is deterministic from the unit's variant seed, quantised to a byte - the same precision an adopter sends to
 * clients - so a server and its clients draw the same unit without replicating anything but the seed and the profile.
 * Instanced band only: the skeletal near band still draws the template's SkeletalMesh.
 */
UCLASS(BlueprintType)
class DLLMASSUNITSRUNTIME_API UMassUnitLookProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Body choices. Empty = the template's StaticMesh with no material override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (TitleProperty = "Mesh"))
	TArray<FMassUnitLookOption> BodyVariants;

	/** Modular pieces drawn with the body (see FMassUnitLookPart for the baking rule). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (TitleProperty = "PartName"))
	TArray<FMassUnitLookPart> Parts;

	/** Uniform scale range (min, max) rolled per unit. Keep it narrow for units that stack or must fit doorways. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (ClampMin = "0.05"))
	FVector2D ScaleRange = FVector2D(1.0, 1.0);

	/** Tint colours, one picked per unit (custom data 13-15; the material decides what it tints). Empty = white. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	TArray<FLinearColor> TintPalette;

	/**
	 * Rolls the look for a 0-1 variant seed. Same seed (to 1/255), same look, on every machine. Loads the chosen soft
	 * meshes and materials synchronously (each asset once).
	 */
	UFUNCTION(BlueprintCallable, Category = "Mass Unit System|Look")
	FMassUnitRolledLook RollLook(float VariantSeed) const;

	/**
	 * Loads every mesh and material any roll can pick and appends them to OutAssets (the caller keeps them referenced).
	 * Call once per profile, outside combat: later rolls then never hitch on a first-time load.
	 */
	void LoadAllOptionAssets(TArray<UObject*>& OutAssets) const;

	/** Most parts any roll of this profile can draw (parts with at least one mesh). */
	UFUNCTION(BlueprintPure, Category = "Mass Unit System|Look")
	int32 GetMaxPartCount() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
