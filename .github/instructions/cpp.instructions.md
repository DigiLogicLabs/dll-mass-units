---
applyTo: "Source/**/*.{h,cpp,cs}"
---
# C++ rules (DLL Mass Units)

Shared rules for every dll-* plugin: [docs/DLL_PLUGIN_STANDARD.md](../../docs/DLL_PLUGIN_STANDARD.md#coding-rules-shared-by-every-dll--plugin).
The rules below are specific to this plugin.

- **Dependencies:** `DLLMassUnitsRuntime.Build.cs` is a contract. Adding a module dependency there is an
  architectural change; prefer a new optional module (see `.github/prompts/add-integration-module.prompt.md`).
  Never add a game module or another dll-* plugin.
- **Naming:** keep the established `UMassUnit*` / `FMassUnit*` / `EMassUnit*` prefixes; never rename existing
  types. CVars `dll.MassUnits.*`. Tags under `MassUnit.*`.
- **Header hygiene:** forward-declare; include in `.cpp`; `DLLMASSUNITSRUNTIME_API` on exported types.
- **File header:** Apache-2.0 SPDX header on every source file (see AGENTS.md).
- **UPROPERTY:** doc comment above (becomes the tooltip), `Category`, `ClampMin/Max`/`ForceUnits` where numeric,
  `EditCondition` for debug sub-toggles. Soft pointers for meshes, materials and Niagara systems.
- **No per-unit Tick, no per-frame logging.** Processors and the subsystem tick early-out when idle. Time cost
  with `MASSUNIT_SCOPE_CYCLE` and counts with `MASSUNIT_SET_COUNTER` (`Core/MassUnitStats.h`).
- **Lifetime:** weak pointers to world objects held across frames; `UPROPERTY` for owned UObjects; entity
  handles validated before use.
- **Testability:** decision logic in plain C++ where possible, covered by `DLLMassUnits.*` tests.
- **Public API is additive** between minor versions. Renames use `[CoreRedirects]` + a changelog migration note.
