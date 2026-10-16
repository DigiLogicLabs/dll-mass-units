# Mass Unit System — Scalability Best Practices

Working rules for building on this plugin at MetalLegs' stated target: **100+ players, 100+ AI, 20+
vehicles, 5000+ projectiles, 30-60 Hz server tick**. Every rule below is tied to code that exists in
this plugin today, with the file to look at. Where a rule exists because of a specific engine
behaviour, that behaviour is named — those are the ones that get silently violated otherwise.

---

## 1. The representation ladder

A unit is never "a mesh". It is whichever representation its current distance band earns. The ladder,
cheapest last:

| Band | Representation | Owner | Budget |
|---|---|---|---|
| Melee / hero range | `USkeletalMeshComponent`, real animation | `UUnitMeshPool` | Hard-capped by `MaxSkeletalMeshUnits` (default 100) |
| Mid | Instanced static mesh, LOD0-LOD2, material-driven animation | `UCrowdInstanceRenderer` | Unbounded, one draw call per mesh per LOD |
| Far | Instanced static mesh, LOD3 crossed-quad | same component | Free |
| Beyond `MaxVisibleDistance` | Nothing | — | Zero |

**Rules**

- **Never let a representation decide its own budget.** `FMassUnitVisualFragment::bWantsSkeletalMesh`
  is a *request* set by distance; `UUnitMeshPool` independently decides whether capacity exists. Keep
  that split. A representation that promotes itself has no upper bound.
- **Always pair a promotion threshold with hysteresis.** `SkeletalMeshHysteresis` (default 1.2)
  widens the exit distance so a unit hovering on the boundary does not allocate and release a skeletal
  mesh every update. Any new threshold you add needs the same treatment.
- **Sort promotions by distance, not by iteration order.** `UUnitMeshPool::UpdateUnitMeshes` sorts
  candidates by `ViewerDistanceSquared` before filling the pool, so the nearest units win the scarce
  slots. Filling in entity order gives the player whichever units happen to be early in the archetype.

---

## 2. Instanced rendering: the stable-slot contract

`UCrowdInstanceRenderer` owns one `UInstancedStaticMeshComponent` per unique static mesh, and each
entity owns an **instance slot for its whole lifetime**.

**Why slots are stable.** Per-instance custom data is addressed by instance index. Mid-array instance
removal reindexes every later instance, which hands one unit's team colour and health to a different
unit. That shows up as colours and damage state popping around the crowd whenever anything dies — a
bug that is very hard to attribute after the fact. Released slots are therefore **parked at zero
scale and recycled**, never removed from the middle.

**Rules**

- Tail-only compaction. `CompactBucket` trims parked slots off the end and never touches a live one.
- One contiguous upload per bucket per update: build the full-length transform mirror, then a single
  `BatchUpdateInstancesTransforms(0, ...)`. Do not reach for `UpdateInstanceTransformById` — the
  engine's stable-ID API is per-instance, so a fully-moving crowd pays thousands of virtual calls.
- **`bMarkRenderStateDirty` stays `false`.** Since 5.5 the primitive instance data manager records
  dirty instances and streams deltas into GPUScene by itself. Passing `true` rebuilds the entire scene
  proxy every update, which is the single easiest way to destroy crowd performance while believing you
  are being careful.
- Custom data uploads only when a value actually changed (`WriteCustomData` compares first). This is
  why animation **start time** is uploaded rather than elapsed time — elapsed time changes every frame
  and would dirty the buffer forever.

**ISM, not HISM.** Deliberate. Under GPUScene, ISM instances are frustum-culled and LOD-selected per
instance on the GPU. HISM layers on a CPU cluster tree that assumes mostly-static instances and must
be rebuilt as they move — a net loss for a crowd where everything moves every update. Only reconsider
if you ship a platform without GPUScene.

---

## 3. The per-instance custom data contract

Channels are declared once in `UE::MassUnitSystem::CrowdCustomData`
(`Public/Visual/CrowdInstanceRenderer.h`) and consumed by `M_SwarmBot_Instanced`.

