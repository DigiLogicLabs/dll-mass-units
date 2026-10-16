# Verify hand-off (start of session)

Audit the newest file in `docs/sessions/` (or the one given) against the repo **as it is now**. Read-only.

1. Read the hand-off, `docs/MEMORY.md` (last 20 lines) and `CHANGELOG.md` *Unreleased*.
2. For every claim (file exists, function added, test passes, build green, item owed), check it: open the
   file, grep the symbol, look at git log. Don't trust the hand-off's own wording.
3. Report a table: claim | **still-true** / **drifted** (what changed) / **never-true** / **unverifiable** | evidence.
4. List the owed items that are still open, in priority order.
5. Make no edits.
