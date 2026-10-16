---
applyTo: "Source/**/Tests/**"
---
# Testing rules (DLL Mass Units)

- Test names: `MassUnitSystem.<Area>.<Behaviour>` (`DLLMassUnits.*` after the rename); flags
  `EditorContext | EngineFilter`.
- Tests must run in **any** project: use only the plugin's own tags, default settings and transient objects
  (the engine-cube fallback needs no assets).
- World-based tests dispatch BeginPlay properly and destroy every spawned actor/entity.
- Prefer testing plain logic; world-based tests only when the behaviour needs a world.
- A test that passes before the fix proves nothing: when fixing a bug, check the test fails without it.
- Report results with the exact command and the pass/fail counts.
