# DLL Mass Units

**dll-mass-units** (plugin `MassUnitSystem`, displayed as *DLL Mass Units*) is a Digi Logic Labs plugin, open source under
[Apache-2.0](LICENSE). It was published as `ue5-mass-units-plugin` before 2026-10; the old GitHub URL redirects.

It is a Blueprint- and C++-ready foundation for large groups of lightweight gameplay units using Unreal Engine's native Mass Entity framework.

Version **1.4.0** is verified with **Unreal Engine 5.7.2** on Windows for Editor, Development, and Shipping targets.

## What works out of the box

- Native, world-owned Mass entities - no Actor or UObject per unit
- A placeable **Mass Unit Spawner** with Blueprint-writable controls
- Visible engine-cube fallback with no project assets required
- Instanced static-mesh rendering for large unit counts
- Movement, targets, teams, lightweight combat, formations, and batched navigation
- Continuous planar or free-3D crowd wandering, spatial-hash separation, random speed/idle variation, and interaction events
- Budgeted behavior LOD, staggered distance visibility, simulation sleeping, and a bounded skeletal-mesh pool
- Opt-in player acquisition, group-shared nav corridors, efficient moving-target follow/repath, cooldown attacks, and interaction/damage activation
- Lightweight native health, healing, death events, and nearest/radius queries exposed to Blueprint
- Standard Actor damage plus optional target Gameplay Effects for GAS characters
- Optional Niagara, selective Gameplay Ability System, and Behavior Tree integrations
- Automatic world-subsystem initialization and deterministic cleanup

## 1. Install

Clone or copy the repository below the project's `Plugins` directory:

```powershell
Set-Location 'YourProject\Plugins'
git clone https://github.com/DigiLogicLabs/dll-mass-units.git dll-mass-units
```

```text
YourProject/
  Plugins/
    dll-mass-units/
      MassUnitSystem.uplugin
```

The folder name is free; Unreal finds the plugin by its `.uplugin`. In MetalLegs it is checked out as
`Plugins/ue5-mass-units-plugin/` until the planned standard rename moves it to `Plugins/dll-mass-units/`
(plugin `DLLMassUnits`, modules `DLLMassUnitsRuntime/Editor`, with CoreRedirects; see `docs/DECISIONS.md`).

Then:

1. Open the `.uproject`.
2. Allow Unreal to compile the plugin when prompted.
3. Open **Edit > Plugins > Gameplay** and confirm **Mass Unit System** is enabled.
4. Restart the editor after the first compile or any C++ plugin update.

The descriptor automatically enables the required engine plugins: `MassGameplay`, `Niagara`, and `GameplayAbilities`. No third-party plugin is required. A Blueprint-only host still needs an Unreal-supported C++ toolchain to compile this source plugin.

## 2. See 25 moving units in a level

This is the standard installation test. It needs no Data Asset, mesh, Niagara system, Mass Entity Config, or Unreal Mass Spawner.

1. Open a playable map, or choose **File > New Level > Basic**.
2. Open **Place Actors** and search for **Mass Unit Spawner**.
3. Drag **Mass Unit Spawner** onto the floor in the visible play area.
4. Leave `Unit Template` empty.
5. Press **Play**. Use **Simulate** for a truly empty level without a player pawn.

Default result:

- 25 native Mass entities spawn in a centered 5 x 5 grid.
- Engine cubes render through one stable dynamic-ISM fallback path.
- Units move 1,500 cm along the spawner's local positive X direction.
- Grid spacing is preserved while moving.
- Stopping play removes only the entities owned by that spawner.

If the grid is outside the camera, select the spawner and press **F** before Play to frame it. The cyan editor arrow shows the default movement direction. Set `Enable Visual Debug` on the spawner to draw cyan spawn boxes and green destination arrows.

## 3. Turn the smoke test into a continuous crowd

The default 1,500 cm move intentionally stops. It is the deterministic installation test. For ongoing crowd behavior:

1. Select the placed **Mass Unit Spawner**.
2. Enable **Crowd > Enable Crowd Simulation**.
3. Press **Play** or **Simulate**.

