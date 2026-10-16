# DLL Mass Units — Decision log

Append-only. Never delete an entry; supersede it with a newer one that says **Supersedes:**.
Format: `## YYYY-MM-DD — <decision in one line>` then Decision / Reason / Rejected / Refs.
Decisions before 2026-10-15 are recorded in [CHANGELOG.md](../CHANGELOG.md) and [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 2026-10-15 — Open source under Apache-2.0; repo renamed to `dll-mass-units`

**Decision:** The plugin is licensed Apache-2.0 (`LICENSE`, `NOTICE`) and the GitHub repo is renamed
`DigiLogicLabs/ue5-mass-units-plugin` → `DigiLogicLabs/dll-mass-units`, following the Digi Logic Labs `dll-*`
standard ([DLL_PLUGIN_STANDARD.md](DLL_PLUGIN_STANDARD.md)). GitHub redirects the old URL.
**Reason:** The repo was already public with no license, which legally means all rights reserved — nobody could
use it. Digi Logic Labs wants "the most open source approach" and will support these plugins from its own games.
**Rejected:** MIT (no explicit patent grant or contribution terms); keeping the engine version in the repo name.

## 2026-10-15 — Rename the plugin and modules, keep every class name (Tier B, owed)

**Decision:** Folder `dll-mass-units`, `DLLMassUnits.uplugin`, modules `DLLMassUnitsRuntime` / `DLLMassUnitsEditor`,
export macros to match, file headers to the Apache SPDX header. Class, struct, enum and tag names do **not**
change. `[CoreRedirects] +PackageRedirects` map `/Script/MassUnitSystemRuntime` and `/Script/MassUnitSystemEditor`.
**Reason:** Plugin and module names are what adopters see in the Plugins window and `Build.cs`; class names are
what their assets serialise. Package redirects fix the first without touching the second.
**When:** in one pass, with the editor closed, after in-progress work on the current module names has landed
(a rename mid-flight turns every open branch into a conflict).
**Rejected:** renaming classes to `UDLLMassUnits*` (breaks every adopter's assets for cosmetics); piecemeal
header swaps (conflicts with in-progress edits).

## 2026-10-15 — dll-* plugins never depend on each other or on a game

**Decision:** This plugin depends on engine modules only. Bridges to `dll-audio`, a game's crowd replication,
factions or targeting live in the adopting game or a small dedicated bridge module.
**Reason:** Each plugin must install alone; cross-plugin dependencies make every release a coordinated release.

## 2026-10-16 — Adopt the dll-* repo layer around the existing docs

**Decision:** Add `AGENTS.md`, `CLAUDE.md`, `.github` instructions/prompts/templates, `.claude` commands and a
reviewer, and the `DECISIONS` / `MEMORY` / `TESTING` / `AI_WORKFLOW` docs. Existing docs keep their names and
are mapped to standard roles in [README.md](README.md); `changelog.md` stays the changelog.
**Reason:** Non-destructive: renaming `prd.md`/`architecture.md`/`changelog.md` to upper case would collide on
case-insensitive file systems and break links while a docs reorganisation is in flight.
**Open:** whether to split GAS into an optional `DLLMassUnitsGAS` module (the runtime currently links
`GameplayAbilities`); a current PRD; a `dll.MassUnits.*` CVar set.

## 2026-10-16 — Docs consolidated to the standard set (supersedes the "keep legacy names" part of the entry above)

**Decision:** `docs/` holds exactly the standard set (README, PRD, ARCHITECTURE, INTEGRATION_GUIDE, TESTING,
ROADMAP, DECISIONS, MEMORY, AI_WORKFLOW, DLL_PLUGIN_STANDARD, RESEARCH, `guides/`, `sessions/`); `CHANGELOG.md`
moves to the repo root. Renames use a two-step `git mv` through a temporary name (case-insensitive file systems).
Current content of `setup`, `quick-start`, `usage`, `api`, `troubleshooting` and `integration_guide` is merged into
INTEGRATION_GUIDE; `architecture` into ARCHITECTURE; `todo` into ROADMAP; the five research notes and the
reference-game analysis into RESEARCH; the authoring, VAT, artist and scalability docs become `guides/`.
Superseded reports and plans (readiness and patch reports, executive summary, technical specification,
implementation roadmap, proposed architecture, sample-project and usage stubs) are deleted; git history keeps them.
**Reason:** One place per question; the docs reorganisation that blocked the rename has landed. The old PRD was an
archived UE 5.6 / GASCompanion concept and is replaced by a current one.
**Rejected:** keeping legacy files beside the standard ones (two answers to the same question drift apart).
**Also:** the former changelog heading "1.5.0" was never tagged and `.uplugin` reads 1.4.0, so that work is listed
under *Unreleased* (targeted as 1.5.0).

