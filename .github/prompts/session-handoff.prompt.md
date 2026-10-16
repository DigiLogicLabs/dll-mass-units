# Session hand-off (end of session)

1. Verify before writing: re-check every claim you are about to make (build line, test counts, files
   touched). Anything you can't verify is written as UNTESTED / UNVERIFIED.
2. Write `docs/sessions/YYYY-MM-DD-<topic>.md`: goal (in the requester's words), what changed (files),
   evidence (exact commands + result lines), status per item (done / UNTESTED / owed), traps hit, next
   steps in priority order.
3. Promote durable items: decisions → `docs/DECISIONS.md`, facts/traps → `docs/MEMORY.md`, user-visible
   changes → `docs/changelog.md`.
4. Stage by path; never `git add -A`.
