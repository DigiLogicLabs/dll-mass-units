# Changelog

All notable changes to DLL Mass Units (plugin `MassUnitSystem`). Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning: [SemVer](https://semver.org/).
Release dates come from the commit that set the `.uplugin` `VersionName`.

## [Unreleased]

Targeted as 1.5.0. Part of this work was first listed under a "1.5.0" heading, but that version was never
tagged and `MassUnitSystem.uplugin` still reads `1.4.0`.

### Removed
- `Gameplay/GASCompanionIntegration.h`, a deprecated header that only forwarded to
  `Gameplay/MassUnitBehaviorIntegration.h`. Migration: include `Gameplay/MassUnitBehaviorIntegration.h`.

### Added
- `UUnitTemplate::BodyMaterialTag` (additive, `Unit Template|Tags`): what a unit's body is made of, for the host
  project's material-keyed systems (gore splats, impact effects). The plugin never reads it; the tag vocabulary is the
  project's. Empty = the host's default.
- **Crowd rendering** — `UCrowdInstanceRenderer` replaces the instanced-mesh fallback: stable per-entity
  instance slots (per-instance data can no longer swap between units when one dies), one contiguous transform
  upload per mesh per update, custom-data uploads only when a value changed, tail-only compaction of recycled slots.
- Per-instance custom data grows from eight to ten floats and becomes a published, append-only contract
  (`UE::MassUnitSystem::CrowdCustomData`): adds a stable per-unit random seed and horizontal speed, and replaces
  elapsed animation time with an animation start stamp so the buffer stops dirtying every frame.
- Crowd rendering cost settings: per-instance GPU cull distances, shadow casting off by default with an override
  (`bCrowdInstancesCastShadows`), a world-position-offset disable distance. Crowd instances no longer affect
  distance-field or dynamic indirect lighting and no longer receive decals.
- Diagnostics for allocated instance slots; the component-count accessor is corrected.
- **Attacker limiting** — `MaxSimultaneousAttackers` bounds peak incoming damage with group attack tokens held for
  one cooldown; `StandoffDistanceMultiplier` parks token-less units on a ring outside attack range;
  `bUseEngagementSlots` spreads engaged units over fixed angular positions. All default to the previous
  behaviour. Tokens expire on a timestamp, hold time scales with the round-robin revisit period, every group
  expires tokens once per update, and disengaging clears the pool. New `ActiveAttackers` and
  `UnitsAwaitingAttackToken` stats; red/yellow attack-state debug drawing.
- **Collective behaviour** (opt-in, off by default; groups without it are unchanged) —
  `FMassUnitCrowdCollectiveConfig`, `ConfigureCrowdGroupCollectiveBehavior`, spawner `bEnableCollectiveBehavior`.
  Alerts recruit nearby same-team groups (awareness with distance falloff, deterministic reaction delay, relays
  bounded by depth and damped per hop). Casualties drain morale; groups rout with a staggered flee, spread fear
  and rally after a minimum duration. Damage and proximity build a per-actor threat table; target switches use a
  margin and a minimum interval. Everything runs per group on the existing crowd timer; signals are delivered on
  the next update (queue capped at 256).
- `ReportCrowdUnitDamaged` (safe for every hit; resolves projectiles to their instigating pawn),
  `ApplyCrowdGroupMoraleShock`, `RaiseCrowdAlertAtLocation`, morale/routing/awareness/anchor queries, events
  `OnCrowdGroupAlerted` / `Routed` / `Rallied` / `TargetChanged`, appended cue values `Alerted` / `Routed` /
  `Rallied` / `TargetChanged`, collective stats, visual debug (signal circles, morale bar, threat line, HUD keys
  7300-7309).
- **Vertex-animation clips** — per-template clip table (`UUnitTemplate::VertexAnimationClips`, `FMassUnitVATClip`)
  for AnimToTexture bakes, written per instance as custom-data channels 10-12 (clip start, frame count, frames
  per second; negative rate plays once and holds). A finished one-shot hands back to Idle; Death holds. An empty
  table keeps the previous behaviour. `RunSpeedThreshold` selects Run over Walk. Templates validate the table
  (duplicate rows, missing Idle, looping Death/Attack, impact delay past the cooldown).
- `AttackSerial` increments per swing and restarts the attack clip in both bands. Optional `AttackImpactDelay`
  lands damage on the clip's impact frame (zero keeps the old timing); a unit that leaves range, loses its
  token, routs or is stunned before impact whiffs.
- `UMassUnitAnimInstance` parent class for near-band Animation Blueprints: receives the unit's state (tag, state,
  speed, health, attack serial, owned tags, team) and `OnAttackSwing` / `OnUnitAssigned` events. A promoted unit
  starts its clip at the vertex-animation phase instead of frame 0.
