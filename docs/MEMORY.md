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

- 2026-10-17 — Stacking is support-based, not slot-based: a climber stands on a unit, so a pile can only be as tall as
  the units under it and collapses by itself when they go. Heights replicate for free in an adopter that sends Location
  with Z (MetalLegs' crowd records compare the full 3D location).

## Traps

- Windows is case-insensitive: `docs/prd.md` vs `docs/PRD.md` are one file. Never add a doc differing only by case.
- Standalone `RunUAT BuildPlugin` needs a short `-Package` path (260-character limit).
- Only one UnrealBuildTool per engine install; `ConflictingInstance` means wait, not `-NoMutex`.
- 2026-10-16 — Supersedes the header line above: all 62 source files now carry the Apache-2.0 SPDX header; README
  carries the dll-mass-units identity, install URL and a License section. Attribution audit: no third-party code,
  assets or URLs in the repo; GASCompanion appears only in archived research docs (never a dependency).
- 2026-10-16 — `UMassUnitSystemSettings::bEnableDebugVisualization` / `DebugVisualizationDuration` are read by
  nothing; visual debug is per spawner (`bEnableVisualDebug`) and per crowd/engagement/collective config. No
  console variables exist yet.
- 2026-10-16 — All 8 `MassUnitSystem.*` automation tests passed headless in the reference adopter
  (MetalLegs `Saved/Logs/AutoTest_MassStressStepB_20261016.log`, `EXIT CODE: 0`).
- 2026-10-16 — Source comments in `MassUnitEntityManager.h` and `MassUnitPresentationTests.cpp` cite
  `docs/crowd_replication_plan.md`; that file lives in the reference adopter, not this repo.
- 2026-10-16 — Docs consolidated to the standard set; changelog is now root `CHANGELOG.md` (DECISIONS 2026-10-16).

- 2026-10-17 — Never touch Mass fragments in a service's `Deinitialize`: at world teardown the Mass entity manager may
  already be gone (`Assertion failed: EntityManager`, MassEntitySubsystem.h:36). Just drop the bookkeeping.
- 2026-10-17 — The same teardown trap hid in `UMassUnitCrowdSystem::Deinitialize` (it unregistered groups, resetting
  fragments): any world torn down with registered groups could assert. It only surfaced when a test left groups alive.
- 2026-10-17 — In tests, spawn targets as `ADefaultPawn`, not `APawn`: a bare `APawn` has no root component, so
  `SpawnActor` ignores the location and it sits at the origin (only the scenario at the origin "works").
- 2026-10-17 — Registering a crowd group resets every member's crowd fragment, which clears `bStackDriven`: stack AFTER
  registering, and expect a re-register (preset switch) to drop piles. `AddUnitsToCrowdGroup` resets only the new units.
- 2026-10-17 — A unit launched with no landing trace (budget spent, tracing off) lands at its LAUNCH height. Anything
  thrown from above the ground must cap its landing (`CapLaunchLandingHeight`) or it lands mid-air.
- 2026-10-17 — Commanded units (`UMassUnitCommandSystem`) keep `bCommanded` across `FMassUnitCrowdFragment::Reset()` on
  purpose: a group re-register must not drop orders. Only `ReleaseUnits` clears it. The crowd's `ResetCrowdFragment`,
  pause and disengage never stop a commanded unit. The test world runs no movement processor, so command tests move
  units by setting their transform and call `TickCommands` with their own clock.
- 2026-10-17 — `UMassUnitCommandSystem` queues `OnMoveOrderFinished` and flushes at the end of each public call / tick:
  never broadcast from inside the `Entries` loop or while holding an entry pointer (a Blueprint "on arrival, issue next
  waypoint" listener re-enters). Other systems that write unit targets (`UFormationSystem`, BT integration, adopter
  `SetUnitDestination` callers) must skip `bCommanded` units or they wipe order paths every frame.
- 2026-10-17 — Look rolls quantise the seed to a byte on purpose: adopters replicate the seed as one byte, and a full-float
  roll on the server would differ from the client's. `RollLook` must keep drawing the same count of random values per
  roll (`PickWeighted` always draws one, even for an empty list) or editing a part reshuffles every other part.
- 2026-10-17 — Mass fragments are NOT scanned by the garbage collector (`FMassEntityManager::AddReferencedObjects` only
  reports shared fragments). A `UPROPERTY` `TObjectPtr` in a regular fragment does not keep anything alive; the editor
  hides it (`RF_Standalone`), packaged builds free the asset. Hold such assets elsewhere (`RetainedAssets` on the entity
  manager) or keep the fragment field weak, like `SourceTemplate`.
- 2026-10-17 — The project build is unity-batched, so a missing include can compile there and still break adopters:
  run the standalone `RunUAT BuildPlugin` (non-unity host project) after adding files. It caught `MassUnitStackSystem.cpp`
  using `FMassUnitVelocityFragment` without `MassUnitCommonFragments.h`.
- 2026-10-18 — Flyers: the altitude band is relative to ground under the GROUP CENTRE (one trace when the group is
  registered, reconfigured or moved), not under each unit; a group wandering over a hill higher than its floor clamp
  can clip into it. While engaged the floor drops to the target's height - 50 so pits stay reachable. The launch landing
  trace only reaches 1500 cm down; a death fall caps its landing at the band floor when that trace misses.
- 2026-10-18 — `FMassUnitCrowdFragment::Reset()` (run whenever a unit joins a group) clears the interaction timer but
  not `EMassUnitState::Interacting`; anything that ends a crowd state must not rely on a fragment timer alone. Engaged
  flyers measure attack range in 3D, so their hover height is OUTSIDE range: "waiting for a token" for a flyer is the
  column over the target, not the attack range (see the hover/dive change in the changelog). First flyer/leaper PIE
  (Testing_MassStress, 10-18): leapers 12 pounces / 12 hits; flyers stayed in band.
