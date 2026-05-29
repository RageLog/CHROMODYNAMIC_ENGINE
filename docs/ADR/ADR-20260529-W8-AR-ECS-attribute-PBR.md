# ADR-20260529-W8-AR-ECS-attribute-PBR

Date: 2026-05-29 (backfilled retroactively; original landing commit phase ~262, Marathon Run 7).

## Context

Pre-W8-AR, the CHROMODYNAMIC sample's PBR sphere grid (16 spheres laid out across the metallic / roughness sweep) lived as a SEPARATE render pass with its own shader, its own descriptor set, its own PBR material instance. ECS-driven primitive entities (the cube / cone / cylinder / torus / general-purpose mesh row) went through the OTHER pass with the kPrimFS branch shader. Two-pipeline, two-shader, two-shadow-pass.

This produced asymmetric behaviour: the PBR spheres reflected the chrome row, cast RT shadows, participated in TAA / motion-blur / SSR. The ECS primitives did NONE of that until each got bolted on individually for each pass. The asymmetry pushed bugs around (W8-AS ghost CSM, W8-AU stale-frustum-skip, W8-BB area light culling) that turned out to be the PBR-vs-prim divergence biting.

## Decision

**PBR becomes a per-entity attribute, not a per-pass distinction.**

`SceneEntity` carries an `is_pbr` bool + `metallic` + `roughness` floats. The single prim shader's kPrimFS branch reads `tint.w` and picks the PBR Cook-Torrance + GGX + multi-light + Karis IBL path when `tint.w == 3.0` (sentinel); the standard path runs otherwise (vertex-coloured / textured albedo, hemisphere + Lambert + cd_lights).

One render loop, one shader, one shadow pass. PBR is just a per-entity attribute now.

## Consequences

- **Asymmetry gone.** ECS entities and PBR spheres go through the same TAA, same motion-blur, same SSR, same shadow path, same RT reflection occlusion, same env-spec.
- **Bug surface dropped massively.** The W8-AS/AU/BB family disappeared with the rewrite because they were all artefacts of the two-pass duplication.
- **Shader complexity bumped modestly.** kPrimFS gained one branch + the PBR uniform reads, but the shader is still under the W4 budget.
- **PBR sphere grid is now ECS-spawned at boot.** 16 entities, one per (metallic, roughness) cell, all driven through the standard ECS transform / scene local store.

## Rejected alternatives

- **Keep two shaders, share material instances.** Rejected — the descriptor-set split was the bug-amplifier.
- **PBR-only pipeline (drop the non-PBR path).** Rejected — the non-PBR vertex-coloured path is what the user demos for "this is the basic engine" first-look. Keep both.
- **Material-graph generated shaders.** Rejected for this phase — too big a lift; the single-shader branch is a stepping stone (see ADR-003 shader-material-pipeline planned material graph for the eventual graph-based generator).

## Reference

- Code: `samples/engine/hello_engine/PrimShader.hpp` kPrimFS branch on `tint.w == 3.0`.
- Code: `samples/engine/hello_engine/HelloPbrGrid.hpp` `spawn_pbr_grid_entities` helper.
- Code: `samples/engine/hello_engine/main.cpp` `SceneEntity::is_pbr` / `metallic` / `roughness` fields.

## Future work

Replicate the pattern for decal (W8-?-decal-attribute) and particle (W8-?-particle-attribute) once those subsystems land. The ECS-attribute pattern is the project's preferred extension point.

