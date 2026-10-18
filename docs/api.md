# Public API

## Entry point

`UMassUnitSubsystem` is a `UTickableWorldSubsystem` available in Game, PIE, and Game Preview worlds. Resolve it with `UMassUnitSubsystem::Get(WorldContextObject)` or Unreal's world-subsystem API.

It owns and exposes:

- `UMassUnitEntityManager`
- `UFormationSystem`
- `UMassUnitNavigationSystem`
- `UMassUnitCrowdSystem`
- `UNiagaraUnitSystem`
- `UUnitMeshPool`
- `UGASUnitIntegration`
- `UMassUnitBehaviorIntegration`
- `UUnitGameplayEventSystem`

## Handles and templates

`FMassUnitHandle` is the Blueprint-facing wrapper. Its `EntityHandle` is an `FMassUnitEntityHandle`, which preserves the index and serial of Unreal's native `FMassEntityHandle` and converts back for native APIs.

`UUnitTemplate` is the creation Data Asset. It owns lightweight stats/team metadata plus optional static, VAT, and skeletal representation assets. Skeletal inputs include an Animation Blueprint and Idle/Move/Attack/Death/Stun clips; explicit state clips take precedence. `AnimationTags` provide stable VAT indices. `GetRequiredFragments()` describes the native archetype used by the manager.

## Unit manager

Primary Blueprint functions:

- `CreateDefaultUnit` for an asset-free visible baseline
- `CreateUnitFromTemplate`
- `DestroyUnit`, `IsUnitValid`, `GetUnitCount`, `GetAllUnits`
- `GetUnitsByType`, `GetUnitsByTeam`
- `GetUnitTransform`, `SetUnitTransform`
- `SetUnitDestination`
- `SetUnitTarget`, `ClearUnitTarget`
- `GetUnitState`, `GetUnitHealth`, `GetUnitHealthPercent`
- `ApplyDamage`, `HealUnit`, `OnUnitHealthChanged`, `OnUnitDied`
- `FindClosestUnit`, `GetUnitsInRadius` with selectable planar or full-3D distance

The `Internal` variants accept native-compatible `FMassUnitEntityHandle` values for C++ systems.

## Quick-start spawner

`AMassUnitSpawner` is a placeable, non-ticking ownership actor. Its setup fields are Blueprint-readable/writable. It can create a centered grid from a `UnitTemplate` or the runtime default, issue direct/navmesh commands while preserving spacing, opt into continuous crowd behavior, expose valid owned handles, and destroy only its own units on end play. `Spawn On Authority Only` is enabled by default to avoid accidental client/server duplication.

Engagement-enabled spawners also expose `ActivateCrowdAgainstActor`, `DeactivateCrowdEngagement`, `FindClosestSpawnedUnit`, `NotifySpawnedUnitInteracted`, and `DamageSpawnedUnitAndActivate`.

The spawner is optional. Production wave managers can call the same unit-manager, crowd, navigation, and formation APIs directly.

## Navigation and formations

`UMassUnitNavigationSystem::RequestPath` queues an individual request. `FindSharedPath` performs one synchronous native navmesh query for systems that share a corridor across many entities. Successful native paths preserve navmesh Z and mark the navigation fragment accordingly; Planar 2D crowd movement can apply its mesh-pivot height offset while keeping units upright. Direct fallback is intentionally straight-line and is not terrain discovery. `CancelPath` cancels queued/in-flight work for one handle. `ProcessPathRequests` exists for explicit use but is already called by the world subsystem.

`UFormationSystem` creates integer formation handles and supports add/remove, target, shape, location/rotation, and member queries.

`UNiagaraUnitSystem` exposes `IsUsingNiagara`, `GetInstancedMeshComponentCount`, `GetInstancedMeshInstanceCount`, `GetInstancedMeshTopologyRevision`, and `GetInstancedMeshCustomDataFloatCount` for first-run rendering and topology-churn diagnostics. The fallback writes eight material floats: animation index/time, visual LOD, team id/RGB, and health percent. VAT animation indices resolve through the template's registered gameplay tags.