`Enable Crowd Simulation` takes precedence over `Move On Begin Play`. With the default crowd settings, units continuously choose deterministic-random destinations inside a 1,500 cm radius, vary their speed and idle duration, separate from nearby units through a spatial hash, and occasionally pause in pairs. Large groups are deterministically partitioned into lightweight managed subgroups (32 units by default). When navigation is enabled, each subgroup shares an ambient corridor instead of requesting one path per entity. No Behavior Tree, StateTree, collision component, Actor, or UObject is created per unit or subgroup.

Useful per-spawner crowd controls:

- `Movement Mode`: `Planar 2D` for ground/navmesh crowds or `Free 3D` for flying, swimming, and volumetric groups
- `Wander Radius`, `Min Wander Distance`, idle range, move timeout, and speed-multiplier range
- `Enable Separation`, `Separation Radius`, and `Separation Weight`
- `Enable Managed Subgroups`, `Managed Subgroup Size`, subgroup wander scale, and shared-path look-ahead
- `Conform To Navmesh Height` plus `Navigation Height Offset` for ground-following Planar 2D units
- `Enable Interactions` plus interaction chance/radius/duration; bind `On Crowd Interaction Started` on the world crowd system
- `Enable Group Cue Events` plus `Group Cue Cooldown`; bind `On Crowd Cue Requested` once to pooled audio/Niagara presentation
- `Max Simulation Distance`: puts ambient units to sleep when every player observer is farther away; zero disables sleeping
- `Random Seed`: repeatable choices per native entity handle
- `Enable Visual Debug`: white group bounds, cyan destinations, and orange interaction links

Blueprint functions `Start Crowd Simulation`, `Stop Crowd Simulation`, and `Force Crowd Update` are available on the spawner. Advanced managers can register any handle array through `Get Crowd System -> Register Crowd Group` and use pause, center, force-update, statistics, and interaction-event APIs.

`Free 3D` uses a three-dimensional spatial hash, sphere-volume destinations, XYZ separation, and pitched movement. Unreal's standard navmesh is planar, so `Use Navigation` is intentionally bypassed for a free-3D group; movement remains direct and fully 3D. `Planar 2D` is navmesh-compatible and can follow corridor elevation while remaining upright.

For hills, ramps, bridges, or obstacles, add a `NavMeshBoundsVolume`, build navigation, press **P** to confirm green coverage, and enable `Use Navigation` on the spawner. Keep `Conform To Navmesh Height` enabled and set `Navigation Height Offset` to the distance from the mesh pivot to its floor contact (50 cm for the default 100 cm cube). Direct fallback deliberately remains a cheap straight line and cannot discover terrain.

## 4. Make the crowd react to a player

Player engagement is opt-in and does not change an existing ambient crowd. The quickest chase test is:

1. Select the **Mass Unit Spawner**.
2. Enable `Enable Crowd Simulation`.
3. Enable `Enable Player Engagement`.
4. Set `Activation Mode` to `Always Acquire Player`.
5. For a visual-only test, disable `Apply Actor Damage`.
6. Press **Play** with a possessed player pawn.

The group acquires the nearest player pawn, stays simulation-awake while that target is valid, follows one group-level target sample through deterministic spread slots, and attacks after entering range. There is no per-unit perception raycast. Representation distance LOD and project-wide update budgets remain active. **Simulate** may not create a possessed pawn; use Play or call `Activate Crowd Against Actor` explicitly.

### Activation modes

| Mode | Behavior |
|---|---|
| `Manual Blueprint Call` | Waits for `Activate Crowd Against Actor` or `Activate Crowd Group For Actor` |
| `Player Interaction or Damage` | `Notify Spawned Unit Interacted` activates the group; `Damage Spawned Unit And Activate` also damages native health |
| `Player Proximity` | Acquires the nearest possessed player inside `Activation Radius` |
| `Always Acquire Player` | Acquires the nearest available possessed player; useful for an immediate chase test |

`Auto Deactivate` releases an interaction/proximity/manual target when no living group member remains within `Deactivation Distance`. `Return To Wander On Deactivate` resumes ambient decisions; disable it to hold the group idle until it is activated again. Always-acquire groups intentionally keep their available player target.

### Interaction or player-hit pattern

The zero-asset dynamic-ISM renderer deliberately uses no per-instance collision. It therefore does not create thousands of collision bodies or return a Mass handle from a cube hit. Resolve a world interaction point from the project's cursor, ground trace, weapon sweep, targeting volume, or selection system, then use the nearest-unit query:

