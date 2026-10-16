# Authoring Pipeline — Blender to Mass Crowd Unit

How to build a minion in Blender that works in both of this plugin's representation bands and holds up
at crowd scale. Read `scalability_best_practices.md` first for the *why* behind the budgets.

---

## 0. Quick checklist

Work top to bottom. Every step is additive — see §9 for why nothing here disturbs existing content.

**Blender**

- [ ] Model faces **−Y**, real-world metric size (~1.0–1.3 m tall), origin at the **feet**
- [ ] Rig: one root bone at origin, ≤ 20 deform bones, **no** root motion, IK/control bones marked non-deform
- [ ] Weights: 1–2 influences per vertex; rigid plates painted 100% to one bone
- [ ] Two meshes on the same armature: detailed (2–3k tris) and crowd (**300–600 verts**)
- [ ] Five clips at a uniform 30 fps: Idle, Move, **Attack (the lunge)**, Death, Stun — see §5a
- [ ] Bake constraints/drivers to keyframes, then `Ctrl+A → All Transforms` on mesh **and** armature
- [ ] Export two FBXs with default axes (`Forward: -Z`, `Up: Y`), *Only Deform Bones* ticked — the
      model must face **+X in Blender** and be in centimetres at export (see §4; corrected 2026-10-15)

**Unreal — import**

- [ ] Import the detailed skeletal FBX first; this creates the shared `USkeleton`
- [ ] Import the crowd mesh **against that same skeleton** (never let it make a second one)
- [ ] **Verify before continuing:** bounds min Z = 0, height ~100–130 uu, mesh faces +X

**Unreal — crowd asset**

- [ ] Make a static mesh from the crowd mesh; remove collision
- [ ] 4 LODs at screen sizes 1.0 / 0.40 / 0.15 / 0.05; set `Percent Triangles = 1.0` on any LOD you authored
- [ ] Confirm **one** material slot
- [ ] (Optional, later) `AnimToTexture` bake in **Vertex** mode → position + normal textures

**Unreal — wiring**

- [ ] New `UUnitTemplate` asset (do not edit `DA_SwarmBot`)
- [ ] `SkeletalMesh` + five clips → near band; `StaticMesh` → crowd band; `TeamID` / `TeamColor`
- [ ] Material reads per-instance custom data 4/5/6 (team) and 7 (health) — else every unit is grey
- [ ] On the placed spawner: **`SpawnHeight = 0`** and **`NavigationHeightOffset = 0`**
- [ ] Crowd group: `ConfigureCrowdGroupEngagement` with `bEnableAttacks`, `bUseSharedNavigationPath = true`

---

## 1. The two-representation rule

You build **one** minion and derive **two** assets from it. This is not optional — the plugin switches
representation by distance and a unit that only has one of them either costs too much or looks wrong.

| Band | Asset | Driven by | Cap |
|---|---|---|---|
| Near | Skeletal mesh + `UAnimSequence`s | `UUnitMeshPool`, real animation | `MaxSkeletalMeshUnits` (default 100) |
| Crowd | Cheap static mesh + vertex-animation textures | Material, per-instance custom data | Unbounded |

**The constraint that decides everything downstream:** `FMassUnitVisualFragment` holds exactly one
`VertexAnimationTexture` and one `NormalMapTexture`. That is the engine `AnimToTexture` plugin's
**Vertex** mode. Bone mode emits three textures (position, rotation, weight) and would require new
fragment fields plus renderer changes. Vertex mode's texture size is `vertices × total frames`, so
**vertex count on the crowd mesh is the hard budget — not triangle count, not bone count.**

---

## 2. Budgets

| Thing | Target | Bound by |
|---|---|---|
| Bones | ≤ 20 | Near-band transform evaluation |
| Skeletal LOD0 | 2–3k triangles | Only ~100 exist at once, so generous |
| **Crowd mesh** | **300–600 vertices** | VAT texture = verts × frames |
| Total animation frames | ≤ 200 @ 30 fps | Same texture |
| Crowd LOD3 | ~4 triangles | Far band |
| Material slots | **1** | One draw call per ISM component |
| Vertex influences | 1–2 (max 4) | Skinning cost; rigid plates want 1 |

---

## 3. Rig

Roughly 16 bones covers a bipedal or multi-legged robot:

```
root  (origin, at the feet, Z = 0, unanimated)
└ pelvis → spine → head
          ├ shoulder.L → elbow.L → hand.L
          ├ shoulder.R → elbow.R → hand.R
          ├ thigh.L → shin.L → foot.L
          └ thigh.R → shin.R → foot.R
```

- **Single root bone** at the origin. Unreal requires one root; more than one produces a skeleton that
  imports but misbehaves.
- **No root motion.** Movement is authored by `MassUnitMovementProcessor`. Root motion fights it and
  the crowd will slide or stutter.
- **Deform bones only in the export.** Rig with IK and control bones freely, then clear their Deform
  flag and tick *Only Deform Bones* on export. IK helpers in the export are a common cause of a
  skeleton that will not accept animations later.
