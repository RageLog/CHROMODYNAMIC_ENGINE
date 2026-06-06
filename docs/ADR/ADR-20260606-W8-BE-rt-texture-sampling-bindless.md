# ADR-20260606-W8-BE-rt-texture-sampling-bindless

Date: 2026-06-06

## Context

[ADR W8-BD](ADR-20260606-W8-BD-per-geom-albedo-SSBO-and-curtain-reflections.md)
shipped the per-(instance, geometry) SSBO that lets ray-query
reflections show the right **per-prim average colour** in chrome
mirror reflections. Phase 833 (the second-deepest root cause in
that ADR's fix chain) then unlocked the full Khronos Sponza prim
set by raising the Vulkan `kMaxBuildGeos` BLAS cap from 32 to 128.

Net result: chrome PBR spheres reflect Sponza correctly **at the
per-prim resolution**. Every curtain panel, vegetation pot, lion-
fountain carving, sandstone column comes through as a recognisable
*colour region*.

What does **not** come through: prim-internal detail. Specifically:

* The damask pattern woven into the curtain fabric.
* The leaf texture of vegetation prims.
* The carved relief of the lion fountain stonework.
* The fine grain of the floor marble.

W8-BD's rejected-alternative #1 (*"sample the albedo texture from
the ray hit"*) was deferred because the host-side return on the
"can you see Sponza?" question was already met by the per-prim
average colour path. The user's follow-up:

> *"yansimalarda hal hicbir detay yok. obje texturleri gozukmuyor
> sanirim"*

makes clear that **the per-prim average is not enough** for the
chrome-Sponza visual story. Chrome is the showcase surface; a
chrome mirror with flat-colour regions reads as polished plastic,
not as a polished metal ball reflecting a richly textured
cathedral interior.

W8-BE is the strand that ships the rejected alternative.

## Decision

**Implement engine-wide bindless texture infrastructure plus
ray-side UV interpolation plus per-prim albedo texture sampling in
the RT reflection branch of `prim.frag.glsl`.**

The work splits into five layers, each landing as its own phase in
the Run 18 marathon:

### Layer 1 — RHI public surface (`engine/render/rhi/`)

* `cd::rhi::DescriptorType::kBindlessSampledImage` — new enum value
  signalling a runtime-indexed sampler2D array.
* `cd::rhi::BindlessTextureArrayDesc` — descriptor for creating a
  fixed-size sampler2D array binding with deferred slot
  population.
* `IDevice::create_bindless_texture_array` — factory.
* `IDevice::write_bindless_texture_slot(handle, slot, view)` —
  late-bound population (any time after creation, before first use).

The surface stays **backend-agnostic**: the description above is
exactly the same shape the future D3D12 path will implement
(D3D12 already supports bindless via descriptor heaps; the API
surface there is *easier* than Vulkan's).

### Layer 2 — Vulkan backend (`engine/render/rhi/src/vulkan/`)

* Enable `VK_EXT_descriptor_indexing` at device creation. Required
  features: `runtimeDescriptorArray`,
  `shaderSampledImageArrayNonUniformIndexing`,
  `descriptorBindingPartiallyBound`,
  `descriptorBindingUpdateAfterBind`.
* Device-level fallback: if the GPU does **not** expose
  `descriptor_indexing`, `create_bindless_texture_array` returns
  `kNotImplemented` and consumers fall back to the
  per-prim-average-colour path (W8-BD behaviour). No regression
  for vendor / driver versions that lag.
* Implement the array as a single
  `VkDescriptorSetLayoutBinding { type = SAMPLER+IMAGE,
  descriptorCount = kMaxBindlessSlots, stageFlags = FRAGMENT,
  ... }` with `UPDATE_AFTER_BIND | PARTIALLY_BOUND |
  VARIABLE_DESCRIPTOR_COUNT` flags.
* `kMaxBindlessSlots = 256` initial cap (covers Sponza's 103 plus
  ~150 spare for the L-rt-tex showcase scenes queued under L1
  Metal / L4 Nanite). Hard maximum is the device limit
  (`maxDescriptorSetSampledImages`, typically ≥ 1024 on desktop
  Vulkan).

### Layer 3 — Sponza data plumbing (`samples/engine/hello_engine/`)

* Bind Sponza's vertex buffer + index buffer as `readonly buffer`
  storage buffers (bindings 12 + 13 in the prim shader) so the
  fragment shader can read vertex UVs at ray hit. CesiumMan +
  procedural primitive BLASes do not need this — they keep the
  per-prim avg colour path.
* Extend the per-prim metadata SSBO (`InstanceMatGpu`, binding 10)
  with two new fields:
  * `uint32_t albedo_tex_slot` — slot index in the bindless array,
    `0xFFFFFFFFu` when the prim has no texture (fall back to avg).
  * `uint32_t index_offset` — base index into the index buffer for
    this prim's triangle list (already in `GltfPrimRange::index_offset`;
    just needs to land in the SSBO).
  Bump `InstanceMatGpu` from 32 B → 48 B; SSBO size 256 KiB → 384 KiB.
  Still trivial.
* Boot-time: walk `s.meshes.gltf_prim_ranges`, write each prim's
  `albedo_view` into the bindless array at slot `i`, record
  `i` in the metadata SSBO.

### Layer 4 — Shader integration

```glsl
// New bindings (additive; existing 0..10 stay):
layout(set = 0, binding = 11) uniform sampler2D
    cd_bindless_albedo[256];                                // L1 + L2
layout(set = 0, binding = 12, std430) readonly buffer SponzaVBuf {
    PrimitiveVertex sponza_verts[];
} cd_sponza_vb;                                              // L3
layout(set = 0, binding = 13, std430) readonly buffer SponzaIBuf {
    uint sponza_idx[];
} cd_sponza_ib;                                              // L3

// Existing reflection_hit_id() extended to return barycentrics + prim_idx.
float reflection_hit_id(vec3 origin, vec3 N, vec3 dir, float tmax,
                        out int  out_inst, out int out_geom,
                        out int  out_prim,
                        out vec2 out_bary);

// At the hit site (inside the RT mirror branch):
if (scene_hit > 0.5 && hit_slot >= 0)
{
    uint tex_slot   = cd_instance_mats.data[hit_slot].albedo_tex_slot;
    uint idx_offset = cd_instance_mats.data[hit_slot].index_offset;
    vec3 hit_alb;
    if (tex_slot != 0xFFFFFFFFu)
    {
        // Recover UV from the BLAS hit and sample the bindless array.
        uint i0 = cd_sponza_ib.sponza_idx[idx_offset + hit_prim*3 + 0];
        uint i1 = cd_sponza_ib.sponza_idx[idx_offset + hit_prim*3 + 1];
        uint i2 = cd_sponza_ib.sponza_idx[idx_offset + hit_prim*3 + 2];
        vec2 uv0 = cd_sponza_vb.sponza_verts[i0].uv;
        vec2 uv1 = cd_sponza_vb.sponza_verts[i1].uv;
        vec2 uv2 = cd_sponza_vb.sponza_verts[i2].uv;
        vec2 uv  = uv0 * (1.0 - bary.x - bary.y)
                 + uv1 * bary.x
                 + uv2 * bary.y;
        hit_alb = texture(cd_bindless_albedo[tex_slot], uv).rgb;
    }
    else
    {
        // Per-prim avg colour fall-through (W8-BD path; covers
        // CesiumMan, PBR grid, procedural prims).
        hit_alb = cd_instance_mats.data[hit_slot].albedo.rgb;
    }
    refl_color = hit_alb * pc.sun_color.rgb * 4.0;   // phase835 energy
    /* ... existing blend ... */
}
```

The `if (tex_slot != 0xFFFFFFFFu)` branch is the **graceful fallback** —
prims without a per-prim texture (CesiumMan, PBR grid, procedural
seeds, the floor) keep the W8-BD per-prim avg colour path. No
behaviour change for non-Sponza surfaces.

### Layer 5 — Tests + ADR + memory

* `test_hello_engine_bindless_metadata.cpp` — locks
  `InstanceMatGpu.albedo_tex_slot == 0xFFFFFFFFu` sentinel value,
  `index_offset` byte layout, fallback contract.
* Capture-and-iterate via `--golden-fixture 5` per the ADR
  agent-iteration-loop.
* Memory lesson: how to avoid the per-prim-average-only path going
  forward (when do you need full texture sampling vs avg colour).

## Consequences

### Positive

* **Texture detail finally lands.** Chrome reflections show the
  actual curtain damask, leaf veins, lion-fountain carving,
  marble grain. The reflection reads as a polished metal mirror,
  not a polished plastic ball.
* **Backend-agnostic public surface.** D3D12 picks this up
  directly (descriptor heaps are easier than Vulkan's descriptor
  indexing). Metal would map to argument buffers. Web/WebGPU
  bindless is queued but the API surface allows the fallback path
  cleanly.
* **No regression on non-Sponza prims.** The `0xFFFFFFFFu` sentinel
  + per-prim-avg fallback means CesiumMan, PBR grid, procedural
  seeds keep their current behaviour.
* **Future-proof.** L1 Metal showcase, L4 Nanite scene, future
  glTF imports all get the same path for free as soon as they
  populate the bindless array at load time.

### Negative

* **VK_EXT_descriptor_indexing dependency.** Required Vulkan 1.2
  + the extension. Pre-1.2 / lacking-extension cells gracefully
  degrade to W8-BD per-prim-avg path. Our test matrix already
  pins Vulkan ≥ 1.2.
* **Per-prim metadata SSBO grows 32 B → 48 B per slot.** Total
  SSBO grows 256 KiB → 384 KiB (8192 slots). Trivial on any GPU.
* **Vertex / index storage-buffer binding cost.** Two extra
  storage buffers (12 + 13) in the fragment shader's descriptor
  set; binding cost ≈ zero per frame after initial setup.
* **Bindless array population is one-shot at boot.** Hot-reload of
  a single Sponza prim texture requires re-walking the array +
  rewriting the slot. Acceptable trade-off — the editor's hot-
  reload path already invalidates the prim's descriptor set; the
  bindless write is one extra descriptor write per reload.
* **Implementation surface.** ~3-5 days of focused work across
  the rhi public surface, Vulkan backend, sample integration,
  shader, tests, and the docs.

### Neutral / informational

* The W8-BD per-prim-average path stays operational for prims
  whose `albedo_tex_slot == 0xFFFFFFFFu` (the sentinel) — that
  is the documented fallback contract. Future samples that ship
  without per-prim textures still work.
* Memory cost growth is ~50% of one Sponza texture (1024² × RGBA8
  = 4 MiB). Even with 103 prims at 1024² that is ~412 MiB; we
  recommend per-prim texture sizing ≤ 512² for production scenes
  to fit a 256 MiB texture budget.

## Rejected alternatives

1. **Texture atlas (stitch all 103 Sponza textures into one big
   2D image).** Would avoid the descriptor-indexing dependency.
   Rejected: atlas preprocessing complicates the asset pipeline
   (per-prim UV remap, padding for mip-bleed, atlas packing
   algorithm), and the atlas's 8K-square memory footprint is
   the same as the bindless array's worst case. Bindless has the
   cleaner API surface and the better fit for D3D12 (where
   descriptor heaps are first-class).
2. **Per-prim descriptor set swap inside the ray loop.**
   Impossible — a single fragment-shader invocation processes
   many ray hits across the chrome surface, all sharing one
   descriptor set. Bindless is the only realistic shader-side
   indexing path.
3. **Pseudo-detail via hash-noise modulation of the average
   colour.** Adds high-frequency variation that reads as "this
   surface has texture" without actually showing the texture.
   Rejected because the goal is to show the **actual** curtain
   pattern, not to fake texture-like noise.
4. **Defer until a multi-bounce path tracer lands.** The
   user-facing return (texture detail in chrome reflections) does
   not require multi-bounce. Single-bounce mirror reflection is
   enough; the per-bounce cost is acceptable in the chrome-only
   case (single ray per pixel).

## Verification

* `--golden-fixture 5` produces a chrome-probe PNG showing the
  Sponza curtain damask pattern recognisably on the probe surface.
* `test_hello_engine_bindless_metadata.cpp` (phase 845) locks the
  metadata layout + sentinel contract.
* Tests 257 → 257 + N (TBD per phase).

## References

* [ADR W8-BD](ADR-20260606-W8-BD-per-geom-albedo-SSBO-and-curtain-reflections.md)
  — per-prim average colour path this ADR extends.
* [ADR agent-iteration-loop](ADR-20260606-golden-fixture-agent-iteration-loop.md)
  — the closed-loop debugging harness used to verify the result.
* `samples/engine/hello_engine/HelloRayQuery.hpp` — `kMaxGeomsPerInst`
  + `InstanceMatGpu` (the existing per-prim avg path; new fields
  land here in phase 840).
* `samples/engine/hello_engine/HelloGltf.hpp` — per-prim texture
  upload loop (extended in phase 843 to also write the bindless
  array slot).
* `samples/engine/hello_engine/shaders/prim.frag.glsl` — fragment
  shader (extended in phase 841 + 842 with the new bindings + UV
  interp + bindless sample).
* `engine/render/rhi/include/cd/rhi/Descriptors.hpp` — new
  `kBindlessSampledImage` + `BindlessTextureArrayDesc` (phase 837).
* `engine/render/rhi/include/cd/rhi/IDevice.hpp` — new
  `create_bindless_texture_array` + `write_bindless_texture_slot`
  (phase 837).
* `engine/render/rhi/src/vulkan/VulkanDevice.cpp` — descriptor
  indexing feature enable + impl (phase 838).
