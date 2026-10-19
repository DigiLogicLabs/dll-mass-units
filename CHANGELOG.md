# Changelog

All notable changes to DLL Mass Units (plugin `MassUnitSystem`). Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning: [SemVer](https://semver.org/).
Release dates come from the commit that set the `.uplugin` `VersionName`.

## [Unreleased]

Targeted as 1.5.0. Part of this work was first listed under a "1.5.0" heading, but that version was never
tagged and `MassUnitSystem.uplugin` still reads `1.4.0`.

### Added - swarm work (2026-10-17 to 2026-10-18; each part lists its own fixes and tests)

#### fixes from the first flyer/leaper PIE run
- Fixed: a unit that joined a crowd group mid-interaction (`RegisterCrowdGroup` on units from another group, a preset
  swap) stayed `Interacting`, and so frozen in place, forever: joining resets the crowd fragment's interaction timer
  but not the unit state. The ambient update now ends any interaction with no timer. Seen in PIE as 18 of 120 flyers
  stuck on the ground after a preset swap.
- Fixed: engaged flyers waiting for an attack token yo-yoed. Hovering at `EngagedHoverHeight` put them outside the 3D
  attack range, so they stopped waiting, dove back in, found the pool full and climbed again. A flyer in the column
  over the target (2D within 1.5x the standoff ring, height within hover + attack range) now either reserves a token
  there and dives, or keeps hovering. A diving flyer holds its token for the dive, so the number of flyers actually
  swinging can briefly be below `MaxSimultaneousAttackers`.
- Tests: `MassUnitSystem.Crowd.SwarmTypes` covers hover-while-full and dive-with-a-free-token;
  `MassUnitSystem.Core.NativeMassLifecycle` covers rejoining a group mid-interaction.

#### leapers (pounce)
- `UMassUnitEntityManager::LeapUnit(Unit, TargetPoint, ArcHeight)`: a living unit jumps on a ballistic arc to the ground
  under a point (its own pivot height kept), held for the flight like a launch but playing `MassUnit.Animation.Leap`
  (Attack fallback) and landing on its feet with no knockdown. `IsUnitLeaping`, `OnUnitLeapt` (leaps are not broadcast
  as `OnUnitLaunched`, which adopters replay as knockdowns). Uses the launch table: no new per-unit tick.
- Engagement pounce: `FMassUnitPlayerEngagementConfig::bEnablePounce`, `PounceMinRange`, `PounceMaxRange`,
  `PounceCooldown`, `PounceArcHeight`, `PounceDamageMultiplier`, `PounceHitRadius`. An engaged planar unit in the range
  window takes an attack token and leaps; landing within the hit radius requests one attack (the normal
  `RequestActorAttack` path). `FMassUnitCrowdStats::TotalPouncesStarted` / `TotalPounceHits`.
- Fragments: `FMassUnitStateFragment::bLeaping`, `FMassUnitCrowdFragment::NextPounceTime`. Tag `MassUnit.Animation.Leap`.
- Review fixes (same day): a launch during a leap ends the leap (knockdown on landing, pending pounce dropped);
  units immune to `MassUnit.Status.Launched` never leap; a landed pounce starts the attack cooldown (no extra swing on
  touchdown); a refused leap gives its attack token back; pounces land short of the target, need a clear WorldStatic
  line to it and a height difference within `PounceMaxHeightDelta`, and never start from inside attack range.
- Test: `MassUnitSystem.Units.Leap`.

#### swarm types and flyers
- `UMassUnitSwarmType` (data asset): a kind of swarm as one asset - `Template`, optional `LookProfile` /
  `AttitudeProfile` overrides, `BehaviorPreset` and `Locomotion` (`EMassUnitLocomotion`: `Ground`, `Air`).
  `SpawnSwarm(World, Type, Center, Count, Radius, Seed, TeamOverride)` spawns and registers one crowd group;
  `MakeCrowdConfig()` returns the group config (the preset's, made to fly for Air).
- Flyers: `FMassUnitCrowdConfig` altitude band for Free 3D groups - `bUseAltitudeBand`, `MinAltitude`, `MaxAltitude`,
  `EngagedHoverHeight`, `FloorClearance`, `bFallWhenKilled` (all off / unchanged by default). Ambient destinations
  (wander, motion styles) are clamped into the band over the ground under the group centre (one WorldStatic trace per
  register / reconfigure / move); the movement processor clamps units above the band floor (no traces); an engaged
  group circles `EngagedHoverHeight` over its target while waiting for an attack token and closes in from above; a
  killed flyer starts a corpse fall on the next `TickLaunches` (landing at the band floor when the landing trace misses).
- `UMassUnitCrowdSystem::RegisterCrowdGroupFromPresetWithCrowd` (C++), `GetCrowdGroupAltitudeBand`.
- Fragment: `FMassUnitCrowdFragment::AltitudeFloorZ`, `bAltitudeFloor`, `bFallWhenKilled`.
- Review fixes (same day): a death fall never replaces a launch the kill already started (adopter kill flings, blasts) -
  it only caps that flight's landing at the floor; a raised floor lifts units at 400 cm/s instead of teleporting them;
  commanded units skip the floor (navmesh orders reach lower path points); routed flyers flee inside the band; the pile
  planner skips banded groups (flyers never pile); band floor / ceiling rings in the crowd visual debug.
- Test: `MassUnitSystem.Crowd.SwarmTypes`.

#### behaviour presets
- `UMassUnitBehaviorPreset` (data asset): a crowd group's whole temperament - `FMassUnitCrowdConfig` (motion style,
  wander, avoidance, LOD), navigation, engagement and collective behaviour - in one shareable asset.
- `UMassUnitCrowdSystem::RegisterCrowdGroupFromPreset`, `ApplyCrowdGroupPreset` (swap a live group's temperament, same
  handle and units), `GetCrowdGroupPreset`, and `SetCrowdGroupConfig` (change a live group's crowd config by hand). The
  managed-subgroup layout stays as registered; commanded and stacked units are left alone; an engaged group whose new
  preset has engagement off disengages.
- Fix: `MassUnitStackSystem.cpp` now includes `MassUnitCommonFragments.h` (it compiled only inside a unity blob; the
  standalone `BuildPlugin` failed).
- Test: `MassUnitSystem.Crowd.BehaviorPresets`.

#### attitudes: per-unit temperament
- `UMassUnitAttitudeProfile` (data asset): `MoveSpeedRange`, `AttackCooldownRange`, `AnimationRateRange` (looping clips
  only), `bLinkRolls` (one "energy" roll drives all three: fast units also attack more often and animate quicker),
  `bRollCourage` + `CourageRange` (rout order: cowards flee first), `ClimbChance` (unwilling units never climb piles).
  `RollAttitude(Seed)` is deterministic per seed byte with its own salt (independent of the look roll).
- Assign per template (`UUnitTemplate::AttitudeProfile`), per batch (`FMassUnitBatchSpawnOptions::AttitudeProfile`) or per
  unit (`SetUnitAttitudeProfile`); `GetUnitAttitudeProfile`, `GetUnitAttitude`. Applied on top of the template's speed
  and cooldown (cooldown never below the impact delay); a grouped, uncommanded unit's crowd base speed follows.
- Fragments: `FMassUnitStateFragment::Courage`, `bAvoidsClimbing`; `FMassUnitVisualFragment::AttitudeProfile`,
  `AnimationRate`. Units without a profile keep the template's values exactly.
- Test: `MassUnitSystem.Units.Attitudes`.
- Review hardening (same day, looks + attitudes): profiles and every look option asset are held by
  `UMassUnitEntityManager::RetainedAssets` (Mass fragments are not GC roots - packaged builds would free them) and a look
  profile's options load once when first used (no first-roll hitch); dedicated servers never roll or load looks; an
  attitude re-applied on a commanded unit updates the order's saved speed instead of the live speed; a template's own
  cooldown is never clamped by an attitude; attitude tempo applies to idle / walk / run only (not climb, hold, stun).

#### looks: modular parts, variants, scale and tint per unit
- `UMassUnitLookProfile` (data asset): weighted `BodyVariants` (mesh + optional material override), modular `Parts`
  (chance + weighted options, empty mesh = no part), `ScaleRange`, `TintPalette`. `RollLook(Seed)` is deterministic per
  seed byte (what adopters replicate) and draws a fixed number of values per roll.
- Assign per template (`UUnitTemplate::LookProfile`), per batch (`FMassUnitBatchSpawnOptions::LookProfile`) or per unit
  (`UMassUnitEntityManager::SetUnitLookProfile`); `GetUnitLookProfile`, `GetUnitLook`. `SetUnitVariantSeed` re-rolls, so
  client puppets match the server from the seed alone.
- Instanced renderer: buckets keyed by mesh + material (`FCrowdBucketKey`); each look part is an instance with the body's
  transform (times the look scale) and custom-data row. New custom-data channels 13-15 `TintR/G/B` (`NumFloats` 16,
  append-only; existing channels unchanged).
- Visual fragment: `LookProfile`, `MaterialOverride`, `LookParts`, `LookScale`, `LookTint`.
- Doc: [unit_composition.md](unit_composition.md). Test: `MassUnitSystem.Units.Looks`.

#### unit commands (RTS control basics, phase R1)
- `UMassUnitCommandSystem` (`UMassUnitSubsystem::GetCommandSystem`): orders for units. `IssueMoveOrder(Units,
  FMassUnitMoveOrder)` lays out formation slots (`EMassUnitFormationShape` Box / Line / Column / Wedge / Circle) around the
  destination facing the move, projects them to the navmesh, gives each unit the nearest free slot (sorted matching past
  `MaxNearestSlotAssignment`), paths each unit there, optionally at the slowest unit's speed, and reports arrival.
  `IssueStopOrder`, `IssueHoldOrder`, `ReleaseUnits`, `ReleaseAllUnits`; queries `IsUnitCommanded`, `GetUnitOrder`,
  `GetUnitOrderSlot`, `GetUnitOrderId`, `GetCommandStats`; static `ComputeFormationSlots`. Events `OnOrderIssued`,
  `OnMoveOrderFinished(OrderId, Arrived, Failed, Superseded)`.
- `FMassUnitCrowdFragment::bCommanded`: a commanded unit is left alone by its crowd group (no wander, motion style,
  engagement, interaction, sleep, pause/disengage stop; separation only) and by the pile planner. `Reset()` keeps the
  flag (the command system owns it). Uncommanded units are unchanged.
- Stuck or knocked-away units ask for a new path (`CommandStuckTime`, `MaxCommandRepaths`), then stop and count as failed.
- Settings (Commands): `CommandUpdateInterval`, `DefaultFormationSpacing`, `CommandStuckTime`, `MaxCommandRepaths`,
  `MaxNearestSlotAssignment`. Stat `Commanded Units`, cycle stat `Commands (orders)`.
- Debug: `dll.MassUnits.CommandDebug`; test commands `dll.MassUnits.CommandMoveTeam <Team> [Shape] [Distance]`,
  `dll.MassUnits.CommandStopTeam <Team> [Hold]`, `dll.MassUnits.CommandReleaseAll`.
- Test: `MassUnitSystem.Units.Commands`.
- Review hardening (same day): `OnMoveOrderFinished` is broadcast after the table is consistent, so listeners may issue,
  stop or release; Stop / Hold / Move reach a commanded unit in mid-air; release restores the unit's own speed (and its
  group's base); engagement and group resets keep a commanded unit's speed; commanded units leave any `UFormationSystem`
  formation and take no blackboard move target; a commanded unit no longer counts toward its group's anchor, subgroup
  anchors or auto-deactivate distance; the repath budget resets after real progress; corpses drop `bCommanded`.

#### stacked poses for client puppets
- `UMassUnitStackSystem::ApplyPresentationStackRole` / `GetPresentationStackRole`: presentation-only pile role for an
  adopter's client puppets (no stack entry, no falls, no events). `FMassUnitCrowdFragment::PresentationStackRole`.
- The movement processor, for a unit with a presentation role: eases its height to the destination height at
  `PresentationClimbSpeed` (setting, 300 cm/s) instead of snapping per record, plays Climb (else Run) while climbing and
  Hold (when the template has the clip) while holding.

#### multi-role piles
- `FMassUnitPlayerEngagementConfig::ExtraAttackersWhileStacked` (default 8): bonus attack tokens only stack-driven units
  (climbers and the ground units holding them) can take once the normal pool (`MaxSimultaneousAttackers`) is full. Before
  it, the ground ring held every token and a climber that reached the target never swung. Only stacking templates are
  affected; `0` restores the shared pool.
- The stack system no longer overrides the attack clip with the climb clip: a climber in reach swings mid-climb.
- The pile planner does not pull a unit that is already attacking off to climb.
- `FMassUnitCrowdStats::ActiveAttackers` counts both pools.
- Test: `MassUnitSystem.Crowd.StackedAttackers`.

#### crowd motion styles, add-to-group, teardown fix
- `FMassUnitCrowdConfig::MotionStyle` (`EMassUnitCrowdMotionStyle`): how a group moves while NOT engaged. `Wander` (the
  default, unchanged), `Flock` (boids: separation + alignment + cohesion around one roaming flock goal that waits for
  stragglers), `Swarm` (units orbit a drifting centre inside `SwarmRadius`, with jitter), `Hive` (units cluster within
  `HiveRadius` of the group centre; some forage out to the wander radius, linger and come back). Tuning in
  `FMassUnitCrowdConfig::Motion` (`FMassUnitCrowdMotionConfig`). Engagement is identical for every style. Flock and
  Swarm steer directly (no per-unit navmesh paths); Hive uses navigation when the group does.
- `GetCrowdGroupMotionGoal`, `GetCrowdGroupMotionStyle` (BlueprintPure).
- `AddUnitsToCrowdGroup(Handle, Units)`: add units to an existing group without resetting its members (refills,
  reinforcements, churn). Before it, adding units meant re-registering the group, which reset every unit (and dropped
  every pile).
- Fix: `UMassUnitCrowdSystem::Deinitialize` no longer resets unit fragments. At world teardown the Mass entity manager
  can already be gone, and a world torn down with registered groups asserted (`MassEntitySubsystem.h:35`).
- Spatial entries carry velocity (flock alignment).
- Test: `MassUnitSystem.Crowd.MotionStyles`.

#### stacking and piles
- `UMassUnitStackSystem` (`UMassUnitSubsystem::GetStackSystem`): units climb onto other units. A sparse stack table,
  ticked once per frame by the subsystem and free when empty. A climber stands one `StackHeight` above its support,
  nudged toward its target; a ground unit with someone on it is frozen in place (Holding). When a support dies, is
  launched, is destroyed or leaves, everything on it falls off through `LaunchUnit`, so piles collapse in a cascade.
  A support killed in place holds its riders for `SupportCorpseHoldSeconds` (0.25) first.
- Pile planner: server only, every `PilePlanInterval` per engaged crowd group. Once `PileMinUnits` stack-capable units
  crowd the group's target, outer units climb onto units nearer the target (at most `MaxClimbersPerPilePass` per plan),
  so the pile grows into a cone. Climbers fall off when the target breaks away (`PileRadius + PileBreakSlack`), and a
  pile comes down when its group disengages, routs, loses its target, switches target or is unregistered. Groups with
  no stack-capable template are re-checked only every 5 s.
- API: `StackUnitOn`, `ReleaseStackedUnit`, `CollapseAllStacks`, `CollapseStacksInRadius`, `IsUnitStacked`,
  `GetUnitStackLevel` / `Role` / `Support`, `GetStackStats` (`FMassUnitStackStats`), `PlanPile`;
  events `OnUnitStackChanged`, `OnUnitStackCollapsed`.
- `UUnitTemplate::Stacking` (`FMassUnitStackingConfig`): opt-in per template (`bCanStack` default false), plus
  `bCanBeClimbed`, `StackHeight`, `ClimbSpeed`, `MaxStackLevel`, `MaxRiders`, `StackForwardOffset`, `PileRadius`,
  `PileMinUnits`, `SupportReach`, `PileBreakSlack`.
- Settings, category Stacking: `bEnableStacking`, `bEnableAutomaticPiles`, `MaxStackedUnits` (256),
  `MaxStackersPerTarget` (40), `PilePlanInterval`, `MaxClimbersPerPilePass`, `MaxPileCandidates`,
  `SupportCorpseHoldSeconds`, `CollapseOutwardSpeed`, `CollapseUpSpeed`.
- `FMassUnitCrowdFragment::bStackDriven`: the movement processor skips units the stack system drives.
- `UMassUnitEntityManager::CapLaunchLandingHeight`: a unit thrown from a pile lands on the pile's ground height when
  its landing trace found no ground (skipped by budget, tracing off); a real ground hit (a step, a slope) is kept.
- `UMassUnitCrowdSystem::GetCrowdGroupHandles`, `GetCrowdGroupUnitsInternal` (C++).
- Tags `MassUnit.Animation.Climb` (falls back to Run), `MassUnit.Animation.Hold` (falls back to Idle), loose tag
  `MassUnit.Stacked`.
- Debug: CVar `dll.MassUnits.StackDebug 1` (lines climber to support, HUD line), command
  `dll.MassUnits.CollapseStacks [radius]`; stat `Stacks`, `Pile Planner`, counter `Stacked Units` (CSV `StackedUnits`).
- Test: `MassUnitSystem.Units.Stacking`.

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
