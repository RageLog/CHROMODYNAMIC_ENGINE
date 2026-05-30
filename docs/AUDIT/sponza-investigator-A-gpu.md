# Investigator A — GPU/Vulkan angle

Independent investigation of the persistent "Sponza receives no non-sun light
and casts no shadows on itself" report, post-phases 446-456. This report is
written in isolation (no peeking at the parallel investigators) and isolates
the GPU/Vulkan-validation–visible failure path.

## Hypothesis

The **per-prim `MaterialInstance` descriptor sets created in
`HelloMeshes::boot_meshes()` are PARTIAL**: they only write bindings 4 (albedo),
8 (normal), and 9 (MR). Every other binding the fragment shader reads
(0=shadow UBO, 1=shadow sampler, **2=cd_tlas**, **3=cd_lights UBO**, 5/6/7=IBL,
**10=cd_instance_mats SSBO**) is left **uninitialized** on the per-prim
`VkDescriptorSet`. When `draw_floor_and_entities()` enters the Sponza loop and
binds `pr.prim_inst` (`HelloEngineFx`-flow main.cpp line ~4103), the per-prim
descriptor set REPLACES the global `s.prim_inst` that previously held the
per-frame TLAS and the LightArray UBO. The shader's `cd_lights.count` then
reads from **uninitialized memory** and `ray_visibility(...)` traverses
**garbage TLAS pointer** — neither of which is required by the Vulkan spec to
produce zero, but on common consumer drivers (NVIDIA / AMD) the result is
either:

  1. `cd_lights.count == 0` (zeroed pool memory) → multi-light loop NEVER
     iterates → user sees "Sponza is not lit by non-sun lights",
  2. `cd_lights.count == garbage_huge` → loop iterates over garbage slots
     with `pos_range.w <= 0` → `continue` skips every slot → same outcome,
  3. `cd_tlas == VK_NULL_HANDLE`-equivalent → `rayQueryInitializeEXT` no-op
     → `vis = 1.0` on every fragment → user sees "Sponza does not self-shadow".

This is consistent with EVERY symptom the user reported across phases 451 / 455
/ 456: the bias fix, the floor-gate fix, and the per-prim normal/MR descriptor
addition all left the **descriptor-aliasing root cause** untouched.

## Evidence

### 1. Per-prim descriptor write list is exactly 3 entries
`samples/engine/hello_engine/HelloMeshes.hpp` lines 234-282:

  - line 254-261 → push binding **4** (albedo)
  - line 262-270 → push binding **8** (normal, if available)
  - line 271-279 → push binding **9** (MR, if available)
  - line 280 → `inst_r->update(tw)` writes ONLY those 3 bindings
  - **no `binding = 0/1/2/3/5/6/7/10` write is ever performed on `inst_r`**

### 2. Global `prim_inst` (the only "complete" descriptor) has all 10 bindings
`samples/engine/hello_engine/main.cpp` lines 4597-4631 write the full set on
`s.prim_inst`. The per-frame TLAS update at HelloTlasRebuild.hpp line 156-163
ALSO targets `s.prim_inst` ONLY — it never iterates per-prim descriptor sets.

### 3. The bind sequence proves the swap
`main.cpp` line 5814 binds the global `s.prim_inst`.
Then `draw_floor_and_entities()` runs; inside the Sponza loop at line 4103
it overwrites the set-0 binding with `pr.prim_inst.bind(cmd, 0)` — **replacing
ALL bindings 0..10 at once** (Vulkan binds descriptor sets atomically, not
per-binding). The shader executing this draw call now reads from the
per-prim set's bindings 2/3/10 — which were NEVER written.

### 4. Vulkan spec wording on uninitialized descriptors
VK 1.3 §14.2.3 "Descriptor Set Allocation": *"Newly allocated descriptor sets
have uninitialized descriptors. Accessing an uninitialized descriptor is
undefined."* `VkDescriptorPoolCreateInfo::flags` does NOT include
`UPDATE_AFTER_BIND_BIT`, so the binding-state at draw time is exactly what
was last written via `vkUpdateDescriptorSets` for THAT set — which is just
4/8/9 for per-prim sets.

### 5. Validation-layer silence is consistent
Standard validation layers do NOT diagnose "reading an uninitialized binding"
at draw time (only via `GPU-AV` / `GPU-Assisted Validation`, which the build
does not enable by default). This matches the "validation reports nothing
but the bug persists" pattern in the user's report.

### 6. The "restore" block at line 4153-4183 ONLY restores bindings 4/8/9 on
`s.prim_inst`
But the Sponza draws use `pr.prim_inst`, NOT `s.prim_inst`. The restore block
fixes the NEXT non-Sponza draw, not the just-completed Sponza draws. The
Sponza fragments have already executed with the broken per-prim set.

### 7. Bias/normal/per-prim-MR fixes don't touch descriptor completeness
- phase451 (`prim.frag.glsl` lines 136-150): tunes the ray BIAS magnitude.
  Useless if `cd_tlas` itself is bogus.
- phase455 (line 654): tightens the floor sentinel. Doesn't touch
  bindings 2/3/10.
