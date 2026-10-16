# Plan a feature

Input: a feature request.

1. Restate it as a requirement for crowds in general, not for one game. If it only makes sense for one game,
   it belongs in that game's bridge; say so.
2. Read `docs/ARCHITECTURE.md` and `docs/DECISIONS.md`. Name any decision the plan touches.
3. Decide the home: runtime module, editor module, or a new/existing optional module. Justify.
4. Design: fragments/tags, processors (which phase, which query), subsystem API, templates/settings,
   Blueprint exposure, debug visualisation, cost at 1k / 10k units and when idle, which budget bounds it,
   behaviour on a dedicated server and in split-screen, what an adopter would replicate.
5. Tests: which `MassUnitSystem.*` automation tests and which manual checks in `docs/TESTING.md`.
6. Docs + memory: which files change (changelog, architecture, DECISIONS, MEMORY, TESTING).
7. Risks, and what would make you choose differently.

Output a plan; don't write code until it is approved.