```text
Player interaction / weapon impact world location
  -> Mass Unit Spawner: Find Closest Spawned Unit
     (or Get Mass Unit Subsystem -> Get Crowd System -> Find Closest Crowd Unit)
  -> Is Unit Valid
  -> Notify Spawned Unit Interacted
     or Damage Spawned Unit And Activate
```

Use a small `Max Distance` appropriate to the unit footprint so a miss does not select a distant entity. Choose planar distance for ground games and 3D distance for flying/swimming targets. For custom renderers that already provide a handle, skip the nearest query and call the interaction/damage function directly.

The equivalent advanced group APIs are:

- `Configure Crowd Group Engagement`
- `Activate Crowd Group For Actor`, `Deactivate Crowd Group Engagement`
- `Notify Unit Interacted`, `Damage Unit And Activate`
- `Find Closest Crowd Unit`, `Is Crowd Group Engaged`, `Get Crowd Group Target Actor`
- `Get Crowd Group Subgroup Count`, `Get Unit Subgroup Index`, `Get Crowd Subgroup Units`
- `On Crowd Engagement Started`, `On Crowd Engagement Ended`, `On Crowd Attack Requested`, `On Crowd Cue Requested`

### Follow, pathing, and attack controls

| Control | Default | Purpose |
|---|---:|---|
| `Target Refresh Interval` | 0.2 s | Samples the target once per group instead of once per entity per frame |
| `Repath Distance` | 125 cm | Rebuilds a unit destination/path only after meaningful target movement |
| `Use Shared Navigation Path` | On | Uses one navmesh corridor per group instead of one path query per entity |
| `Shared Path Repath Interval` | 0.5 s | Caps expensive group navmesh path refresh frequency |
| `Shared Path Repath Distance` | 250 cm | Requires meaningful target movement before rebuilding the shared corridor |
| `Shared Path Look Ahead Distance` | 350 cm | Advances followers along the corridor while local separation keeps spacing |
| `Follow Distance` | 150 cm | Base deterministic stand-off radius around the player |
| `Follow Spread` | 125 cm | Prevents all units from collapsing onto one location |
| `Engaged Move Speed Multiplier` | 1.1 x | Multiplies the original template move speed during engagement |
| `Enable Attacks` | On | Uses each native unit's cooldown and range for attack requests |
| `Attack Range Override` | 0 cm | Zero uses the `UnitTemplate` attack range; positive values override the group |
| `Damage Multiplier` | 1.0 x | Scales template base damage and unit level |
| `Apply Actor Damage` | On | Routes damage through Unreal's `AnyDamage` pipeline |
| `Gameplay Effect To Target` | Empty | Optionally applies a GAS Gameplay Effect when the target exposes an ASC |

Planar groups can enable `Use Navigation`. Engagement then calculates one navmesh corridor from the living group centroid and shares its look-ahead points with every entity; local spatial-hash separation and deterministic final slots provide individual motion. A final follow slot is projected onto the navmesh once so spread remains terrain-aligned without building an individual path or issuing a ground raycast. Disable `Use Shared Navigation Path` only when units truly require independent routes; those requests remain bounded by `Max Path Requests Per Frame`. Free-3D groups use direct XYZ steering because the engine navmesh is planar. All groups still use the project-wide `Max Crowd Units Per Update` round-robin budget.

Engagement visual debug draws one purple group target/corridor by default. `Draw Unit Destinations` is an advanced opt-in that draws a red arrow for every entity; those arrows are debug geometry, not traces or perception queries.

### Native health and GAS ownership

Every unit has lightweight native `Health`, `Max Health`, damage, attack range, and cooldown fragments. Blueprint can use `Get Unit Health`, `Get Unit Health Percent`, `Apply Damage`, `Heal Unit`, `On Unit Health Changed`, and `On Unit Died` without creating an Actor or Ability System Component per entity. `Find Closest Unit` and `Get Units In Radius` support either planar or 3D distance.

For a GAS player target, assign `Gameplay Effect To Target`; the plugin finds the target Actor's ASC and applies the effect on authority. If that Gameplay Effect owns health damage, disable `Apply Actor Damage` to avoid applying damage twice. `On Crowd Attack Requested` always fires and is the extension point for project-specific hit reactions, cues, abilities, or damage routing.

