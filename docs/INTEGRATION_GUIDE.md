# DLL Mass Units — Integration guide

How to set up the plugin and use every feature, step by step. Names in `code` are C++ identifiers; the
Blueprint/Details-panel name is the same words with spaces (`bEnableCrowdSimulation` → *Enable Crowd
Simulation*). Defaults quoted here are the C++ defaults.

## 1. Install

1. Clone or copy the repo below your project's `Plugins/` folder. The folder name is free; Unreal finds the
   plugin by `MassUnitSystem.uplugin`, which must sit directly inside that folder.
   ```powershell
   git clone https://github.com/DigiLogicLabs/dll-mass-units.git Plugins/dll-mass-units
   ```
2. Regenerate project files if your IDE does not pick up the modules, open the project and accept the compile
   prompt. Restart the editor after the first compile and after every plugin update.
3. Requirements: UE 5.7 (validated with 5.7.2) and a C++ toolchain that engine accepts. No third-party plugin.

## 2. Enable and depend on it

- Enable **DLL Mass Units** (category *Digi Logic Labs*) in **Edit > Plugins**, or add
  `{ "Name": "MassUnitSystem", "Enabled": true }` to the `.uproject`. The descriptor has `EnabledByDefault: false`
  and turns on the engine plugins `MassGameplay`, `Niagara` and `GameplayAbilities`.
- C++ modules that call the API add the runtime module:
  ```csharp
  PublicDependencyModuleNames.Add("MassUnitSystemRuntime");
  ```
- Nothing to construct: `UMassUnitSubsystem` creates itself in Game, PIE and Game Preview worlds. Get it with
  `UMassUnitSubsystem::Get(WorldContextObject)` (Blueprint: *Get Mass Unit Subsystem*). It does not exist in
  editor asset worlds and is not a Game Instance subsystem. Do not run the Mass processors by hand.
