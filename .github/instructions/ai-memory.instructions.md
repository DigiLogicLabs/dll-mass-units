---
applyTo: "docs/**"
---
# Documentation & memory rules (DLL Mass Units)

Every meaningful change leaves **Code + Documentation + Memory**.

| Where | What | How |
|---|---|---|
| `CHANGELOG.md` | Every user-visible change | Under *Unreleased*; breaking changes with a migration note |
| `docs/ARCHITECTURE.md` | Modules / processors / extension points changed | Edit in place |
| `docs/DECISIONS.md` | A choice between alternatives | Append `## YYYY-MM-DD — <decision>`; supersede, never delete |
| `docs/MEMORY.md` | Verified non-obvious fact or trap | One dated line; mark `UNVERIFIED` when unproven |
| `docs/TESTING.md` | New test or manual check | Add to the tables |
| `docs/sessions/` | Session hand-off | `YYYY-MM-DD-<topic>.md` via `/session-handoff` |

- Dates are absolute (`2026-10-16`), never "today" or "last week".
- Don't duplicate what code or git history already records; record the *why* and the traps.
- Append-only edits to shared docs: never rewrite another contributor's entry.
- Windows is case-insensitive: never add a doc whose name differs from an existing one only by case.
