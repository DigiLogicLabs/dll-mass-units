# Crowd units: vertex animation, near-band animation, and statuses

**Written:** 2026-10-14.
**Covers:** what the plugin needs from a unit, small rigged characters such as goblins in particular, so that
- it animates in the crowd band (instanced static mesh plus vertex-animation texture),
- it animates in the near band (pooled skeletal mesh), and
- gameplay effects and tags reach it.

**Companion docs:**
- [`authoring_pipeline.md`](authoring_pipeline.md): budgets, pivot, LOD.
- [`scalability_best_practices.md`](scalability_best_practices.md).

## 1. The pipeline

```
DCC mesh ─► rig on a standard skeleton ─► Cascadeur clips ─► FBX import (SkeletalMesh + AnimSequences)
      │
      ├─► NEAR BAND  UUnitTemplate::SkeletalMesh + Idle/Move/Attack/Death/StunAnimation
      │              (or AnimationBlueprintClass parented to UMassUnitAnimInstance)
      │
      └─► CROWD BAND UAnimToTextureDataAsset ─► Tools/Python/mass_unit_vat_bake.py
                     ─► baked textures + UUnitTemplate::VertexAnimationClips + StaticMesh
                     ─► crowd material using MF_MassUnitVATFrame (Tools/Python/mass_unit_vat_material.py)
```

Both bands are fed from **one** set of clips on **one** skeleton, so close and far units always play the same move.

### Rig: a standard skeleton is fine

- **Use the UE5 Mannequin skeleton (or one retargeted from it) for every goblin and minion variant.** That gives:
  - shared clips,
  - IK Retargeter compatibility,
  - Cascadeur's mannequin profile.
- **Bake in AnimToTexture *Bone* mode, not Vertex mode.**
  - Bone mode stores bone transforms, so texture size grows with *bones × frames* and is independent of vertex count. A 2–3k-triangle goblin on the roughly 90-bone mannequin skeleton is fine.
  - Vertex mode stores every vertex, so the crowd mesh must stay at about **600 vertices or fewer**.
  - The authoring doc's "≤ 20 bones" budget refers to the cost of the near-band skeletal mesh, not to the bake.
- **No root motion.** The simulation moves the unit, so strip translation from the root as the Glatch import does.
- **Pivot at the feet. Face +X.**
- **Attack clips play in place.** Note the impact frame: it becomes `AttackImpactDelay`.

### Clips

| Tag (`MassUnit.Animation.*`) | Loops | Notes |
|---|---|---|
| Idle | yes | **Required.** Every missing clip falls back to Idle. |
| Walk | yes | Also used for Run when there is no Run clip. |
| Run | yes | Played above `UUnitTemplate::RunSpeedThreshold`. 0 means Walk only. |
| Attack | no | **Restarts on every swing.** When it finishes, the unit returns to Idle until the next swing. |
| Death | no | Holds its last frame. It must fit within the project's corpse linger time (`DeadUnitLingerSeconds`, 3 s). |
| Stun | yes or no | Played while a stun status is active. |

The bake script reads each clip's tag from the sequence name: `idle`, `walk|move|locomotion|jog`, `run|sprint`, `attack|swing|melee|bite`, `death|die`, or `stun|daze|hitreact`. Use `tag_map` to override a name.

## 2. Crowd band: how a clip reaches the GPU

The renderer resolves each unit's current clip and writes it into the per-instance custom data. The channels are an append-only contract defined in `CrowdInstanceRenderer.h`:

| Channel | Value |
|---|---|
| 1 | `AnimationStartTime`: the world time the clip started. It is re-stamped every swing, and moves forward by the clip length when a finished one-shot hands over to Idle. |
| 8 | `RandomSeed`: offsets looping clips per unit. |
| 10 | `ClipStartFrame` |
| 11 | `ClipFrameCount`. 0 means the template has no clip table. |
| 12 | `ClipFramesPerSecond`. Positive loops; negative plays once and holds. |

`MF_MassUnitVATFrame` turns these channels into a frame number. Feed that frame to AnimToTexture's `MF_BoneAnimation` or `MF_VertexAnimation` with **AutoPlay off**. Set the material instance's texture and bounds parameters with `UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset`.

**What this costs:**
- No per-frame CPU animation work.
- Custom data is uploaded only when a unit **changes clip or swings**, not every frame.

## 3. Near band: clips or an Animation Blueprint

The skeletal-mesh pool (`UUnitMeshPool`) uses one of two paths, depending on the template.

### Single-node clips (template clip slots set)
- Every swing restarts the attack clip.
- A finished one-shot returns to Idle.
- A unit promoted from the crowd band starts at the same phase the vertex animation was showing, so there is no snap to frame 0.

### Animation Blueprint (leave the clip slots empty, set `AnimationBlueprintClass`)
**Reparent the ABP to `UMassUnitAnimInstance`.** The pool pushes these values into it on every visual update:
- `AnimationTag`, `UnitState`, `StateTime`
- `Speed`, `HealthPercent`
- `AttackSerial`
- `bIsDead`, `bIsStunned`, `bIsAttacking`
- `OwnedTags` (statuses included)
- `TeamID`

