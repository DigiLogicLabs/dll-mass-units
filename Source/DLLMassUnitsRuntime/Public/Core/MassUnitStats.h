// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

// Mass Unit System - profiling instrumentation.
//
// One stat group for the whole plugin, so `stat MassUnitSystem` shows every simulation, crowd and presentation cost
// side by side, and Unreal Insights shows the same scopes as CPU trace events. Counters are also emitted as CSV
// custom stats (category "MassUnits") for `csvprofile start/stop` captures and PerfReportTool, so stress runs at
// different unit counts compare file to file.
//
// Zero gameplay effect: these macros only measure. Stats compile out of Shipping builds.

#pragma once

#include "Stats/Stats.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_STATS_GROUP(TEXT("MassUnitSystem"), STATGROUP_MassUnitSystem, STATCAT_Advanced);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(DLLMASSUNITSRUNTIME_API, MassUnits);

/**
 * Cycle stat + Insights trace scope + CSV timing (column "MassUnits/<StatId>", ms) with one name. Use once per scope, at
 * its top. The CSV part costs nothing unless a capture is running.
 */
#define MASSUNIT_SCOPE_CYCLE(StatId) \
	SCOPE_CYCLE_COUNTER(StatId); \
	TRACE_CPUPROFILER_EVENT_SCOPE(StatId); \
	CSV_SCOPED_TIMING_STAT(MassUnits, StatId)

/** Set a DWORD counter stat and the CSV stat of the same name in one call (per-frame values). */
#define MASSUNIT_SET_COUNTER(StatId, CsvName, Value) \
	SET_DWORD_STAT(StatId, (Value)); \
	CSV_CUSTOM_STAT(MassUnits, CsvName, static_cast<int32>(Value), ECsvCustomStatOp::Set)
