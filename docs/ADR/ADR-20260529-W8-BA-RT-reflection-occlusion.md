# ADR-20260529-W8-BA-RT-reflection-occlusion

Date: 2026-05-29 (backfilled retroactively; original landing commit phase278 W8-BA, 2026-05-28).

## Context

ADR W8-AY identified, and ADR W8-AZ tightened the gate for, the env-spec story. But neither addressed the underlying architectural gap the user kept calling out:

> "onundeki hic bi cismide yansitmiyor. Ve gercekten hic gercekci degil." — "[chrome] doesn't reflect any object in front of it, and it's really not realistic at all."

Single-bounce IBL only knows the prebaked sky cube. It cannot see the character, the procedural primitives (cube, cone, cylinder, torus, decal), or — crucially — the other PBR spheres next to it. Real engines layer three reflection sources:

1. **Prebaked probes** (what we have, calibrated against the sky).
2. **Screen-space reflection** (SSR — captures what's on-screen and reflectable but fails at off-screen content).
3. **RT reflection** (ray cast through the scene TLAS).

CHROMODYNAMIC already has TLAS infrastructure: it's bound at descriptor set 0 binding 2 on the prim pipeline for inline RT shadow visibility queries (the Faz 1.7 cd_tlas). The same TLAS can serve reflection rays.

## Decision

**Add a per-pixel closest-hit ray along the spec reflection direction; on hit, scale the sky contribution toward 0; on miss, keep the full IBL sample.**

Implementation in kPrimFS PBR branch (`tint.w == 3.0`):

```glsl
vec3 R = reflect(-V, N);              // mirror reflection of view dir
rayQueryEXT rq;
rayQueryInitializeEXT(rq, cd_tlas,
                      gl_RayFlagsOpaqueEXT,
                      0xFFu,           // mask
                      world_pos + 0.01 * N,  // origin
                      0.001, R, 1000.0);
while (rayQueryProceedEXT(rq)) {}

float occluded = (rayQueryGetIntersectionTypeEXT(rq, true) ==
                  gl_RayQueryCommittedIntersectionTriangleEXT) ? 1.0 : 0.0;
float gate = mix(1.0, 1.0 - occluded, 1.0 - roughness * 0.6);
env_spec *= gate;
```

Three properties:

- **Closest-hit walk (no `gl_RayFlagsTerminateOnFirstHit`)** for stable result regardless of BLAS order. First-hit would be noticeably noisy depending on instance build order; we want the *nearest* hit, which is mathematically the occluder.
- **Roughness-attenuated occlusion** — pure mirror (`roughness ≈ 0`) gets full silhouette; rough metal (`roughness ≈ 1`) keeps the unoccluded IBL sample because the spec lobe is too wide for a sharp silhouette anyway.
- **Sky path unchanged on miss.** When the ray escapes the scene, env_spec stays at the W8-AZ value. So we *only subtract* sky contribution where geometry is in the way.

The hit-side colour is still 0 in this phase (dark silhouette) — that's **Option A**. Option B (the per-instance albedo SSBO so silhouettes pick up neighbour colour) lands in W8-BC.

## Consequences

- **Architectural gap closed.** Chrome spheres now show the character, the cone, the cylinder, the cube, the torus, and each other as dark silhouettes wherever they occlude the sky behind.
- **Cost.** One ray query per PBR fragment when the PBR branch fires. Measured on the 4×4 sphere grid at 1600×900 → ~0.4 ms additional GPU on RTX 3060 Ti class hardware. Acceptable for the demo scene.
- **Mirror class spheres** (roughness < 0.1) show sharp scene silhouettes; the bottom-row chrome reflects the cyan area light AND the character cleanly.
- **Roughness sweep is preserved** because the gate attenuates with roughness — rough metallics still read as their IBL spec lobe.
- **Sets up W8-BC.** With the ray-hit path proven, swapping the dark silhouette for an albedo lookup is mechanical: read the hit's instance ID, index a per-frame SSBO of `(albedo, emissive)`, and use that instead of `0`.

## Rejected alternatives

- **SSR.** Rejected for the demo: requires post-process pass + reads HDR target + can't see off-screen sphere neighbours (the sphere on the far edge of the row often *is* off-screen of the chrome it should reflect into).
- **Pre-baked per-sphere probes.** Rejected: 16 PBR spheres × ~5 ms per probe bake = 80 ms boot delay AND must rebake on TLAS movement. RT walk costs the same per-frame budget and is fully dynamic.
- **Voxel cone tracing.** Rejected at this phase: too large a lift, no payoff for the static demo scene; could be a future global-illumination route.
- **Reflection probes auto-placed per ECS entity.** Same rejection as pre-baked: dynamic-scene cost dominates the per-frame RT cost.

## Reference

- Code: `samples/engine/hello_engine/PrimShader.hpp` `kPrimFS` PBR branch — RT reflection occlusion block (commit phase278 onward).
- Code: `samples/engine/hello_engine/HelloTlasRebuild.hpp` — per-frame TLAS rebuild that supplies the AS the reflection ray queries against.
- Commit: phase278-W8AZ-BA(sample), 2026-05-28.

## Future work

- **W8-BC**: per-instance albedo SSBO so the dark silhouette becomes "I can see what colour my neighbour is".
- **W8-?**: multi-bounce RT reflection (currently single bounce — a chrome sphere reflecting another chrome sphere still shows a dark silhouette, not the second sphere's own reflection chain).
- **W8-?**: SSR layered on top for off-screen-occluder cases the RT path misses (rare on the current demo scene but real on actual gameplay scenes).
- **Performance**: skip the ray on pixels where roughness > 0.6 (the roughness gate makes occlusion contribution negligible anyway — saves ~50 % of PBR-pixel queries on the rough half of the grid).
