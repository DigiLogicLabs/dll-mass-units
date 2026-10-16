# AGENTS.md — DLL Mass Units

Canonical instructions for AI coding agents (Codex, Claude Code, Copilot, others) and a fast orientation for
human contributors. `CLAUDE.md` and `.github/copilot-instructions.md` point here — **edit this file, not them.**

## What this repo is

An open-source (Apache-2.0) Unreal Engine 5 plugin by Digi Logic Labs (repo `DigiLogicLabs/dll-mass-units`,
formerly `ue5-mass-units-plugin`; the old URL redirects): large crowds of lightweight units on the native
**Mass Entity** framework, with no Actor or UObject per unit. Movement (planar / free 3D), teams, lightweight
combat and native health, formations, batched navigation, budgeted behaviour LOD, instanced rendering with a
bounded skeletal-mesh near band, and an optional Gameplay Ability System seam. Version and verified engine:
`MassUnitSystem.uplugin` and `README.md`.

## Read before non-trivial work

1. [docs/README.md](docs/README.md) — knowledge-graph index
2. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — modules, subsystems, processors
3. [docs/DECISIONS.md](docs/DECISIONS.md) — the *why*; don't relitigate a recorded decision without new evidence
4. [docs/MEMORY.md](docs/MEMORY.md) — verified facts and traps
5. [CHANGELOG.md](CHANGELOG.md) — what changed, *Unreleased* first

## Layout (current names — see "Pending rename")

```
MassUnitSystem.uplugin
Source/MassUnitSystemRuntime/      runtime module
  Public/Config/                   UMassUnitSystemSettings (Project Settings, "Mass Unit System")
  Public/Core/                     UMassUnitSubsystem (UTickableWorldSubsystem: the one crowd tick), gameplay tags
  Public/Entity/                   fragments, processors, entity manager, spawner, UUnitTemplate
  Public/Gameplay/                 crowd system, formations, GAS and Behavior Tree integration
  Public/Navigation/               batched navigation
  Public/Visual/                   instance renderer, Niagara unit system, skeletal mesh pool
Source/MassUnitSystemEditor/       editor module + automation tests (Private/Tests, MassUnitSystem.*)
docs/                              knowledge graph
.github/instructions, .github/prompts   tool-agnostic rules and task workflows
.claude/commands, .claude/agents   Claude Code wrappers + a read-only reviewer
build_plugin.bat / build_plugin.sh standalone RunUAT BuildPlugin wrappers
```

## Non-negotiables

1. **Never depend on a game or another dll-* plugin.** Runtime depends on engine modules only (Mass*,
   GameplayAbilities, GameplayTags, GameplayTasks, NavigationSystem, Niagara, ...). Bridges to a game's crowd
   replication, factions or audio live in the adopting game.
2. **GAS is an optional seam, not the unit model.** Units have native health; Gameplay Effects reach units only
   through the integration layer. A host with no ASC anywhere must still work. (The runtime module currently
   links `GameplayAbilities`; splitting GAS into its own optional module is open, see DECISIONS.)
3. **No per-unit Tick, no per-unit UObject.** Work runs in Mass processors or the single subsystem tick; it must
   early-out when idle and stay inside the settings budgets (behaviour LOD, visibility stagger, mesh pool,
   airborne/ragdoll caps).
4. **The plugin does not replicate units.** Network presentation is the adopter's job (for example a relevancy
   bubble replicating compact records). Don't add per-unit replicated actors.
5. **No game vocabulary.** The plugin owns the `MassUnit.*` tag root; games add leaves.
6. **Visual debug built in** (per spawner and per crowd/engagement/collective config, `bEnableVisualDebug`; the
   settings flag `bEnableDebugVisualization` is not wired yet); `stat MassUnitSystem` and CSV category `MassUnits`
   for cost. No per-frame logging. A `dll.MassUnits.*` CVar set is owed by the standard.
7. **Non-destructive.** Public API (`UFUNCTION`, `UPROPERTY`, public headers) is additive between minors;
   breaking changes need a changelog migration note and a DECISIONS entry. Renames use `[CoreRedirects]`.
8. **Testable logic, honest status.** Automation tests under `MassUnitSystem.*` (`DLLMassUnits.*` after the
   rename). Never claim "compiles" or "tested" without an artifact. `UNTESTED` is valid.