```
0 AnimationIndex     3 TeamID          6 TeamColorB     9 Speed
1 AnimationStartTime 4 TeamColorR      7 HealthPercent
2 LODLevel           5 TeamColorG      8 RandomSeed
```

**Rules**

- **Append only.** Renumbering a channel silently repaints every material that reads it. Add at the
  end and bump `NumFloats`.
- Anything that varies per unit belongs here, not in a material instance. A dynamic material instance
  per unit reintroduces per-unit UObjects and is the usual reason crowd rendering collapses.
- Give every `PerInstanceCustomData` node a sensible `ConstDefaultValue`. Outside an instanced
  component — thumbnails, preview actors, a plain `AStaticMeshActor` — the node returns its default,
  and a default of zero renders the unit black with no emissive, which reads as a broken material.
- `RandomSeed` exists so units de-synchronise. Derive it from the entity index through a
  golden-ratio scatter (`MakeRandomSeed`), not `Index % N`, or units spawned in one batch animate in
  lockstep.

---

## 4. LOD authoring

Generated by `Tools/Python/make_swarmbot_visual.py`. Current budget:

| LOD | Triangles | Screen size | Content |
|---|---|---|---|
| 0 | 144 | 1.00 | Full bot: torso, head, optic, shoulders, cannon, muzzle, thruster, legs, feet |
| 1 | 108 | 0.40 | Optic, muzzle, thruster dropped |
| 2 | 48 | 0.15 | Four blocks: torso (shoulders absorbed), head, two legs |
| 3 | 4 | 0.05 | Crossed quads |

**Rules**

- **Turn off automatic reduction on every hand-authored LOD.** `SetNumSourceModels` seeds LODs 1+
  with 50% / 25% / 12.5% reduction. Left alone, the reducer re-simplifies your authored LOD on top of
  itself and quietly discards the silhouette you chose. The generator resets
  `percent_triangles`/`percent_vertices` to 1.0 for every LOD.
- **One material slot.** A second slot doubles the draw calls of every instanced component. The
  emissive mask is packed into UV0's U band (body `[0,1]`, emissive `[2,3]`, recovered with
  `saturate(U-1)`), because mesh description exposes no way to register a second UV channel from
  Python and vertex colours are not settable there either.
- **Crossed quads, not camera-facing billboards, for the far LOD.** A true billboard needs world
  position offset evaluated per vertex per instance. At the range where LOD3 engages, the unit covers
  a handful of pixels and the cross reads identically for free.
- Author the pivot at the feet (Z = 0). See the pivot warning in §8.
- Resolve face winding geometrically rather than assuming a convention. `add_quad` flips a quad whose
  normal points at the hull centre. An inverted crowd mesh presents as "units are invisible from the
  front", which is miserable to debug at 3000 instances.

---

## 5. Update budgeting — the four independent clocks

Scale comes from these being *separate*. Conflating them is the most common regression.

| Clock | Setting | Default | Governs |
|---|---|---|---|
| Visual upload | `VisualUpdateInterval` | 0.033 s | ISM/Niagara transform + custom data upload |
| Crowd decisions | `CrowdUpdateInterval` | 0.1 s | Steering, destinations, spatial hash |
| Crowd throughput | `MaxCrowdUnitsPerUpdate` | 500 | Units touched per crowd callback |
| Visibility / LOD | `VisibilityLODUpdateIntervals` | 0.05→1.0 s per band | Distance and LOD reclassification |

**Rules**

- **Stagger everything periodic.** `UMassUnitVisibilityProcessor` jitters each entity's next update by
  `0.85–1.15×` off the entity index. Without jitter, a batch spawned on one frame re-evaluates on the
  same frame forever, producing a periodic spike that profiles as "random hitching".
- Distant units think less often, by design: the LOD band indexes the interval array. Extend the array
  rather than adding a new timer.
- Round-robin, do not truncate. A budget that always processes the first N units starves the rest.
- Movement stays smooth even though decisions are budgeted — steering runs on the Mass movement
  processor every tick, decisions do not. Keep that separation when adding behaviour.

---

## 6. Processor discipline