- **Rigid weighting is correct for robots.** Assign each armour plate 100% to one bone. It is cheaper,
  it looks right (plates should not bend), and it keeps the vertex-animation bake clean.
- **One skeleton shared by every minion variant.** This is the single biggest decision for
  scale and long-term maintenance: identical bone names mean one `USkeleton`, every animation works on
  every variant with no retargeting, and unit variants collapse to pure `UUnitTemplate` data.

---

## 4. Scale and orientation

Unreal is Z-up, **+X forward**, +Y right, 1 unit = 1 cm.

- ~~Model the minion facing **−Y** in Blender. With the default FBX export axes
  (`Forward: -Z Forward`, `Up: Y Up`) that lands as **+X forward** in Unreal.~~ **Wrong — measured
  2026-10-15 on the ankle biter:** a −Y-facing model with those axes lands facing **+Y** (the usual
  skeletal-mesh convention, which a Character Blueprint hides with a −90° mesh yaw). The crowd has no
  such offset: `UUnitMeshPool` and `UCrowdInstanceRenderer` apply the unit transform to the mesh
  directly and `MassUnitMovementProcessor` yaws units to their velocity, so the mesh itself **must
  face +X**. Face the model **+X in Blender** at export time (the ankle biter's exporter rotates the
  rig +90° about Z in a temporary state and reverts the file).
- Model at real-world metric size. Export in **centimetres**: scene unit scale 0.01 with the rig and
  meshes scaled ×100 and applied (pose-bone *location* keys ×100 too), then `apply_scale_options =
  FBX_SCALE_NONE`. A metre-scale scene exported with `FBX_SCALE_ALL` imports with the **root bone at
  scale 100** — it renders the right size but breaks ragdoll bodies and the bone bake.
- **Apply all transforms** (`Ctrl+A → All Transforms`) on both mesh and armature before exporting.
  Unapplied non-uniform scale skins correctly in Blender and shears in Unreal — a slow bug to find.
- Bake constraints and drivers down to keyframes. They do not survive FBX.

**Verify after import, do not assume:** bounds should read min Z = 0 and roughly 100–130 uu tall, and
the mesh should face +X. Both are one glance in the asset editor and both are silent failures.

---

## 5. Animation set

Author exactly what the plugin resolves. `UUnitMeshPool::ResolveAnimationAsset` switches on these tags
and nothing else; anything additional is dead weight in the VAT bake.

| Tag | `UUnitTemplate` field | Loops |
|---|---|---|
| `Animation.Idle` (fallback for anything unmatched) | `IdleAnimation` | yes |
| `Animation.Walk` / `Animation.Run` | `MoveAnimation` | yes |
| `Animation.Attack` | `AttackAnimation` | **no** |
| `Animation.Death` | `DeathAnimation` | **no** |
| `Animation.Stun` | `StunAnimation` | yes |

- Uniform **30 fps** across every clip. The VAT texture height is total frames summed over all clips,
  so a 40-frame walk instead of 120 is a 3× saving on the largest asset the unit owns.
- Make loops seamless in Blender. A VAT loop with a pop is far more visible replicated across a
  thousand units than it is on one character.
- Loop/one-shot behaviour is already handled in C++ by `ShouldLoopAnimation` — you do not configure it
  per asset, so match the table above rather than inventing new tags.

### 5a. Authoring the lunge attack

The plugin has exactly one attack tag, so **the lunge *is* the `Attack` clip**. There is no separate
lunge slot to fill.

The critical instruction: **author the lunge in place.** The unit must not travel in the animation.
Keep the root bone at the origin for the whole clip and express the pounce through body pose — crouch,
coil, extend, recover. Forward travel is added outside the animation, by the material's world position
offset surge (cosmetic tier) or by a movement-processor arc (real leap, not built yet). An Attack clip
that translates the root will double up with whichever of those is driving it and the unit will
overshoot or snap back.

- Keep it short — 20–30 frames at 30 fps. It is range-gated and cooldown-limited, so a long clip just
  desynchronises from the damage event.
- Damage fires on cooldown expiry, not on an animation notify. Put the visual impact roughly a third
  of the way in so the hit reads as landing near the start of the clip rather than at the end.
- It does **not** loop (`ShouldLoopAnimation` already excludes Attack and Death). Author it to settle
  back into a pose that blends cleanly to Idle.
- If you want the coiled wind-up to read at crowd distance, make it a **silhouette** change — the body
  dropping and rising — not a detail change. At LOD2 the unit is four blocks.


---

## 6. Build order

1. **Model, rig, animate** in Blender. Produce two meshes on the same armature and weights: a detailed
   one (2–3k tris) and a deliberately cheap crowd one (300–600 **verts**).
2. **Export two FBXs** — skeletal (mesh + armature + animations), and the crowd mesh skinned to the
   same armature.
