# DLL Mass Units — Testing

**Rule:** never claim "compiles" or "tested" without an artifact (build log line, test report, PIE note).
`UNTESTED` is a valid, honest status.

## 1. Standalone build (every change)

```powershell
& "<UE>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin `
  "-Plugin=<repo>\MassUnitSystem.uplugin" "-Package=<short temp>\mu_pkg" -TargetPlatforms=Win64 -Rocket
# or, with UE_ENGINE_PATH set:  build_plugin.bat [OutputDir] [Platform] [Configuration]
#                               UE_ENGINE_PATH=/path/to/UE ./build_plugin.sh [out] [platform] [config]
```

Pass = `BUILD SUCCESSFUL`. This builds the plugin in an isolated host project (Editor, Game, Shipping, UHT),
which proves it has no hidden dependency on any adopting game. Keep `-Package` short (260-character path limit);
only one UnrealBuildTool may run per engine install.

## 2. Automation tests

All tests live in `Source/MassUnitSystemEditor/Private/Tests/` (prefix `MassUnitSystem.`; `DLLMassUnits.` after
the rename). They build their own world, so they need no map or content.

| Test | Covers |
|---|---|
| `MassUnitSystem.Core.NativeMassLifecycle` | Native entity creation, independent fragments, health/healing, spatial queries, combat, path fallback/cancellation, navmesh-height planar and free-3D movement, formations, managed subgroups, one ambient and one engagement corridor with zero per-unit requests, pause/resume, interactions, interaction/damage activation, moving-target follow, attack requests, ISM custom data, asset-free defaults, stable ISM updates, destruction, world teardown |
| `MassUnitSystem.Crowd.CollectiveBehavior` | Alerts recruit in range only, morale shock routs and units flee, fear, threat retargeting with hysteresis, damage engages groups, groups without the config keep old behaviour |
| `MassUnitSystem.Units.StatusAndTags` | Hierarchical template tags, DoT ticks and expiry, stuns and slows apply and lift, immunity, refresh and stacking, cleanse by parent tag, death clears statuses |
| `MassUnitSystem.Units.AnimationContract` | VAT clip fallbacks, clip-table validation, per-swing restart, impact delay |
| `MassUnitSystem.Units.Launch` | Launch refusal (scale 0, immunity), flight, knockdown duration, corpse tumble/bounce/settle, ragdoll request, events |
| `MassUnitSystem.Units.PresentationReplication` | Variant seeds, `GetUnitPresentation` swing counter, `ApplyPresentationState` (no damage queued, corpses never revived) |
| `MassUnitSystem.Spawner.PatternLayouts` | Every spawn pattern, and parity between spawner layouts and the static helpers |
| `MassUnitSystem.Spawner.BatchSpawn` | Null-template batch, per-unit transforms, team override and team index cleanup, seeded variants, `MaxUnits` clipping |
| `MassUnitSystem.Units.Stacking` | Climbing onto supports, stack levels and roles, supports frozen, cascade collapse when a support dies or is launched |
| `MassUnitSystem.Crowd.MotionStyles` | Wander, Flock, Swarm and Hive ambient movement; `AddUnitsToCrowdGroup` without a reset |
| `MassUnitSystem.Crowd.StackedAttackers` | Units in a pile attack too, on their own token pool |
| `MassUnitSystem.Units.Commands` | Move orders in formation, stop, hold, release, `OnMoveOrderFinished`, crowd hand-off |
| `MassUnitSystem.Units.Looks` | Seed-rolled body variants, parts, scale and tint; byte-precision determinism |
| `MassUnitSystem.Units.Attitudes` | Seed-rolled pace, attack rhythm, tempo, courage and climb willingness |
| `MassUnitSystem.Crowd.BehaviorPresets` | Registering from a preset, swapping a live group's preset |
| `MassUnitSystem.Crowd.SwarmTypes` | Ground and Air swarm types, the altitude band and its destinations, determinism, flyer death falls, hover-or-dive while engaged |
| `MassUnitSystem.Units.Leap` | `LeapUnit` arc and landing, refusals, a knockback mid-leap, pounces limited by the token pool and landing hits |

Run in editor: *Tools → Session Frontend → Automation* (or *Tools → Test Automation*), filter `MassUnitSystem`.
Headless:

```powershell
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests MassUnitSystem; Quit" -unattended -nullrhi -nosplash
```

If the first tick crashes with `-nullrhi` (seen on some UE 5.7.2 installs), use `-RenderOffscreen` instead.
New reflected types need a full editor build with the editor closed before the tests can see them.

Report the exact command and pass/fail counts. Add a test for every pure-logic change; the test world does not
tick, so drive time explicitly (as the launch and collective tests do).

## 3. Manual checks (PIE)

| Check | How | Pass |
|---|---|---|
| Install smoke | Place a **Mass Unit Spawner**, empty template, Play | 25 cubes move 1,500 cm; End Play removes them |
| Continuous crowd | `bEnableCrowdSimulation` | Units keep wandering, separate, pause in pairs |
| Terrain | Navmesh over a hill, `bUseNavigation`, `NavigationHeightOffset` 50 | Units follow the slope within a few cm of terrain + offset |
| Engagement | `Always Acquire Player`, move the pawn | One purple corridor, follow slots, attack requests on cooldown |
| Attack tokens | `MaxSimultaneousAttackers` 3, engagement debug on | At most 3 red spheres; others yellow at the standoff ring |
| Custom mesh | Template with a feet-pivot static mesh, `SpawnHeight` 0 | Units on the ground, tinted by team, topology revision stable while moving |
| Near band | Skeletal mesh + clips; walk across `SkeletalMeshDistance` | No flicker at the boundary; active count ≤ capacity |
| Cost | `stat MassUnitSystem` | Counters move with unit count; launches/statuses ~0 when unused |
| Net modes | Listen + dedicated PIE | Plugin adds no replicated actors; units spawn only on authority |

## 4. Recorded results

| Item | Result |
|---|---|
| 1.4.0 package | UAT `BuildPlugin` UE 5.7: Editor Development, Game Development, Game Shipping, UHT all succeeded (2026-07-14) |
| 1.4.0 automation | `MassUnitSystem.Core.NativeMassLifecycle` from the packaged plugin: 1 pass, 0 warnings, 0 errors |
| 1.4.0 PIE | In the reference adopter: 25 cubes traversed a hill within 5.74 cm of terrain + 50 cm; one shared engagement corridor, zero per-unit path requests, attack requests (transient PIE, 2026-07-14) |
| Unreleased automation | 2026-10-16, reference adopter (MetalLegs), Development editor, headless: all 8 `MassUnitSystem.*` tests `Success`, `EXIT CODE: 0` |
| Unreleased automation (2026-10-18) | Reference adopter (MetalLegs), Development editor, headless `MassUnitSystem+MetalLegs.Combat.Crowd+MetalLegs.MassStress`: 28/28 `Success` (all `MassUnitSystem.*` tests above) |
| Unreleased standalone build | 2026-10-18: `RunUAT BuildPlugin` `BUILD SUCCESSFUL` (swarm types, flyers, leapers) |
| Unreleased PIE / network | UNTESTED in this repo |