- Iterate with `ForEachEntityChunk` and `GetFragmentView` / `GetMutableFragmentView`. Chunked linear
  access is the entire point of Mass; random `GetFragmentDataPtr` per entity gives up the cache
  behaviour. `UCrowdInstanceRenderer` does use random access deliberately, because it is handed an
  arbitrary handle list rather than a query — that is the exception, not a licence.
- Declare the narrowest access you need (`ReadOnly` vs `ReadWrite`). It is what lets the scheduler
  parallelise, and it is free to get right up front.
- Hoist settings reads out of the loop. Read `GetDefault<UMassUnitSystemSettings>()` once per
  `Execute`, never per entity.
- Capture by value into the chunk lambda only what is small; large arrays should be captured by
  reference from an outer scope that outlives the call.
- **Early-out on the dedicated server for anything visual.** The visibility processor returns
  immediately on `NM_DedicatedServer`. Any new presentation processor must do the same or the server
  pays full cost for work nobody sees.
- Allocate nothing per entity per update. The renderer keeps full-length mirrors and `Reset()`s
  scratch arrays rather than reallocating.

---

## 7. Rendering cost that is not triangles

- **Shadows are the crowd killer.** Thousands of small movable casters re-rendered per cascade dwarfs
  the mesh cost. Crowd instances default to `CastShadow = false`, `bCastContactShadow = false`,
  `bCastFarShadow = false` (`bCrowdInstancesCastShadows` overrides for small, close encounters).
- Keep crowds out of the global lighting passes: `bAffectDistanceFieldLighting = false`,
  `bAffectDynamicIndirectLighting = false`, `SetReceivesDecals(false)`.
- `SetCullDistances(InstanceStartCullDistance, InstanceEndCullDistance)` culls per instance on the
  GPU and is cheaper than anything you can do on the CPU. Set the start distance below the end
  distance to fade rather than pop.
- No collision, no overlap events, no navigation: `SetCollisionEnabled(NoCollision)`,
  `SetGenerateOverlapEvents(false)`, `SetCanEverAffectNavigation(false)`, and pass
  `bUpdateNavigation = false` to `AddInstances`. Navigation rebuilds triggered by moving instances are
  an easy accidental frame-time cliff.

---

## 8. Content setup for a new unit type

1. Author or generate the static mesh with a **feet pivot (Z = 0)** and a full LOD chain.
2. Assign a single material that reads the custom-data channels in §3.
3. Point a `UUnitTemplate` at it (`StaticMesh`, optional `SkeletalMesh` + state clips for the near
   band, `TeamID`, `TeamColor`).
4. **Set the ground offset to 0.** `AMassUnitSpawner::SpawnHeight` and
   `FMassUnitCrowdConfig::NavigationHeightOffset` both default to **50**, which exists to centre the
   100 cm asset-free engine cube. A feet-pivot mesh left at 50 floats half a metre off the ground.
   This is the single most likely "why is my new unit hovering" question.
5. Verify draw calls did not multiply: `GetInstancedMeshComponentCount()` should equal the number of
   distinct unit meshes in view, not the number of units.

---

## 9. Networking

- **Do not replicate per entity.** Mass entities are not Actors and must not acquire an ASC, an Actor,
  or a replicated component each. Crowd state is summarised through the project's rep bubble
  (`MLCrowdRepBubble`) rather than replicated per unit.
- Damage enters through the GAS seam (`MLMassUnitDamageLibrary` → `ApplyDamageInternal`), not by
  writing `FMassUnitStateFragment::Health` directly, so faction gating, rewards, and feedback all fire.
- Presentation is client-derived. Health percent, team colour, and animation phase are read off
  fragments and pushed into custom data on the client. Never send them as RPCs.
- Server authority still applies: the server decides deaths and damage; clients only decide how a unit
  is drawn.

---

---

## 9a. Crowd combat — many units attacking one player

Two separate paths exist, and confusing them wastes a lot of time.

| Direction | Path | Status |
|---|---|---|
| Unit → unit | `UMassUnitCombatProcessor`, `FMassUnitTargetFragment::TargetEntity` | Complete |
| Unit → **Actor** (the player) | Crowd group engagement, `FMassUnitPlayerEngagementConfig` | Complete, **configuration only** |
| Player → unit | `UMLMassCrowdCombatSubsystem::DamageUnit`, segment hit tests, crits, death events | Complete |

