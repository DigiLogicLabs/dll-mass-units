# DLL Mass Units

**Large crowds of lightweight units on Unreal Engine's native Mass Entity framework.** Open source
(Apache-2.0) by [Digi Logic Labs](https://digilogiclabs.com) ([GitHub](https://github.com/DigiLogicLabs)). Built for, and developed alongside, our own
UE games. Formerly `ue5-mass-units-plugin`; the old GitHub URL redirects.

> **Status: 1.4.0 (stable foundation), UE 5.7.** Core simulation, rendering, crowds and player engagement are
> released; collective behaviour, statuses, launches, vertex-animation clips and batch spawning are in
> *Unreleased* ([CHANGELOG](CHANGELOG.md)). The plugin does not replicate units, ships no art, and is still
> named `MassUnitSystem` until the planned `DLLMassUnits` rename ([ROADMAP](docs/ROADMAP.md)).

## Why

Thousands of Actors or Characters cost too much: per-actor tick, components, collision and, with GAS, an
Ability System Component each. DLL Mass Units keeps every unit as a Mass entity (fragments, no UObject), runs
behaviour as budgeted group decisions, and only gives the few units next to the player an expensive
representation.

## What's in 1.4.0 (plus Unreleased)

| Piece | What it does |
|---|---|
| `UMassUnitSubsystem` | World subsystem that owns every service below. One tick for the whole crowd; created automatically in Game/PIE worlds. |
| `UMassUnitEntityManager` | Create/destroy units (`CreateUnitFromTemplate`, `CreateUnitsFromTemplate`, `CreateDefaultUnit`), transforms, destinations, targets, native health, queries. *Unreleased:* statuses, unit tags, `LaunchUnit`. |
| `UUnitTemplate` | Data asset: stats, team, static/skeletal meshes, state clips, vertex-animation clip table. |
| `AMassUnitSpawner` | Placeable, non-ticking bootstrap actor: spawn patterns, crowd, engagement and debug toggles. Works with no assets. |
| `UMassUnitCrowdSystem` | Timer-driven crowd groups: wander, separation, interactions, managed subgroups, behaviour LOD, sleeping, player engagement, attacker limiting. *Unreleased:* collective alerts, morale, threat. |
| `UMassUnitNavigationSystem`, `UFormationSystem` | Budgeted async paths with direct fallback, shared group corridors; formation slots. |
| `UCrowdInstanceRenderer`, `UNiagaraUnitSystem`, `UUnitMeshPool` | Instanced static meshes with stable slots and a published custom-data contract; optional Niagara; a capped pool of near-band skeletal meshes. |
| `UGASUnitIntegration`, `UMassUnitBehaviorIntegration` | Opt-in bridges: externally owned ASCs and Behavior Trees for selected units only. |
| Profiling and debug | `stat MassUnitSystem`, CSV category `MassUnits`, per-spawner/per-group `bEnableVisualDebug`. |

## Install

1. Clone into your project's `Plugins/` folder:
   `git clone https://github.com/DigiLogicLabs/dll-mass-units.git Plugins/dll-mass-units`
2. Enable **DLL Mass Units** in the Plugins window (or add `"MassUnitSystem"` to your `.uproject`). It enables
   the engine plugins `MassGameplay`, `Niagara` and `GameplayAbilities`.
3. C++ users: add `"MassUnitSystemRuntime"` to your module's `Build.cs` dependencies.

Tested with **Unreal Engine 5.7.2** (Win64 Editor, Development and Shipping). It is a source plugin, so a
Blueprint-only project still needs a C++ toolchain to compile it.

## Quick start

**No assets:** place a **Mass Unit Spawner** in a level, leave `Unit Template` empty, press Play. 25 engine
cubes spawn in a grid and move 1,500 cm along the spawner's +X. Tick **Crowd > Enable Crowd Simulation** for a
continuous crowd, and **Enable Player Engagement** with `Activation Mode = Always Acquire Player` to make it
chase you.

**C++:**

```cpp
#include "Core/MassUnitSubsystem.h"
#include "Entity/MassUnitEntityManager.h"

if (UMassUnitSubsystem* Units = UMassUnitSubsystem::Get(this))
{
    UMassUnitEntityManager* Manager = Units->GetUnitManager();
    FMassUnitHandle Unit = Manager->CreateUnitFromTemplate(MyUnitTemplate, SpawnTransform);
    Manager->SetUnitDestination(Unit, TargetLocation);
}
```

**Blueprint:** `Get Mass Unit Subsystem` → `Get Unit Manager` → `Create Unit From Template` (or
`Create Default Unit`) → keep the `MassUnitHandle` → `Set Unit Destination`.

Full setup, every option and troubleshooting: [docs/INTEGRATION_GUIDE.md](docs/INTEGRATION_GUIDE.md).

## Design rules

- **No Actor, UObject, ASC or tick per unit.** Work runs in Mass processors or the one subsystem tick, inside
  the budgets in Project Settings.
- **The plugin never replicates units.** Network presentation belongs to the adopting game.
- **GAS is an optional seam.** Units have native health; a crowd can apply a Gameplay Effect to the player it
  attacks, and only units you opt in get an (externally owned) ASC.
- **Depends on the engine only**, never on a game or another dll-* plugin. The plugin owns the `MassUnit.*` tag
  root; games add leaves.

## Documentation

[docs/README.md](docs/README.md) is the index: [PRD](docs/PRD.md), [architecture](docs/ARCHITECTURE.md),
[integration guide](docs/INTEGRATION_GUIDE.md), [testing](docs/TESTING.md), [roadmap](docs/ROADMAP.md),
[decisions](docs/DECISIONS.md), [research](docs/RESEARCH.md), and long task guides in
[docs/guides/](docs/guides/).

**Working with AI assistants:** [`AGENTS.md`](AGENTS.md) is the canonical rule set (Claude Code, Codex,
Copilot all read it); the session workflow is in [docs/AI_WORKFLOW.md](docs/AI_WORKFLOW.md).

## Tests

Automation tests live under `MassUnitSystem.*` (`DLLMassUnits.*` after the rename): Session Frontend →
Automation, or `-ExecCmds="Automation RunTests MassUnitSystem; Quit"`. Details in [docs/TESTING.md](docs/TESTING.md).

## Sister plugins

Part of the Digi Logic Labs `dll-*` plugin family — see [docs/DLL_PLUGIN_STANDARD.md](docs/DLL_PLUGIN_STANDARD.md).

## License

Free and open source under the [Apache License 2.0](LICENSE) (see also [NOTICE](NOTICE)). In plain words: use it
in any project, personal or commercial, change it, ship it in your games and redistribute it, with no fees or
royalties. When you redistribute the plugin or a modified copy, keep the `LICENSE` and `NOTICE` text with it (for a
shipped game, its credits or documentation is enough) and mark the files you changed. The license includes a patent
grant for this code; it does not grant use of the Digi Logic Labs name or logos, and the software comes without
warranty. All source is original Digi Logic Labs work. Unreal Engine and its Mass Entity framework are Epic Games products used under the Unreal Engine EULA and are not part of this repository. Contributions welcome - see [CONTRIBUTING.md](CONTRIBUTING.md).

More from Digi Logic Labs: [digilogiclabs.com](https://digilogiclabs.com).