Unpromoted Mass units use native health as their source of truth. The existing `GAS Unit Integration` remains a selective bridge to an externally owned ASC for elite/near-player units; it does not allocate an ASC per crowd entity or automatically mirror native health into GAS attributes.

## 5. Use your own unit mesh and stats

1. In the Content Browser choose **Add > Miscellaneous > Data Asset**.
2. Select `UnitTemplate`.
3. Name it, for example `DA_Unit_Infantry`.
4. Set health, damage, attack range/cooldown, move speed, team, and visual assets.
5. Assign the Data Asset to the placed spawner's `Unit Template` field.

Rendering selection is automatic and bounded:

1. The closest eligible units may use the template skeletal mesh while pool capacity is available.
2. Remaining units use the template static mesh, the project fallback static mesh, or the engine cube.
3. A configured Niagara renderer may consume the documented `Unit*` arrays instead of the ISM fallback.

For skeletal units, optionally assign an Animation Blueprint and/or Idle, Move, Attack, Death, and Stun clips. Explicit state clips take precedence. Skeletal entry/exit hysteresis prevents representation flicker near the distance boundary. For VAT/static-mesh materials, the ISM fallback supplies eight per-instance floats in this order: animation index, animation time, visual LOD, team id, team R, team G, team B, and health percent. Animation indices come from the template's registered `Animation Tags`, not the unit-state enum ordinal.

## Blueprint setup

### Recommended: Blueprint subclass of the spawner

Create a Blueprint with parent class `MassUnitSpawner`. Its setup and optimization fields are Blueprint-writable:

- `Unit Template`, `Unit Count`
- `Columns`, `Unit Spacing`, `Spawn Height`
- `Spawn On Begin Play`, `Move On Begin Play`, `Destination Offset`
- `Enable Crowd Simulation`, `Movement Mode`, and all nested crowd behavior/LOD controls
- `Enable Player Engagement` and all nested activation/follow/combat controls
- `Use Navigation`, `Acceptance Radius`
- `Spawn On Authority Only`, `Destroy Spawned Units On End Play`
- `Enable Visual Debug`, `Debug Duration`

Useful Blueprint functions:

- `Spawn Units`
- `Move Spawned Units By Offset`
- `Command Spawned Units To Location`
- `Start Crowd Simulation`, `Stop Crowd Simulation`, `Force Crowd Update`
- `Activate Crowd Against Actor`, `Deactivate Crowd Engagement`
- `Find Closest Spawned Unit`, `Notify Spawned Unit Interacted`, `Damage Spawned Unit And Activate`
- `Get Valid Spawned Units`
- `Get Valid Spawned Unit Count`
- `Destroy Spawned Units`

The spawner is only an ownership/bootstrap Actor. It does not tick and it does not represent each unit with an Actor.

### Direct subsystem pattern

Use this in a wave manager, encounter actor, Game Mode, or Level Blueprint:

```text
Event BeginPlay
  -> Get Mass Unit Subsystem
  -> Get Unit Manager
  -> Create Default Unit          (asset-free test)
     or Create Unit From Template (production data)
  -> store MassUnitHandle
  -> Set Unit Destination
```

Keep returned handles in the system that owns the wave. Before reusing a long-lived handle, call `Is Unit Valid`. Call `Destroy Unit` when that owner ends.

The subsystem is a `UTickableWorldSubsystem`; never construct it manually or add it to a Game Instance.

## Scale and optimization controls

Project-wide settings are under **Project Settings > Plugins > Mass Unit System**. Blueprints can read them through `Get Mass Unit System Settings`; per-spawner controls are Blueprint-readable and writable.