- **Unit tags and timed statuses** on `UMassUnitEntityManager` (`ApplyUnitStatus`, `StunUnit`,
  `RemoveUnitStatus`, `RemoveUnitStatusesMatching`, `AddUnitTags`, `RemoveUnitTags`, `UnitHasTag`,
  `UnitMatchesTagQuery`, `FindUnitsMatchingTagQuery`, `OnUnitStatusDamageTick`, `OnUnitStatusChanged`):
  periodic damage, slows, stuns, stacks, template `GrantedTags` and `StatusImmunityTags`. Ticks at 10 Hz only for
  units with a status.
- `UMassUnitCrowdSystem::SetCrowdTargetFilter` lets the project decide hostility for target acquisition,
  proximity threat and damage reports. Unset = previous behaviour.
- **Launches** — `UMassUnitEntityManager::LaunchUnit(Unit, Velocity, Instigator, bApplyTemplateScale)`: ballistic
  arcs in a sparse table advanced by `TickLaunches` from the subsystem tick (free with nothing airborne, capped by
  `MaxAirborneUnits`). Living units are held by `MassUnit.Status.Launched` for the flight and then
  `KnockdownSeconds`; corpses tumble, bounce once and settle. One budgeted landing trace per launch.
  `OnUnitLaunched` / `OnUnitLanded`, `IsUnitAirborne`, `GetAirborneUnitCount`, `UUnitTemplate::LaunchVelocityScale`
  (0 = never launched), settings category *Launch*.
- `UUnitMeshPool::StartRagdolls`: near-band kills with a Physics Asset become simulating ragdolls (at most
  `MaxActiveRagdolls`); meshes that cannot simulate fall back to the fling.
- **Presentation API** for adopters that replicate crowds themselves (network-agnostic; the game owns the wire):
  `GetUnitPresentation`, `ApplyPresentationState`, `GetUnitVariantSeed`, `SetUnitVariantSeed`. Every unit gets a
  stored variant seed at spawn.
- **Profiling** — `Core/MassUnitStats.h`: stat group `STATGROUP_MassUnitSystem` (`stat MassUnitSystem`) and CSV
  category `MassUnits`. `MASSUNIT_SCOPE_CYCLE` = cycle stat + Insights CPU scope + CSV timing in one line;
  `MASSUNIT_SET_COUNTER` sets a counter stat and a CSV stat. Timed: movement, combat and visibility processors,
  subsystem tick, mesh pool, visual update, instance renderer, crowd update, engagements, collective behaviour,
  launches, statuses, prune. Counters: units, airborne, skeletal-mesh units, ragdolls, render instances, crowd
  groups (`GetCrowdGroupCount` added). Measurement only; stats compile out of Shipping.
- **Batch spawn** — `UMassUnitEntityManager::CreateUnitsFromTemplate(Template, Transforms, FMassUnitBatchSpawnOptions)`:
  template assets load once; the batch is clipped at `MaxUnits` (one warning); a null template uses the asset-free
  default. `FMassUnitBatchSpawnOptions`: team override (ID, colour), `RandomSeed`, `bSeedVariants`.
- **Spawner patterns** — `AMassUnitSpawner::SpawnPattern` (`Grid` default and unchanged, `Disc`, `Ring`, `Line`,
  seeded `Scatter`), `PatternRadius`, `SpawnOptions`; visual debug draws the pattern outline. Static BlueprintPure
  `ComputePatternOffsets` / `ComputePatternRadius` lay out units without a spawner actor.
- Tests: `MassUnitSystem.Crowd.CollectiveBehavior`, `.Units.StatusAndTags`, `.Units.AnimationContract`,
  `.Units.Launch`, `.Units.PresentationReplication`, `.Spawner.PatternLayouts`, `.Spawner.BatchSpawn`.
- Docs: authoring pipeline, artist brief, VAT/status guide and scalability rules (now in `docs/guides/`).

### Changed
- `CreateUnitFromTemplate` shares the batch-spawn body; its behaviour is unchanged. `SpawnUnits()` uses the batch
  call. `CalculateLocalSpawnOffsets` / `GetPatternRadius` are BlueprintPure and call the static helpers (layouts
  unchanged, parity tested).
- Re-applying a status never shortens it: the refresh keeps the later expiry, and a permanent status stays permanent.
- `GetCrowdGroupCount` is live; `GetCrowdStats().RegisteredGroups` is a snapshot that keeps its old value after
  the last group is removed (the update timer stops when idle). Documented, not changed.
- Docs consolidated to the dll-* standard: root `CHANGELOG.md`, standard `docs/` set, superseded reports and
  plans removed (git history keeps them).

