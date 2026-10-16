# DLL Mass Units — Testing

**Rule:** never claim "compiles" or "tested" without an artifact (build log line, test report, PIE note).
`UNTESTED` is a valid, honest status.

## 1. Standalone build (every change)

```powershell
& "<UE>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin `
  "-Plugin=<repo>\MassUnitSystem.uplugin" "-Package=<short temp>\mu_pkg" -TargetPlatforms=Win64 -Rocket
# or: build_plugin.bat <out> Win64 Development   (needs UE_ENGINE_PATH)
```

Pass = `BUILD SUCCESSFUL`. This builds the plugin in an isolated host project, which proves it has no hidden
dependency on any adopting game. Keep `-Package` short (260-character limit).

## 2. Automation tests

| Test | Status |
|---|---|
| `MassUnitSystem.Core.NativeMassLifecycle` | on `main` |
| `MassUnitSystem.Spawner.BatchSpawn`, `MassUnitSystem.Spawner.PatternLayouts` | arriving (in-progress work) |
| `MassUnitSystem.Crowd.CollectiveBehavior` | arriving |
| `MassUnitSystem.Units.Launch`, `.StatusAndTags`, `.AnimationContract`, `.PresentationReplication` | arriving |

Tests live in `Source/MassUnitSystemEditor/Private/Tests/`. Run in editor: *Tools → Session Frontend →
Automation*, filter `MassUnitSystem`. Headless:

```powershell
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests MassUnitSystem; Quit" -unattended -nullrhi -nosplash
```

Report the exact command and pass/fail counts. Update this table when a test lands or is renamed.

## 3. Manual checks

| Check | How | Pass |
|---|---|---|
| Install smoke | README section 2: place a **Mass Unit Spawner**, empty template, PIE | 25 cubes move |
| Debug visualization | Project Settings → Mass Unit System → Debug → `bEnableDebugVisualization` | shapes drawn, no log spam |
| Cost | `stat MassUnitSystem` / CSV category `MassUnits` (with the profiling work) | counters move with unit count; ~zero when idle |
| Net modes | listen + dedicated PIE in an adopter that replicates crowds | plugin adds no replicated actors |
