// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MassUnitFormationProcessor.generated.h"

/**
 * Compatibility processor type. Formation targets are updated by UFormationSystem,
 * which owns the world-level formation registry and can be called from Blueprint.
 */
UCLASS()
class MASSUNITSYSTEMRUNTIME_API UMassUnitFormationProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassUnitFormationProcessor();
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
