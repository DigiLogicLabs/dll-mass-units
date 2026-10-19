// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Config/UnitConfigDataAsset.h"

UUnitConfigDataAsset::UUnitConfigDataAsset()
    : FormationSpacing(150.0f)
    , SkeletalMeshDistance(300.0f)
    , MaxVisibleDistance(10000.0f)
{
    // Set default LOD distance thresholds
    LODDistanceThresholds.Add(500.0f);   // LOD 0
    LODDistanceThresholds.Add(1500.0f);  // LOD 1
    LODDistanceThresholds.Add(3000.0f);  // LOD 2
    LODDistanceThresholds.Add(6000.0f);  // LOD 3
}
