# DLL Mass Units — Decision log

Append-only. Never delete an entry; supersede it with a newer one that says **Supersedes:**.
Format: `## YYYY-MM-DD — <decision in one line>` then Decision / Reason / Rejected / Refs.
Decisions before 2026-10-15 are recorded in [changelog.md](changelog.md) and [architecture.md](architecture.md).

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

## 2026-10-17 — Swarm patterns are crowd motion styles, not new systems

**Decision:** Flock, Swarm and Hive are values of `FMassUnitCrowdConfig::MotionStyle`, implemented inside the crowd
system's existing budgeted update (same spatial hash, same per-unit visit, same LOD). Group-level goal state lives on the
crowd group; the only per-unit addition is a `MotionPhase` byte. Wander stays the default and is untouched.
**Reason:** One update path keeps the cost model (budget, LOD, sleeping) and engagement/collective behaviour working for
every style. A style is data: an adopter switches a group's behaviour by config, no new actors or processors.
**Rejected:** a separate flocking processor (second per-frame loop over every unit); per-style subclasses of the crowd
system.

## 2026-10-17 — Piles are units standing on units, driven by one sparse stack table

**Decision:** `UMassUnitStackSystem` keeps a table of stacked units (climber -> support), ticked once per frame by the
subsystem. Stacked units are flagged `bStackDriven` and skipped by the movement processor; a pile planner per engaged
crowd group starts climbers. Falls reuse `LaunchUnit`. Opt-in per template; settings cap the table.
**Reason:** Units stay Mass entities (no physics bodies, no per-unit UObject). Support-based stacking collapses
naturally - losing a support drops what is on it - and needs no slot bookkeeping. Reusing launches means landings,
knockdowns and the adopter's launch replication already handle every fall.
**Rejected:** precomputed cone slots with no supports (units float when the ring under them dies); physics stacks
(cost and determinism at crowd scale); a new fragment for stack state (archetype change for every unit when only a
few hundred stack).

## 2026-10-16 — Adopt the dll-* repo layer around the existing docs

**Decision:** Add `AGENTS.md`, `CLAUDE.md`, `.github` instructions/prompts/templates, `.claude` commands and a
reviewer, and the `DECISIONS` / `MEMORY` / `TESTING` / `AI_WORKFLOW` docs. Existing docs keep their names and
are mapped to standard roles in [README.md](README.md); `changelog.md` stays the changelog.
**Reason:** Non-destructive: renaming `prd.md`/`architecture.md`/`changelog.md` to upper case would collide on
case-insensitive file systems and break links while a docs reorganisation is in flight.
**Open:** whether to split GAS into an optional `DLLMassUnitsGAS` module (the runtime currently links
`GameplayAbilities`); a current PRD; a `dll.MassUnits.*` CVar set.

## 2026-10-17 — Orders are a sparse command table plus a hand-off flag, not a second simulation

**Decision:** `UMassUnitCommandSystem` keeps orders for commanded units only (sparse table, interval-gated check, free
when empty) and marks them `FMassUnitCrowdFragment::bCommanded`, the same hand-off pattern as `bStackDriven`. The crowd
system skips a commanded unit's destination logic (separation only); movement, navigation, launches and combat are the
existing ones. Formation slots are computed per order (no `UFormationSystem` registration). R1 keeps commanded units out
of automatic piles.
**Reason:** An RTS layer must reuse the same units (piles, crush, gore, replication records all apply) without changing
uncommanded behaviour. A flag costs one byte per unit and no archetype change; the old `UFormationSystem` owns slot
movement through its own processor and would fight the navigation path.
**Rejected:** a separate "squad" unit type; pulling commanded units out of their crowd group (loses group membership,
engagement events and the rep bubble's group bookkeeping); driving moves through `UFormationSystem`.

## 2026-10-17 — Looks are a separate, seed-rolled profile layered on the template; parts are extra instances

**Decision:** Appearance lives in `UMassUnitLookProfile` (body variants, parts, scale, tint), referenced by a template
and overridable per batch and per unit. The look is rolled from the unit's variant seed at byte precision. Each modular
part is its own instance in a mesh + material bucket that shares the body's transform and custom-data row.
**Reason:** The plugin's goal is one minion reused with many looks and many purposes (see unit_composition.md), so look
must be independent of stats/behaviour. Seed-rolling costs zero extra replication. Separate instances keep one draw call
per distinct piece and work with any VAT material without merging meshes.
**Rejected:** looks inline on `UUnitTemplate` (one look per template, duplication); runtime mesh merging per combination
(combinatorial assets, hitches); replicating chosen part indices (bandwidth for data the seed already implies).

## 2026-10-17 — Individual temperament is a seed-rolled attitude profile; group temperament stays on the crowd group

**Decision:** `UMassUnitAttitudeProfile` rolls per-unit multipliers (speed, cooldown, looping tempo), courage and climb
willingness from the variant seed with its own salt, applied on top of the template at spawn (and on re-assign).
Motion style, engagement and morale remain crowd-group config.
**Reason:** Variety must be per unit to read as individuals, but group behaviour is what makes a swarm a swarm; mixing
them would make every group setting per-unit (cost and authoring burden). Multipliers on template values keep one
source of truth for balance. Tempo applies to looping clips only so attack impact delays stay matched to their clips.
**Rejected:** per-unit copies of crowd configs; random jitter at runtime (non-deterministic, unreplicable); attitude in
the crowd fragment (crowd resets would erase it).
