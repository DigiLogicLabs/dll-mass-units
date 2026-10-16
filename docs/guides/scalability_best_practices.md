# Guide — Scalability rules

Working rules for building on this plugin at large scale (hundreds to thousands of units, many players). Each
rule is tied to code that exists today; where a rule exists because of an engine behaviour, the behaviour is named,
because those are the ones that get silently violated.

## 1. The representation ladder

| Band | Representation | Owner | Budget |
|---|---|---|---|
| Near | `USkeletalMeshComponent`, real animation | `UUnitMeshPool` | `MaxSkeletalMeshUnits` (100) |
| Mid | Instanced static mesh, LOD0-2, material-driven animation | `UCrowdInstanceRenderer` | One draw call per mesh per LOD |
| Far | Same component, lowest LOD | same | ~free |
| Beyond `MaxVisibleDistance` | Nothing | — | Zero |

- **A representation never decides its own budget.** `FMassUnitVisualFragment::bWantsSkeletalMesh` is a request
  set by distance; `UUnitMeshPool` decides whether capacity exists.
- **Every promotion threshold has hysteresis.** `SkeletalMeshHysteresis` (1.2) widens the exit distance so a unit
  on the boundary does not allocate and release a mesh every update. New thresholds need the same.
- **Promote by distance, not iteration order.** `UUnitMeshPool::UpdateUnitMeshes` sorts candidates by
  `ViewerDistanceSquared` so the nearest units win the scarce slots.

## 2. Instanced rendering: the stable-slot contract

`UCrowdInstanceRenderer` owns one `UInstancedStaticMeshComponent` per unique static mesh; each entity keeps an
instance slot for its whole life.

- **Why stable:** custom data is addressed by instance index. Removing an instance mid-array reindexes every later
  one and hands one unit's colour and health to another. Released slots are parked at zero scale and recycled.
- Tail-only compaction (`CompactBucket`).
- One contiguous upload per bucket per update (`BatchUpdateInstancesTransforms(0, ...)`), not per-instance calls.
- **`bMarkRenderStateDirty` stays `false`.** The primitive instance data manager streams dirty instances to
  GPUScene by itself; `true` rebuilds the whole scene proxy every update.
- Custom data uploads only when a value changed (`WriteCustomData` compares first). That is why the animation
  *start time* is uploaded, not elapsed time.
- **ISM, not HISM.** Under GPUScene, ISM instances are culled and LOD-selected per instance on the GPU; HISM's
  CPU cluster tree assumes static instances and must be rebuilt as they move.

## 3. The per-instance custom data contract

Declared once in `UE::MassUnitSystem::CrowdCustomData` (`Public/Visual/CrowdInstanceRenderer.h`):

```
0 AnimationIndex      4 TeamColorR    8 RandomSeed      12 ClipFramesPerSecond
1 AnimationStartTime  5 TeamColorG    9 Speed
2 LODLevel            6 TeamColorB   10 ClipStartFrame
3 TeamID              7 HealthPercent 11 ClipFrameCount
```

- **Append only.** Renumbering silently repaints every material that reads a channel. Add at the end, bump `NumFloats`.
- Anything per unit belongs here, never in a material instance per unit (per-unit UObjects, no batching).
- Give every `PerInstanceCustomData` node a sensible `ConstDefaultValue`; outside an instanced component the node
  returns it, and zero renders black.
- `RandomSeed` comes from a golden-ratio scatter of the entity index (`MakeRandomSeed`) or the batch seed, not
  `Index % N`, so units spawned together do not animate in lockstep.

## 4. LOD authoring

- Turn off automatic reduction on every hand-made LOD (`SetNumSourceModels` seeds 50/25/12.5%).
- One material slot: a second one doubles draw calls of every instanced component. Pack masks into UV ranges.
- Crossed quads, not camera-facing billboards, for the farthest LOD (a billboard needs per-vertex WPO).
- Feet pivot (Z = 0).
- Check face winding geometrically; an inverted crowd mesh looks like "units invisible from the front".

## 5. Four independent clocks

| Clock | Setting | Default | Governs |
|---|---|---|---|
| Visual upload | `VisualUpdateInterval` | 0.033 s | Transform + custom-data upload |
| Crowd decisions | `CrowdUpdateInterval` | 0.1 s | Steering, destinations, spatial hash |
| Crowd throughput | `MaxCrowdUnitsPerUpdate` | 500 | Units touched per crowd callback |
| Visibility / LOD | `VisibilityLODUpdateIntervals` | 0.05-1.0 s per band | Distance and LOD reclassification |

- **Stagger everything periodic.** `UMassUnitVisibilityProcessor` jitters each entity's next update off its index;
  without jitter a batch spawned on one frame re-evaluates on the same frame forever (periodic spikes).
- Distant units think less often: extend the interval arrays rather than adding timers.
- Round-robin, never truncate; truncation starves the tail.
- Movement stays smooth because steering runs every frame in the processor; decisions do not.

## 6. Processor discipline