9. **Only our content.** Never add marketplace or licensed third-party assets.

## Pending rename (Tier B — not done yet)

| Now | After |
|---|---|
| folder `Plugins/ue5-mass-units-plugin/` | `Plugins/dll-mass-units/` |
| `MassUnitSystem.uplugin` | `DLLMassUnits.uplugin` |
| modules `MassUnitSystemRuntime` / `MassUnitSystemEditor` | `DLLMassUnitsRuntime` / `DLLMassUnitsEditor` |
| `MASSUNITSYSTEMRUNTIME_API` / `MASSUNITSYSTEMEDITOR_API` | `DLLMASSUNITSRUNTIME_API` / `DLLMASSUNITSEDITOR_API` |

**Class names stay** (`UMassUnitSubsystem`, `UUnitTemplate`, ...): renaming classes breaks every adopter's assets
for no benefit. After the rename, adopters add to `DefaultEngine.ini`:

```ini
[CoreRedirects]
+PackageRedirects=(OldName="/Script/MassUnitSystemRuntime",NewName="/Script/DLLMassUnitsRuntime")
+PackageRedirects=(OldName="/Script/MassUnitSystemEditor",NewName="/Script/DLLMassUnitsEditor")
```

and rename config sections `[/Script/MassUnitSystemRuntime.*]` to `[/Script/DLLMassUnitsRuntime.*]`, the
`.uproject` plugin entry (`MassUnitSystem` to `DLLMassUnits`) and `Build.cs` module names. Do it with the editor
closed, then a full build.

## Coding conventions

- New types keep the established `UMassUnit*` / `FMassUnit*` prefix inside this plugin (pre-standard plugin);
  never rename existing ones. CVars `dll.MassUnits.*`.
- New files start with:
  ```
  // Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
  // SPDX-License-Identifier: Apache-2.0
  ```
- Every exposed `UPROPERTY` has a doc comment (it becomes the tooltip) and a `Category`.
- Forward-declare in headers; include in `.cpp`. Soft references for meshes, materials, Niagara systems.
- More detail: [.github/instructions/cpp.instructions.md](.github/instructions/cpp.instructions.md).

## Build & test

```powershell
# Standalone build (proves no hidden dependency on any game); or: build_plugin.bat <out> Win64 Development
& "<UE>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin "-Plugin=<repo>\MassUnitSystem.uplugin" "-Package=<short temp>\pkg" -TargetPlatforms=Win64 -Rocket
# Tests (inside any project that has the plugin)
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests MassUnitSystem; Quit" -unattended -nullrhi
```

Keep `-Package` on a short path (Windows 260-character limit). Only one UnrealBuildTool can run per engine
install; a `ConflictingInstance` failure means wait. Full matrix: [docs/TESTING.md](docs/TESTING.md).

## Every change leaves Code + Docs + Memory

- Code + tests.
- Docs: the relevant `docs/*.md`; [CHANGELOG.md](CHANGELOG.md) under *Unreleased*.
- Memory: a dated line in `docs/MEMORY.md` for non-obvious facts/traps; a `docs/DECISIONS.md` entry for choices
  (append & supersede, never delete). Rules: [.github/instructions/ai-memory.instructions.md](.github/instructions/ai-memory.instructions.md).

## Task → prompt map

| Task | Prompt file | Claude Code |
|---|---|---|
| Start a session | `.github/prompts/verify-handoff.prompt.md` | `/verify-handoff` |
| Plan a feature | `.github/prompts/plan-feature.prompt.md` | `/plan-feature` |
| Add an optional integration module | `.github/prompts/add-integration-module.prompt.md` | `/add-integration-module` |
| Review a diff | `.github/prompts/review-diff.prompt.md` | `/review-diff` (or the `dll-mass-units-reviewer` agent) |
| Cut a release | `.github/prompts/release.prompt.md` | `/release` |
| End a session | `.github/prompts/session-handoff.prompt.md` | `/session-handoff` |

Codex has no repo-shareable slash commands: ask it to follow the prompt file directly.

## Git

- Stage by path; never `git add -A` (adopting projects often have other tools writing nearby).
- Conventional, descriptive commit messages; one concern per commit.
- Don't commit `Binaries/`, `Intermediate/`, packaged output or personal settings (`.claude/settings.local.json`).
