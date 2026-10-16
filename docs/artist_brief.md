# Artist Brief — Crowd Unit for MetalLegs

Hand this to the modeller as-is. It is self-contained: no engine knowledge needed.

Example unit used throughout: a **mechanical spider**. Substitute your creature; the rules do not change.

---

## 1. Software and format

| | |
|---|---|
| DCC | Blender (any 4.x) |
| Delivery format | **FBX binary**, one file per item below |
| Textures | PNG or TGA, power-of-two, delivered separately (not embedded) |
| Do **not** deliver | .blend as the deliverable, Alembic, glTF, embedded textures |

**Also send the .blend** alongside the FBXs so we can fix things without a round trip.

---

## 2. Scale, orientation, pivot — all three are hard rules

- **Units:** metric, real-world size. The spider should be **60–90 cm wide, 30–50 cm tall**.
- **Facing:** the unit faces **−Y** in Blender (walks toward −Y).
- **Pivot / origin:** at the **centre of the footprint, on the ground plane**. Feet touch Z = 0.
  Not the body centre, not between the legs at body height. On the floor.
- **Before export:** `Ctrl+A → All Transforms` on the mesh **and** the armature. Scale must read 1,1,1
  and rotation 0,0,0 in the N-panel.

> Unapplied scale looks perfectly fine in Blender and shears the model in-engine. This is the single
> most common delivery failure.

---

## 3. Two meshes, one armature

Deliver **two** versions of the model, skinned to the **same** armature:

| Mesh | Budget | Purpose |
|---|---|---|
| **Hero** | 2,000–3,000 triangles | Seen close up |
| **Crowd** | **300–600 vertices** (not triangles) | Seen in large numbers |

The crowd mesh is a *separate, simplified model* — not a decimated hero mesh. Keep the silhouette
(leg spread, body shape); throw away everything else. Vertex count is the hard limit, so avoid
unnecessary edge loops and hard-edge splits.

**One material per mesh.** Not two, not three. If parts need to glow, keep them in the same material
and tell us which UV region they occupy — do not split them into a second material slot.

---

## 4. Rig

**Maximum 20 bones total.** This is a firm budget, and 8 legs make it tight. For a spider:

```
root            (at origin, on the ground, never animated)
└ body
  ├ head
  └ leg_A_L, leg_B_L, leg_C_L, leg_D_L    (2 bones each: upper + lower)
    leg_A_R, leg_B_R, leg_C_R, leg_D_R
```

8 legs × 2 bones = 16, plus root/body/head = **19**. If you need 3 bones per leg, drop to 6 legs.

Rules:

- **One root bone**, at the origin, on the ground, with **no animation on it ever**.
- **No root motion.** The unit must animate in place. Movement is driven by the game.
- Rig with IK and control bones if you like, but **mark them non-deform** and tick
  *Only Deform Bones* on export. IK bones in the FBX break the skeleton.
- **Weights: 1–2 influences per vertex, 4 absolute maximum.** For a machine, paint each armour plate
  and leg segment **100% to a single bone**. Rigid is correct — plates should not bend.
- **Bone names must be identical across every unit we ever build.** Same names = animations shared for
  free across all units. Agree the naming once, then never change it.

---

## 5. Animations

**30 fps. Every clip. No exceptions.**

| Clip | Frames | Loops | Notes |
|---|---|---|---|
| **Idle** | 40–60 | **Yes** | Subtle — leg twitch, body sway. Seamless loop. |
| **Move** | 24–32 | **Yes** | Walk/scuttle cycle. Seamless loop. |
| **Attack** | 20–30 | No | The lunge — see below |
| **Death** | 30–45 | No | Collapse. Ends settled, motionless. |
| **Stun** | 24–30 | **Yes** | Stagger/glitch. Loops until released. |

**Total across all five clips must stay under ~200 frames.** Shorter is genuinely better — clip length
is our most expensive per-unit cost.

### The Attack clip (the lunge) — read this twice

The spider **must not travel** in this animation. Root bone stays at the origin for the entire clip.

Animate the *pounce shape*: crouch → coil → explosive extension → recover. The forward movement is
added by the game. If the root translates, the unit overshoots and snaps back in-engine.

Put the visual impact roughly **one third into the clip**, not at the end.

### Loops

Idle, Move, and Stun must loop **seamlessly** — first and last frame identical in pose and velocity.
A visible pop is multiplied across hundreds of units on screen and reads as a bug.

---

## 6. Naming

```
Meshes      SK_SpiderBot            (hero)
            SK_SpiderBot_Crowd      (crowd)
Animations  A_SpiderBot_Idle
            A_SpiderBot_Move
            A_SpiderBot_Attack
            A_SpiderBot_Death
            A_SpiderBot_Stun
Textures    T_SpiderBot_BaseColor / _Normal / _ORM
```

---

## 7. Export settings (Blender FBX exporter)

- **Path Mode:** Copy, embed textures **off**
- **Forward:** `-Z Forward`   **Up:** `Y Up`   *(these are the defaults — leave them alone)*
- **Apply Scalings:** `FBX All`
- **Limit to:** Selected Objects
- **Armature:** *Only Deform Bones* **on**, *Add Leaf Bones* **off**
- **Bake Animation:** on, *Force Start/End Keying* on, *NLA Strips* off, *All Actions* off

**Deliver as separate files:**

```
SK_SpiderBot.fbx              mesh + armature, no animation
SK_SpiderBot_Crowd.fbx        crowd mesh + same armature, no animation
A_SpiderBot_Idle.fbx          armature + one action
A_SpiderBot_Move.fbx
A_SpiderBot_Attack.fbx
A_SpiderBot_Death.fbx
A_SpiderBot_Stun.fbx
```

Bake all constraints and drivers to keyframes first — they do not survive FBX.

---

## 8. Delivery checklist

- [ ] Faces −Y, origin on the ground at the footprint centre
- [ ] All transforms applied (scale 1,1,1 / rotation 0,0,0) on mesh and armature
- [ ] Two meshes, same armature: hero 2–3k tris, crowd **300–600 verts**
- [ ] One material slot per mesh
- [ ] ≤ 20 bones, single root, no root motion, deform bones only
- [ ] 1–2 weight influences per vertex
- [ ] Five clips at 30 fps, under ~200 frames total
- [ ] Idle / Move / Stun loop seamlessly
- [ ] Attack does not translate the root
- [ ] Naming matches §6
- [ ] .blend included

---

## 9. Common rejections

| Problem | Why it fails |
|---|---|
| Scale not applied | Model shears in-engine |
| Origin at body centre | Unit floats or sinks into the floor |
| Root motion in clips | Fights game-driven movement; unit slides or stutters |
| Attack clip translates | Unit overshoots and snaps back |
| IK bones in the FBX | Skeleton will not accept animations |
| Two+ material slots | Doubles rendering cost per unit |
| Crowd mesh over 600 verts | Blows the per-unit memory budget |
| Loop pops | Massively visible across a crowd |
| Mixed frame rates | Animation desyncs between units |
