# DLL Mass Units — Product Requirements

> **Owner:** Digi Logic Labs · **License:** Apache-2.0 · **Status:** 1.4.0 released, more in *Unreleased*
> **Reference adopter:** MetalLegs (UE 5.7, GAS, 1-4 split-screen + online, large crowds)
> Supersedes the pre-1.1 planning PRD (UE 5.6, GASCompanion, Niagara-first), which no longer described the product.

## 1. Problem

Games with swarms, sieges or battlefields need hundreds to thousands of enemies on screen. An Actor or Character
per unit brings per-actor tick, components, collision, replication and, in GAS games, an ASC each; that cost
grows linearly and caps crowds in the low hundreds. Unreal's Mass Entity framework removes the per-unit object,
but out of the box it offers no ready unit model: no health, teams, combat, crowd behaviour, player engagement
or crowd rendering contract.

## 2. Vision

> **Drop in a spawner and get a crowd that wanders, notices the player, attacks, takes damage and dies —
> with no object per unit, bounded cost at every distance, and a clean seam to a GAS game.**

## 3. Users

| User | Needs |
|---|---|
| **Gameplay programmer** | Spawn, command, damage and query units from C++ or Blueprint with stable handles. |
| **Designer** | Tune crowds, engagement and budgets in the Details panel and Project Settings; no code. |
| **Technical artist** | A clear mesh/animation contract: pivot, budgets, custom-data channels, VAT clip table. |
| **GAS game** | Crowds that damage GAS players and receive GAS weapon hits without an ASC per unit. |
| **Network programmer** | A plugin that never replicates on its own, plus an API to drive client puppets. |
| **AI contributors** | Checked-in rules, decisions, memory and prompts ([AI_WORKFLOW.md](AI_WORKFLOW.md)). |

## 4. Requirements

Priority: **P0** required, **P1** should, **P2** could. Status: **Done** (released, with the evidence named),
**Unreleased** (merged, automation-tested, see [TESTING.md](TESTING.md)), **Planned** ([ROADMAP.md](ROADMAP.md)).

### Core
| ID | Requirement | Pri | Status |
|---|---|---|---|
| C1 | Units are native Mass entities with independent fragments; no Actor/UObject per unit. | P0 | Done 1.1 (lifecycle test) |
| C2 | Stable Blueprint handles that cannot address a recycled entity. | P0 | Done 1.1 |
| C3 | Units visible and moving with zero project assets. | P0 | Done 1.2 (lifecycle test) |
| C4 | Data-asset unit definitions (`UUnitTemplate`); variants are data. | P0 | Done 1.1 |
| C5 | Native health, damage, healing, death events, nearest/radius queries. | P0 | Done 1.4 |
| C6 | Batch spawn and deterministic spawn patterns. | P1 | Unreleased |
| C7 | Unit tags and timed statuses (DoT, slow, stun, stacks, immunity). | P1 | Unreleased |
| C8 | Knockback launches and capped near-band ragdolls. | P2 | Unreleased |
| C9 | Safe world teardown regardless of subsystem deinit order. | P0 | Done 1.1 |

### Crowd behaviour
| ID | Requirement | Pri | Status |
|---|---|---|---|
| B1 | Continuous deterministic wander, separation and paired interactions. | P0 | Done 1.3 |
| B2 | Planar (navmesh) and free-3D movement modes. | P0 | Done 1.4 |
| B3 | Player engagement: manual, interaction/damage, proximity, always. | P0 | Done 1.4 (PIE in adopter) |
| B4 | Bounded incoming damage: attack tokens, standoff ring, engagement slots. | P1 | Unreleased |
| B5 | Collective alerts, morale/rout/rally, threat-based targeting. | P1 | Unreleased |
| B6 | Project-defined hostility filter. | P1 | Unreleased |
| B7 | Coalesced group cues for pooled audio/VFX. | P1 | Done 1.4 |

