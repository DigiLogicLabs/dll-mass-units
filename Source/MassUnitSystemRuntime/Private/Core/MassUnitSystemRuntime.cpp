// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "Core/MassUnitSystemRuntime.h"
#include "Core/MassUnitStats.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMassUnitSystem);

// CSV custom stats for stress captures (`csvprofile start` / -csvCaptureFrames). See Core/MassUnitStats.h.
CSV_DEFINE_CATEGORY_MODULE(MASSUNITSYSTEMRUNTIME_API, MassUnits, true);

IMPLEMENT_MODULE(FMassUnitSystemRuntimeModule, MassUnitSystemRuntime);

void FMassUnitSystemRuntimeModule::StartupModule()
{
	UE_LOG(LogMassUnitSystem, Log, TEXT("Mass Unit System runtime module started"));
}

void FMassUnitSystemRuntimeModule::ShutdownModule()
{
	UE_LOG(LogMassUnitSystem, Log, TEXT("Mass Unit System runtime module shut down"));
}
