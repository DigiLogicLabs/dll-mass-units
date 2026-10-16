# Review a diff

Review the current diff (or the given range) against the `AGENTS.md` non-negotiables. Read-only.

Check, citing `file:line`:
1. Dependency creep (`*.Build.cs`, `.uplugin` `"Plugins"`, includes from a game or another dll-* plugin).
2. GAS turned from a seam into a requirement (code paths that fail without an ASC; units needing GAS to take damage).
3. Per-unit Tick/UObject/actor, processors without an idle early-out, work outside the settings budgets.
4. Replication added (RPCs, `Replicated`, `GetLifetimeReplicatedProps`, per-unit replicated actors).
5. Game vocabulary (tags, names, factions) leaking into the plugin.
6. Public API removed/renamed without redirects + a changelog migration note.
7. Missing tooltips/categories on exposed properties; missing visual debug or stats for a new system.
8. Logic without tests; tests that depend on project config or assets.
9. Docs/memory not updated (changelog, architecture, DECISIONS, MEMORY, TESTING).
10. Licensing: Apache SPDX header on new files, no third-party content.

Report findings ranked by severity, each with a concrete failure scenario. Say "none" when clean.
