# Guide — Vertex animation, near-band animation and unit statuses

What a unit needs so that it animates in the crowd band (instanced static mesh + vertex-animation texture),
animates in the near band (pooled skeletal mesh), and receives gameplay statuses and tags. Written against the
*Unreleased* (1.5.0) API.

Companion guides: [authoring_pipeline.md](authoring_pipeline.md) (budgets, pivot, LODs) and
[scalability_best_practices.md](scalability_best_practices.md).

## 1. The pipeline

```
DCC mesh ─► rig on one shared skeleton ─► clips ─► FBX import (SkeletalMesh + AnimSequences)
   │
   ├─► NEAR BAND   UUnitTemplate::SkeletalMesh + Idle/Move/Attack/Death/StunAnimation
   │               (or AnimationBlueprintClass parented to UMassUnitAnimInstance)
   │
   └─► CROWD BAND  engine AnimToTexture bake ─► textures + static mesh
                   ─► UUnitTemplate::StaticMesh + VertexAnimationClips
                   ─► a crowd material that turns custom data into a frame (project-owned)
```

Both bands use one set of clips on one skeleton, so near and far units play the same move. The plugin ships no
bake script and no material; the reference adopter keeps its own in its game repo.

### Rig and bake

- One skeleton (a standard one such as the UE5 Mannequin, or your own) for every variant: shared clips, retarget
  compatibility, variants become pure template data.
- **AnimToTexture Bone mode** stores bone transforms: texture size grows with bones × frames and is independent of
  vertex count, so a 2-3k-triangle crowd mesh is fine. **Vertex mode** stores every vertex: keep the crowd mesh at
  about 600 vertices or fewer. The plugin only supplies frame numbers, so either mode works; the material decides.
- No root motion (the simulation moves the unit). Pivot at the feet, face +X. Attack clips play in place; note
  the impact frame for `AttackImpactDelay`.

### Clips

| Tag (`MassUnit.Animation.*`) | Loops | Notes |
|---|---|---|
| `Idle` | yes | **Required.** Any missing clip falls back to Idle. |
| `Walk` | yes | Used for Run when there is no Run clip (and vice versa). |
| `Run` | yes | Played above `UUnitTemplate::RunSpeedThreshold` (0 = Walk only). |
| `Attack` | no | Restarts on every swing; returns to Idle when it ends (`bReturnToIdleWhenDone`). |
| `Death` | no | Holds its last frame; keep it shorter than your corpse linger time. |
| `Stun` | either | Played while a stun status is active. |

Each `FMassUnitVATClip` row has `AnimationTag`, `StartFrame` and `NumFrames` (from the AnimToTexture data asset),
`FramesPerSecond`, `bLoop`, `bReturnToIdleWhenDone`. The template validates the table (duplicate rows, missing
Idle, looping Death/Attack, impact delay longer than the cooldown).

## 2. Crowd band: how a clip reaches the GPU

The renderer writes the unit's current clip into per-instance custom data (append-only contract in
`Public/Visual/CrowdInstanceRenderer.h`):

| Channel | Value |
|---|---|
| 1 `AnimationStartTime` | World time the clip started; re-stamped per swing and when a one-shot hands back to Idle |
| 8 `RandomSeed` | Per-unit 0-1 offset so loops don't march in lockstep |
| 9 `Speed` | Horizontal cm/s (walk/run cross-fade) |
| 10 `ClipStartFrame` | First frame in the baked texture |
| 11 `ClipFrameCount` | Frames in the clip; 0 = template has no clip table |
| 12 `ClipFramesPerSecond` | Positive loops; negative plays once and holds |

Your material computes `frame = ClipStartFrame + f(GameTime - AnimationStartTime, fps, count, seed)` and feeds it to
AnimToTexture's bone or vertex material function with auto-play off.
`UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset` sets the texture and bounds parameters.

Cost: no per-frame CPU animation; custom data is uploaded only when a unit changes clip or swings.

## 3. Near band: clips or an Animation Blueprint

