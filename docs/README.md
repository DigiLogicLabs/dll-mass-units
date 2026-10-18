# DLL Mass Units — Knowledge graph

Start here. Everything a contributor (human or AI) needs, in reading order. The plugin predates the
Digi Logic Labs `dll-*` standard, so some roles map to older file names (kept so links don't break; Windows is
case-insensitive, so `prd.md` and a new `PRD.md` cannot coexist).

| Standard role | Doc | Answers |
|---|---|---|
| Install / quick start | [../README.md](../README.md), [quick-start.md](quick-start.md), [setup.md](setup.md) | Install, first 25 units, requirements |
| Usage / API | [usage.md](usage.md), [api.md](api.md) | Creating and controlling units; public entry points |
| Architecture | [architecture.md](architecture.md) | Ownership, subsystems, processors |
| Unit composition | [unit_composition.md](unit_composition.md) | The goal (one minion, many purposes / looks / attitudes), the layers, the roadmap |
| Decisions | [DECISIONS.md](DECISIONS.md) | Why things are the way they are (append-only log) |
| Memory | [MEMORY.md](MEMORY.md) | Verified facts and traps the code doesn't make obvious |
| Roadmap / status | [todo.md](todo.md) | Current status and validation backlog |
| Testing | [TESTING.md](TESTING.md) | Build, automation, PIE and network test matrix |
| Troubleshooting | [troubleshooting.md](troubleshooting.md) | Compile and runtime problems |
| Changelog | [changelog.md](changelog.md) | Versions, *Unreleased* first (the root `CHANGELOG.md` of other dll-* repos) |
| PRD | [prd.md](prd.md) | **Archived** pre-1.1 planning document; a current PRD is owed |
| AI workflow | [AI_WORKFLOW.md](AI_WORKFLOW.md) | How we work with AI assistants on this repo |
| Shared standard | [DLL_PLUGIN_STANDARD.md](DLL_PLUGIN_STANDARD.md) | Conventions shared by every `dll-*` plugin |
| Hand-offs | [sessions/](sessions/) | Dated session hand-offs |

**Arriving:** a docs reorganisation in progress moves the root-level research and guide files
(`integration_guide.md`, `implementation_roadmap.md`, `technical_specification.md`, research notes, readiness and
patch reports) into `docs/`, and adds authoring-pipeline and VAT-animation guides. Link them from this table
when they land.

Root files: [`AGENTS.md`](../AGENTS.md) (canonical agent rules), [`CLAUDE.md`](../CLAUDE.md),
[`CONTRIBUTING.md`](../CONTRIBUTING.md), [`LICENSE`](../LICENSE), [`NOTICE`](../NOTICE).