| Control | Default | Purpose |
|---|---:|---|
| `Max Units` | 10,000 | Hard safety cap for plugin-owned units in one world |
| `Visual Update Interval` | 0.033 s | ISM/Niagara upload rate; increase to reduce visual update cost |
| `Crowd Update Interval` | 0.1 s | Base rate for behavior decisions and spatial steering; smooth Mass movement remains independent |
| `Max Crowd Units Per Update` | 500 | Round-robin budget for crowd decisions during one timer callback |
| `Max Shared Path Builds Per Crowd Update` | 8 | Caps newly built engagement/subgroup corridors during one crowd update |
| `Crowd Spatial Cell Size` | 200 cm | Spatial-hash cell size for local separation and interaction searches |
| `LOD Distance Thresholds` | 500/1,500/3,000/6,000 cm | Distance bands written to unit visual LOD data |
| `Visibility LOD Update Intervals` | 0.05/0.1/0.2/0.5/1.0 s | Staggered distance/culling refresh rates from near to far |
| `Crowd Simulation LOD Distances` | 2,500/5,000/10,000 cm | Player-observer bands that reduce ambient decision frequency |
| `Crowd Simulation LOD Interval Multipliers` | 1/2/4/8 x | Behavior update scaling for successive distance bands |
| `Max Visible Distance` | 10,000 cm | Excludes farther units from visual submission; zero disables culling |
| `Max Skeletal Mesh Units` | 100 | Caps close-range skeletal components; set to zero for instanced-only use |
| `Skeletal Mesh Distance` | 300 cm | Distance inside which eligible units request skeletal representation |
| `Skeletal Mesh Hysteresis` | 1.2 x | Keeps an existing skeletal owner until it crosses the wider exit boundary |
| `Max Path Requests Per Frame` | 100 | Limits asynchronous navmesh requests submitted per world tick |
| `Enable Instanced Mesh Fallback` | On | Makes units visible without a custom Niagara system |
| `Fallback Static Mesh` | Empty | Optional project-wide mesh used when a template has no static mesh |
| `Default Niagara System` | Empty | Optional custom GPU renderer; dynamic ISM remains the zero-setup default |
| `Fallback To Direct Path` | On | Keeps movement functional when no navmesh data exists |

Blueprint diagnostics:

- `Get Unit Count`: number of valid plugin-owned Mass entities
- `Get Valid Spawned Unit Count`: valid units owned by one spawner
- `Is Using Niagara`: whether custom Niagara rendering is active
- `Get Instanced Mesh Component Count`: allocated fallback mesh groups
- `Get Instanced Mesh Instance Count`: currently submitted fallback instances
- `Get Instanced Mesh Topology Revision`: changes only when slots are added/removed; it remains stable while units move
- `Get Instanced Mesh Custom Data Float Count`: eight for the standard animation/LOD/team/health material layout
- `Get Active/Available Skeletal Mesh Count` and `Get Skeletal Mesh Capacity`: bounded close-range representation use
- `Get Queued Request Count`: queued and in-flight navigation requests
- `Get Crowd Stats`: registered/managed-subgroup/active/sleeping/engaged counts, update budget usage, destinations, interactions, attacks, ambient/engagement shared-path builds, per-unit path requests, and neighbor checks

Safe starting pattern:

1. Prove the install with 25 default cubes and direct movement.
2. Assign one static mesh and test 100 units.
3. Enable crowd simulation and increase through 100, 500, and 1,000 while watching `Get Crowd Stats` and Unreal Insights.
4. Tune crowd budgets, behavior LOD, maximum simulation distance, visual range, and representation for the target hardware.
5. Add navmesh routing, skeletal units, Niagara, GAS, or Behavior Trees one feature at a time.
6. Keep actor-backed GAS/Behavior Tree integration selective instead of attaching it to every entity.

`Max Units` is a safety cap, not a performance guarantee. Mesh complexity, materials, navigation, gameplay logic, platform, and camera coverage determine the real budget.

## Navigation and formations

Direct movement works without a Nav Mesh Bounds Volume.

For obstacle- and terrain-aware Planar 2D movement:

1. Add a **Nav Mesh Bounds Volume**.
2. Resize it over every intended hill, ramp, bridge, and destination.
3. Build navigation and press **P** to verify continuous green coverage.
4. Enable `Use Navigation` on the spawner, or call `Request Path` from Blueprint.
5. Keep `Conform To Navmesh Height` on and set the mesh-pivot `Navigation Height Offset` (50 cm for the default cube).
6. Start with managed subgroups enabled at 32 units and inspect `Get Crowd Stats`: a 25-unit ambient test should build one subgroup corridor and queue zero per-unit paths.

The navmesh corridor carries Z elevation into Planar 2D movement without a downward trace per entity. Free 3D groups intentionally use direct XYZ steering; projects needing volumetric obstacle avoidance should supply a project-specific 3D navigation strategy.

