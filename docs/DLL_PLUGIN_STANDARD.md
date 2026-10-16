# Digi Logic Labs — `dll-*` plugin standard

Every open-source Unreal plugin from Digi Logic Labs follows these conventions, so they install,
build and read the same way.

## Naming

| Thing | Convention | Example |
|---|---|---|
| GitHub repo | `DigiLogicLabs/dll-<kebab-name>` | `dll-audio`, `dll-mass-units` |
| Folder in `Plugins/` | same as repo | `Plugins/dll-audio/` |
| Plugin name (`.uplugin` file) | `DLL<PascalName>` | `DLLAudio.uplugin` |
| FriendlyName | `DLL <Name>` | "DLL Audio" |
| Category | `Digi Logic Labs` | |
| Modules | `DLL<Name>Runtime`, `DLL<Name>Editor`, optional `DLL<Name><Integration>` | `DLLAudioRuntime`, `DLLAudioGAS` |
| New C++ types | `UDLL<Name>*`, `FDLL<Name>*`, `EDLL<Name>*` | `UDLLAudioSubsystem` |
| Console variables | `dll.<Name>.*` | `dll.Audio.Debug` |
| Settings | Project Settings → Plugins → "DLL <Name>" | |

Engine version is **not** part of any name; it lives in the `.uplugin` `EngineVersion` and the README.

Plugins that predate this standard keep their existing **class names** when renamed (renaming classes
breaks every adopter's assets for no benefit). Plugin/module renames use `[CoreRedirects]`.

## Repository contents

- `LICENSE` — Apache-2.0 for every dll-* plugin
- `README.md` — see *README skeleton* below
- `CHANGELOG.md` — semantic versioning; `.uplugin` `VersionName` matches the latest entry
- `CONTRIBUTING.md`
- `docs/` — see *Docs set* below
- `Config/FilterPlugin.ini` — so docs/license ship with packaged plugins
- Source files start with:
  ```
  // Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
  // SPDX-License-Identifier: Apache-2.0
  ```

**Never commit third-party marketplace or licensed content** (sound packs, meshes, vendor Blueprints).
Repos contain our own code and our own content only.

## README skeleton (same order in every repo)

Keep the README short: it answers *what, why, how do I start*. Everything deeper lives in `docs/`.

1. `# DLL <Name>` + one-line tagline + license/owner line
2. **Status** callout — version, maturity, what is not done yet
3. **Why** — the problem, in a few lines
4. **What's in X.Y.Z** — one table, one row per public piece
5. **Install** — clone, enable, `Build.cs` dependency, engine version tested
6. **Quick start** — the smallest working example (C++ and the Blueprint equivalent)
7. **Design rules** — 3-5 bullets
8. **Documentation** — link to `docs/README.md` and the AI layer
9. **Tests** — automation prefix and how to run it
10. **Sister plugins** — link to this standard
11. **License**

## Docs set (exactly this; nothing else at `docs/` top level)

| File | Holds |
|---|---|
| `README.md` | Index of the files below |
| `PRD.md` | Goals, users, requirement IDs + status, non-goals |
| `ARCHITECTURE.md` | Modules, classes, data flow, extension points |
| `INTEGRATION_GUIDE.md` | Setup and every use case, step by step (the "how to use it" document) |
| `TESTING.md` | Automation tests, manual matrix, how to record results |
| `ROADMAP.md` | Versions and what each adds |
| `DECISIONS.md` | Append-only decision log |
| `MEMORY.md` | Dated verified facts and traps |
| `AI_WORKFLOW.md` | The AI session loop |
| `DLL_PLUGIN_STANDARD.md` | This file, identical in every repo |
| `RESEARCH.md` *(optional)* | Background research that shaped the design |
| `PROVENANCE.md` *(only while needed)* | Audit of any file whose origin is not yet cleared |
| `guides/*.md` *(optional)* | Long task-specific guides linked from INTEGRATION_GUIDE (e.g. an art pipeline) |
| `sessions/` | Dated hand-offs |

`CHANGELOG.md` lives at the repo root. Superseded reports, plans and old research are deleted (git keeps
them) or folded into `RESEARCH.md`; they are not left beside the current docs.

## AI-ready repository layer (required)

Every dll-* repo carries the same AI context so any contributor's assistant gets the same rules:

| File | Purpose |
|---|---|
| `AGENTS.md` | Canonical agent rules: what the repo is, layout, non-negotiables, build/test, task → prompt map |
| `CLAUDE.md` | `@AGENTS.md` + Claude Code specifics |
| `.github/copilot-instructions.md` | Pointer to `AGENTS.md` for Copilot |
| `.github/instructions/*.instructions.md` | Modular rules by file glob: cpp, architecture, testing, ai-memory |
| `.github/prompts/*.prompt.md` | Tool-agnostic workflows: verify-handoff, plan-feature, add-integration-module, review-diff, release, session-handoff |
| `.claude/commands/*.md` | One-line wrappers around the prompts |
| `.claude/agents/<plugin>-reviewer.md` | Read-only reviewer against the non-negotiables |
| `docs/README.md` | Knowledge-graph index |
| `docs/PRD.md` | Goals, users, requirement IDs, non-goals, metrics |
| `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/TESTING.md`, `docs/INTEGRATION_GUIDE.md` | How, when, proof, adoption |
| `docs/DECISIONS.md` | Append-only decision log |
| `docs/MEMORY.md` | Dated verified facts and traps |
| `docs/AI_WORKFLOW.md` | The session loop |
| `docs/sessions/` | Dated hand-offs |
| `.github/PULL_REQUEST_TEMPLATE.md`, `.github/ISSUE_TEMPLATE/` | Contribution checklists |

`dll-audio` is the reference implementation of this layer; copy it when starting or upgrading a dll-* repo.

## Architecture rules

1. **Core module depends on the engine only** (plus engine plugins it fundamentally needs). Integrations
   with GAS, Mass, etc. are separate optional modules.
2. **dll-* plugins never depend on each other.** Bridges between them live in the adopting game or in a
   small dedicated bridge module.
3. **No game vocabulary in the plugin.** Plugins own a tag root; games add leaves.
4. **Server-authoritative, event-driven, no Tick by default.** Timers for periodic work; Tick only for
   continuous simulation and always guarded.
5. **Visual debug built in.** Master toggle + granular toggles in settings, a `dll.<Name>.Debug` CVar,
   world-space shapes and a screen HUD. No per-frame logging.
6. **Testable cores.** Pure logic in plain C++ classes with automation tests under `DLL<Name>.*`.

## Releases

Semantic versioning. Tag releases `vX.Y.Z` on GitHub. Pre-1.0 APIs may change between minors; the
CHANGELOG lists every breaking change with its migration.
