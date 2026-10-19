// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Gameplay/MassUnitAttitudeProfile.h"

#include "Math/RandomStream.h"

namespace MassUnitAttitude
{
	static float Lerp(const FVector2D& Range, float Alpha, float MinValue)
	{
		const double Low = FMath::Max(static_cast<double>(MinValue), FMath::Min(Range.X, Range.Y));
		const double High = FMath::Max(static_cast<double>(MinValue), FMath::Max(Range.X, Range.Y));
		return static_cast<float>(FMath::Lerp(Low, High, static_cast<double>(Alpha)));
	}
}

FMassUnitRolledAttitude UMassUnitAttitudeProfile::RollAttitude(float VariantSeed) const
{
	// Byte precision (what adopters replicate) and a salt of its own, so attitude and look do not move together.
	const int32 SeedByte = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(VariantSeed, 0.0f, 1.0f) * 255.0f), 0, 255);
	FRandomStream Stream(0xA771 + SeedByte * 104729);

	// A fixed number of draws per roll, whatever the settings, so toggling one option never reshuffles the others.
	const float Energy = Stream.FRand();
	const float SpeedAlpha = Stream.FRand();
	const float CooldownAlpha = Stream.FRand();
	const float AnimationAlpha = Stream.FRand();
	const float CourageAlpha = Stream.FRand();
	const float ClimbAlpha = Stream.FRand();

	FMassUnitRolledAttitude Attitude;
	Attitude.MoveSpeedMultiplier = MassUnitAttitude::Lerp(MoveSpeedRange, bLinkRolls ? Energy : SpeedAlpha, 0.05f);
	// Energetic units attack more often: the linked roll runs the cooldown range from max down to min.
	Attitude.AttackCooldownMultiplier = MassUnitAttitude::Lerp(AttackCooldownRange, bLinkRolls ? 1.0f - Energy : CooldownAlpha, 0.05f);
	Attitude.AnimationRate = MassUnitAttitude::Lerp(AnimationRateRange, bLinkRolls ? Energy : AnimationAlpha, 0.05f);
	Attitude.Courage = bRollCourage ? FMath::Clamp(MassUnitAttitude::Lerp(CourageRange, CourageAlpha, 0.0f), 0.0f, 1.0f) : -1.0f;
	Attitude.bWillClimb = ClimbAlpha < ClimbChance || ClimbChance >= 1.0f;
	return Attitude;
}
