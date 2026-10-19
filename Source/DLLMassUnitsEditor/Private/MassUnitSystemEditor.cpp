// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#include "MassUnitSystemEditor.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMassUnitSystemEditor, Log, All);

IMPLEMENT_MODULE(FMassUnitSystemEditorModule, DLLMassUnitsEditor);

void FMassUnitSystemEditorModule::StartupModule()
{
	UE_LOG(LogMassUnitSystemEditor, Verbose, TEXT("Mass Unit System editor module started"));
}

void FMassUnitSystemEditorModule::ShutdownModule()
{
	UE_LOG(LogMassUnitSystemEditor, Verbose, TEXT("Mass Unit System editor module shut down"));
}
