# DLL Mass Units — Research

Condensed from the 2025 pre-implementation research notes (Mass Entity, Niagara and vertex animation, GAS
integration, optimisation techniques, a reference-game analysis) and the planning documents built on them.
Those notes assumed UE 5.6, a Niagara-first renderer and a third-party GAS plugin; most of their architecture
was not built. What follows is what still holds and what we concluded. The full notes are in git history.

## Mass Entity

- Fragments (data), tags (data-less flags), archetypes (entities with identical composition, stored in chunks),
  processors (stateless logic over queries) and a command buffer for deferred composition changes.
- Chunked, linear iteration is the whole performance win; random per-entity access gives it up.
- **Conclusion:** units are plain Mass entities with plugin fragments; processors do per-frame work; gameplay code
  holds stable handles, never pointers. We do not use `MassEntityConfig` traits or the engine Mass spawner, so the
  plugin works without authoring Mass assets.

## Rendering and vertex animation

- Vertex-animation textures (VAT) store baked animation in textures; the engine's AnimToTexture plugin bakes
  them and provides material functions. Animation then costs GPU time, not game-thread time.
- Niagara can render large instance counts, but driving a particle system per unit type is more setup than most
  projects want.
- Instanced static meshes under GPUScene are frustum-culled and LOD-selected per instance on the GPU; HISM's CPU
  cluster tree assumes mostly static instances.
- **Conclusion:** dynamic ISM is the default renderer (zero assets, stable slots); Niagara is optional; per-unit
  variation goes through per-instance custom data, never a material instance per unit. VAT is driven by a clip
  table in the template, so one material plays any clip.

## Representation swapping (reference game)

Public developer commentary on *Kingsmaker* (an RTS/shooter hybrid) described GPU vertex animation for thousands
of soldiers, a small pool of skeletal meshes swapped in for units near the player or under the crosshair, and
equipment drawn as separate instanced parts. We have not verified these details; we used them only as design
inspiration.

**Conclusion:** a capped, nearest-first skeletal pool with hysteresis (`UUnitMeshPool`), seeded from the
vertex-animation phase so the swap does not snap.

## GAS at crowd scale

- An ASC per unit is the cost we are avoiding; pooling ASCs or proxy ASCs adds complexity without removing the
  per-unit attribute model.
- Earlier plans targeted GASCompanion, a third-party plugin. It was never a dependency of shipped code and was
  removed in 1.1.
- **Conclusion:** native fragment health is the source of truth. GAS appears at the edges only: crowd attacks
  apply an effect to the (GAS) target; opted-in units map to an ASC the game owns; statuses and tags on units are
  a lightweight stand-in for duration effects.

## Optimisation techniques that shaped the design

| Technique | Where it lives |
|---|---|
| Separate update clocks (movement every frame, decisions on a timer, visibility staggered by LOD) | Processors, crowd timer, visibility processor |
| Round-robin budgets instead of truncation | `MaxCrowdUnitsPerUpdate`, `MaxPathRequestsPerFrame`, `MaxSharedPathBuildsPerCrowdUpdate` |
| Spatial hashing for neighbours | `CrowdSpatialCellSize` |
| Group-level decisions and shared corridors | Crowd groups, managed subgroups |
| Behaviour LOD and sleeping by observer distance | `CrowdSimulationLOD*`, `MaxSimulationDistance` |
| Object pooling | Skeletal mesh pool, recycled instance slots |
| Interest management / relevancy | Left to the adopter (the plugin does not replicate) |

All performance figures in the old notes were hypotheses; treat scale only as measured on target hardware.
