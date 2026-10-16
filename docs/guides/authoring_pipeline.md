# Guide — Authoring a crowd unit (Blender to Unreal)

How to build a unit in Blender that works in both representation bands and holds up at crowd scale. The *why*
behind the budgets is in [scalability_best_practices.md](scalability_best_practices.md); the animation wiring is in
[vat_animation_and_status.md](vat_animation_and_status.md); a version for an external modeller is
[artist_brief.md](artist_brief.md).

## 0. Checklist

**Blender**
- [ ] Real-world metric size, origin at the **feet**, model faces **+X** at export (see §4)
- [ ] One root bone at the origin, ≤ 20 deform bones, no root motion, IK/control bones marked non-deform
- [ ] Weights: 1-2 influences per vertex; rigid plates 100% to one bone
- [ ] Two meshes on the same armature: detailed (2-3k tris) and crowd (see §2 for its budget)
- [ ] Clips at a uniform 30 fps: Idle, Walk (and optionally Run), Attack, Death, Stun
- [ ] Bake constraints/drivers to keys; apply all transforms on mesh **and** armature; export in centimetres

**Unreal**
- [ ] Import the detailed skeletal FBX first (creates the shared `USkeleton`); import the crowd mesh against that
      same skeleton
- [ ] Verify: bounds min Z = 0, sensible height, mesh faces +X
- [ ] Crowd static mesh: no collision, one material slot, LOD chain with reduction off on hand-made LODs
- [ ] Bake clips with AnimToTexture; fill `VertexAnimationClips`
- [ ] New `UUnitTemplate`; near band = `SkeletalMesh` + clips (or an ABP), crowd band = `StaticMesh` + clip table,
      `TeamID` / `TeamColor`
- [ ] Material reads per-instance custom data (team 4-6, health 7, clip 10-12), else every unit is identical
- [ ] On the spawner `SpawnHeight = 0`; in the crowd config `NavigationHeightOffset = 0`

## 1. Two representations from one unit

| Band | Asset | Driven by | Cap |
|---|---|---|---|
| Near | Skeletal mesh + `UAnimSequence`s (or ABP) | `UUnitMeshPool` | `MaxSkeletalMeshUnits` (100) |
| Crowd | Cheap static mesh + baked animation textures | Material, per-instance custom data | One draw call per mesh per LOD |

A unit with only one of them either costs too much up close or looks wrong in the crowd.

## 2. Budgets

| Thing | Target | Bound by |
|---|---|---|
| Bones | ≤ 20 | Near-band evaluation cost (only ~100 exist at once) |
| Skeletal LOD0 | 2-3k triangles | Near band only |
| Crowd mesh | Bone-mode bake: a few thousand triangles is fine. Vertex-mode bake: **300-600 vertices** | Vertex mode texture = vertices × frames |
| Total frames, all clips | ≤ ~200 at 30 fps | Texture height |
| Far LOD | a handful of triangles (crossed quads) | Far band |
| Material slots | **1** | Each slot is another draw call per instanced component |
| Vertex influences | 1-2 (max 4) | Skinning cost |

## 3. Rig

```
root  (origin, at the feet, Z = 0, never animated)
└ pelvis → spine → head
          ├ shoulder.L → elbow.L → hand.L      (and .R)
          └ thigh.L → shin.L → foot.L          (and .R)
```

- **Single root bone** at the origin.
- **No root motion.** `UMassUnitMovementProcessor` moves the unit; root motion makes it slide or stutter.
- **Deform bones only in the export.** Rig freely, clear Deform on IK/control bones, tick *Only Deform Bones*.
- **Rigid weighting for machines.** Plates should not bend; it is cheaper and bakes cleanly.
- **One skeleton for every variant.** Identical bone names mean one `USkeleton`, shared clips, no retargeting.

## 4. Scale and orientation

Unreal is Z-up, **+X forward**, 1 unit = 1 cm.

- The crowd has no mesh yaw offset: `UUnitMeshPool` and `UCrowdInstanceRenderer` apply the unit transform
  directly and the movement processor yaws units to their velocity, so **the mesh itself must face +X**. A
  character Blueprint usually hides a +Y-facing mesh with a -90° yaw; crowd units cannot.
