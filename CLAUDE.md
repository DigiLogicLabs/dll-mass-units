# CLAUDE.md — DLL Mass Units

@AGENTS.md

Claude Code specifics:
- Slash commands in `.claude/commands/` wrap the tool-agnostic prompts in `.github/prompts/`.
- `dll-mass-units-reviewer` (`.claude/agents/`) is a read-only reviewer for diffs against the non-negotiables.
- Start sessions with `/verify-handoff`, end with `/session-handoff` (hand-offs in `docs/sessions/`).
- Personal approvals go in `.claude/settings.local.json` (never committed).
