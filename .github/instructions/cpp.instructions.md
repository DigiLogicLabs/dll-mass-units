---
applyTo: "Source/**/*.{h,cpp,cs}"
---
# C++ rules (DLL Mass Units)

- **Dependencies:** `MassUnitSystemRuntime.Build.cs` is a contract. Adding a module dependency there is an
  architectural change; prefer a new optional module (see `.github/prompts/add-integration-module.prompt.md`).
  Never add a game module or another dll-* plugin.
- **Naming:** keep the established `UMassUnit*` / `FMassUnit*` / `EMassUnit*` prefixes; never rename existing
  types. CVars `dll.MassUnits.*`. Tags under `MassUnit.*`.
- **Header hygiene:** forward-declare; include in `.cpp`; `MASSUNITSYSTEMRUNTIME_API` on exported types
  (becomes `DLLMASSUNITSRUNTIME_API` at the rename).
- **New file header:** Apache-2.0 SPDX header (see AGENTS.md). Existing "All Rights Reserved" headers are
  swapped in one pass at the rename, not piecemeal.
- **UPROPERTY:** doc comment above (becomes the tooltip), `Category`, `ClampMin/Max`/`ForceUnits` where numeric,
  `EditCondition` for debug sub-toggles. Soft pointers for meshes, materials and Niagara systems.
- **No per-unit Tick, no per-frame logging.** Processors and the subsystem tick early-out when idle. Time cost
  with `MASSUNIT_SCOPE_CYCLE` and counts with `MASSUNIT_SET_COUNTER` (`Core/MassUnitStats.h`).
- **Lifetime:** weak pointers to world objects held across frames; `UPROPERTY` for owned UObjects; entity
  handles validated before use.
- **Testability:** decision logic in plain C++ where possible, covered by `MassUnitSystem.*` tests.
- **Public API is additive** between minor versions. Renames use `[CoreRedirects]` + a changelog migration note.
