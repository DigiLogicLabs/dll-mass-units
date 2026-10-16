# Minimal Sample Project

1. Install and compile the plugin using [docs/setup.md](docs/setup.md).
2. Create two `UnitTemplate` Data Assets with different `TeamID` values and optional static meshes.
3. In a gameplay Blueprint, get `Mass Unit Subsystem` and its `Unit Manager`.
4. Spawn one unit from each template at different transforms.
5. Assign one unit as the other's target to observe lightweight combat.
6. Call `Request Path` or `Set Unit Destination` to exercise movement.
7. Create a formation, add units, and set a formation target.
8. Run `MassUnitSystem.Core.NativeMassLifecycle` to validate the native foundation before adding project content.

No GASCompanion, Niagara asset, or custom mesh is required for this smoke sample. See [docs/usage.md](docs/usage.md) for optional integrations.
