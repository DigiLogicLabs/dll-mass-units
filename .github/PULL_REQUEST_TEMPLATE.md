## What & why
<!-- the crowd problem this solves (not a single game's) -->

## Checklist
- [ ] No dependency on a game or another dll-* plugin; runtime `Build.cs` unchanged (or the feature is in an optional module)
- [ ] Works without GAS on the units; no per-unit Tick/UObject; idle cost ~zero; bounded by a settings budget
- [ ] No replication added
- [ ] Visual debug / stats for new systems
- [ ] `MassUnitSystem.*` tests added/updated. Result: <!-- paste counts -->
- [ ] Standalone `RunUAT BuildPlugin`. Result: <!-- paste the BUILD SUCCESSFUL line -->
- [ ] `docs/changelog.md` *Unreleased*, DECISIONS / MEMORY / TESTING updated as needed
- [ ] Only our own content; Apache-2.0 headers on new files

## Tested in
<!-- engine version, platform, unit counts, net modes (standalone / listen / dedicated / split-screen), or UNTESTED -->