- Iterate with `ForEachEntityChunk` and fragment views; per-entity random access gives up the cache win (the
  renderer's random access is the exception: it is handed an arbitrary handle list).
- Declare the narrowest access (`ReadOnly` vs `ReadWrite`) so the scheduler can parallelise.
- Read `GetDefault<UMassUnitSystemSettings>()` once per `Execute`, never per entity.
- **Early-out on a dedicated server for anything visual** (the visibility processor returns on `NM_DedicatedServer`).
- Allocate nothing per entity per update; reuse scratch arrays.

## 7. Rendering cost that is not triangles

- **Shadows:** thousands of small movable casters per cascade dwarf mesh cost. Crowd instances cast no shadows
  unless `bCrowdInstancesCastShadows` is on.
- Crowd instances do not affect distance-field or dynamic indirect lighting and do not receive decals.
- `InstanceStartCullDistance` / `InstanceEndCullDistance` cull per instance on the GPU; start below end to fade.
- `WorldPositionOffsetDisableDistance` stops WPO where a unit is a few pixels.
- No collision, overlaps or navigation influence on crowd instances; moving instances that affect navigation
  trigger rebuilds.

## 8. Content checklist for a new unit type

1. Static mesh with a feet pivot and a full LOD chain; one material reading the channels in §3.
2. `UUnitTemplate` with `StaticMesh` (and optional skeletal mesh + clips), `TeamID`, `TeamColor`.
3. **Ground offset 0:** `SpawnHeight` and `NavigationHeightOffset` default to 50 for the engine cube.
4. `GetInstancedMeshComponentCount()` should equal distinct meshes in view, not units.

## 9. Networking

- Never replicate per entity; never give a crowd unit an Actor, ASC or replicated component. Adopters summarise
  crowds themselves (e.g. a relevancy bubble of compact records) and drive client units with the presentation API.
- Damage enters through the game's seam into `ApplyDamage` / `ReportCrowdUnitDamaged`, not by writing
  `FMassUnitStateFragment::Health`, so events, threat and alerts fire.
- Presentation (health tint, team colour, animation phase) is derived on each client from fragments; never RPC it.
- The server decides damage and deaths; clients decide only how a unit is drawn.

## 10. Many units attacking one player

| Direction | Path |
|---|---|
| Unit → unit | `UMassUnitCombatProcessor` via `FMassUnitTargetFragment::TargetEntity` |
| Unit → Actor (player) | Crowd group engagement (`FMassUnitPlayerEngagementConfig`) — configuration only |
| Player → unit | The game's weapon/GAS path → `ApplyDamage` / `DamageUnitAndActivate` / `ReportCrowdUnitDamaged` |

`UMassUnitCombatProcessor` only attacks entities; wiring the player into it is the wrong instinct. Attacking the
player is `ConfigureCrowdGroupEngagement`: `ActivationMode`, `bEnableAttacks`, `AttackRangeOverride`,
`bApplyActorDamage`, `GameplayEffectToTarget` (a GAS effect on the player without an ASC per unit), and
`bUseSharedNavigationPath` (one corridor per group — leave it on). Presentation hangs on `OnCrowdAttackRequested`.

### Attacker limiting

| Setting | Default | Effect |
|---|---|---|
| `MaxSimultaneousAttackers` | 0 (unlimited) | Units must hold one of N group tokens to swing |
| `StandoffDistanceMultiplier` | 1.35 | Where token-less units wait, × attack range |
| `bUseEngagementSlots` / `EngagementSlotCount` | off / 12 | Spread engaged units over fixed angles |

Peak incoming damage becomes `MaxSimultaneousAttackers / AttackCooldown` instead of "however many pathed into
range". Without it: unbounded DPS, and every unit steering to one point forms a solid ring that pushes forever.

Details that matter if this is ever reimplemented:
- Tokens expire on a timestamp, so a unit that dies or leaves mid-swing cannot leak one.
- The hold outlasts the round-robin revisit period (`ceil(GroupSize / Budget)` updates), not just the cooldown;
  otherwise the cap leaks exactly when the group is largest. Over-holding only ever reduces attackers.
- Every group prunes once per update, not only on acquisition, or stale holders linger in `ActiveAttackers`.
- Disengaging clears the pool.
- Tokens do not rotate: the same N keep attacking while others wait. Deliberate for now (see ROADMAP).

Diagnostics: `ActiveAttackers`, `UnitsAwaitingAttackToken`; with engagement debug on, in-range units draw red
(swinging) or yellow (waiting). A solid red ring means the cap is unset or too high.

## 11. Diagnostics before blaming the renderer

| Question | Call |
|---|---|
| Draw calls? | `GetInstancedMeshComponentCount()` |
| Units drawn? | `GetInstancedMeshInstanceCount()` |
| Leaking parked slots? | `GetInstancedMeshAllocatedSlotCount()` vs the above |
| Churning instances? | `GetInstancedMeshTopologyRevision()` — stable while units move |
| Pool thrashing? | `GetActiveSkeletalMeshCount()` / `GetSkeletalMeshCapacity()` |
| Where does time go? | `stat MassUnitSystem`, CSV category `MassUnits` |

A topology revision that climbs every update usually means a unit oscillating across a threshold that needs hysteresis.

## 12. Anti-patterns

| Don't | Because | Instead |
|---|---|---|
| One subclass per unit variant | Class explosion, no batching | One class, many `UUnitTemplate`s |
| An Actor or ASC per crowd unit | Defeats Mass | Fragments + the damage seam |
| `RemoveInstance` mid-array | Swaps per-unit data | Park and recycle the slot |
| `bMarkRenderStateDirty = true` | Rebuilds the scene proxy | Leave false |
| Upload elapsed animation time | Dirties custom data every frame | Upload the start time |
| Material instance per unit | Per-unit UObjects | Per-instance custom data |
| Second material slot for a glow | Doubles draw calls | Pack a mask into UVs |
| HISM for a moving crowd | CPU tree rebuilds | ISM under GPUScene |
| Hand LODs with reduction on | Reducer discards the silhouette | Reset percent triangles to 1.0 |
| Per-frame `UE_LOG` | Log spam at scale | Visual debug + screen HUD |
| A new timer per system | Unstaggered spikes | Extend the LOD interval bands |
