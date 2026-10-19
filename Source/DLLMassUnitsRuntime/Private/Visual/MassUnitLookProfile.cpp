// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Visual/MassUnitLookProfile.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "MassUnitLookProfile"

namespace MassUnitLook
{
	/** Weighted pick; INDEX_NONE when the list is empty or every weight is zero. Always draws exactly one value. */
	static int32 PickWeighted(const TArray<FMassUnitLookOption>& Options, FRandomStream& Stream)
	{
		const float Alpha = Stream.FRand();
		float Total = 0.0f;
		for (const FMassUnitLookOption& Option : Options)
		{
			Total += FMath::Max(0.0f, Option.Weight);
		}
		if (Total <= 0.0f)
		{
			return INDEX_NONE;
		}
		float Roll = Alpha * Total;
		for (int32 Index = 0; Index < Options.Num(); ++Index)
		{
			Roll -= FMath::Max(0.0f, Options[Index].Weight);
			if (Roll <= 0.0f && Options[Index].Weight > 0.0f)
			{
				return Index;
			}
		}
		// Float round-off: the last option with weight.
		for (int32 Index = Options.Num() - 1; Index >= 0; --Index)
		{
			if (Options[Index].Weight > 0.0f)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	static FMassUnitLookPiece Load(const FMassUnitLookOption& Option)
	{
		FMassUnitLookPiece Piece;
		Piece.Mesh = Option.Mesh.LoadSynchronous();
		Piece.Material = Option.Material.LoadSynchronous();
		return Piece;
	}
}

FMassUnitRolledLook UMassUnitLookProfile::RollLook(float VariantSeed) const
{
	// The seed to a byte: what an adopter replicates, so server and clients roll the same stream.
	const int32 SeedByte = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(VariantSeed, 0.0f, 1.0f) * 255.0f), 0, 255);
	FRandomStream Stream(0x4D55 + SeedByte * 7919);

	// Every roll draws the same number of values in the same order whatever came up, so changing an option's mesh,
	// material or weight never reshuffles other parts. Adding or removing a part (or a part's whole option list) shifts
	// the stream for everything after it, scale and tint included: append new parts at the end.
	FMassUnitRolledLook Look;
	const int32 BodyIndex = MassUnitLook::PickWeighted(BodyVariants, Stream);
	if (BodyIndex != INDEX_NONE)
	{
		Look.Body = MassUnitLook::Load(BodyVariants[BodyIndex]);
	}
	for (const FMassUnitLookPart& Part : Parts)
	{
		const bool bAppears = Stream.FRand() < Part.Chance;
		const int32 OptionIndex = MassUnitLook::PickWeighted(Part.Options, Stream);
		if (bAppears && OptionIndex != INDEX_NONE)
		{
			FMassUnitLookPiece Piece = MassUnitLook::Load(Part.Options[OptionIndex]);
			if (Piece.Mesh)
			{
				Look.Parts.Add(Piece);
			}
		}
	}
	const float ScaleAlpha = Stream.FRand();
	const double MinScale = FMath::Max(0.05, FMath::Min(ScaleRange.X, ScaleRange.Y));
	const double MaxScale = FMath::Max(0.05, FMath::Max(ScaleRange.X, ScaleRange.Y));
	Look.Scale = static_cast<float>(FMath::Lerp(MinScale, MaxScale, static_cast<double>(ScaleAlpha)));
	const int32 TintIndex = Stream.RandHelper(FMath::Max(1, TintPalette.Num()));
	Look.Tint = TintPalette.IsValidIndex(TintIndex) ? TintPalette[TintIndex] : FLinearColor::White;
	return Look;
}

void UMassUnitLookProfile::LoadAllOptionAssets(TArray<UObject*>& OutAssets) const
{
	auto LoadOption = [&OutAssets](const FMassUnitLookOption& Option)
	{
		if (UObject* Mesh = Option.Mesh.LoadSynchronous())
		{
			OutAssets.Add(Mesh);
		}
		if (UObject* Material = Option.Material.LoadSynchronous())
		{
			OutAssets.Add(Material);
		}
	};
	for (const FMassUnitLookOption& Option : BodyVariants)
	{
		LoadOption(Option);
	}
	for (const FMassUnitLookPart& Part : Parts)
	{
		for (const FMassUnitLookOption& Option : Part.Options)
		{
			LoadOption(Option);
		}
	}
}

int32 UMassUnitLookProfile::GetMaxPartCount() const
{
	int32 Count = 0;
	for (const FMassUnitLookPart& Part : Parts)
	{
		Count += Part.Chance > 0.0f && Part.Options.ContainsByPredicate([](const FMassUnitLookOption& Option)
		{
			return !Option.Mesh.IsNull() && Option.Weight > 0.0f;
		}) ? 1 : 0;
	}
	return Count;
}

#if WITH_EDITOR
EDataValidationResult UMassUnitLookProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (int32 Index = 0; Index < BodyVariants.Num(); ++Index)
	{
		if (BodyVariants[Index].Mesh.IsNull() && BodyVariants[Index].Material.IsNull())
		{
			Context.AddWarning(FText::Format(LOCTEXT("EmptyBody", "Body variant {0} has neither a mesh nor a material: it shows the template's mesh unchanged."), Index));
		}
	}
	for (const FMassUnitLookPart& Part : Parts)
	{
		if (Part.Options.IsEmpty())
		{
			Context.AddWarning(FText::Format(LOCTEXT("EmptyPart", "Part '{0}' has no options and never shows."), FText::FromName(Part.PartName)));
		}
	}
	if (FMath::Min(ScaleRange.X, ScaleRange.Y) <= 0.0)
	{
		Context.AddError(LOCTEXT("BadScale", "Scale Range must be above zero."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
