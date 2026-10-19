# Contributing to DLL Mass Units

Thanks for helping! Digi Logic Labs maintains this plugin and uses it in our own games, so changes are
reviewed against real production use.

- **Issues:** include engine version, platform, net mode (standalone / listen / dedicated), unit count and a
  minimal repro. For performance issues attach `stat MassUnitSystem` output or a CSV capture (category `MassUnits`).
- **Pull requests:** one change per PR; follow [docs/DLL_PLUGIN_STANDARD.md](docs/DLL_PLUGIN_STANDARD.md);
  add or update automation tests under `DLLMassUnits.*`; add a [CHANGELOG.md](CHANGELOG.md) entry
  under *Unreleased*.
- **Scope:** no dependency on any game or on another dll-* plugin. New engine-plugin or third-party
  dependencies go in a separate optional module.
- **Content:** only content you own and can license under Apache-2.0. No marketplace assets.
- By submitting a contribution you agree it is licensed under the Apache License 2.0 (section 5 of the LICENSE).

## Getting started (fork and pull request)

This plugin is built alongside our own games: `main` is what we ship with, and it should always build. Anyone can
use it, fork it and send changes back.

1. **Fork** [dll-mass-units](https://github.com/DigiLogicLabs/dll-mass-units) on GitHub and clone your fork.
2. Put it in an Unreal Engine 5.7 C++ project as `Plugins/dll-mass-units/` (any project; a blank C++ project is enough),
   enable **DLL Mass Units** in *Edit → Plugins*, and add `"DLLMassUnitsRuntime"` to your module's `Build.cs` if you call it from C++.
3. Create a branch for one change (`fix/...`, `feature/...`).
4. Build the plugin on its own, which proves it has no hidden dependency on your project:
   `RunUAT.bat BuildPlugin "-Plugin=<path>\DLLMassUnits.uplugin" "-Package=<short temp path>" -TargetPlatforms=Win64`
   must end with `BUILD SUCCESSFUL`.
5. Run the automation tests: *Tools → Session Frontend → Automation*, filter `DLLMassUnits.*`, or headless
   `UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests DLLMassUnits; Quit" -unattended -nullrhi`.
6. Update the docs the change touches and add a line under *Unreleased* in [CHANGELOG.md](CHANGELOG.md).
7. Open a pull request against `main` and fill in the template. Small, focused PRs get reviewed fastest.

Please read the [Code of Conduct](CODE_OF_CONDUCT.md). Security problems go through [SECURITY.md](SECURITY.md),
not public issues.