- After the planned rename the names become `DLLMassUnits` / `DLLMassUnitsRuntime`; adopters then add the
  `[CoreRedirects]` listed in [`AGENTS.md`](../AGENTS.md#pending-rename-tier-b--not-done-yet).

## 3. First run: place a spawner (no assets)

1. Open a level with a player start (or use **Simulate** in an empty level).
2. **Place Actors** → search **Mass Unit Spawner** → drag it onto the floor. Leave `UnitTemplate` empty.
3. Press **Play**.

Result: `UnitCount` (25) units in a centred grid of engine cubes, moving `DestinationOffset` (1,500 cm along
the spawner's +X) with spacing preserved. Stopping play destroys only that spawner's units. Press **F** on the
spawner to frame it; the cyan arrow shows the move direction; `bEnableVisualDebug` draws spawn boxes, pattern
outline and destination arrows.

Spawner fields worth knowing: `UnitCount`, `Columns`, `UnitSpacing`, `SpawnHeight` (50, centres the 100 cm
cube), `SpawnPattern` (`Grid`, `Disc`, `Ring`, `Line`, `Scatter`), `PatternRadius`, `SpawnOptions` (team
override, `RandomSeed`, `bSeedVariants`), `bSpawnOnBeginPlay`, `bMoveOnBeginPlay`, `bUseNavigation`,
`AcceptanceRadius`, `bSpawnOnAuthorityOnly` (default on), `bDestroySpawnedUnitsOnEndPlay`.
Functions: `SpawnUnits`, `MoveSpawnedUnitsByOffset`, `CommandSpawnedUnitsToLocation`, `DestroySpawnedUnits`,
`GetValidSpawnedUnits`, `GetValidSpawnedUnitCount`. The spawner never ticks.

### Without a spawner (wave manager, Game Mode, Level Blueprint)

```
Get Mass Unit Subsystem -> Get Unit Manager
  -> Create Default Unit | Create Unit From Template | Create Units From Template (batch)
  -> keep the MassUnitHandle(s) -> Set Unit Destination / Request Path
```

Keep handles in the system that owns the wave, check `IsUnitValid` before reusing a long-lived one, and call
`DestroyUnit` when the owner ends. In C++, use the manager API or a short-lived `FMassEntityView`; never hold
fragment pointers across Mass structural changes.

## 4. Your own units: meshes and stats

Create a **Data Asset** of class `UnitTemplate` (e.g. `DA_Unit_Infantry`) and assign it to the spawner's
`UnitTemplate`.

| Group | Fields |
|---|---|
| Identity | `UnitType`, `UnitClass` (gameplay tags), `BaseLevel` |
| Combat | `BaseHealth`, `BaseDamage`, `MoveSpeed`, `AttackRange`, `AttackCooldown`, `AttackImpactDelay`, `LaunchVelocityScale` |
| Team | `TeamID`, `TeamColor`, `TeamFaction` |
| Tags | `GrantedTags`, `StatusImmunityTags` |
| Crowd band | `StaticMesh`, `VertexAnimationTexture`, `NormalMapTexture`, `AnimationTags`, `VertexAnimationClips`, `RunSpeedThreshold` |
| Near band | `SkeletalMesh`, `AnimationBlueprintClass`, `IdleAnimation`, `MoveAnimation`, `AttackAnimation`, `DeathAnimation`, `StunAnimation` |
| Behaviour | `DefaultBehavior`, `DefaultFormation`, `DefaultAbilities` |

Representation is automatic: the nearest eligible units use `SkeletalMesh` while the pool has capacity; the rest
use `StaticMesh`, else the project `FallbackStaticMesh`, else the engine cube. A custom mesh needs a feet pivot,
`SpawnHeight = 0` on the spawner and `NavigationHeightOffset = 0` in the crowd config, and a material that reads
per-instance custom data, or every unit renders identical and untinted. See
[guides/authoring_pipeline.md](guides/authoring_pipeline.md).

Two opposing crowds from one template: set `SpawnOptions.bOverrideTeam` and `TeamID` on one spawner.

## 5. Continuous crowds

Tick `bEnableCrowdSimulation` on the spawner (it overrides `bMoveOnBeginPlay`). Units then pick
deterministic-random destinations, vary speed and idle time, separate through a spatial hash and occasionally
pause in pairs. No Behavior Tree, StateTree, collision, Actor or UObject per unit or subgroup.

`CrowdConfig` (`FMassUnitCrowdConfig`):

| Field | Default | Purpose |
|---|---:|---|
| `MovementMode` | Planar 2D | `Planar2D` for ground/navmesh, `Free3D` for flying/swimming (always direct XYZ steering) |
| `WanderRadius` / `MinWanderDistance` | 1500 / 300 cm | Destination range |
| `MinIdleTime` / `MaxIdleTime` / `MaxMoveTime` | 0.5 / 2 / 8 s | Pauses and move timeout |
| `MinMoveSpeedMultiplier` / `MaxMoveSpeedMultiplier` | 0.8 / 1.2 | Speed variation |
| `bEnableSeparation`, `SeparationRadius`, `SeparationWeight` | on, 140 cm, 1.25 | Local spacing |
| `bEnableManagedSubgroups`, `ManagedSubgroupSize` | on, 32 | Split big groups; each subgroup shares one corridor |
| `SubgroupWanderRadiusScale`, `SubgroupPathLookAheadDistance` | 0.65, 350 cm | Subgroup wander and corridor look-ahead |
| `bConformToNavmeshHeight`, `NavigationHeightOffset` | on, 50 cm | Follow corridor Z; offset = pivot height above floor |
| `bEnableInteractions`, `InteractionChance`, `InteractionRadius` | on, 0.15, 275 cm | Paired pauses (`OnCrowdInteractionStarted`) |
| `bEnableGroupCueEvents`, `GroupCueCooldown` | on, 0.35 s | Coalesced `OnCrowdCueRequested` moments |
| `MaxSimulationDistance` | 15000 cm | Sleep units farther than every player observer (0 = never) |
| `RandomSeed` | 1337 | Repeatable choices |
| `bEnableVisualDebug` | off | Group bounds, destinations, interaction links |

Spawner functions: `StartCrowdSimulation`, `StopCrowdSimulation`, `ForceCrowdUpdate`, `IsCrowdSimulationActive`,
`GetCrowdGroupHandle`. Without a spawner: `Get Crowd System → RegisterCrowdGroup(Handles, Center, Config,
bUseNavigation)`, then `SetCrowdGroupPaused`, `SetCrowdGroupCenter`, `ForceCrowdGroupUpdate`,
`UnregisterCrowdGroup`, `GetCrowdStats`, `GetCrowdGroupSubgroupCount`, `GetUnitSubgroupIndex`,
`GetCrowdSubgroupUnits`.

Bind `OnCrowdCueRequested` once in a world manager and route `MovementStarted`, `AmbientInteraction`,
`EngagementStarted/Ended`, `Attack` (and the collective cues) to pooled audio/Niagara. Never attach an audio or
Niagara component per unit.

## 6. Make crowds react to players

Engagement is opt-in per group. Fastest test: `bEnableCrowdSimulation` + `bEnablePlayerEngagement` +
`PlayerEngagementConfig.ActivationMode = Always Acquire Player`, untick `bApplyActorDamage`, press **Play**
(Simulate has no possessed pawn).

| `ActivationMode` | Behaviour |
|---|---|
| Manual Blueprint Call | Waits for `ActivateCrowdAgainstActor` (spawner) / `ActivateCrowdGroupForActor` |
| Player Interaction or Damage (default) | `NotifySpawnedUnitInteracted`, `DamageSpawnedUnitAndActivate` or `ReportCrowdUnitDamaged` activates the group |
| Player Proximity | Nearest possessed player inside `ActivationRadius` (1200 cm) |
| Always Acquire Player | Nearest possessed player, kept while valid |

`bAutoDeactivate` releases a target once no living member is within `DeactivationDistance` (5000 cm);
`bReturnToWanderOnDeactivate` resumes wandering.

| Field | Default | Purpose |
|---|---:|---|
| `TargetRefreshInterval` | 0.2 s | One target sample per group, not per unit |
| `RepathDistance` | 125 cm | Rebuild a unit destination only after real target movement |
| `bUseSharedNavigationPath` | on | One corridor per group; per-unit paths only if off |
| `SharedPathRepathInterval` / `SharedPathRepathDistance` / `SharedPathLookAheadDistance` | 0.5 s / 250 cm / 350 cm | Corridor cost vs responsiveness |
| `FollowDistance` / `FollowSpread` | 150 / 125 cm | Deterministic stand-off slots around the target |
| `EngagedMoveSpeedMultiplier` | 1.1 | Chase speed |
| `bEnableAttacks`, `AttackRangeOverride`, `DamageMultiplier` | on, 0 (= template), 1.0 | Attacks on each unit's cooldown |
| `MaxSimultaneousAttackers` | 0 (unlimited) | Attack tokens: peak damage = N / cooldown |
| `StandoffDistanceMultiplier` | 1.35 | Where token-less units wait |
| `bUseEngagementSlots`, `EngagementSlotCount` | off, 12 | Spread attackers around the target |
| `bApplyActorDamage`, `DamageTypeClass` | on | Unreal `AnyDamage` on the target |
| `GameplayEffectToTarget`, `GameplayEffectLevel` | none, 1 | GAS effect on the target's ASC (authority) |
| `bEnableVisualDebug`, `bDrawUnitDestinations` | off, off | Purple corridor; per-unit red arrows (opt-in); red/yellow attack spheres |

Group API: `ConfigureCrowdGroupEngagement`, `ActivateCrowdGroupForActor`, `DeactivateCrowdGroupEngagement`,
`IsCrowdGroupEngaged`, `GetCrowdGroupTargetActor`; events `OnCrowdEngagementStarted`, `OnCrowdEngagementEnded`,
`OnCrowdAttackRequested` (always fires; hang hit reactions and custom damage here).

Install a hostility rule in C++ with `SetCrowdTargetFilter` (applies to acquisition, proximity threat and damage
reports; explicit activation is not filtered).

### Hitting a unit

The instanced renderer has no per-instance collision, so a trace never returns a unit. Resolve a world point
(cursor, weapon sweep, ground trace), then:

```
FindClosestSpawnedUnit (spawner)  or  Get Crowd System -> FindClosestCrowdUnit(Location, MaxDistance, bUse3DDistance)
  -> IsUnitValid
  -> NotifySpawnedUnitInteracted / DamageSpawnedUnitAndActivate
     or ApplyDamage + ReportCrowdUnitDamaged (any mode; feeds threat and alerts)
```

Keep `MaxDistance` close to the unit footprint. Use 3D distance for flying units.

## 7. Health and GAS

Every unit has native health: `GetUnitHealth`, `GetUnitHealthPercent`, `ApplyDamage`, `HealUnit`,
`OnUnitHealthChanged`, `OnUnitDied`, plus `FindClosestUnit` and `GetUnitsInRadius` (planar or 3D). Unit-vs-unit
combat (`SetUnitTarget`) needs different `TeamID`s and works without GAS.

- **Crowd → GAS player:** set `GameplayEffectToTarget`. The plugin finds the target's ASC and applies it on
  authority. If that effect deals the damage, untick `bApplyActorDamage` or damage lands twice.
- **GAS weapon → unit:** your game converts the hit into `ApplyDamage` / `DamageUnitAndActivate` /
  `ReportCrowdUnitDamaged`. Units have no attributes, so effect modifiers and execution calculations do not run
  on them; map effect tags to unit statuses yourself ([guides/vat_animation_and_status.md](guides/vat_animation_and_status.md)).
- **Selective ASC:** `GetGASIntegration → RegisterAbilitySystemForUnit(Unit, ASC)` maps a unit to an ASC your game
  owns; then `GrantAbility`, `ActivateAbility`, `ApplyGameplayEffect`; `UnregisterAbilitySystemForUnit` when done.
  The plugin never creates an ASC. Use it for the few units near the player, not the crowd.

## 8. Statuses, tags and launches *(Unreleased)*

- Tags: `AddUnitTags`, `RemoveUnitTags`, `UnitHasTag`, `UnitHasAnyTags`, `UnitMatchesTagQuery`,
  `FindUnitsMatchingTagQuery`. Owned tags = template `GrantedTags` + loose tags + active status tags.
- Statuses (`FMassUnitStatusEffect`, the crowd stand-in for a duration effect): `ApplyUnitStatus`, `StunUnit`,
  `RemoveUnitStatus`, `RemoveUnitStatusesMatching`, `GetUnitStatuses`; periodic damage, speed multiplier, stun,
  stacks. If `OnUnitStatusDamageTick` is bound, the plugin does not apply tick damage itself.
- Launches: `LaunchUnit(Unit, Velocity, Instigator)`; settings category *Launch* (`bEnableUnitLaunch`,
  `MaxAirborneUnits` 512, `KnockdownSeconds`, `MaxActiveRagdolls` 12, landing traces).

Details: [guides/vat_animation_and_status.md](guides/vat_animation_and_status.md).

## 9. Collective behaviour *(Unreleased)*

Opt-in per group: spawner `bEnableCollectiveBehavior` + `CollectiveConfig`, or
`ConfigureCrowdGroupCollectiveBehavior`. Alerts recruit nearby same-team groups, casualties drain morale until a
group routs and later rallies, and a threat table picks targets. World events: `RaiseCrowdAlertAtLocation`,
`ApplyCrowdGroupMoraleShock`. Queries: `GetCrowdGroupMorale`, `IsCrowdGroupRouting`, `GetCrowdGroupAwareness`,
`GetCrowdGroupAnchor`. Events: `OnCrowdGroupAlerted`, `OnCrowdGroupRouted`, `OnCrowdGroupRallied`,
`OnCrowdGroupTargetChanged`. Every tunable has a tooltip on `FMassUnitCrowdCollectiveConfig`.

## 10. Navigation and formations

Direct movement needs no navmesh. For hills, ramps and obstacles (Planar 2D only):

1. Add a **Nav Mesh Bounds Volume** over every walkable area, build navigation, press **P** to check green coverage.
2. Tick `bUseNavigation` on the spawner (or call `RequestPath`).
3. Keep `bConformToNavmeshHeight` on and set `NavigationHeightOffset` to the pivot height (50 for the cube, 0 for
   a feet-pivot mesh).
4. Check `GetCrowdStats`: a 25-unit ambient group should build one subgroup corridor and request zero per-unit paths.

`RequestPath` queues an async request (budget `MaxPathRequestsPerFrame`); `CancelPath` cancels queued and
in-flight work. With no nav data and `bFallbackToDirectPath` on, requests become a straight line, which cannot
discover terrain. Turn the fallback off to make missing navigation fail loudly.

Formations: `UFormationSystem` (`GetFormationSystem`) → `CreateFormation(Location, Rotation, Shape)`, `AddUnitToFormation`,
`SetFormationTarget`, `SetFormationShape` (`Rectangle`, `Line`, `Column`, `Wedge`, `Circle`),
`RemoveUnitFromFormation`, `DestroyFormation`. Use formations for commanded squads, crowd groups for ambient
populations.

## 11. Scale controls

**Project Settings > Plugins > Mass Unit System** (`UMassUnitSystemSettings`, Blueprint: *Get Mass Unit System
Settings*).

| Setting | Default | Purpose |
|---|---:|---|
| `MaxUnits` | 10,000 | Safety cap per world (not a performance promise) |
| `VisualUpdateInterval` | 0.033 s | ISM/Niagara upload rate |
| `CrowdUpdateInterval` | 0.1 s | Crowd decision timer |
| `MaxCrowdUnitsPerUpdate` | 500 | Round-robin decision budget |
| `MaxSharedPathBuildsPerCrowdUpdate` | 8 | New corridors per crowd update |
| `MaxPathRequestsPerFrame` | 100 | Async nav requests per tick |
| `CrowdSpatialCellSize` | 200 cm | Separation/interaction hash cell |
| `LODDistanceThresholds` | 500/1500/3000/6000 cm | Visual LOD bands |
| `VisibilityLODUpdateIntervals` | 0.05/0.1/0.2/0.5/1.0 s | Staggered visibility refresh per band |
| `CrowdSimulationLODDistances` / `...IntervalMultipliers` | 2500/5000/10000 cm; 1/2/4/8 | Behaviour LOD |
| `MaxVisibleDistance` | 10,000 cm | Visual cull (0 = off) |
| `MaxSkeletalMeshUnits` / `SkeletalMeshDistance` / `SkeletalMeshHysteresis` | 100 / 300 cm / 1.2 | Near band |
| `InstanceStartCullDistance` / `InstanceEndCullDistance` | 0 / 0 | Per-instance GPU fade/cull |
| `bCrowdInstancesCastShadows` | off | Crowd shadows (expensive) |
| `WorldPositionOffsetDisableDistance` | 5000 cm | Stop WPO on distant instances |
| `bEnableInstancedMeshFallback`, `FallbackStaticMesh`, `DefaultNiagaraSystem` | on, none, none | Rendering path |

Scale in steps: 25 cubes → one mesh at 100 → crowd at 100/500/1,000 while watching `GetCrowdStats` and
Insights → tune budgets, LOD, `MaxSimulationDistance`, visible range → then add navmesh, skeletal units, Niagara,
GAS one at a time. The rules behind the budgets: [guides/scalability_best_practices.md](guides/scalability_best_practices.md).

## 12. Animation and rendering

- **Crowd band (VAT):** bake clips with the engine's AnimToTexture, fill `VertexAnimationClips`, and use a
  material that turns custom-data channels 10-12 into a frame. The plugin ships no material or bake script.
  [guides/vat_animation_and_status.md](guides/vat_animation_and_status.md).
- **Near band:** set the five state clips, or leave them empty and set `AnimationBlueprintClass` to an ABP
  parented to `UMassUnitAnimInstance`.
- **Custom-data channels** (`UE::MassUnitSystem::CrowdCustomData`): 0 animation index, 1 animation start time,
  2 LOD, 3 team ID, 4-6 team RGB, 7 health 0-1, 8 random seed, 9 speed, 10-12 clip start/frames/fps.
- **Niagara instead of ISM:** set `DefaultNiagaraSystem`. The plugin writes `UnitCount` (int); `UnitPositions`,
  `UnitVelocities`, `UnitScales`, `UnitTeamColors` (vector arrays); `UnitRotations` (quat array);
  `UnitTeamIDs`, `UnitAnimationIndices`, `UnitAnimationTimes`, `UnitLODLevels`, `UnitVisibilityFlags` (float arrays).
- **Art:** [guides/authoring_pipeline.md](guides/authoring_pipeline.md), [guides/artist_brief.md](guides/artist_brief.md).

## 13. Behavior Trees (selective)

`GetBehaviorIntegration → SetBehaviorTree(Unit, Tree, Blackboard)` creates transient components for that unit
only. Keys written when present: `Position`, `State`, `StateTime`, `UnitLevel`, `Health`, `TargetLocation`,
`TargetPriority`, `HasTarget`, `TeamID`. Keys read: `RequestedTargetLocation`, `HasRequestedTarget`,
`RequestedTargetPriority`, `ClearTarget` (consumed), `RequestedState`. `ExecuteBTTask` writes the tag name to a
Name key `RequestedTask`.

## 14. Debugging

There are no console variables yet (a `dll.MassUnits.*` set is owed).

| Tool | Where |
|---|---|
| Cost | `stat MassUnitSystem`; CSV category `MassUnits` (`CsvProfile Frames=N` after load); Insights scopes. Stats compile out of Shipping. |
| Spawner debug | `bEnableVisualDebug`, `DebugDuration`: spawn boxes, pattern outline, destinations |
| Crowd debug | `CrowdConfig.bEnableVisualDebug` (white bounds, cyan destinations/corridors, orange interactions) |
| Engagement debug | `PlayerEngagementConfig.bEnableVisualDebug` (purple corridor; red swinging / yellow waiting spheres), `bDrawUnitDestinations` |
| Collective debug | `CollectiveConfig.bEnableVisualDebug` + `bDebugDrawSignals`, `bDebugDrawGroupStatus`, `bDebugDrawThreat`, `bDebugDrawScreenHUD` (HUD keys 7300-7309) |
| Counts | `GetUnitCount`, `GetValidSpawnedUnitCount`, `GetCrowdStats`, `GetCrowdGroupCount`, `GetQueuedRequestCount`, `GetAirborneUnitCount` |
| Rendering | `IsUsingNiagara`, `GetInstancedMeshComponentCount` (≈ distinct meshes), `GetInstancedMeshInstanceCount`, `GetInstancedMeshAllocatedSlotCount`, `GetInstancedMeshTopologyRevision` (stable while units move), `GetInstancedMeshCustomDataFloatCount` |
| Near band | `GetActiveSkeletalMeshCount`, `GetAvailableSkeletalMeshCount`, `GetSkeletalMeshCapacity` |
| Log | `LogMassUnitSystem` (warnings only; no per-frame logging) |

`bEnableDebugVisualization` in Project Settings exists but nothing reads it yet; use the per-spawner/per-config
toggles above.

## 15. Troubleshooting

| Symptom | Check |
|---|---|
| Plugin won't compile | UE 5.7 and a matching toolchain; `.uplugin` directly inside one `Plugins/` subfolder; regenerate project files; delete only the plugin's `Binaries`/`Intermediate`; `build_plugin.bat` isolates Editor/Game/Shipping/UHT errors |
| Spawner missing in Place Actors | Plugin enabled and compiled; restart the editor |
| Subsystem is null | Call from a Game/PIE/Game Preview world object |
| Unit count is zero / creation fails | Spawn from a game world; `MaxUnits` reached; valid template; `IsUnitValid`; `LogMassUnitSystem` warnings |
| Units exist but are invisible | A local player/spectator view exists; within `MaxVisibleDistance`; `bEnableInstancedMeshFallback` on; `GetInstancedMeshInstanceCount` > 0 means data was submitted |
| Units flicker while moving | Use 1.2.1+; `GetInstancedMeshTopologyRevision` should stay stable |
| Units move once and stop | Default move is a one-shot smoke test; enable crowd simulation; check pause, interaction, `SleepingUnits` / `MaxSimulationDistance` |
| Units don't move at all | Destination or path set; nav data built or direct fallback on; dead/stunned units don't move |
| Units float or sink | `SpawnHeight` and `NavigationHeightOffset` must match the mesh pivot (0 for a feet pivot) |
| Units clip through hills | Planar 2D + `bUseNavigation` + full green navmesh + `bConformToNavmeshHeight` |
| Engagement on but units wander | Always/Proximity need a possessed pawn (Play, not Simulate); interaction mode needs a notify/damage call; `IsCrowdGroupEngaged`, `GetCrowdGroupTargetActor` |
| Units follow but never hurt the GAS player | Set `GameplayEffectToTarget` or handle `OnCrowdAttackRequested`; don't double up with `bApplyActorDamage` |
| Unit-vs-unit combat does nothing | Different `TeamID`s, `SetUnitTarget`, in range, cooldown elapsed |
| GAS bridge calls return false | Register a valid externally owned ASC for the unit first |
| Crowd too expensive | Raise `CrowdUpdateInterval` / LOD multipliers, lower `MaxCrowdUnitsPerUpdate`, set `MaxSimulationDistance`, keep radii near the cell size, cut `MaxVisibleDistance`; never an ASC/BT/Actor per ambient unit |
| Headless automation crashes on first tick | Some UE 5.7.2 installs fail with `-NullRHI`; use `-RenderOffscreen` with a real RHI; don't add `-NoShaderCompile` on a fresh host |