3. **Import** the detailed skeletal mesh first; this creates the shared `USkeleton`. Import the crowd
   mesh **against that same skeleton**, not as a new one.
4. **Bake the VAT.** Enable the engine's `AnimToTexture` plugin, create a `UAnimToTextureDataAsset`:
   - `Mode = Vertex` (required — see §1)
   - `SkeletalMesh` → the crowd skeletal mesh
   - `StaticMesh` → a static-mesh copy of it
   - `AnimSequences` → the five clips
   - Bake. It writes `VertexPositionTexture` and `VertexNormalTexture`, and writes index UVs into the
     static mesh's configured UV channel.
5. **LOD the crowd static mesh** — four LODs at screen sizes 1.0 / 0.40 / 0.15 / 0.05. If you authored
   any LOD by hand, set its `Percent Triangles` back to 1.0; `SetNumSourceModels` seeds 50/25/12.5%
   automatic reduction that will re-simplify your work on top of itself.
6. **Remove collision** from the crowd static mesh. Crowd instances never collide.
7. **Wire the `UUnitTemplate`:**
   - `SkeletalMesh` + the five clips → near band
   - `StaticMesh` + `VertexAnimationTexture` + `NormalMapTexture` → crowd band
   - `TeamID` / `TeamColor` → per-instance custom data channels 3–6
8. **Set `SpawnHeight` and `NavigationHeightOffset` to 0** on the placed spawner and crowd config. Both
   default to 50 to centre the legacy engine cube; a feet-pivot mesh left at 50 hovers half a metre.

---

## 7. Material

A custom mesh with its own material renders **grey and identical for every unit**. The instanced path
creates no per-unit material instances by design — everything per-unit arrives through custom data.

Minimum viable: add `PerInstanceCustomData` nodes reading indices 4/5/6, append into a float3, and use
it as the team tint. Index 7 is health 0–1. Give every node a non-zero `ConstDefaultValue` or
thumbnails and preview actors render black. `M_SwarmBot_Instanced` is a working reference; the full
channel table is in `Public/Visual/CrowdInstanceRenderer.h`.

For VAT playback the material additionally samples the position/normal textures, using custom data
channel 0 (`AnimationIndex`) to select the clip's row block and channel 1 (`AnimationStartTime`)
against game time to derive the frame. Channel 8 (`RandomSeed`) offsets phase so units do not march in
lockstep; channel 9 (`Speed`) can cross-fade walk and run.

---

## 9. Non-destructive rollout

Nothing in this pipeline requires modifying existing content. Follow this and the current SwarmBot
placeholder keeps working throughout, so you always have a known-good comparison.

| Do | Instead of |
|---|---|
| Create a **new** `UUnitTemplate` for the new unit | Editing `DA_SwarmBot` |
| Put assets under a new folder, e.g. `/Game/MassUnits/<UnitName>/` | Adding to `/Game/MassUnits/Meshes` |
| Set `SpawnHeight` / `NavigationHeightOffset` on **your** placed spawner | Changing the C++ defaults (they exist for the legacy cube) |
| Add `PerInstanceCustomData` nodes to a **copy** of `M_SwarmBot_Instanced` | Editing the shared material |
| Configure a **new** crowd group for the new unit | Repointing the existing group |
| Leave `SM_SwarmBot` and its generator in place | Deleting the placeholder once the real unit lands |

The renderer needs no changes for a new unit: `UCrowdInstanceRenderer` buckets by `UStaticMesh`, so a
new mesh automatically gets its own component and draw call the first time a unit using it becomes
visible. Adding a unit type is pure content.

**Rollback** is deleting the new folder and the new template. No shared asset was touched.

**One caveat:** the current crowd renderer is compile-verified but **not PIE-verified**, and the running
editor does not yet contain it. Do an editor-closed editor-target build and confirm the SwarmBot
placeholder renders and moves *before* investing in custom art — otherwise a problem in the renderer
will look like a problem in your mesh.

---

## 10. Known gap

**Nothing in the plugin consumes `VertexAnimationTexture` yet.** `UVertexAnimationManager` maps
animation tags to stable indices, and `UCrowdInstanceRenderer` uploads that index plus a start stamp
into custom data — but the material that samples the VAT with those inputs, and the wiring from
template → fragment → material, do not exist. Until that lands, instanced units are **static-posed**
and only the ~100 units in the skeletal pool animate.

Cheap interim option that needs no Blender work and no textures: drive a procedural walk in the
material from `Speed`, `RandomSeed`, and `AnimationStartTime` via world position offset. A handful of
instructions, and it tells you whether the crowd reads correctly in motion before you commit to the
bake pipeline.

---

*For handing requirements to an external modeller, use `artist_brief.md` — the same constraints written
without engine context.*

*See also: `scalability_best_practices.md` (budgets and the rules behind them), `usage.md` (API),
`Tools/Python/make_swarmbot_visual.py` (the generated placeholder unit and its LOD conventions).*
