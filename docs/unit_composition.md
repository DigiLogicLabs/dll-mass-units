# Unit composition: one minion, many purposes, looks and attitudes

**Goal (Digi Logic Labs, 2026-10-17):** the plugin is multi-purpose. It scales to very large numbers of units on screen,
works with systems like GAS, supports randomised modular characters on vertex animation, and lets the same Mass minion
be reused for different purposes and with different looks, feels and attitudes. Its systems are built to complement
each other.

This page is the map: which layer owns what, how the layers combine, and what is still missing.

## 1. The layers

A unit is assembled from independent, reusable layers. Each layer is data (an asset or a config struct) that can be
swapped without touching the others.

| Layer | Owns | Where | Swap per |
|---|---|---|---|
| **Identity + stats** | unit type/class tags, health, damage, speed, attack range/cooldown/impact, launch scale, granted tags, immunities, body material tag | `UUnitTemplate` | template |
| **Look** | body variants, modular parts, material overrides, scale range, tint palette | `UMassUnitLookProfile` (template `LookProfile`, batch `FMassUnitBatchSpawnOptions::LookProfile`, unit `SetUnitLookProfile`) | template, batch, unit |
| **Animation** | VAT clip table (shared by body and parts), skeletal near band clips | `UUnitTemplate::VertexAnimationClips`, skeletal fields | template |
| **Attitude (individual)** | per-unit pace, attack rhythm, looping animation tempo, courage (rout order), willingness to climb | `UMassUnitAttitudeProfile` (template `AttitudeProfile`, batch, unit `SetUnitAttitudeProfile`) | template, batch, unit |
| **Group behaviour** | motion style (wander/flock/swarm/hive), engagement (activation, chase, attack tokens, slots), collective (alerts, morale, rout, threat) | `FMassUnitCrowdConfig`, `FMassUnitPlayerEngagementConfig`, `FMassUnitCollectiveBehaviorConfig` per crowd group | group |
| **Roles** | stacking/piles, multi-role attackers, orders (move/stop/hold) | `FMassUnitStackingConfig` (template), `UMassUnitStackSystem`, `UMassUnitCommandSystem` | template / unit |
| **Gameplay seam** | native health/statuses/tags, GAS bridge, gameplay events, attack requests to actors | `UMassUnitEntityManager`, `UGASUnitIntegration`, `OnCrowdAttackRequested` | adopter |
| **Presentation on clients** | compact records the adopter replicates: location, yaw, health, team, template, presentation, variant seed, stack role | adopter (e.g. a relevancy bubble) | — |

Rules that keep the layers composable:

1. **Same unit, any purpose.** Enemy horde, friendly squad, ambient crowd and pile climbers are the same entity type;
   behaviour comes from layers and hand-off flags (`bStackDriven`, `bCommanded`), never from a second unit type.
2. **Deterministic from the seed.** Anything visual that varies per unit (look, animation phase, tint) is derived from
   the unit's variant seed at byte precision, so replicating the seed (1 byte) reproduces the unit on every machine.
3. **Append-only contracts.** Per-instance custom data channels, enum values and record fields only grow.
4. **Budgets, not ticks.** New layers add work to the existing budgeted passes (crowd update, renderer pass, sparse
   tables), free when unused.
5. **No game vocabulary.** Tags, damage types and factions are the adopter's; the plugin carries opaque tags.

## 2. Looks (built 2026-10-17)

`UMassUnitLookProfile`:
- **BodyVariants**: weighted meshes, each with an optional material override (a skin, a faction texture).
- **Parts**: modular pieces (head, hat, weapon, armour), each with a chance and weighted options; an option with no mesh
  means "no part".
- **ScaleRange** and **TintPalette**.

The roll is deterministic per seed byte. Every roll draws the same number of random values whatever comes up, so
editing one part's options does not reshuffle the others.

Renderer:
- Each part is its own instance in a bucket keyed by mesh + material.
- A part uses the body's transform and the body's full custom-data row.
- Tint goes in channels 13-15 (`TintR/G/B`), so materials can read it.
- Cost: one instance per unit plus one per part; one component per distinct mesh + material.

**Authoring rule for modular VAT kits:** bake the body and every skinned part against the same skeleton in one
AnimToTexture pass. They must share the clip table (same frame layout). Rigid props can be baked as skinned parts too.

**Limits:**
- The skeletal near band still draws the template's `SkeletalMesh` (see L2).
- A batch or per-unit look override must be replicated by the adopter. Clients spawn puppets from the template, so the
  template's own profile replicates for free.

## 3. Roadmap (gaps against the goal)

| Id | What | Why |
|---|---|---|
| L2 | Skeletal near band wears the rolled look (leader-pose part components from a parallel skeletal part list) | no pop when a unit enters the near band |
| L3 | Per-part material parameters (palette index, mask channel) and a decal/emblem slot | faction emblems, wear without extra meshes |
| ~~A1~~ | **Built 2026-10-17**: `UMassUnitAttitudeProfile` (speed, cooldown, looping tempo, linked "energy" roll, courage, climb chance). Still open: aggression (per-unit engage/chase reach), idle fidget clips | same group, individual feel |
| ~~A2~~ | **Built 2026-10-17**: `UMassUnitBehaviorPreset` + `RegisterCrowdGroupFromPreset` / `ApplyCrowdGroupPreset` / `SetCrowdGroupConfig` | "attitudes" swappable like looks |
| R2 | Orders: auto-acquire, attack / attack-move, stances | controllable squads (see adopter design) |
| G1 | GAS seam audit: tag-driven ability selection per unit, attribute-to-native mapping documented end to end | reuse across GAS projects |
| P1 | Look/attitude previews in the editor (roll N seeds in a viewport) | designers see variety before PIE |

Each item lands with code, a test under `MassUnitSystem.*`, a changelog line and, for choices, a DECISIONS entry.
