# AI workflow — DLL Mass Units

How Digi Logic Labs and outside contributors work with AI assistants on this plugin. The aim: context
accumulates in the repo instead of evaporating with a chat session, and every tool gets the same rules.

## Entry points (one canon, thin pointers)

| Tool | Reads | Notes |
|---|---|---|
| Any agent (Codex, others) | [`AGENTS.md`](../AGENTS.md) | **Canonical.** Edit rules here only. |
| Claude Code | [`CLAUDE.md`](../CLAUDE.md) → imports `AGENTS.md` | Slash commands in `.claude/commands/`, reviewer in `.claude/agents/` |
| GitHub Copilot | [`.github/copilot-instructions.md`](../.github/copilot-instructions.md) + `.github/instructions/*.instructions.md` | Instructions apply by file glob |

The tool-agnostic layer is `.github/instructions/` (rules) and `.github/prompts/` (task workflows). Claude
Code's slash commands are one-line wrappers around the prompts, so the two never drift.

## Session loop

```
START   /verify-handoff        audit the newest docs/sessions/ hand-off against the repo (read-only)
WORK    /plan-feature          generic crowd requirement, home module, design, cost, tests, docs
        /add-integration-module   when a dependency shouldn't go in the runtime module
        /review-diff           or the dll-mass-units-reviewer agent, before committing
END     /session-handoff       verify claims, write docs/sessions/YYYY-MM-DD-<topic>.md, promote durable items
RELEASE /release               changelog → version → verified build + tests → tag
```

Without Claude Code, ask your assistant to follow the matching file in `.github/prompts/`.

## Rules that matter most for AI-written changes

1. **Evidence or `UNTESTED`.** No "it compiles" without a build line; no "tests pass" without counts.
2. **Decisions are sticky.** Read `docs/DECISIONS.md` before proposing an alternative; supersede with a new
   entry, never silently diverge.
3. **Code + docs + memory** in the same change (`.github/instructions/ai-memory.instructions.md`).
4. **Stay in scope.** No game dependencies, no game vocabulary; integrations are modules.
5. **Stage by path.** Never `git add -A`.

## Sharing improvements upstream

Digi Logic Labs uses this plugin in its own games. When a game session discovers something about the
plugin (a trap, a missing extension point, a better default), record it here: a MEMORY line, a DECISIONS
entry or an issue. Don't leave it only in the game's docs.
