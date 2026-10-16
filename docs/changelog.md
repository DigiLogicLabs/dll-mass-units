# Changelog

## Unreleased - template body material

- `UUnitTemplate::BodyMaterialTag` (additive, `Unit Template|Tags`): what a unit's body is made of, for the host
  project's material-keyed systems (gore splats, impact effects). The plugin never reads it; the tag vocabulary is the
  project's. Empty = the host's default.

## Unreleased - pattern helpers for tools

- `AMassUnitSpawner::ComputePatternOffsets` / `ComputePatternRadius`: static, BlueprintPure versions of the spawner
  layouts, so tools (the MetalLegs stress director) can lay out units without a spawner actor.
  `CalculateLocalSpawnOffsets` / `GetPatternRadius` now call them; every layout is unchanged (parity tested in
  `MassUnitSystem.Spawner.PatternLayouts`).
- `UMassUnitCrowdSystem::GetCrowdGroupCount` comment: it is live, unlike `GetCrowdStats().RegisteredGroups`, a snapshot
  that keeps its old value after the last group is removed (the update timer stops when idle).

## Unreleased - batch spawn and spawner patterns

- `UMassUnitEntityManager::CreateUnitsFromTemplate(Template, Transforms, FMassUnitBatchSpawnOptions)`: create N units
  in one call. Template assets are loaded once; the batch is clipped at `MaxUnits` (one warning). A null template uses
  the asset-free default. `CreateUnitFromTemplate` now shares the same body; its behaviour is unchanged.
- `FMassUnitBatchSpawnOptions`: optional team override (ID, colour), `RandomSeed`, `bSeedVariants` (deterministic
  variant seeds). Defaults change nothing.
- `AMassUnitSpawner`: `SpawnPattern` (`Grid` default and unchanged, `Disc`, `Ring`, `Line`, seeded `Scatter`),
  `PatternRadius`, `SpawnOptions`; `SpawnUnits()` uses the batch call. `CalculateLocalSpawnOffsets` /
  `GetPatternRadius` are BlueprintPure. Visual debug draws the pattern outline.
- Tests: `MassUnitSystem.Spawner.PatternLayouts`, `MassUnitSystem.Spawner.BatchSpawn`.

## Unreleased - profiling instrumentation

- `Core/MassUnitStats.h`: one stat group `STATGROUP_MassUnitSystem` (`stat MassUnitSystem`) and CSV category `MassUnits`.
  `MASSUNIT_SCOPE_CYCLE(StatId)` = cycle stat + Unreal Insights CPU scope + CSV timing (`MassUnits/<StatId>`, ms) in one
  line; `MASSUNIT_SET_COUNTER` sets a counter stat and a CSV stat together.
- Timed: movement, combat and visibility/LOD processors; subsystem tick (total), skeletal mesh pool, visual update
  (Niagara / instance renderer), instance renderer update; crowd update, engagements, collective behaviour; launches,
  timed statuses, prune.
- Per-frame counters: units, airborne, skeletal-mesh units, ragdolls, render instances, crowd groups
  (`UMassUnitCrowdSystem::GetCrowdGroupCount` added). Measurement only - no behaviour change; stats compile out of Shipping.
- Capture recipe: start the CSV capture by console command after load (`CsvProfile Frames=N`). In UE 5.7,
  `-csvCaptureFrames` at startup asserts (`IsRayTracingAllowed` before RHI init), with or without `-nullrhi`.

## Unreleased - launches, kill flings, near-band ragdolls

- `UMassUnitEntityManager::LaunchUnit(Unit, Velocity, Instigator, bApplyTemplateScale)`: ballistic arcs integrated in a
  sparse table by `TickLaunches` (called from the subsystem tick; free with nothing airborne, capped by `MaxAirborneUnits`).
  Living units are held down by `MassUnit.Status.Launched` for the whole flight (re-armed if lifted mid-air) and then for
  `KnockdownSeconds`; corpses tumble, bounce once and settle flat. One budgeted landing trace per launch.
  `OnUnitLaunched` / `OnUnitLanded` events; `IsUnitAirborne`, `GetAirborneUnitCount`.
- `UUnitTemplate::LaunchVelocityScale` (0 = never launched). Units immune to the Launched status are not launched.
- `UUnitMeshPool::StartRagdolls`: near-band kills with a Physics Asset become simulating ragdolls (max `MaxActiveRagdolls`),
  reset into the pool when the unit is destroyed; meshes that cannot simulate fall back to the fling.
- Settings category "Launch". Automation: `MassUnitSystem.Units.Launch`.

## Unreleased - vertex-animation clips, unit statuses and tags

- Added a per-template vertex-animation clip table (`UUnitTemplate::VertexAnimationClips`, `FMassUnitVATClip`)
  for AnimToTexture bakes, written per instance as appended custom-data channels 10-12 (clip start, frame count,
  frames per second; negative rate plays once and holds). A finished one-shot hands back to Idle; Death holds.
  Empty table keeps the previous behaviour. `RunSpeedThreshold` selects Run over Walk. Templates validate the
  table (duplicate rows, missing Idle, looping Death/Attack, impact delay past the cooldown).
