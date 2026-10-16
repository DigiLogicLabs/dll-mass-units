# Guide — Artist brief: crowd unit

Hand this to a modeller as-is; no engine knowledge needed. The engine-side reasons are in
[authoring_pipeline.md](authoring_pipeline.md). Example unit: a **mechanical spider**; the rules do not change for
other creatures.

## 1. Software and format

| | |
|---|---|
| DCC | Blender 4.x |
| Delivery | **FBX binary**, one file per item in §7, plus the `.blend` |
| Textures | PNG or TGA, power-of-two, delivered separately (not embedded) |
| Not accepted | Alembic, glTF, embedded textures, `.blend` as the only deliverable |

## 2. Scale, orientation, pivot (hard rules)

- **Size:** real-world metric. The spider is 60-90 cm wide, 30-50 cm tall.
- **Facing:** the unit faces **+X** in Blender (walks toward +X).
- **Origin:** centre of the footprint, **on the ground** (feet at Z = 0).
- **Before export:** `Ctrl+A → All Transforms` on mesh and armature; scale 1,1,1 and rotation 0,0,0.

Unapplied scale looks fine in Blender and shears in-engine. It is the most common delivery failure.

## 3. Two meshes, one armature

| Mesh | Budget | Purpose |
|---|---|---|
| **Hero** | 2,000-3,000 triangles | Seen close up |
| **Crowd** | **300-600 vertices** (not triangles) | Seen in large numbers |

The crowd mesh is a separate simplified model, not a decimated hero: keep the silhouette, drop the rest, avoid
extra edge loops and hard-edge splits. **One material per mesh**; if parts glow, keep them in the same material
and tell us which UV region they use.

## 4. Rig

**At most 20 bones.** For a spider:

```
root            (origin, on the ground, never animated)
└ body
  ├ head
  └ leg_A_L .. leg_D_L, leg_A_R .. leg_D_R    (2 bones each: upper + lower)
```

8 legs × 2 + root/body/head = 19. Need 3 bones per leg? Use 6 legs.

- One root bone at the origin with no animation on it.
- No root motion: animate in place.
- IK/control bones are fine but mark them non-deform; export deform bones only.
- Weights: 1-2 influences per vertex (4 max). Armour plates and leg segments 100% to one bone.
- Bone names identical across every unit we build. Agree them once.

## 5. Animations — 30 fps, every clip

| Clip | Frames | Loops | Notes |
|---|---|---|---|
| Idle | 40-60 | yes | Subtle; seamless loop |
| Move | 24-32 | yes | Walk/scuttle; seamless loop |
| Attack | 20-30 | no | The lunge, see below |
| Death | 30-45 | no | Collapse; ends settled |
| Stun | 24-30 | yes | Stagger/glitch |

Total across all clips under ~200 frames; shorter is better.

**Attack:** the spider must not travel. The root stays at the origin; animate the pounce shape (crouch → coil →
extend → recover). Put the visual impact about one third in, and tell us the frame number.

**Loops:** first and last frame identical in pose and velocity. A pop repeats across hundreds of units.

## 6. Naming

```
SK_SpiderBot, SK_SpiderBot_Crowd
A_SpiderBot_Idle, _Move, _Attack, _Death, _Stun
T_SpiderBot_BaseColor, _Normal, _ORM
```

## 7. Export (Blender FBX)

- Scene in centimetres: unit scale 0.01, rig and meshes scaled ×100 with scale applied (animation location keys
  scaled too). **Apply Scalings:** *All Local*. Forward/Up: defaults (`-Z Forward`, `Y Up`).
- Path mode *Copy*, embed textures off; *Limit to Selected Objects*.
- Armature: *Only Deform Bones* on, *Add Leaf Bones* off.
- Bake Animation on, *Force Start/End Keying* on, *NLA Strips* off, *All Actions* off.
- Bake constraints and drivers to keys first.

Files: `SK_SpiderBot.fbx` (mesh + armature), `SK_SpiderBot_Crowd.fbx` (crowd mesh + same armature), one
`A_SpiderBot_<Clip>.fbx` per clip (armature + one action).

If unsure about units or facing, deliver the `.blend` and we will export.

## 8. Delivery checklist

- [ ] Faces +X; origin on the ground at the footprint centre
- [ ] Transforms applied on mesh and armature
- [ ] Hero 2-3k tris, crowd 300-600 verts, same armature, one material each
- [ ] ≤ 20 bones, single root, no root motion, deform bones only, 1-2 influences
- [ ] Five clips at 30 fps, < ~200 frames total; Idle/Move/Stun loop seamlessly; Attack does not travel
- [ ] Names per §6; `.blend` included

## 9. Common rejections

| Problem | Why it fails |
|---|---|
| Scale not applied / metre export | Shears, or imports with a scaled root bone |
| Origin at body centre | Unit floats or sinks |
| Faces the wrong axis | Units walk sideways |
| Root motion, or Attack translates | Unit slides, overshoots, snaps back |
| IK bones in the FBX | Skeleton rejects animations |
| Two+ material slots | Doubles rendering cost |
| Crowd mesh over 600 verts | Exceeds the per-unit memory budget |
| Loop pops, mixed frame rates | Visible across the whole crowd |