Formation shapes are `Rectangle`, `Line`, `Column`, `Wedge`, and `Circle`. Use the formation service when a group should retain assigned slots while moving.

## Verify the plugin

Open **Tools > Test Automation**, filter for `MassUnitSystem`, and run:

```text
MassUnitSystem.Core.NativeMassLifecycle
```

The test covers native entity creation, independent fragments, health/healing and spatial queries, combat, path fallback/cancellation, nav-height planar movement, free-3D movement, formations, deterministic managed subgroups, one ambient corridor and one engagement corridor with zero per-unit requests, pause/resume, paired interactions, player interaction/damage activation, moving-target follow refresh, attack requests, eight-float ISM data, asset-free defaults, stable ISM updates, destruction, and safe world teardown.

## Quick troubleshooting

**Mass Unit Spawner does not appear in Place Actors**

- Confirm the plugin compiled and is enabled.
- Restart the editor after adding/updating the source plugin.

**Unit count is zero**

- Spawn only from Game, PIE, or Game Preview worlds.
- Check `Max Units` and `Get Valid Spawned Unit Count`.

**Units exist but are invisible**

- Confirm a local player/spectator view exists.
- Check `Max Visible Distance` and `Enable Instanced Mesh Fallback`.
- Call `Get Instanced Mesh Instance Count`; a positive value means rendering data was submitted.

**Units do not move**

- Keep `Use Navigation` off for the first test.
- With navigation enabled, add/build a Nav Mesh Bounds Volume or keep direct-path fallback enabled.

**Ground units clip through hills or keep their original Z**

- Use `Planar 2D`, enable `Use Navigation`, and confirm the entire route is green with **P**.
- Keep `Conform To Navmesh Height` enabled and set `Navigation Height Offset` for the mesh pivot.
- A direct fallback path cannot infer terrain; fix navmesh coverage instead of relying on fallback for hills.

**Engagement is enabled but units keep wandering**

- `Always Acquire Player` and `Player Proximity` require a possessed player pawn; use Play, not Simulate, for the automatic test.
- In interaction mode, call `Notify Spawned Unit Interacted` or `Damage Spawned Unit And Activate` with a valid Actor.
- Use `Is Crowd Group Engaged` and `Get Crowd Group Target Actor` to verify acquisition.

**Units follow but never damage the GAS player**

- Set `Gameplay Effect To Target` to a damage Gameplay Effect accepted by the player's ASC.
- Or handle `On Crowd Attack Requested` in the project damage pipeline.
- Disable `Apply Actor Damage` when the Gameplay Effect already owns health damage.

More cases are covered in [docs/troubleshooting.md](docs/troubleshooting.md).

## Build the plugin in isolation

Set `UE_ENGINE_PATH` to the Unreal Engine root and run:

```powershell
$env:UE_ENGINE_PATH = 'D:\Unreal Engine\UE_5.7'
.\build_plugin.bat
```

```bash
UE_ENGINE_PATH=/path/to/UnrealEngine ./build_plugin.sh
```

The package build checks Editor Development, Game Development, Game Shipping, and Unreal Header Tool processing.

## More documentation

- [Five-minute quick start](docs/quick-start.md)
- [Setup](docs/setup.md)
- [Usage and integration](docs/usage.md)
- [Public API](docs/api.md)
- [Architecture](docs/architecture.md)
- [Scalability best practices](docs/scalability_best_practices.md) — the rules for building at crowd scale, with rationale
- [Authoring pipeline (Blender to crowd unit)](docs/authoring_pipeline.md) — rig, budgets, LODs, VAT bake
- [Artist brief](docs/artist_brief.md) — self-contained spec sheet to hand a modeller
- [Troubleshooting](docs/troubleshooting.md)

## Intentional boundaries

- The repository does not bundle production art, VAT textures, or a Niagara asset.
- VAT authoring/decoding remains project-specific.
- Mass entities are not replicated as individual Actors; multiplayer representation/replication is project-specific.
- UE 5.7.2 is the validated baseline. Recompile and test for another engine minor version.

## License

Copyright Digi Logic Labs LLC. Licensed under the [Apache License, Version 2.0](LICENSE); see [NOTICE](NOTICE).
All source in this repository is original Digi Logic Labs work. Unreal Engine and its Mass Entity framework are
Epic Games products used under the Unreal Engine EULA and are not part of this repository.