### Fixed
- Repeated swings showed no animation (the attack clip now restarts per swing).
- "Player Interaction or Damage" activation never fired from damage: `ReportCrowdUnitDamaged` now engages
  `OnInteraction` groups.

### Notes
- CSV capture: start it by console command after load (`CsvProfile Frames=N`). In UE 5.7, `-csvCaptureFrames` at
  startup asserts (`IsRayTracingAllowed` before RHI init), with or without `-nullrhi`.

## [1.4.0] - 2026-07-14
### Added
- Opt-in group-level player engagement with manual, interaction/damage, proximity and always-acquire modes.
- Budgeted moving-target sampling, repath thresholds, deterministic follow spread, automatic
  release/return-to-wander, Blueprint lifecycle/attack events.
- One shared navmesh corridor per engaged group, waypoint look-ahead, spatial-hash separation while following,
  group-only debug by default, and diagnostics proving the default path issues zero per-unit nav queries.
- Deterministic managed subgroups with shared ambient corridors, bounded shared-path work, subgroup membership
  queries, coalesced presentation cues for pooled sound/VFX.
- Terrain-height conformance for Planar 2D navmesh paths with a configurable mesh-pivot offset and no per-unit
  ground trace.
- Planar and free-3D crowd modes: 3D spatial hashing, sphere-volume wandering, XYZ separation, direct pitched movement.
- Native health queries, healing, health/death events, nearest-unit and radius queries, a one-call
  damage-and-activate path.
- Standard Actor damage and optional GAS Gameplay Effects for engaged Actor targets without an ASC or Actor per entity.
- Skeletal representation hysteresis/capacity diagnostics, state clip and Animation Blueprint inputs, stable
  tag-based VAT indices, eight-float ISM animation/LOD/team/health data.
- Spawner, diagnostics, README workflow and automation coverage for all of the above.
### Verified
- UAT `BuildPlugin` UE 5.7 (Editor, Development Game, Shipping Game, UHT); `MassUnitSystem.Core.NativeMassLifecycle`
  1 pass, 0 warnings; transient PIE terrain and engagement runs in the reference adopter.

## [1.3.0] - 2026-07-13
### Added
- World-owned, timer-driven crowd service: deterministic bounded wandering, random speed/idle variation,
  pause/resume, movable centres, forced decisions, Blueprint diagnostics.
- Spatial-hash separation steering and paired non-combat interaction events with no per-unit Actor, UObject,
  collision component or Behavior Tree.
- Behaviour LOD distance multipliers, round-robin decision budgets, observer-based simulation sleeping, staggered
  multi-view visual LOD refresh.
- Safe cancellation of queued/in-flight navigation for crowd pause, interaction, sleeping and unregister.
- Spawner opt-in controls and automation coverage for crowd behaviour and cleanup.

## [1.2.1] - 2026-07-13
### Fixed
- Moving units flickered: the HISM fallback is replaced with dynamic ISM components (no cluster-tree rebuilds),
  stable slots, batched in-place transform updates, add/remove deltas only on topology change.
### Added
- Blueprint topology-revision diagnostic and regression coverage.

## [1.2.0] - 2026-07-13
### Added
- Placeable, non-ticking `Mass Unit Spawner` with Blueprint-writable scale, layout, movement, lifecycle,
  networking and debug controls for a zero-asset workflow.
- Asset-free `Create Default Unit` and explicit `Get Mass Unit Subsystem` Blueprint discovery metadata.
- Instanced-mesh diagnostics and automation coverage for default creation, spawner ownership, movement
  commands, rendering fallback and cleanup.
- Five-minute quick-start guide.

## [1.1.0] - 2026-07-11
### Changed
- Native world-owned Mass entities and fragments replace the shared static fallback entity store.
- Bootstrap is a tickable world subsystem with a native Mass dependency.
- GAS reworked as an opt-in bridge to externally owned ASCs.
- Module and plugin dependencies reduced to what is used.
### Added
- Stable Blueprint-compatible wrappers around native entity handles.
- Working movement, combat, visibility, navigation, formation and destruction paths.
- Asset-free HISM renderer; custom Niagara rendering optional/configurable.
- Bounded skeletal mesh pooling and truthful vertex-animation tag/index registration.
- Optional selective Behavior Tree/Blackboard bridge.
- Native lifecycle automation test, safe world shutdown, portable build helpers, setup/API docs.
### Removed
- GASCompanion references and dependency (third-party plugin; never required by shipped code).
- Public headers that shadowed Unreal Engine Mass headers.
### Verified
- UAT packaging with UE 5.7.2 for Win64 Editor, Development and Shipping.

## [1.0.0] - 2025-03-15
- Initial repository version.
