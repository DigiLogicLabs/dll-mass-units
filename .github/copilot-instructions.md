# Copilot instructions — DLL Mass Units

Follow [AGENTS.md](../AGENTS.md) (canonical). Modular rules in `.github/instructions/` apply by file glob.
Key points: never depend on a game or another dll-* plugin; GAS is an optional seam (units have native health);
no per-unit Tick or UObject, stay inside the crowd budgets; the plugin does not replicate units; no game
vocabulary; visual debug for every system; every change leaves code + docs + memory; never claim "tested"
without an artifact.
