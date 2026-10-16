// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MassUnitCombatProcessor.generated.h"

/** Lightweight, server-authoritative-friendly combat simulation for Mass units. */
UCLASS()
class MASSUNITSYSTEMRUNTIME_API UMassUnitCombatProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassUnitCombatProcessor();
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.0f;
};
