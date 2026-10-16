# Build and Implementation Guide

The maintained build instructions are in [docs/setup.md](docs/setup.md). Set `UE_ENGINE_PATH` and run `build_plugin.bat` on Windows or `build_plugin.sh` on Linux/macOS.

Implementation starts at `UMassUnitSubsystem`, `AMassUnitSpawner`, and `UUnitTemplate`; see [docs/architecture.md](docs/architecture.md) and [docs/api.md](docs/api.md). Version 1.2.0 uses native Mass Entity and Unreal's built-in GameplayAbilities plugin and has no GASCompanion requirement.
