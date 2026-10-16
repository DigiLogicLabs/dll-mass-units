# Integration Guide

This guide applies to Mass Unit System 1.2.0 and Unreal Engine 5.7. For full details, use the [five-minute quick start](docs/quick-start.md), [setup](docs/setup.md), [usage](docs/usage.md), and the [public API reference](docs/api.md).

## Integrate

1. Clone the repository into `<YourProject>/Plugins/`.
2. Regenerate project files and compile. Required engine plugins are declared by the descriptor; GASCompanion is not required.
3. Create a `UnitTemplate` Data Asset and configure its simulation/team/visual defaults.
4. Resolve `UMassUnitSubsystem` from a gameplay world.
5. Create units through `GetUnitManager()->CreateUnitFromTemplate(...)`.
6. Store `FMassUnitHandle` values and validate them before later use.

The subsystem and Mass processors initialize automatically. Do not construct them or invoke processors manually.

## Choose only the integrations you need

- Basic units: no additional setup; HISM/cube rendering and lightweight combat work immediately.
- Navmesh routes: add and build a Nav Mesh Bounds Volume, then call `RequestPath`.
- Formations: create a formation, add unit handles, and set its target.
- Niagara: provide a system matching the arrays in [docs/usage.md](docs/usage.md).
- GAS: register a project-owned ASC for selected units.
- Behavior Trees: provide a tree and blackboard for selected units.

## Validate

Package the plugin with the provided build helper and run `MassUnitSystem.Core.NativeMassLifecycle` from Unreal Automation before integrating project-specific content.
