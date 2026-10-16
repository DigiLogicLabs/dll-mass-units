// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Entity/MassUnitFormationProcessor.h"

#include "Entity/MassUnitFragments.h"

UMassUnitFormationProcessor::UMassUnitFormationProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = false;
}

void UMassUnitFormationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassUnitFormationFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassUnitFormationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// UFormationSystem performs this work because it owns formation handles and slot layouts.
}
