// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Gameplay/MassUnitSwarmType.h"

#include "Core/MassUnitSubsystem.h"
#include "Entity/UnitTemplate.h"
#include "Gameplay/MassUnitAttitudeProfile.h"
#include "Gameplay/MassUnitBehaviorPreset.h"
#include "Math/RandomStream.h"
#include "Visual/MassUnitLookProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "MassUnitSwarmType"

FMassUnitCrowdConfig UMassUnitSwarmType::MakeCrowdConfig() const
{
	FMassUnitCrowdConfig Config = BehaviorPreset ? BehaviorPreset->Crowd : FMassUnitCrowdConfig();
	if (Locomotion == EMassUnitLocomotion::Air)
	{
		Config.MovementMode = EMassUnitCrowdMovementMode::Free3D;
		Config.bUseAltitudeBand = true;
		Config.MinAltitude = MinAltitude;
		Config.MaxAltitude = FMath::Max(MinAltitude, MaxAltitude);
		Config.EngagedHoverHeight = EngagedHoverHeight;
		Config.bFallWhenKilled = bFallWhenKilled;
	}
	return Config;
}

FMassUnitSwarmSpawnResult UMassUnitSwarmType::SpawnSwarm(const UObject* WorldContextObject, UMassUnitSwarmType* SwarmType, FVector Center,
	int32 Count, float Radius, int32 RandomSeed, int32 TeamOverride)
{
	FMassUnitSwarmSpawnResult Result;
	UMassUnitSubsystem* Subsystem = UMassUnitSubsystem::Get(WorldContextObject);
	UMassUnitEntityManager* Manager = Subsystem ? Subsystem->GetUnitManager() : nullptr;
	UMassUnitCrowdSystem* CrowdSystem = Subsystem ? Subsystem->GetCrowdSystem() : nullptr;
	if (!SwarmType || !SwarmType->Template || !Manager || !CrowdSystem || Count <= 0)
	{
		return Result;
	}

	const bool bAir = SwarmType->Locomotion == EMassUnitLocomotion::Air;
	const float LowZ = bAir ? SwarmType->MinAltitude : 0.0f;
	const float HighZ = bAir ? FMath::Max(SwarmType->MinAltitude, SwarmType->MaxAltitude) : 0.0f;
	FRandomStream Stream(RandomSeed);
	TArray<FTransform> Transforms;
	Transforms.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = Stream.FRandRange(0.0f, 2.0f * UE_PI);
		const float Distance = FMath::Sqrt(Stream.FRand()) * FMath::Max(0.0f, Radius);
		const FVector Location = Center + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, Stream.FRandRange(LowZ, HighZ));
		Transforms.Emplace(FRotator(0.0f, Stream.FRandRange(-180.0f, 180.0f), 0.0f), Location);
	}

	FMassUnitBatchSpawnOptions Options;
	Options.RandomSeed = RandomSeed;
	Options.bSeedVariants = SwarmType->bSeedVariants;
	Options.LookProfile = SwarmType->LookProfile;
	Options.AttitudeProfile = SwarmType->AttitudeProfile;
	if (TeamOverride >= 0)
	{
		Options.bOverrideTeam = true;
		Options.TeamID = TeamOverride;
	}
	Result.Units = Manager->CreateUnitsFromTemplate(SwarmType->Template, Transforms, Options);
	if (Result.Units.IsEmpty())
	{
		return Result;
	}
	Result.CrowdGroupHandle = CrowdSystem->RegisterCrowdGroupFromPresetWithCrowd(
		Result.Units, Center, SwarmType->BehaviorPreset, SwarmType->MakeCrowdConfig());
	return Result;
}

#if WITH_EDITOR
EDataValidationResult UMassUnitSwarmType::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!Template)
	{
		Context.AddError(LOCTEXT("NoTemplate", "A swarm type needs a Template (the minion it spawns)."));
		Result = EDataValidationResult::Invalid;
	}
	if (Locomotion == EMassUnitLocomotion::Air && MaxAltitude < MinAltitude)
	{
		Context.AddWarning(LOCTEXT("BadBand", "Max Altitude is below Min Altitude: the band collapses to Min Altitude."));
	}
	if (Locomotion == EMassUnitLocomotion::Air && Template && Template->Stacking.bCanStack)
	{
		Context.AddWarning(LOCTEXT("AirStack", "The template can stack. Flyers never pile (the planner skips banded groups), but the same template on the ground will."));
	}
	if (Locomotion == EMassUnitLocomotion::Air && BehaviorPreset && BehaviorPreset->bUseNavigation)
	{
		Context.AddWarning(LOCTEXT("AirNav", "Flyers ignore the preset's navigation (Free 3D movement never uses the navmesh)."));
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
