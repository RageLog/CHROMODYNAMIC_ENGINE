# ADR-20260529-W8-BC-per-instance-albedo-SSBO

Date: 2026-05-29 (backfilled retroactively; original landing commit phase ~280 / inside the W8-BC pass; foundation extracted in phase293 N1E).

## Context

ADR W8-BA shipped the RT scene reflection occlusion path — a ray cast from the chrome surface along the spec reflection direction; on hit, scale the sky contribution down. The hit-side shaded as **black** (Option A): the neighbour appeared as a dark silhouette against the gated sky cube. Functionally correct ("the chrome can see its neighbour"), but visually flat — the user could tell *that* a silhouette was there, not *what* was there.

Real chrome reflects coloured surroundings. A magenta neon strip behind a chrome sphere should produce magenta in the reflection. The W8-BA dark silhouette robbed that cue.

The W8-AZ-BA commit message had already queued the upgrade explicitly: *"Option B (per-instance albedo SSBO → colored hit shading) queued for W8-BB after visual validation of the ray path."* The validation passed; W8-BC ships Option B.

## Decision

**Per-frame, per-instance material table uploaded to a binding-10 SSBO. The PBR branch's RT reflection ray reads the hit instance ID and indexes the SSBO to colour the silhouette.**

### Memory layout

```cpp
struct InstanceMatGpu {        // 32 B per slot (std430)
    cd::math::Vec4f albedo;    // xyz=linear RGB, w=alpha (1.0 default)
    cd::math::Vec4f emissive;  // xyz=linear RGB, w=intensity multiplier
};
constexpr std::uint32_t kMaxInstMats   = 256;
constexpr std::uint32_t kInstMatBytes  = kMaxInstMats * sizeof(InstanceMatGpu);
```

256 slots × 32 B = 8 KiB SSBO, well below the 16 KiB push-constant + 64 KiB UBO budgets. Comfortably covers the ECS entity count (<40) + 16 PBR spheres + floor + future probes without ever resizing.

### Upload path

CPU side, in the same loop that builds the per-frame TLAS instance array, the corresponding `InstanceMatGpu` slot is filled with the entity's albedo. The instance index used in `rhi::AccelInstance` is the same as the SSBO slot index — that's the contract.

`fill_inst_mat(slot, albedo)` helper (factored to `HelloRayQuery.hpp` in phase293 N1E):

```cpp
inline void fill_inst_mat(InstanceMatGpu& im,
                          cd::math::Vec3f albedo) noexcept {
    im.albedo   = { albedo.x, albedo.y, albedo.z, 1.0F };
    im.emissive = { 0, 0, 0, 0 };
}
```

### Shader read

```glsl
layout(set = 0, binding = 10, std430) readonly buffer InstanceMaterials {
    InstanceMat data[];
} cd_instance_mats;

// inside the W8-BA RT reflection block:
if (rayQueryGetIntersectionTypeEXT(rq, true) ==
        gl_RayQueryCommittedIntersectionTriangleEXT) {
    uint inst_id = rayQueryGetIntersectionInstanceIdEXT(rq, true);
    vec3 neighbor_albedo = cd_instance_mats.data[inst_id].albedo.rgb;
    // Tonemap-compatible darken by 0.4 so chrome doesn't out-shine
    // the source surface; keeps the reflection energetically sane.
    env_spec = mix(env_spec, neighbor_albedo * 0.4, occlusion_strength);
}
```

`rayQueryGetIntersectionInstanceIdEXT` returns the **same index** the CPU pushed via `rhi::AccelInstance`. That's why the upload pairs 1:1 with the TLAS instance build — the SSBO is structurally part of the TLAS contract, not a separate resource.

## Consequences

- **Coloured silhouettes.** Magenta neon area behind a chrome sphere shows magenta in the reflection. Cyan area panel shows cyan. The character's albedo (currently W8-BC distinct neutral grey per HelloTlasRebuild.hpp) shows up as a grey form in adjacent chrome.
- **No additional ray cost** versus W8-BA — the same query already returned the instance ID; we just consume it.
- **Binding 10 added** to kPrimFS descriptor set. Bumps the per-frame descriptor write count by 1 but stays well within the layout limits (the cd_prim pipeline now has 11 bindings, room for ~32 on common Vulkan implementations).
- **Future-proof for materials beyond albedo.** The `InstanceMatGpu.emissive` slot is reserved for the next phase (emissive-bouncing lights into chrome). Adding spec colour / IOR would be ABI-incompatible and would push to a 64 B slot, halving slot capacity — but 128 instances is still plenty for the demo class.
- **The `instance ID == slot index` invariant** becomes load-bearing. The HelloRayQuery extract (phase293 N1E) hoisted `make_accel_instance` + `fill_inst_mat` into the same header precisely so reviewers can audit both halves of the contract side-by-side.

## Rejected alternatives

- **Stay with the dark silhouette (Option A).** Rejected per user feedback that the dark silhouette robbed the "colored chrome" cue.
- **Per-entity descriptor sets.** Rejected: that would mean rebinding the descriptor set per entity in the TLAS, breaking the single-draw-per-PBR-mesh architecture. SSBO indexed by instance ID is the textbook fix.
- **Push constants for material.** Rejected: 128 entities × 32 B = 4 KiB push, exceeds the 128 B push budget per draw.
- **Hit-side per-vertex colour lookup** (sample the BLAS's vertex buffer at the triangle hit point and read the albedo attribute). Rejected: requires `rayQueryGetIntersectionPrimitiveIndexEXT` + indirect BLAS-data lookup with no clear win versus the simpler per-instance SSBO. Per-vertex colour would be the move for sub-mesh material variation (mosaic textures, vertex-colour gradients) which the demo doesn't have.

## Reference

- Code: `samples/engine/hello_engine/HelloRayQuery.hpp` — `InstanceMatGpu`, `kMaxInstMats`, `kInstMatBytes`, `make_accel_instance`, `fill_inst_mat` (extracted in phase293 N1E).
- Code: `samples/engine/hello_engine/HelloTlasRebuild.hpp` — `W8-BC: distinct neutral grey so chrome reflections show a proper [silhouette]` per-instance fill loop.
- Code: `samples/engine/hello_engine/main.cpp:4566` — SSBO buffer create + descriptor write (binding 10).
- Code: `samples/engine/hello_engine/PrimShader.hpp` — `kPrimFS` PBR-branch ray hit albedo read.
- Foundation extract: phase293-N1E, 2026-05-28.

## Future work

- **W8-?** emissive lookup for neighbouring lights (cyan area, magenta neon) so chrome carries an explicit emissive bounce in the reflection.
- **W8-?** sub-mesh material (vertex-colour or per-prim) for entities with non-uniform albedo (textured glTF assets would benefit).
- **W8-?** spec colour / IOR per instance — push to 64 B slot, halve capacity to 128 instances. Requires the materials buffer to migrate from per-frame full-upload to dirty-set partial upload to keep bandwidth in check.
- **Spec lobe-aware hit shading.** Currently the hit-shading darkens by a fixed 0.4. A roughness-aware energy-conserving formula would lift the chrome quality further — same SSBO, smarter shader.
