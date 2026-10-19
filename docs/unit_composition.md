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

## 4. Swarm types (adopter goal, 2026-10-18)

Adopter goal: "flyers, crawlers, climbers, ankle biters, etc. ... a robust amount of different swarm types with slightly
different behaviors and patterns". A swarm type is NOT a new class: it is a bundle of the layers in section 1 plus one
new axis, **locomotion** (how a unit occupies space). Behaviour variety already comes from motion styles, presets and
attitude rolls; looks from look profiles.

| Type | Locomotion | Exists today | Gap |
|---|---|---|---|
| Ankle biter (ground swarm) | Ground (Planar2D, navmesh) | yes - template, piles, motion styles, orders | - |
| Climber (piles, walls) | Ground + stacking | piles (C2), multi-role piles | C3 wall ramp: piles lean on walls and ledges |
| Flyer (bats, drones, wasps) | Air: altitude band over ground, no navmesh | crowd `Free3D` movement mode, launch gravity | altitude band + bob, dive-attack and pull-up, falls on death (launch path), flying in the Swarm Lab, cost check |
| Leaper / pouncer | Ground + jumps | `LaunchUnit` (airborne table, landing traces) | a pounce attack that uses the launch path at the target |
| Crawler (walls, ceilings) | Surface: unit sticks to a surface normal | nothing | sparse "on surface" table like the stack table (normal, up-vector), traces only in the near band, ground fallback far away |
| Burrower / swimmer (later) | Under ground / water plane | `Free3D` | hidden-while-moving state, surface-to-attack |

Proposed order (each with test, Swarm Lab pen, changelog line):

| Id | What |
|---|---|
| T0 | `UMassUnitSwarmType` data asset: template + look + attitude + behaviour preset + locomotion, and `SpawnSwarmType` - one call to get "a swarm of X" |
| T1 | Flyers: altitude band, bob, dive/pull-up attack, death fall; a Flyer pen |
| T2 | Leapers: pounce attack on the launch path |
| T3 | C3 wall ramp for climbers |
| T4 | Surface crawlers (sparse surface table, near-band only) |

Rules: locomotion is opt-in per template (default Ground, existing units unchanged); every new per-unit table is sparse
like the stack and launch tables; clients get poses from the existing crowd record plus at most a few bits.

Status 2026-10-18: **T0 built** (`UMassUnitSwarmType`, `SpawnSwarm`) and **T1 built in part** (altitude band, floor
clamp, hover-then-dive, death fall; test `MassUnitSystem.Crowd.SwarmTypes`). Not yet: a bob, flying clips, a flyer mesh,
PIE and cost measurements.

Status 2026-10-18 (later): **T2 built** - `LeapUnit` + engagement pounce (test `MassUnitSystem.Units.Leap`). Not yet:
a Leap clip, PIE, replication of leaps to adopters' clients (`OnUnitLeapt` is the hook).
