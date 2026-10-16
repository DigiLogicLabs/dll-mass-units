# DLL Mass Units — Roadmap

Released history is in [CHANGELOG.md](../CHANGELOG.md). Version numbers below are planning targets.

## Released
- **1.1** native Mass entities, subsystem, handles, templates, movement/combat/visibility processors, navigation,
  formations, optional Niagara, skeletal pool, GAS and Behavior Tree bridges.
- **1.2 / 1.2.1** zero-asset spawner; flicker-free dynamic-ISM rendering.
- **1.3** budgeted crowd service: wander, separation, interactions, behaviour LOD, sleeping.
- **1.4** player engagement, shared corridors, managed subgroups, terrain height, free-3D mode, native health
  events, GAS/Actor damage on targets, near-band hysteresis.

## 1.5.0 — Crowd presentation and combat depth (in *Unreleased*)
- [x] Stable-slot `UCrowdInstanceRenderer`, 13-channel custom-data contract, rendering cost settings
- [x] Attack tokens, standoff ring, engagement slots
- [x] Collective behaviour (alerts, morale, threat), hostility filter, `ReportCrowdUnitDamaged`
- [x] VAT clip table, `UMassUnitAnimInstance`, attack impact timing
- [x] Unit tags and statuses; launches and near-band ragdolls
- [x] Presentation API for adopter-side replication
- [x] Profiling stats/CSV; batch spawn; spawner patterns
- [x] Automation: 8 tests pass (2026-10-16, reference adopter)
- [ ] Standalone `BuildPlugin` on the release commit
- [ ] Bump `.uplugin` `VersionName`, tag `v1.5.0`

## 2.0.0 — dll-* standard rename (breaking: plugin and module names)
- [ ] `MassUnitSystem.uplugin` → `DLLMassUnits.uplugin`; modules → `DLLMassUnitsRuntime` / `DLLMassUnitsEditor`;
      export macros; test prefix `DLLMassUnits.*`. Class names unchanged; `[CoreRedirects]` migration note.
      Done in one pass with the editor closed ([DECISIONS.md](DECISIONS.md) 2026-10-15).
- [ ] `dll.MassUnits.*` CVars (at least `dll.MassUnits.Debug`, wired to the visual-debug toggles; today the
      settings flag `bEnableDebugVisualization` is unused).
- [ ] Decide and, if accepted, split GAS into an optional `DLLMassUnitsGAS` module.

## Validation backlog (no version; gates "production ready")
- [ ] Fresh clone of `main` into a blank project: repeat the 25-unit test with no undocumented setup.
- [ ] Custom static mesh: replaces cubes with no per-unit components; topology revision stable while moving;
      health custom data changes on damage.
- [ ] VAT: a project-owned bake plays Idle → Walk/Run → Attack → Death/Stun by tag, never by state ordinal.
- [ ] Skeletal band: no flicker at the boundary, nearest units win slots, active count ≤ capacity, components
      return to the pool, attack/death clips don't loop.
- [ ] Pooled cues: one `OnCrowdCueRequested` binding drives pooled audio/Niagara; cooldown coalesces bursts with
      hundreds of attackers.
- [ ] GAS end to end: GAS weapon → unit health/death → group activation; crowd attack → effect on the player ASC
      once per cooldown; authority-only in listen/dedicated.
- [ ] Scale runs at 500, 2,000 and the target maximum with game-thread, render-thread and GPU numbers recorded.
- [ ] Multiplayer smoke: authority-only spawning, damage and effects; deterministic cleanup.

## Later (ideas, not commitments)
- Distance-tiered aggression (near units contest tokens, mid approach, far dormant) on the existing LOD bands.
- Optional token rotation so waiting units take turns.
- A real leap/pounce state (ballistic arc in the movement processor, token-gated).
- Sample content in a separate repo or optional content plugin (meshes, VAT material, cues).
