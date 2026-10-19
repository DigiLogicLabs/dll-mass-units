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

## 2026-10-18 — A swarm type is a bundle of existing layers plus locomotion; flyers are a band on Free 3D groups

**Decision:** `UMassUnitSwarmType` only chooses existing layers (template, look, attitude, behaviour preset) and adds one
axis, `EMassUnitLocomotion`. Air does not get its own simulation: it is the existing Free 3D crowd mode plus an altitude
band in `FMassUnitCrowdConfig` (destinations clamped into the band, a per-unit floor clamp in the movement processor,
hover-then-dive when engaged) and a corpse fall through the launch system on death.
**Reason:** The adopter wants many swarm kinds (flyers, crawlers, climbers, ankle biters) with slightly different
behaviour; most of that variety already exists as data. Reusing Free 3D and the launch table keeps flyers at zero new
per-unit tables and zero per-unit traces (the ground is one trace per group). The band lives on the crowd config so any
Free 3D group (swimmers later) can use it without a swarm type.
**Rejected:** a flyer subclass or processor (duplicates crowd logic); per-unit ground traces for altitude (cost at
crowd scale; terrain under a moving group is approximated by the ground under its centre, refreshed when the centre
moves); locomotion on `UUnitTemplate` (one template could not both walk and fly).

## 2026-10-18 — A pounce is a leap on the launch table plus an attack token, resolved on landing

**Decision:** Leapers jump with `LeapUnit`, an entry in the existing sparse launch table flagged `bLeap` (held for the
flight, own clip, no knockdown). The crowd system starts pounces from engaged units in a range window only when an attack
token is free, and lands the hit on touchdown through the same `RequestActorAttack` seam as a swing.
**Reason:** Reusing the launch table keeps flight cost, the airborne cap and landing traces in one place; the token keeps
MaxSimultaneousAttackers meaningful (a pack does not all jump at once); resolving on landing means a dodged pounce misses
without any hit-detection pass. Leaps are a separate event (`OnUnitLeapt`) so adopters that replay `OnUnitLaunched` as
a knockdown do not floor their client puppets.
**Rejected:** a separate leap processor (second airborne integrator); damage at take-off (undodgeable); a pounce that
ignores tokens (every unit in range jumps together).

## 2026-10-19 — Tier B applied: folder/plugin/module names follow the dll-* standard; class names kept

**Decision:** The 2026-10-15 rename (Tier B) is applied: folder `Plugins/dll-mass-units`, `DLLMassUnits.uplugin`,
modules `DLLMassUnitsRuntime` / `DLLMassUnitsEditor`, export macros `DLLMASSUNITSRUNTIME_API` /
`DLLMASSUNITSEDITOR_API`, settings section `[/Script/DLLMassUnitsRuntime.MassUnitSystemSettings]`, automation test
prefix `DLLMassUnits.*`. Class, struct, enum and file names, the `UE::MassUnitSystem` namespace, the `MassUnit.*` tag
root, processor group names, `stat MassUnitSystem` and `LogMassUnitSystem` keep their names. Supersedes the "owed"
status of the 2026-10-15 entry.
**Reason:** One naming scheme across every dll-* plugin (what adopters see in the Plugins window, `Build.cs` and the
test tree). Class names are what adopters' assets and code serialise, so keeping them means assets keep loading with
two package redirects and adopter C++ only changes its `Build.cs` line. The redirects and the full upgrade steps are in
[INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md#17-migrating-from-14x-pre-rename-module-names); the CHANGELOG carries the
breaking-change note.
**Rejected:** renaming classes as well (breaks every adopter's assets for cosmetics); keeping the pre-standard
module names (the plugin would stay the odd one out in the dll-* family).
