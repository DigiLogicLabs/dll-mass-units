# DLL Mass Units — Project memory

Short, dated, verified facts and traps that the code doesn't make obvious. One line each, newest at the
bottom. Mark unverified items **UNVERIFIED**. Promote anything that becomes a rule into
[DECISIONS.md](DECISIONS.md). Rules for writing here: [../.github/instructions/ai-memory.instructions.md](../.github/instructions/ai-memory.instructions.md).

## Facts

- 2026-10-15 — Repo renamed on GitHub to `DigiLogicLabs/dll-mass-units`; `ue5-mass-units-plugin` URLs redirect.
  Local clones should run `git remote set-url origin https://github.com/DigiLogicLabs/dll-mass-units.git`.
- 2026-10-15 — `LICENSE` (Apache-2.0) and `NOTICE` added on `main`. Until then the public repo had no license.
- 2026-10-16 — Source file headers still read `// Copyright Digi Logic Labs LLC. All Rights Reserved.` (59 files
  in the reference adopter's working copy). The repo `LICENSE` governs; headers are swapped in the Tier B rename.
- 2026-10-16 — Plugin, folder and module names are still the pre-standard ones (`MassUnitSystem.uplugin`,
  `MassUnitSystemRuntime`, `MassUnitSystemEditor`); adopters reference `/Script/MassUnitSystemRuntime` in assets
  and config sections. Rename plan: [DECISIONS.md](DECISIONS.md) 2026-10-15.
- 2026-10-16 — `.uplugin` `EnabledByDefault` set to `false` (standard); adopters enable it explicitly in their
  `.uproject` (the reference adopter already lists `"MassUnitSystem": Enabled`).
- 2026-10-16 — `UMassUnitSubsystem` is a `UTickableWorldSubsystem`: one tick for the whole crowd, not per unit.
- 2026-10-16 — The runtime module has a hard `GameplayAbilities` dependency even though GAS use is optional at
  runtime. Splitting it out is an open decision.

## Traps

- Windows is case-insensitive: `docs/prd.md` vs `docs/PRD.md` are one file. Never add a doc differing only by case.
- Standalone `RunUAT BuildPlugin` needs a short `-Package` path (260-character limit).
- Only one UnrealBuildTool per engine install; `ConflictingInstance` means wait, not `-NoMutex`.
- 2026-10-16 — Supersedes the header line above: all 62 source files now carry the Apache-2.0 SPDX header; README
  carries the dll-mass-units identity, install URL and a License section. Attribution audit: no third-party code,
  assets or URLs in the repo; GASCompanion appears only in archived research docs (never a dependency).
