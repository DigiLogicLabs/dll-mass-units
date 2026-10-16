# Cut a release

1. Confirm `CHANGELOG.md` *Unreleased* is complete; move it under `## [X.Y.Z] - YYYY-MM-DD`.
2. Bump `.uplugin` `Version` (+1) and `VersionName` (`X.Y.Z`); set `EngineVersion` to the verified engine.
3. Run the standalone build and the `MassUnitSystem` tests; paste the result lines into the release notes.
   Never release on an unverified build.
4. Update the README "Status"/version line.
5. Tag `vX.Y.Z`; create the GitHub release from the changelog section, breaking changes first.
6. Record anything learnt in `docs/MEMORY.md`.
