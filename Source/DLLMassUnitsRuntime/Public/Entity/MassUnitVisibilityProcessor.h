// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MassUnitVisibilityProcessor.generated.h"

/** Updates plugin-specific distance LOD and representation intent. */
UCLASS()
class DLLMASSUNITSRUNTIME_API UMassUnitVisibilityProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassUnitVisibilityProcessor();
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
