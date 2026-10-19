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
