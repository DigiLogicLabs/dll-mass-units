# Add an optional integration module

Use when a feature needs a dependency the runtime module should not take (a specific engine plugin such as
StateTree or Chaos Destruction, AnimToTexture tooling, a third-party SDK), or when splitting an existing
integration (GAS, Behavior Tree, Niagara) out of the runtime module.

1. Name: `DLLMassUnits<Integration>`. Folder
   `Source/<ModuleName>/`.
2. `Build.cs`: depend on the runtime module + the integration's modules. The runtime's `Build.cs` gains nothing.
3. `.uplugin`: add the module (type, loading phase) and any engine plugin it needs under `"Plugins"`.
   Document in README what adopters must enable.
4. Register with the runtime only through its extension points (subsystem API, events, templates). If an
   extension point is missing, add it to the runtime first as a generic, dependency-free interface.
5. Debug + tests + docs: settings section, `dll.MassUnits.<Integration>.*` CVars, `DLLMassUnits.<Integration>.*`
   tests, `docs/ARCHITECTURE.md` module table, a DECISIONS entry, `CHANGELOG.md`.
6. Verify with the standalone `RunUAT BuildPlugin` (short `-Package` path).