It also fires two events:
- `OnAttackSwing`: play the attack montage here.
- `OnUnitAssigned`: pooled meshes are reused, so reset per-unit state here.

Build the locomotion state machine exactly as you would for a character ABP.

## 4. Swing timing

`UUnitTemplate::AttackImpactDelay` is the time from the start of a swing to the frame its damage lands. Set it to the attack clip's impact pose, for example 0.33 s for a 30 fps clip that hits on frame 10.
- **Zero** keeps the original behaviour: damage lands on the frame the swing starts.
- **Whiffs:** a unit that leaves range, loses its attack token, routs or is stunned before the impact frame deals no damage.
- **Crowd timing:** engaged crowd units are revisited round-robin, so an impact lands on the first visit after the delay, not on the exact frame.

## 5. Gameplay tags and statuses (the unit's stand-in for an ASC)

A Mass unit has no ASC. `UMassUnitEntityManager` gives each unit three things:

### Owned tags
The template's `GrantedTags`, plus loose tags added with `AddUnitTags`, plus the tag of every active status. Query them the same way you would query an ASC:
- `UnitHasTag` (hierarchical)
- `UnitHasAnyTags`
- `UnitMatchesTagQuery`
- `FindUnitsMatchingTagQuery` (radius ≤ 0 searches the whole world)

### Timed statuses
`FMassUnitStatusEffect` is the crowd stand-in for a duration Gameplay Effect. Apply with `ApplyUnitStatus`. Each status has:
- a tag;
- a duration (≤ 0 means permanent);
- `DamagePerPeriod`, `Period` and `DamageTypeTag`;
- `MoveSpeedMultiplier` (multiplies with other statuses; 0 roots the unit);
- `bStuns`;
- `bStacks` and `MaxStacks`.

Rules:
- Re-applying a status refreshes it. If the status stacks, re-applying also adds a stack.
- Remove statuses with `RemoveUnitStatus` (exact tag) or `RemoveUnitStatusesMatching` (a parent tag, which works as a cleanse).
- `StunUnit` applies a `MassUnit.Status.Stunned` status.
- `UUnitTemplate::StatusImmunityTags` refuses statuses hierarchically.
- Death clears every status.

### Damage ticks
- `OnUnitStatusDamageTick` fires on each tick.
- **When something is bound to it, the plugin does not apply the damage itself.** This lets a project route ticks through its own damage pipeline.
- When nothing is bound, the plugin applies the damage directly.

**Cost:** statuses are advanced at 10 Hz, and only for units that currently have one. Units with no status cost nothing.

## 6. Who is a valid target

`UMassUnitCrowdSystem::SetCrowdTargetFilter` installs the project's hostility rule. The filter is applied in three places:
- automatic target acquisition;
- proximity threat;
- damage reports. Friendly fire neither builds threat nor turns the crowd.

Explicit `ActivateCrowdGroupForActor` calls are not filtered. With no filter installed, behaviour is unchanged.

**MetalLegs:** `UMLMassCrowdCombatSubsystem` installs a team filter, controlled by `UMLCrowdStatusSettings::bFilterCrowdTargetsByTeam`.

## 7. MetalLegs GAS bridge (game module)

These pieces live in `Source/GASDocumentation`, not in the plugin.

- **`UMLCrowdEffectLibrary`:** applies an **existing** Gameplay Effect spec to crowd units.
  - Instant damage goes through `UMLMassCrowdCombatSubsystem::DamageUnit`, which handles crit, armour, procs, numbers and kill credit.
  - Each granted tag of a duration effect becomes a unit status.
  - Periodic `Data.Damage` becomes the status's damage tick.
  - Calls: `ApplyGameplayEffectSpecToCrowdUnit`, `ApplyGameplayEffectToCrowdUnit`, `ApplyGameplayEffectSpecToCrowdInRadius`, `GetHostileCrowdUnitsInRadius`.
- **`UMLCrowdStatusSettings`:** defines what each status tag does to a crowd unit:
  - Chilled 0.6× speed, Slowed 0.5×;
  - Frozen, Stunned and Incapacitated stun (hard crowd-control tags from the Crowd Control settings stun too);
  - Burning ticks as thermal damage, Shocked as shock, Corroded as corrosive.
  - It also holds the team-filter switch.
- **Explosions:** `UExplosionConfig::AdditionalTargetEffects`, for example a flashbang's stun, now reaches crowd units in the blast.
- **Area damage:** `bIgnoreSourceMassUnitTeam` is now honoured.

**Limits:**
- An effect's attribute modifiers and execution calculations do not run on a unit, because a unit has no attributes.
- Author crowd-relevant effects with `Data.Damage` SetByCaller and granted status tags. The project's damage and status effects are already authored this way.