`UMassUnitCombatProcessor` only ever attacks another **entity**. It cannot damage the player — it
resolves `Target.TargetEntity` through the entity manager and bails if that is invalid. Wiring the
player into it is the wrong instinct.

### Attacking the player is config, not code

`ConfigureCrowdGroupEngagement(GroupHandle, Config, true)` on `UMassUnitCrowdSystem` is the whole
entry point. The relevant fields of `FMassUnitPlayerEngagementConfig`:

- `ActivationMode` — manual, on interaction/damage, on player proximity, or always acquire
- `bEnableAttacks`, `AttackRangeOverride` (zero uses each template's `AttackRange`)
- `bApplyActorDamage` — standard Actor damage
- `GameplayEffectToTarget` + `GameplayEffectLevel` — applies a Gameplay Effect to the player **without
  allocating an ASC or an Actor per Mass entity**. This is the seam that keeps crowd combat inside GAS.
- `bUseSharedNavigationPath` — one navmesh corridor per group rather than a path query per unit. This
  is what makes hundreds of simultaneous attackers viable at all; leave it on.
- `EngagedMoveSpeedMultiplier`, follow spread, auto-deactivate distance

Attack events surface on the `OnCrowdAttack` delegate as `(AttackingUnit, TargetActor, ...)`, which is
where presentation (lunge VFX, sound, hit reaction) should hang. Diagnostics: `EngagedGroups`,
`EngagedUnits`, `AttacksRequested`.

### Attacker limiting

Both controls below default to **off**, preserving the pre-token behaviour. Turn them on deliberately.

| Setting | Default | Effect |
|---|---|---|
| `MaxSimultaneousAttackers` | `0` (unlimited) | Units must hold one of N group attack tokens to swing |
| `StandoffDistanceMultiplier` | `1.35` | Where token-less units wait, as a multiple of attack range |
| `bUseEngagementSlots` | `false` | Spread engaged units over fixed angular slots around the target |
| `EngagementSlotCount` | `12` | Number of those slots |

A token is taken when a unit commits an attack, so **peak incoming damage is
`MaxSimultaneousAttackers / AttackCooldown`** — a designed number rather than a function of how many
units happened to path into range. Tokens expire on a timestamp rather than being explicitly released,
so a unit that dies, despawns, or leaves range mid-swing cannot leak one and starve the pool.

Three details that are easy to get wrong if this is ever reimplemented:

- **The hold outlasts the revisit period, not just the cooldown.** Engaged units are processed
  round-robin under `MaxCrowdUnitsPerUpdate`, so a large group revisits a given unit only every
  `ceil(GroupSize / Budget)` updates. A token that expired inside that gap would be released by a unit
  still mid-attack and picked up by another, leaking the cap precisely when the crowd is largest.
  Over-holding only ever produces *fewer* simultaneous attackers, so erring long is the safe direction.
- **Every group prunes once per update, not only on acquisition.** Pruning solely inside the acquire
  path means that once all units leave attack range nothing expires the stale holders, and
  `ActiveAttackers` keeps reporting them indefinitely.
- **Disengaging clears the pool.** Otherwise a re-engage begins with tokens still held by units that
  are no longer attacking anything, throttling the first wave.

Diagnostics: `ActiveAttackers` (tokens held) and `UnitsAwaitingAttackToken` (in range, waiting).
With `bEnableVisualDebug`, in-range units draw a sphere — **red** while swinging, **yellow** while
waiting for a token. A solid red ring means the cap is unset or too high.

Slots are derived from the entity index rather than assigned, so they need no bookkeeping and survive
respawns. Two units can share a slot; the existing separation steering pushes them apart.

**Known characteristic - tokens do not rotate.** A unit renews its token every update while it stays in
range, so the same N units keep attacking and the rest wait at standoff until one of the holders dies or
withdraws. That is correct for the primary goal (incoming damage stays bounded, and the doughnut is
gone) but it means the waiting ranks look static rather than taking turns. If rotation is wanted, renew
only while `AttackCooldownRemaining > 0` so a unit must re-contend after each swing - at the cost of
units occasionally backing off mid-fight, which needs its own hysteresis. Left as-is deliberately until
the current behaviour has been seen in PIE.

**Without these two, at scale:**

1. **Unbounded damage.** 200 units × 5 damage on a 1 s cooldown is 1000 DPS. Balance stops being a
   function of unit strength and becomes a function of how many happened to path into range.
2. **The doughnut.** Every engaged unit steers to the same point, so they pile into a solid ring and
   the ones behind push forward forever. It reads as a swarm of units clipping through each other
   rather than a coordinated attack.

Still open: **tiered aggression by distance band** — reuse the existing crowd LOD bands so near units
contest tokens, mid units approach, and far units stay dormant. Costs nothing extra because the
banding already runs.

Token acquisition runs on the crowd group's budgeted update (`MaxCrowdUnitsPerUpdate`), not per frame —
it is a decision, and decisions are already budgeted and staggered.

### Leap and pounce attacks

Movement is planar steering plus a range-gated attack; there is no ballistic or dash state. A leaping
attack is genuinely new work, and there are two tiers:

- **Cosmetic lunge** — keep the existing range-gated attack, add a lunge animation and a short forward
  world-position-offset surge in the material driven by `AnimationStartTime`. No C++, no new state, no
  effect on collision or damage timing. Reads well for small fast units.
- **Real leap** — a transient state where the unit leaves crowd steering, follows a ballistic arc to a
  predicted landing point, and applies damage on landing. Needs a new state in
  `EMassUnitState`, an arc integrator in the movement processor that skips normal steering while
  active, and a token (see above) so the whole swarm cannot leap at once. Budget it as its own task
  with PIE verification; a half-implemented leap that fights the steering produces units that teleport
  or stick.

Start with the cosmetic lunge and attack tokens. Together they deliver the "swarm of robotic spiders
mobbing the player" read, and neither requires touching the movement integrator.

## 10. Diagnostics to check before blaming the renderer

| Question | Call |
|---|---|
| How many draw calls am I paying? | `GetInstancedMeshComponentCount()` |
| How many units are actually drawn? | `GetInstancedMeshInstanceCount()` |
| Am I leaking parked slots? | `GetInstancedMeshAllocatedSlotCount()` vs the above |
| Is something churning instances every frame? | `GetInstancedMeshTopologyRevision()` — should be **stable** while units move |
| Is the skeletal pool thrashing? | Pool capacity/hysteresis diagnostics on `UUnitMeshPool` |

A topology revision that climbs every update means slots are being allocated and released
continuously — usually a unit oscillating across a visibility threshold that needs hysteresis.

---

## 11. Anti-patterns

| Do not | Because | Instead |
|---|---|---|
| One subclass per unit variant | Class explosion, no batching | One class, many `UUnitTemplate` / config assets |
| An Actor or ASC per crowd unit | Defeats the entire point of Mass | Fragments + the GAS damage seam |
| `RemoveInstance` mid-array | Reindexes later instances, swaps per-unit data | Park at zero scale, recycle the slot |
| `bMarkRenderStateDirty = true` on batch updates | Rebuilds the scene proxy every update | Leave it false; GPUScene streams deltas |
| Uploading elapsed animation time | Dirties custom data every frame forever | Upload the start time, derive phase in the material |
| A dynamic material instance per unit | Per-unit UObjects, no batching | Per-instance custom data |
| A second material slot for a glow | Doubles draw calls per component | Pack a mask into UVs |
| HISM for a moving crowd | CPU cluster tree rebuilt as instances move | ISM under GPUScene |
| Hand-authored LODs with reduction left on | Reducer discards your silhouette | Reset `percent_triangles` to 1.0 |
| Per-frame `UE_LOG` for crowd state | Log spam at 5000 units | Visual debug + on-screen HUD |
| A new timer per system | Unstaggered spikes | Extend the existing LOD interval bands with jitter |

---

*Companion documents: `architecture.md` (structure), `optimization_techniques_research.md`
(background), `usage.md` (API). Project-level rules live in `MetalLegsGAS/CLAUDE.md`.*