- Measured 2026-10-15: a model facing -Y in Blender, exported with the default FBX axes (`-Z Forward`, `Y Up`),
  lands facing **+Y**. Face the model +X in Blender at export time (e.g. rotate the rig +90° about Z in a
  temporary state and revert).
- Export in **centimetres**: scene unit scale 0.01 with rig and meshes scaled ×100 and applied (pose-bone location
  keys ×100 too), then `apply_scale_options = FBX_SCALE_NONE`. A metre scene exported with `FBX_SCALE_ALL` imports
  with the root bone at scale 100: it looks right but breaks ragdoll bodies and the bone bake.
- `Ctrl+A → All Transforms` on mesh and armature. Unapplied non-uniform scale shears in Unreal.
- Bake constraints and drivers to keys; they do not survive FBX.

Verify after import: min Z = 0, expected height, faces +X. All three fail silently.

## 5. Animation set

| Tag (`MassUnit.Animation.*`) | Template slot | Loops |
|---|---|---|
| `Idle` (fallback for anything missing) | `IdleAnimation` | yes |
| `Walk` / `Run` | `MoveAnimation` (near band); separate clip rows in the VAT table | yes |
| `Attack` | `AttackAnimation` | no |
| `Death` | `DeathAnimation` | no |
| `Stun` | `StunAnimation` | yes |

- Uniform 30 fps. Total frames drive texture size, so a 40-frame walk instead of 120 is a 3× saving.
- Seamless loops; a pop repeated across a thousand units reads as a bug.
- **Attacks play in place.** Express a lunge through pose (crouch, coil, extend, recover), never root travel. Keep
  it short (20-30 frames) and put the impact about a third in. Set `AttackImpactDelay` to that frame's time so
  damage lands on it.
- Make the wind-up a silhouette change if it must read at distance; at far LODs the unit is a few blocks.

## 6. Build order

1. Model, rig, animate; two meshes on one armature.
2. Export two FBXs: skeletal (mesh + armature + clips) and the crowd mesh skinned to the same armature.
3. Import the skeletal mesh first, then the crowd mesh against that skeleton.
4. Bake with the engine's **AnimToTexture** plugin (`UAnimToTextureDataAsset`: skeletal mesh, static-mesh copy,
   clips; Bone or Vertex mode per §2). Copy each clip's start frame and length into `VertexAnimationClips`.
5. LOD the crowd static mesh (e.g. screen sizes 1.0 / 0.40 / 0.15 / 0.05). On hand-made LODs set
   `Percent Triangles` back to 1.0; `SetNumSourceModels` seeds 50/25/12.5% reduction that re-simplifies your work.
6. Remove collision from the crowd static mesh.
7. Wire the template (checklist above).
8. Set `SpawnHeight` and `NavigationHeightOffset` to 0. Both default to 50 to centre the 100 cm engine cube; a
   feet-pivot mesh left at 50 hovers half a metre.

## 7. Material

The instanced path creates no material instance per unit; everything per-unit arrives through custom data
(channel table in `Public/Visual/CrowdInstanceRenderer.h`).

- Minimum: `PerInstanceCustomData` 4/5/6 → team tint; 7 → health 0-1.
- Give every `PerInstanceCustomData` node a non-zero `ConstDefaultValue`, or thumbnails and preview actors render black.
- Animation: channels 1, 8 and 10-12 → frame number → AnimToTexture's material function (see the VAT guide).
- Without a bake, a cheap interim is a procedural walk in world position offset driven by `Speed`, `RandomSeed`
  and `AnimationStartTime`.

## 8. Non-destructive rollout

| Do | Instead of |
|---|---|
| Create a new `UUnitTemplate` for a new unit | Editing a template other levels use |
| Put assets in their own folder | Mixing them into a shared folder |
| Set `SpawnHeight` / `NavigationHeightOffset` on your spawner | Changing the C++ defaults (they exist for the cube) |
| Copy a working crowd material | Editing the shared one |
| Configure a new crowd group | Repointing an existing group |

The renderer needs no change for a new unit: `UCrowdInstanceRenderer` buckets by `UStaticMesh`, so a new mesh gets
its own component the first time a unit using it is visible. Rollback is deleting the new folder and template.