### Navigation
| ID | Requirement | Pri | Status |
|---|---|---|---|
| N1 | Budgeted async paths with direct fallback and safe cancellation. | P0 | Done 1.3 |
| N2 | One shared corridor per group/subgroup; zero per-unit paths by default. | P0 | Done 1.4 (lifecycle test, PIE in adopter) |
| N3 | Planar units follow navmesh height without per-unit traces. | P0 | Done 1.4 (PIE in adopter) |
| N4 | Formations with deterministic slots. | P1 | Done 1.1 |
| N5 | Volumetric (3D) obstacle-aware navigation. | P2 | Not planned in the plugin (adopter strategy) |

### Rendering and animation
| ID | Requirement | Pri | Status |
|---|---|---|---|
| R1 | Instanced rendering with stable slots, no topology churn while moving. | P0 | Done 1.2.1; stable-slot renderer Unreleased |
| R2 | Published, append-only per-instance custom-data contract. | P0 | Unreleased (13 channels) |
| R3 | Capped near-band skeletal pool with hysteresis, nearest-first. | P0 | Done 1.4 |
| R4 | VAT clip table driving crowd-band animation from custom data. | P1 | Unreleased |
| R5 | Near-band ABP base class (`UMassUnitAnimInstance`). | P1 | Unreleased |
| R6 | Optional Niagara renderer fed by arrays. | P2 | Done 1.1 |
| R7 | Crowd rendering cost controls (shadows, cull, WPO distance). | P1 | Unreleased |

### Integration
| ID | Requirement | Pri | Status |
|---|---|---|---|
| I1 | Crowd attacks apply a Gameplay Effect or Actor damage to the target. | P0 | Done 1.4 |
| I2 | Opt-in mapping of selected units to externally owned ASCs. | P1 | Done 1.1 |
| I3 | Opt-in Behavior Tree bridge for selected units. | P2 | Done 1.1 |
| I4 | Network-agnostic presentation API for adopter-side replication. | P1 | Unreleased |
| I5 | GAS split into an optional module so a non-GAS project links nothing extra. | P1 | Planned (open decision) |

### Developer experience
| ID | Requirement | Pri | Status |
|---|---|---|---|
| D1 | Placeable zero-asset spawner with Blueprint-writable controls. | P0 | Done 1.2 |
| D2 | Visual debug per spawner/group; Blueprint diagnostics. | P0 | Done 1.4 |
| D3 | `stat MassUnitSystem`, CSV `MassUnits`, Insights scopes. | P1 | Unreleased |
| D4 | `dll.MassUnits.*` CVar set (debug, stats). | P1 | Planned |
| D5 | Automation tests for every pure-logic feature. | P0 | Ongoing (8 tests) |
| D6 | Standalone `BuildPlugin` passes Editor, Game, Shipping. | P0 | Done 1.4 |

## 5. Non-goals

- **No unit replication.** No replicated actor or component per unit; adopters own network presentation.
- **No ASC, Actor, Behavior Tree or tick per ambient unit.** Those are opt-in for a selected few.
- **No art.** No meshes, VAT textures, materials, Niagara systems, sounds or effects ship in the plugin.
- **No game vocabulary.** The plugin owns `MassUnit.*`; factions, damage types and statuses are the game's.
- **No dependency on a game, another dll-* plugin or marketplace content.**
- **Not a physics crowd.** Separation is steering, not collision or reciprocal velocity avoidance.

## 6. Success metrics

| Metric | Target |
|---|---|
| First run | Visible moving units from a fresh install with no assets and no setup beyond placing one actor. |
| Idle cost | Statuses, launches and collective behaviour cost nothing for units/groups that don't use them. |
| Navigation | Zero per-unit path requests for a default crowd group. |
| Network | Zero bytes replicated by the plugin. |
| Scale | Budgets hold at 1,000+ units; measured per target platform, never promised by `MaxUnits`. |
| Openness | Every release builds standalone and passes `MassUnitSystem.*`. |

## 7. Constraints

- UE 5.7 (`EngineVersion` 5.7.0; validated 5.7.2). Other minors need their own build and test pass.
- Apache-2.0; only content we own.