- phase456 (`HelloMeshes.hpp` 246-282): ADDS per-prim normal+MR descriptors
  but leaves bindings 2/3/10 still unwritten on per-prim sets.

### 8. The fallback branch at main.cpp 4106-4117 actually works
When `pr.prim_inst.is_valid()` is FALSE (per-prim allocation failed), the
code rewrites binding 4 on the SHARED `s.prim_inst` and rebinds it. This
fallback path keeps bindings 2/3/10 intact (last set by boot + the per-frame
TLAS update). Sponza lighting on this fallback path WOULD WORK — which is
also consistent with users on under-capacity descriptor pools occasionally
reporting "it worked once."

### 9. Sponza-scale interaction is a red herring
Sponza glTF root node scale `0.008` (asset/samples/Sponza/Sponza.gltf node[0])
is correctly compensated by the ECS entity scale `0.01` (main.cpp 1870), and
the TLAS instance transform from `model_for(ent)` (main.cpp 5777-5782)
correctly carries that scale. The ray-query math IS self-consistent, so
this is NOT a coordinate-space bug — it's a descriptor-set bug.

### 10. Normal direction is NOT inverted
The glTF Sponza ships with authored exterior-facing normals
(`GltfLoader.cpp` line 159-160 reads NORMAL accessor verbatim, no flip).
Pipeline cull state and Mikkelsen TBN derivatives also don't invert N.
NDotL is fine; the issue is the LOOP NEVER EXECUTES, not that NDotL is zero.

## Confidence

**HIGH (90%)** that this is the *primary* root cause for the
"non-sun-lights have no effect on Sponza" half of the bug.

**MEDIUM-HIGH (75%)** for the "Sponza does not self-shadow" half — the
per-prim `cd_tlas` (binding 2) is *also* uninitialized, but driver behavior
on UB ray-query intrinsics can sometimes happen to no-op (vis=1) or sometimes
crash. Symptom matches the no-op case.

Confidence is not 100% because we cannot rule out a SECOND independent bug
without instrumenting the GPU (e.g., a forced `cd_lights.count = 99u;
out_color = vec4(1,0,1,1); return;` early-out at the top of `main()` to prove
the binding is bogus). Recommend that diagnostic as the next step.

## Recommended next step

1. **Single-line shader probe**: at the top of `prim.frag.glsl`'s `main()`,
   add `if (pc.tint.w > 3.5 && pc.tint.w < 4.5) { out_color = vec4(float(cd_lights.count) / 8.0, 0.0, 0.0, 1.0); return; }`
   and run hello_engine.
   - Red intensity 0 → count is 0 (descriptor uninitialized as hypothesized).
   - Red intensity 1.0 → count is 8 (descriptor is somehow valid; bug
     lies elsewhere — fall through to a second probe on `cd_tlas`).

2. **Fix (assuming the probe confirms)**: in
   `HelloMeshes.hpp::boot_meshes()`, the per-prim descriptor write list
   currently has 3 entries (bindings 4/8/9). Extend it with the SAME
   bindings that `s.prim_inst` gets at main.cpp 4597-4631:
   - binding 0 (shadow UBO), 1 (shadow sampler), 2 (TLAS), 3 (lights UBO),
     5 (IBL spec), 6 (IBL diff), 7 (BRDF LUT), 10 (inst-mat SSBO).
   Pass those handles in via the `boot_meshes` signature (already takes
   `device`, `albedo`, `prim_inst`, `prim_material`, normal/MR views — just
   add the rest).

3. **Per-frame TLAS update fix**: extend
   `HelloTlasRebuild.hpp::rebuild_tlas_and_transition_depth` to ALSO update
   binding 2 (and 10) on EVERY per-prim instance, not just on the global
   `prim_inst`. Today it iterates a single MaterialInstance reference; a
   minimal change accepts a `std::span<MaterialInstance*>` of "also-update"
   slots.

4. **Architectural follow-up** (defer): introduce a `BindlessSet`-style
   "scene constants" descriptor set (set 1) carrying TLAS + LightArray +
   IBL + shadow, freeing set 0 for purely per-material textures. Each
   material's per-prim descriptor only manages set 0; set 1 is updated
   ONCE per frame and bound ONCE per frame. This eliminates the entire
   class of "did I remember to copy bindings 2/3/10 to every textured
   prim's descriptor" bug.

## Out-of-scope but worth flagging

- The fallback path at main.cpp 4106-4117 works by accident — a single
  shared descriptor set updated mid-recording is also UB per Vulkan spec
  (the update must be visible to GPU before the bind). If the descriptor
  pool ever runs out and the per-prim allocation fails, the engine starts
  exercising this UB path. Better to make the explicit per-prim set work
  correctly than to rely on the alias.

- `descriptor_pool_` is sized for `maxSets=1024` and 1024
  combined-image-samplers (`VulkanDevice.cpp` line 1729). Sponza has ~28
  prims × 3 bindings = 84 samplers, well under cap. So allocation success
  isn't the question — it's the WRITE coverage on the allocated sets.