- Swings: `AttackSerial` increments per swing and restarts the attack clip in both bands (repeated swings showed
  no animation before). Optional `AttackImpactDelay` lands damage on the clip's impact frame; zero keeps the old
  timing. A unit that leaves range, loses its token, routs or is stunned before impact whiffs.
- Near band: a promoted unit starts its clip at the vertex-animation phase instead of frame 0. New
  `UMassUnitAnimInstance` parent class receives the unit's state (tag, state, speed, health, attack serial,
  owned tags, team) and `OnAttackSwing` / `OnUnitAssigned` events, so standard-skeleton ABPs work without a pawn.
- Unit gameplay tags and timed statuses on `UMassUnitEntityManager` (`ApplyUnitStatus`, `StunUnit`,
  `RemoveUnitStatus(es)Matching`, `Add/RemoveUnitTags`, `UnitHasTag`, `UnitMatchesTagQuery`,
  `FindUnitsMatchingTagQuery`, `OnUnitStatusDamageTick`, `OnUnitStatusChanged`): periodic damage, slows, stuns,
  stacks, template `GrantedTags` and `StatusImmunityTags`. Ticks at 10 Hz only for units with a status.
- `UMassUnitCrowdSystem::SetCrowdTargetFilter` lets the project decide hostility for target acquisition,
  proximity threat and damage reports. Unset = previous behaviour.
- Re-applying a status never shortens it: the refresh keeps the later expiry, and a permanent status stays
  permanent (a surface re-arming a brief status every pulse no longer cuts a longer one short).
- Automation: `MassUnitSystem.Units.StatusAndTags`, `MassUnitSystem.Units.AnimationContract`.
- Guide: `docs/vat_animation_and_status.md`.

## Unreleased - collective behavior

- Added opt-in group-level collective behavior (`FMassUnitCrowdCollectiveConfig`,
  `ConfigureCrowdGroupCollectiveBehavior`, spawner `bEnableCollectiveBehavior`). It is off by default,
  and groups without it are unchanged.
  - **Alerts:** an engaging group recruits nearby same-team groups. Recruits build awareness with
    distance falloff and engage after a deterministic reaction delay. Relays are bounded by depth and
    damped per hop.
  - **Morale:** casualties drain morale. Groups rout with a staggered flee, spread fear, and rally
    after a minimum duration.
  - **Threat table:** damage and proximity build threat per actor. Target switches use a margin and a
    minimum interval, and the group takes over the next actor when its target is lost.
  - **Cost:** everything runs per group on the existing crowd timer. Signals are delivered on the next
    update (queue capped at 256), and the living-count/anchor pass is throttled and cached.
- Added `ReportCrowdUnitDamaged`, a safe call for every hit that resolves projectiles to their
  instigating pawn. It engages OnInteraction groups, which the documented "Player Interaction or Damage"
  mode promised but no damage path delivered.
- Added `ApplyCrowdGroupMoraleShock`, `RaiseCrowdAlertAtLocation`, morale/routing/awareness/group
  queries, the events `OnCrowdGroupAlerted`/`Routed`/`Rallied`/`TargetChanged`, and the appended cue
  values `Alerted`/`Routed`/`Rallied`/`TargetChanged`.
- Added collective stats, visual debug (signal circles, morale bar, threat line, HUD keys 7300-7309) and
  the automation test `MassUnitSystem.Crowd.CollectiveBehavior`.

## 1.5.0

- Replaced the instanced-mesh fallback with `UCrowdInstanceRenderer`: stable per-entity instance slots
  so per-instance data can no longer swap between units when one dies, one contiguous transform upload
  per mesh per update, custom-data uploads only when a value actually changed, and tail-only compaction
  of recycled slots.
- Expanded per-instance custom data from eight floats to ten and made the channel layout a published,
  append-only contract (`UE::MassUnitSystem::CrowdCustomData`), adding a stable per-unit random seed and
  horizontal speed, and replacing elapsed animation time with an animation start stamp so the buffer
  stops dirtying every frame.
- Added crowd rendering cost controls to the settings: per-instance GPU cull distances, shadow casting
  off by default with an override, and a world-position-offset disable distance. Crowd instances no
  longer affect distance-field or dynamic indirect lighting and no longer receive decals.
- Added diagnostics for allocated slots and corrected the component-count accessor, so instance
  recycling and draw-call growth are observable without a profiler.
- Documented the scale rules this all rests on in `docs/scalability_best_practices.md`, including the
  split between unit-vs-unit combat and the configuration-only crowd-vs-Actor engagement path, and the
  missing attacker-limiting that bounds incoming damage at scale.
