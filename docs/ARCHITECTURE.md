# DLL Mass Units — Architecture

## Modules

| Module | Type | Depends on | Holds |
|---|---|---|---|
| `DLLMassUnitsRuntime` | Runtime | Core, CoreUObject, Engine, MassEntity, MassCommon, MassSimulation, GameplayAbilities, GameplayTags, GameplayTasks, NavigationSystem, AIModule, Niagara, DeveloperSettings (private: Projects, RenderCore) | Everything that ships in a game |
| `DLLMassUnitsEditor` | Editor | Runtime, UnrealEd, MassEntity, GameplayTags, Slate | Automation tests (`Private/Tests/`) |

Engine plugins enabled by the descriptor: `MassGameplay`, `Niagara`, `GameplayAbilities`. Plugin and module
names follow the dll-* standard; class names, the `UE::MassUnitSystem` namespace, processor group names and
`stat MassUnitSystem` kept their pre-standard names ([DECISIONS.md](DECISIONS.md) 2026-10-15, 2026-10-19).

## Ownership

```
UWorld (Game, PIE, Game Preview only)
  UMassEntitySubsystem                engine; owns FMassEntityManager and every unit's fragments
  UMassUnitSubsystem                  UTickableWorldSubsystem; depends on the entity subsystem
    UMassUnitEntityManager            facade: create/destroy, health, queries, statuses, tags, launches
    UMassUnitCrowdSystem              crowd groups on one timer: wander, engagement, collective behaviour
    UMassUnitNavigationSystem         batched async paths, shared corridors, direct fallback
    UFormationSystem                  formation membership and slot targets
    UNiagaraUnitSystem                visual upload: UCrowdInstanceRenderer (ISM) or a Niagara system
      UVertexAnimationManager         animation tag -> stable index registry
    UUnitMeshPool                     capped near-band USkeletalMeshComponents (+ ragdolls)
    UGASUnitIntegration               opt-in: unit -> externally owned ASC
    UMassUnitBehaviorIntegration      opt-in: transient Behavior Tree/Blackboard per selected unit
    UUnitGameplayEventSystem          gameplay-tag event dispatch without per-unit UObjects
```

There is no per-unit Actor or UObject. A `FMassUnitHandle` (Blueprint) wraps a `FMassUnitEntityHandle`, which
keeps the native entity index and serial; a destroyed entity's serial invalidates old handles.
`AMassUnitSpawner` is an optional, non-ticking ownership actor that creates units and registers them with the
crowd system.

## Data flow per frame

```
Mass processing phases (all net modes)
  UMassUnitMovementProcessor    group MassUnitSystem.Movement    integrate movement toward destination/path
  UMassUnitCombatProcessor      group MassUnitSystem.Combat      unit-vs-unit attacks (after Movement)
  UMassUnitVisibilityProcessor  group MassUnitSystem.Visibility  staggered distance/LOD (after Combat; returns
                                                                 immediately on a dedicated server)
UMassUnitSubsystem::Tick
  prune invalid units -> statuses (10 Hz, only units that have one) -> launches (only airborne units)
  -> formations -> navigation request budget -> behaviour-tree bridge
  -> mesh pool (nearest units win the skeletal slots; ragdoll requests) -> visual upload
UMassUnitCrowdSystem timer (CrowdUpdateInterval, round-robin MaxCrowdUnitsPerUpdate)
  behaviour LOD + sleeping -> wander/separation (spatial hash) -> interactions
  -> engagement (target sample, shared corridor, follow slots, attack tokens) -> collective signals
```

Smooth motion runs every frame in the movement processor; decisions run on the budgeted crowd timer. Keeping
those clocks separate is what lets the crowd scale ([guides/scalability_best_practices.md](guides/scalability_best_practices.md)).
`UMassUnitFormationProcessor` is a disabled compatibility processor; `UFormationSystem` owns formation work.

## Representation

Simulation state stays in Mass; representation is chosen per unit by distance and capacity:

| Band | Representation | Owner | Bound |
|---|---|---|---|
| Near | `USkeletalMeshComponent` (state clips or an ABP parented to `UMassUnitAnimInstance`) | `UUnitMeshPool` | `MaxSkeletalMeshUnits`, `SkeletalMeshDistance` × `SkeletalMeshHysteresis` |
| Crowd | Instanced static mesh, stable slots, per-instance custom data | `UCrowdInstanceRenderer` | One component per unique mesh |
| Custom | Niagara system reading `Unit*` arrays | `UNiagaraUnitSystem` | Only when `DefaultNiagaraSystem` is set |
| Beyond `MaxVisibleDistance` | Nothing | — | — |

Mesh choice for the crowd band: template static mesh, else `FallbackStaticMesh`, else the engine cube. The
custom-data channels are an append-only contract in `UE::MassUnitSystem::CrowdCustomData`
(`Public/Visual/CrowdInstanceRenderer.h`).

## Networking

The plugin does not replicate units. `AMassUnitSpawner::bSpawnOnAuthorityOnly` (default on) avoids duplicate
simulation. Adopters that show crowds on clients replicate compact records themselves and drive client
"puppet" units through `GetUnitPresentation` / `ApplyPresentationState` and the variant-seed accessors.
Presentation (health tint, team colour, animation phase) is derived on each machine from fragments.

## Extension points

| Point | How | Typical use |
|---|---|---|
| Unit types | `UUnitTemplate` data assets | Variants are data, not subclasses |
| Crowd events | `OnCrowdAttackRequested`, `OnCrowdCueRequested`, `OnCrowdEngagementStarted/Ended`, `OnCrowdInteractionStarted`, `OnCrowdGroup*` | Hit reactions, pooled audio/VFX, game damage routing |
| Unit events | `OnUnitHealthChanged`, `OnUnitDied`, `OnUnitStatusChanged`, `OnUnitStatusDamageTick`, `OnUnitLaunched`, `OnUnitLanded` | Kill credit, feedback; bind `OnUnitStatusDamageTick` to route status damage through your own pipeline |
| Hostility | `UMassUnitCrowdSystem::SetCrowdTargetFilter` (C++ delegate) | Faction/team rules for acquisition, threat, damage reports |
| Damage into units | `ApplyDamage`, `DamageUnitAndActivate`, `ReportCrowdUnitDamaged` | A game's weapon or GAS seam calls these |
| Player damage | `FMassUnitPlayerEngagementConfig::GameplayEffectToTarget` / `bApplyActorDamage` | GAS or `AnyDamage` on the attacked Actor |
| Animation | `UMassUnitAnimInstance` subclass; `VertexAnimationClips` + material | Near-band ABPs; crowd-band VAT |
| Selective services | `UGASUnitIntegration`, `UMassUnitBehaviorIntegration` | Promote a few units to an ASC or Behavior Tree |
| Rendering | `DefaultNiagaraSystem` | Replace the ISM path with a project Niagara system |

## Shutdown

The engine's Mass subsystem owns entity lifetime and tears entities down with the world. Plugin services clear
their indexes and components without touching the Mass manager after it has begun shutdown; the lifecycle test
covers this ordering.

## Open structure questions

- GAS is linked by the runtime module even though its use is optional; splitting it into an optional module is
  open ([DECISIONS.md](DECISIONS.md)).
- A `dll.MassUnits.*` CVar set is owed by the standard; none exists yet.
