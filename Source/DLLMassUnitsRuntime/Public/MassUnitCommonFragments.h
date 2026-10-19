// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassUnitCommonFragments.generated.h"

USTRUCT()
struct DLLMASSUNITSRUNTIME_API FMassUnitVelocityFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Value = FVector::ZeroVector;
};

USTRUCT()
struct DLLMASSUNITSRUNTIME_API FMassUnitForceFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Value = FVector::ZeroVector;
};

USTRUCT()
struct DLLMASSUNITSRUNTIME_API FMassUnitLookAtFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Direction = FVector::ForwardVector;
};