`UUnitMeshPool` owns the bounded close-range skeletal components and exposes active, available, and capacity diagnostics. Candidate selection favors the closest eligible entities, retains existing owners when possible, and applies the project-wide skeletal-distance hysteresis before returning a unit to its instanced representation.

## Crowd behavior

`UMassUnitCrowdSystem` registers arbitrary handle arrays as integer crowd groups. `FMassUnitCrowdConfig` exposes planar/free-3D movement, navmesh-height conformance and pivot offset, bounded wander, speed/idle randomness, spatial separation, paired interactions, deterministic managed subgroups, simulation distance, coalesced presentation cues, and visual debugging. With Planar 2D navigation enabled, each managed ambient subgroup shares one corridor; `MaxSharedPathBuildsPerCrowdUpdate` bounds new corridor work. Group APIs support register/unregister, pause/resume, center changes, forced decisions, counts, subgroup membership queries, and `FMassUnitCrowdStats`.

`FMassUnitPlayerEngagementConfig` is opt-in per group and controls activation mode/radius, automatic release, target sampling, deterministic follow distance/spread, engaged speed, attacks, Actor damage, and an optional target Gameplay Effect. With navigation enabled, `Use Shared Navigation Path` defaults on: the group samples the target once, builds one navmesh corridor from its living-unit centroid, and gives followers look-ahead waypoints while the spatial hash supplies local separation. Each final spread slot receives one navmesh projection for accurate slope height without an individual corridor or ground raycast. `Shared Path Repath Interval`, `Shared Path Repath Distance`, and `Shared Path Look Ahead Distance` expose the cost/responsiveness tradeoff to Blueprint. Disable shared navigation only when every entity genuinely needs an independent path.

No perception raycast or collision trace is issued per entity. Visual debug draws one cyan ambient subgroup corridor or purple engagement corridor by default; the advanced `Draw Unit Destinations` option enables red per-unit destination arrows. `FMassUnitCrowdStats::ManagedSubgroups`, `SharedPathsBuilt`, `AmbientSharedPathsBuilt`, `PerUnitPathsRequested`, and the navigation service's queued-request count make the active strategy observable. Public APIs configure, activate/deactivate, notify interaction, damage-and-activate, resolve the nearest crowd unit, query subgroup membership, and query engagement/target state. `OnCrowdInteractionStarted`, `OnCrowdEngagementStarted`, `OnCrowdEngagementEnded`, and `OnCrowdAttackRequested` are Blueprint multicast hooks. `OnCrowdCueRequested` emits rate-limited MovementStarted, AmbientInteraction, EngagementStarted/Ended, and Attack moments per subgroup for a project-owned pooled audio/Niagara manager.

`FMassUnitCrowdConfig::MotionStyle` picks the ambient movement: `Wander` (random destinations, the default), `Flock`
(boids around a roaming goal), `Swarm` (orbiting cloud around a drifting centre) or `Hive` (home cluster plus foragers).
`FMassUnitCrowdConfig::Motion` holds each style's tuning; `GetCrowdGroupMotionGoal` / `GetCrowdGroupMotionStyle` expose
it. Engaged groups ignore the style and chase as before. `AddUnitsToCrowdGroup` grows a live group without resetting it.

## Stacking and piles

`UMassUnitStackSystem` (from `UMassUnitSubsystem::GetStackSystem`) lets units climb onto other units. It is opt-in per
template (`UUnitTemplate::Stacking.bCanStack`) and capped by the Stacking settings. `StackUnitOn(Climber, Support,
FaceTarget)` puts a climber on a support: it climbs to one `StackHeight` above the support's pivot and the movement
processor leaves it alone (`FMassUnitCrowdFragment::bStackDriven`). A ground support is frozen in place while something
stands on it. Losing a support (death, launch, destruction, release) throws its riders off with `LaunchUnit`, which
cascades up the pile. The pile planner does this automatically for engaged crowd groups on the server. Queries:
`IsUnitStacked`, `GetUnitStackLevel`, `GetUnitStackRole`, `GetUnitStackSupport`, `GetStackStats`. Topple piles with
`CollapseAllStacks` / `CollapseStacksInRadius`. Events: `OnUnitStackChanged`, `OnUnitStackCollapsed`. The plugin
replicates nothing: heights reach clients through whatever replicates unit locations (Z included).

