---
applyTo: "**"
---
# Architecture rules (DLL Mass Units)

- **Units are Mass entities, not actors.** Per-unit state lives in fragments; behaviour lives in processors or
  the subsystem's single tick. The near band (skeletal-mesh pool, ragdolls) is a bounded presentation layer,
  never the source of truth.
- **Budgets are part of the design.** Behaviour LOD, visibility stagger, simulation sleeping, mesh pool size and
  airborne/ragdoll caps are settings; new per-unit work states its cost when idle (must be ~zero) and which
  budget bounds it.
- **GAS stays a seam.** Native health/damage is the model; Gameplay Effects and ASC-owning targets are reached
  through the integration layer only. Hosts without GAS on their units must still work.
- **No networking in the plugin.** Expose stable unit identity and compact state so adopters can replicate it;
  don't add replicated actors per unit.
- **Game rules live in the game.** Factions, rewards, audio, crowd replication and targeting are adopter-side
  bridges or a dedicated bridge module, never core branches.
- Update `docs/architecture.md` when the processor pipeline changes. Check `docs/DECISIONS.md` before proposing
  an alternative to something already decided.
