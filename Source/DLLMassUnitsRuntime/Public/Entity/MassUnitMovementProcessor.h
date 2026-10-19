// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MassUnitMovementProcessor.generated.h"

/** Moves plugin-owned Mass entities along navigation paths or direct targets. */
UCLASS()
class DLLMASSUNITSRUNTIME_API UMassUnitMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassUnitMovementProcessor();
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", ForceUnits = "cm/s^2"))
	float Acceleration = 1000.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", ForceUnits = "cm/s^2"))
	float Deceleration = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", ForceUnits = "deg/s"))
	float TurningRate = 360.0f;
};