- Added crowd attacker limiting: `MaxSimultaneousAttackers` bounds peak incoming damage to a designed
  number by requiring a group attack token held for one cooldown, `StandoffDistanceMultiplier` parks
  token-less units on a ring outside attack range instead of letting them crowd the target, and
  `bUseEngagementSlots` spreads engaged units over fixed angular positions rather than converging on a
  point. All default to the previous behaviour. Tokens expire on a timestamp so a unit that dies or
  leaves range mid-swing cannot starve the pool. New `ActiveAttackers` and `UnitsAwaitingAttackToken`
  diagnostics, plus red/yellow attack-state debug drawing for confirming behaviour without an attack
  animation. Token hold duration scales with the round-robin revisit period so the cap cannot leak on
  large groups, every group expires tokens once per update rather than only on acquisition, and
  disengaging clears the pool.
- Added `docs/authoring_pipeline.md`: the Blender-to-crowd-unit workflow, rig and vertex budgets, the
  Vertex-mode `AnimToTexture` constraint imposed by the single-texture visual fragment, and the LOD and
  pivot conventions a custom mesh must satisfy.


## 1.4.0

- Added opt-in group-level player engagement with manual, interaction/damage, proximity, and always-acquire modes.
- Added budget-compatible moving-target sampling, repath thresholds, deterministic follow spread, automatic release/return-to-wander, and Blueprint lifecycle/attack events.
- Added one shared navmesh corridor per engaged group, waypoint look-ahead, spatial-hash separation while following, group-only debug by default, and diagnostics proving that the default path issues zero per-unit nav queries.
- Added deterministic managed subgroups with shared ambient corridors, bounded shared-path work, subgroup membership queries, and coalesced presentation cues for pooled sound/VFX.
- Added terrain-height conformance for Planar 2D navmesh paths, including a configurable mesh-pivot offset and no per-unit ground trace.
- Added planar and true free-3D crowd modes, including 3D spatial hashing, sphere-volume wandering, XYZ separation, and direct pitched movement.
- Added native health queries, healing, health/death events, nearest-unit and radius queries, plus a one-call damage-and-activate path.
- Added standard Actor damage and optional GAS Gameplay Effects for engaged Actor targets without allocating an ASC or Actor per Mass entity.
- Added skeletal representation hysteresis/capacity diagnostics, state clip and Animation Blueprint inputs, stable tag-based VAT indices, and eight-float ISM animation/LOD/team/health data.
- Extended the placeable spawner, diagnostics, README workflow, and packaged automation coverage for engagement, subgroup navigation, terrain height, representation data, and free-3D behavior.

## 1.3.0

- Added a world-owned, timer-driven crowd service with deterministic bounded wandering, random speed/idle variation, pause/resume, movable centers, forced decisions, and Blueprint diagnostics.
- Added spatial-hash separation steering and paired non-combat interaction events without per-unit Actors, UObjects, collision components, or Behavior Trees.
- Added behavior LOD distance multipliers, round-robin decision budgets, observer-based simulation sleeping, and staggered multi-view visual LOD refresh intervals.
- Added safe queued/in-flight navigation cancellation for crowd pause, interaction, sleeping, and unregister paths.
- Added spawner opt-in controls plus automation coverage for crowd destinations, deterministic variation, interactions, pause/resume, and cleanup.

## 1.2.1

- Replaced the moving-unit HISM fallback with dynamic ISM components so continuous movement does not rebuild hierarchical cluster trees.
- Replaced per-refresh clear/hide/re-add rendering with stable slots, batched in-place transform updates, and add/remove deltas only when topology changes.
- Added a Blueprint topology-revision diagnostic and regression coverage for flicker-free transform updates and cleanup.

## 1.2.0

- Added a placeable, non-ticking Mass Unit Spawner with Blueprint-writable scale, layout, movement, lifecycle, networking, and debug controls for a zero-asset blank-level workflow.
- Added asset-free `Create Default Unit` and explicit `Get Mass Unit Subsystem` Blueprint discovery metadata.
- Added instanced-mesh diagnostics and automation coverage for default creation, spawner ownership, movement commands, rendering fallback, and cleanup.
- Added a five-minute quick-start guide and repeatable production setup pattern.

## 1.1.0

- Replaced the shared static fallback entity store with native world-owned Mass entities and fragments.
- Added stable Blueprint-compatible wrappers around native entity handles.
- Implemented functional movement, combat, visibility, navigation, formation, and destruction paths.
- Changed plugin bootstrap to a tickable world subsystem with a native Mass dependency.
- Added an asset-free HISM renderer and made custom Niagara rendering optional/configurable.
- Implemented bounded skeletal mesh pooling and truthful vertex-animation tag/index registration.
- Reworked GAS as an opt-in bridge to externally owned ASCs; removed GASCompanion references/dependency.
- Implemented an optional selective Behavior Tree/Blackboard bridge.
- Removed public headers that shadowed Unreal Engine Mass headers.
- Reduced module and plugin dependencies to the features actually used.
- Added a native lifecycle automation test, safe world shutdown, portable build helpers, and current setup/API documentation.
- Validated UAT packaging with UE 5.7.2 for Win64 Editor, Development, and Shipping targets.

## 1.0.0

- Initial repository version.
