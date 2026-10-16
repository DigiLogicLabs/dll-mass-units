// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Config/MassUnitSystemSettings.h"

UMassUnitSystemSettings::UMassUnitSystemSettings()
{
	CategoryName = TEXT("Plugins");
	LODDistanceThresholds = {500.0f, 1500.0f, 3000.0f, 6000.0f};
	VisibilityLODUpdateIntervals = {0.05f, 0.1f, 0.2f, 0.5f, 1.0f};
	CrowdSimulationLODDistances = {2500.0f, 5000.0f, 10000.0f};
	CrowdSimulationLODIntervalMultipliers = {1.0f, 2.0f, 4.0f, 8.0f};
}

UMassUnitSystemSettings* UMassUnitSystemSettings::Get()
{
	return GetMutableDefault<UMassUnitSystemSettings>();
}