`UUnitMeshPool` picks one of two paths per template:

- **Clip slots set** (single-node playback): each swing restarts the attack clip; a finished one-shot returns to
  Idle; a unit promoted from the crowd band starts at the phase the vertex animation was showing.
- **`AnimationBlueprintClass` set, clip slots empty:** parent the ABP to `UMassUnitAnimInstance`. The pool pushes
  `Unit`, `AnimationTag`, `UnitState`, `StateTime`, `Speed`, `HealthPercent`, `AttackSerial`, `bIsDead`,
  `bIsStunned`, `bIsAttacking`, `OwnedTags`, `TeamID` on each visual update, and fires `OnAttackSwing` (play the
  attack montage) and `OnUnitAssigned` (pooled meshes are reused: reset per-unit state here).

## 4. Swing timing

`UUnitTemplate::AttackImpactDelay` is the time from swing start to the frame damage lands (e.g. 0.33 s for a
30 fps clip hitting on frame 10).

- Zero keeps the original behaviour: damage lands when the swing starts.
- A unit that leaves range, loses its attack token, routs or is stunned before the impact frame whiffs.
- Engaged crowd units are revisited round-robin, so an impact lands on the first visit after the delay.

## 5. Tags and statuses (the unit's stand-in for an ASC)

A unit has no ASC. `UMassUnitEntityManager` gives it:

**Owned tags** — template `GrantedTags` + loose tags (`AddUnitTags` / `RemoveUnitTags`) + every active status tag.
Query with `UnitHasTag` (hierarchical), `UnitHasAnyTags`, `UnitMatchesTagQuery`, `FindUnitsMatchingTagQuery`
(radius ≤ 0 searches the world).

**Timed statuses** — `FMassUnitStatusEffect`, applied with `ApplyUnitStatus`:

| Field | Meaning |
|---|---|
| `StatusTag` | Identity; also an owned tag while active |
| `Duration` | ≤ 0 = permanent |
| `DamagePerPeriod`, `Period`, `DamageTypeTag` | Damage tick (scaled by stacks) |
| `MoveSpeedMultiplier` | Multiplies with other statuses; 0 roots the unit |
| `bStuns` | Unit cannot move or attack |
| `bStacks`, `MaxStacks` | Re-applying adds a stack up to the cap |

Rules: re-applying refreshes and never shortens (the later expiry wins, permanent stays permanent).
`RemoveUnitStatus` removes an exact tag; `RemoveUnitStatusesMatching` removes by parent tag (a cleanse).
`StunUnit` applies `MassUnit.Status.Stunned`. `StatusImmunityTags` refuses statuses hierarchically. Death clears
every status. Statuses advance at 10 Hz and only for units that have one.

**Damage ticks** — `OnUnitStatusDamageTick` fires per tick. If anything is bound, the plugin does **not** apply
the damage itself, so a game can route ticks through its own pipeline (crits, armour, kill credit). Unbound, the
plugin applies it directly. `OnUnitStatusChanged` reports apply/remove.

## 6. Who is a valid target

`UMassUnitCrowdSystem::SetCrowdTargetFilter` installs the game's hostility rule for automatic target acquisition,
proximity threat and damage reports (friendly fire neither builds threat nor turns the crowd). Explicit
`ActivateCrowdGroupForActor` calls are not filtered. With no filter, behaviour is unchanged.

## 7. Bridging a GAS game

These pieces belong in the game, not the plugin:

- A library that takes an existing Gameplay Effect spec and applies it to units: instant damage → the game's
  damage path → `ApplyDamage` / `ReportCrowdUnitDamaged`; each granted tag of a duration effect → a unit status;
  periodic SetByCaller damage → the status's damage tick.
- A settings table mapping status tags to speed multipliers, stuns and damage types.
- Limits to document for designers: attribute modifiers and execution calculations never run on a unit (it has no
  attributes), so author crowd-relevant effects with SetByCaller damage and granted status tags.

The reference adopter (MetalLegs) implements this as a crowd-effect library plus a status-settings table in its
game module.