## Looks

`UMassUnitLookProfile` dresses units: weighted body variants (mesh + optional material), modular parts drawn with the
body (chance + weighted options), a scale range and a tint palette (custom data 13-15). Set it on a template
(`LookProfile`), a spawn batch (`FMassUnitBatchSpawnOptions::LookProfile`) or a unit (`SetUnitLookProfile`). The roll is
deterministic from the variant seed at byte precision, so replicating the seed reproduces the look. Parts share the
body's transform and per-instance data: bake a modular VAT kit in one AnimToTexture pass. `GetUnitLook` reports what a
unit wears; `RollLook` previews a seed. Instanced band only (the skeletal near band draws the template mesh).

## Attitudes

`UMassUnitAttitudeProfile` gives units of one template individual temperaments: speed, attack cooldown and looping
animation tempo ranges (optionally linked into one "energy" roll), courage (the order units flee in when their group
routs) and a climb chance (unwilling units never climb piles). Set it on a template (`AttitudeProfile`), a batch
(`FMassUnitBatchSpawnOptions::AttitudeProfile`) or a unit (`SetUnitAttitudeProfile`); `GetUnitAttitude` reports the roll.
Group-wide behaviour (motion style, engagement, morale) stays on the crowd group.

## Behaviour presets

`UMassUnitBehaviorPreset` bundles a crowd group's crowd config, navigation, engagement and collective behaviour.
`RegisterCrowdGroupFromPreset(Units, Center, Preset)` registers a group from it; `ApplyCrowdGroupPreset(Group, Preset)`
swaps a live group's temperament (same handle and units; the subgroup layout from registration is kept; commanded and
stacked units are untouched). `SetCrowdGroupConfig` changes a live group's crowd config by hand.

## Commands (RTS control)

`UMassUnitCommandSystem` (from `UMassUnitSubsystem::GetCommandSystem`) gives units orders. `IssueMoveOrder(Units, Order)`
moves a selection into a formation (`FMassUnitMoveOrder`: Destination, Facing, Shape, Spacing, AcceptanceRadius,
bUseNavigation, bMatchSpeed) and returns an order id; `OnMoveOrderFinished` fires when every unit arrived, failed or got
another order. `IssueStopOrder` / `IssueHoldOrder` stop units in place (they stay under orders), `ReleaseUnits` /
`ReleaseAllUnits` hand them back to their crowd group. A unit under orders has `FMassUnitCrowdFragment::bCommanded`: the
crowd keeps it apart from neighbours but never moves it, and the pile planner skips it. Queries: `IsUnitCommanded`,
`GetUnitOrder` (`EMassUnitOrder` None / Idle / Move / Hold), `GetUnitOrderSlot`, `GetUnitOrderId`, `GetCommandStats`.
`ComputeFormationSlots` is the pure layout (move previews). Call it on the server; selection, input, ownership and order
replication belong to the adopter. Attack orders, auto-acquire and stances are the next phase.

## Optional bridges

`UGASUnitIntegration` maps valid Mass handles to externally owned ASCs and exposes ability/effect operations.

`UMassUnitBehaviorIntegration` attaches or removes transient Behavior Tree/Blackboard components and synchronizes recognized keys.

`UUnitGameplayEventSystem` registers gameplay-tag callbacks and dispatches payloads without coupling every unit to a UObject actor.

## Native fragments and processors

`MassUnitFragments.h` declares the plugin's transform, state, target, ability, team, visual, formation, navigation, crowd, and LOD fragments. `MassUnitCommonFragments.h` provides velocity, force, and look-direction fragments. Non-trivial fragments explicitly opt into Mass fragment traits.

The runtime module provides auto-registered movement, combat, and visibility processors. `UMassUnitFormationProcessor` remains a disabled compatibility processor; formation work is owned by `UFormationSystem` to avoid double updates.
