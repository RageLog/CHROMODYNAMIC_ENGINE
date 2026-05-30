# Sponza Root-Cause Reconciliation (Q1 Wake-Up Brief)

Synthesis of three independent, read-only investigators run in parallel
on the persistent "Sponza receives no non-sun light and casts no
shadows on itself" report after phases 446-456.

- Investigator A: GPU / Vulkan-state angle (`sponza-investigator-A-gpu.md`)
- Investigator B: Math / precision angle (`sponza-investigator-B-math.md`)
- Investigator C: Data-integrity angle (`sponza-investigator-C-data.md`)

This document is the single page the user should read on wake-up
before deciding what to spend the next session on.

---

## 1. Side-by-side hypothesis comparison

| Aspect | Investigator A (GPU) | Investigator B (Math) | Investigator C (Data) |
|---|---|---|---|
| **Top hypothesis** | Per-prim `MaterialInstance` descriptor sets only write bindings 4/8/9; bindings 0/1/2/3/5/6/7/10 stay UNDEFINED. Vulkan binds sets atomically, so binding `pr.prim_inst` replaces the global `s.prim_inst` and the shader reads garbage `cd_tlas`, `cd_lights`, `cd_shadow`, `cd_ibl_*`, `cd_instance_mats`. | Phase 456's per-prim normal map combined with screen-derivative `cotangent_frame()` produces TBN flips on Sponza's mirrored-UV columns/arches; the perturbed N feeds the ray-query origin `v_world_pos + N*0.01`, pushing the ray INTO the wall, so every non-sun visibility ray returns `vis = 0`. | Identical to A: `pr.prim_inst` is allocated but only bindings 4/8/9 written; bindings 0/1/2/3/5/6/7/10 are in Vulkan UNDEFINED state. ~28 Sponza draws sample uninitialised memory for lights / TLAS / CSM / IBL / inst-mats. |
| **Confidence** | HIGH 90% (lights half), MEDIUM-HIGH 75% (self-shadow half) | Not explicitly numeric; presented as "the precision-adjacent issue that DOES fire". Implicitly medium — requires phase456 to be active for the symptom. | VERY HIGH (single-sentence claim: code path verified, exact match to symptoms). |
| **What explains "non-sun lights have no effect"** | `cd_lights.count` reads from zero-filled uninitialised UBO → multi-light loop never iterates. | `vis = 0` on every non-sun ray → contribution multiplied by zero per fragment. | Same as A — `cd_lights.count` reads zero → loop runs 0 iterations on Sponza prims only. |
| **What explains "Sponza doesn't self-shadow / no sun shadow"** | `cd_tlas` binding 2 is undefined → ray query is implementation-defined (usually no-op, `vis = 1`). CSM uses bindings 0/1 also uninitialised → mis-projected shadow lookup. | TBN flip on mirrored UVs sends ray origin through wall, so RT shadow rays consistently return `vis = 0` (i.e. EVERYTHING in shadow), not the symptom directly. | Same as A — bindings 0/1 (CSM UBO + map) and binding 2 (TLAS) are undefined → CSM lookup random / RT rays into garbage AS → mostly no-hit on contemporary drivers. |
| **Where the bug lives (code)** | `HelloMeshes.hpp:234-282` (per-prim descriptor write list is 3 entries instead of 11). | `prim.frag.glsl:716` (`cotangent_frame` with screen derivatives) + `prim.frag.glsl:807,851` (ray-query origin offset using perturbed N). | `HelloMeshes.hpp:238-282` + `Material.cpp:438-460` (MaterialInstance::create allocates but does not write defaults). |
| **What the fix looks like** | Extend per-prim descriptor write list to cover bindings 0/1/2/3/5/6/7/10 with the same handles `s.prim_inst` gets at `main.cpp:4597-4631`; update binding 2 (and 10) on EVERY per-prim set on each TLAS rebuild. Architectural follow-up: split into "scene constants" set 1 (TLAS+lights+IBL+shadow) bound once per frame and "per-material" set 0. | Either (a) force `normal_strength = 0` on every Sponza prim, or (b) replace ray-query origin offset to use the GEOMETRIC `safe_N` (pre-perturbation) instead of normal-mapped `N`. | Same as A: extend `tw` writes vector in `HelloMeshes.hpp` to carry the missing 8 bindings; OR move to bindless (push-constant texture index + descriptor indexing) to eliminate per-prim set proliferation. |
| **Already-tested-and-failed prior fixes the hypothesis explains** | phase451 bias tune, phase455 floor sentinel, phase456 per-prim normal/MR all leave bindings 2/3/10 untouched on per-prim sets — explains why three rounds of fixes didn't help. | Phase 456 ACTIVATED the bug (pre-phase-456 the gate forced normal-map FALSE for `is_gltf_prim`). Doesn't explain why phase451/455 didn't help. | Identical to A. Also notes phase 456 traded "one bug (descriptor aliasing on texture binding)" for "a strictly worse bug (8 unwritten bindings)". |
| **What is RULED OUT in this report** | Coordinate-space scale (asset 0.008 × entity 0.01 verified consistent through TLAS); normal direction inversion in loader; descriptor pool exhaustion. | Coordinate-frame mismatch (v_world_pos and lights both meter-scale); matrix layout / row-vs-column-major; shadow-map ortho fitting (irrelevant to non-sun bug because non-sun lights don't consult the shadow map); N·L sign; distance attenuation crushing too aggressively. | TLAS instance push order (Sponza at idx 0, transform correct); light UBO packing (`static_assert(sizeof==656)` holds); `sponza_geom_albedos` vs `gltf_prim_ranges` ordering; multi-geom BLAS exceeding 32-geom cap; LightUboGpu `pad[3]` packing drift (previously fixed). |

## 2. Overlap analysis

### Strong convergence (A and C agree on the SAME root cause)

A and C both finger the **per-prim descriptor set's missing 8 of 11
bindings** as the smoking gun, and they identify the same code site
(`HelloMeshes.hpp:234-282`) and the same fix shape. They arrived at
this independently from different starting points — A walked the
Vulkan binding-state machine, C walked the data-flow chain — and
landed on the identical mechanism. This is the strongest possible
signal short of a runtime probe.

Both also predict the SAME observable: "every other entity (CesiumMan,
PBR spheres, procedural prims) is lit correctly because they bind the
SHARED `prim_inst` which IS fully wired at boot — only Sponza is
broken because only Sponza uses per-prim sets." That prediction
matches the user's symptom exactly.

### Investigator B is COMPATIBLE but secondary

B's TBN-flip hypothesis is internally consistent and would produce
a "non-sun lights kill Sponza" symptom on the **assumption that the
ray query actually reaches the TLAS**. But A and C show the TLAS
binding itself is undefined on per-prim Sponza draws — so the ray
query in question never has a chance to correctly resolve regardless
of the bias direction. B's analysis is correct as a SECONDARY defect
that will surface AFTER the descriptor-set fix lands; if A/C's fix is
applied and Sponza still has wall-occlusion artefacts on columns, B's
TBN-flip story is the next candidate.

B also independently rules out the four "obvious" coordinate-space
and matrix-layout failure modes, which strengthens A and C's case by
elimination.

### No genuine conflicts

The three investigators do not contradict each other on any factual
claim. They agree:

1. Sponza vertices arrive at the GPU in cm, scaled to meters by the
   ECS entity transform (B verified; A confirmed; C confirmed).
2. The light UBO and TLAS data are correctly built CPU-side (C
   verified; A and B both implicitly rely on this).
3. The multi-light loop in `prim.frag` is correctly coded; the bug is
   that `count` arrives as zero (A and C) or that the geometry term
   collapses it (B-secondary).
4. The fallback path at `main.cpp:4106-4117` (used when the per-prim
   allocation fails) actually WOULD work — because it rebinds the
   complete shared `s.prim_inst` — which explains "occasional reports
   that it worked once" (A explicitly, C implicitly).

The only divergence is one of EMPHASIS:
- A and C: the root cause is descriptor incompleteness (Vulkan-spec
  UB), and B's TBN issue is a secondary defect to chase later.
- B: the precision/TBN angle is presented as the top hypothesis but
  explicitly acknowledges that descriptor aliasing and TLAS-build
  questions are out of scope and would need a separate pass.

When B's "out-of-scope" caveats are combined with A's and C's primary
findings, the picture is unambiguous: **descriptor incompleteness
first, TBN-flip second**.

## 3. Ranked next-step actions

### #1 (highest-confidence fix candidate) — Complete the per-prim descriptor writes in `HelloMeshes::boot_meshes()`

**File**: `samples/engine/hello_engine/HelloMeshes.hpp` lines 234-282.

**Change**: Extend the per-prim descriptor `tw` writes vector to
ALSO carry bindings 0, 1, 2, 3, 5, 6, 7, 10 with the same handles the
boot-time shared-instance setup uses at `main.cpp:4597-4631`. This
requires passing the additional handles (shadow UBO, shadow sampler,
TLAS, lights UBO, IBL spec, IBL diff, BRDF LUT, instance-mats SSBO)
into `boot_meshes` — they are already in scope at the call site.

**Secondary**: In `HelloTlasRebuild::rebuild_tlas_and_transition_depth`,
iterate every per-prim `MaterialInstance` (currently only the global
`s.prim_inst` is updated) when re-writing binding 2 (TLAS) and
binding 10 (instance-mats SSBO) each frame. Accept a
`std::span<MaterialInstance*>` of "also-update" slots.

**Confidence this fixes the bug**: A says 90% for the "lights" half
and 75% for the "self-shadow" half. C says VERY HIGH. Combined
convergence puts this at ~90-95% chance of resolving the user's
observable symptom on its own.

**Estimated work**: 1 focused phase (1-2 hours). Plumbing only;
shader and material classes unchanged.

### #2 (second-most-likely / co-fix) — Replace ray-query origin offset with geometric normal on the gltf_prim path

**File**: `samples/engine/hello_engine/shaders/prim.frag.glsl`
lines 807 (area light) and 851 (point/spot).

**Change**: At each `rayQueryInitializeEXT(..., v_world_pos + N * 0.01,
...)` site, replace the perturbed `N` with the geometric `safe_N`
(the pre-normal-map normal computed at line 712-ish, before the
`cotangent_frame` perturbation). The shaded N stays normal-mapped
for the BRDF; only the ray-bias normal is geometric.

**Why second**: B's TBN-flip story may also be firing on Sponza
columns. Even after #1 fixes the descriptor delivery, mirrored-UV
columns can still produce `vis = 0` from wall self-intersection. If
#1 alone does not fully resolve the symptom on columns, #2 is the
quickest follow-up. Cost: ~3 lines of GLSL.

**Optional bisection test before committing**: at
`HelloGltf.hpp:241`, set `range.normal_strength = 0.0F` for every
Sponza prim. If the symptom changes between "normal_strength = 0"
and "normal_strength = glTF.scale", that confirms B's hypothesis is
also active.

### #3 (validation steps before declaring victory)

a. **Single-line shader probe (do BEFORE the fix)** to PROVE the
   descriptor-set hypothesis. Add to the top of
   `prim.frag.glsl::main()`:

```glsl
if (pc.tint.w > 3.5 && pc.tint.w < 4.5) {
    out_color = vec4(float(cd_lights.count) / 8.0, 0.0, 0.0, 1.0);
    return;
}
```

Build, run Sponza, screenshot:
- Red intensity 0 → A/C confirmed (count is zero; descriptor uninit).
- Red intensity 0.5 (4/8) → descriptor WAS wired but something else
  is wrong → revisit B's TBN hypothesis or look elsewhere.

b. **RenderDoc capture (post-fix)** — open a Sponza frame, navigate
   to a Sponza-prim draw call, inspect descriptor set 0:
   - Confirm bindings 0..10 ALL show populated resources (not
     "VK_NULL_HANDLE" / "Unused").
   - Verify binding 2 (TLAS) handle matches the per-frame TLAS
     created by `HelloTlasRebuild`.
   - Verify binding 3 (lights UBO) buffer offset matches the
     `s.lights_ubo` allocation.

c. **Golden-image diff** against
   `tests/golden/sponza/sponza_lit.png` (a Sponza screenshot baseline
   should be captured BEFORE the fix and another AFTER; SSIM threshold
   ~0.95 + FLIP heatmap to confirm Sponza receivers visibly change).

d. **Vulkan validation with GPU-Assisted Validation enabled** —
   re-run hello_engine with `VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT`
   in `VkValidationFeaturesEXT`. GPU-AV diagnoses uninitialised
   descriptor reads at draw time; standard validation does not. This
   should have caught the bug originally; enabling it is cheap.

e. **Cross-API smoke** — confirm the fix also passes the D3D12
   parity smoke test (Run 28's `tests/d3d12_parity_smoke`). The
   per-prim descriptor concept is Vulkan-specific; the D3D12 path
   uses root signatures and tables — symptom may not manifest there
   but parity must be preserved.

## 4. Items NEEDING USER ACTION

These items cannot be resolved without runtime evidence the AI
investigators do not have access to.

### Mandatory before applying #1

1. **Run the shader probe from §3a and share the screenshot.**
   This proves (or disproves) the descriptor-set hypothesis BEFORE
   you spend an hour implementing the fix. Probability the probe
   shows "red intensity 0" is ~90% based on the convergence of A
   and C, but the cost of the probe is 10 minutes including build.

2. **RenderDoc capture of a CURRENT (pre-fix) Sponza frame**, with
   one Sponza-prim draw call's descriptor set 0 expanded. Save the
   .rdc to `docs/AUDIT/renderdoc/sponza-prefix.rdc` (or share the
   relevant descriptor-table screenshot). This is the definitive
   evidence the per-prim set is incomplete.

### Recommended but not strictly mandatory

3. **Confirm the user wants the descriptor fix (#1) or the architectural
   bindless refactor (alternative from C)** before the fix lands.
   - #1 fix: cheap, surgical, ~1 phase, keeps the per-prim sets.
   - Bindless: ~3-5 phases, removes the entire bug class, but touches
     `cd::material`, `cd::rhi` descriptor indexing support, and the
     shader push-constant block. Better long-term but larger blast
     radius and impacts more libraries.

4. **Decide whether to pre-empt B's TBN issue** in the same fix
   batch (#2 from §3) or wait to see if #1 alone resolves the user
   symptom. Recommended: include #2 in the same commit — it is 3
   lines and the cost of a stale follow-up is higher.

5. **GPU-Assisted Validation policy** — should the default debug
   preset enable GPU-AV (slows draws ~3-5×) so future descriptor-
   completeness bugs surface immediately? This is a project-policy
   call, not a code question.

### Items the investigators COULD NOT verify and explicitly flagged as runtime-only

- Whether `cd_lights.count` actually reads as 0 vs. garbage non-zero
  on the user's GPU (driver-dependent UB). Probe §3a resolves.
- Whether the per-prim descriptor pool ever fails to allocate
  (forcing the fallback path that "works by accident"). RenderDoc
  capture of allocation logs or counter `pool_remaining` would
  resolve.
- Whether mirrored UVs on Sponza columns actually produce TBN flips
  in the user's mesh asset (depends on the exact Khronos Sponza
  variant + glTF tangent buffer or derivative path). Bisection
  test from §3b resolves.

## 5. Honest uncertainty inventory

- **The descriptor-incompleteness story is consistent with EVERY
  symptom**, but neither A nor C had a runtime instrumentation
  available; both rely on Vulkan-spec analysis + code reading. The
  probability the analysis is correct is high; the probability the
  fix is sufficient on its own is slightly lower (could be
  ~85-90%) because a SECOND defect (B's TBN flip) is also
  plausible.
- **None of the three investigators ran a build or executed the
  engine.** No RenderDoc capture, no GPU-AV log, no shader probe.
  The case rests on static analysis only.
- **Phases 451 / 455 / 456 were also static-reasoning fixes** that
  did not resolve the bug. The bias-fix and floor-fix did not
  TARGET the descriptor-completeness layer; they targeted symptoms
  one layer higher. That is consistent with A/C's diagnosis (right
  layer for the first time), but the user has reasonable cause to
  be skeptical of "this round of static reasoning is the one that
  works". The shader probe (§3a) is the single cheapest piece of
  evidence to escalate confidence from "high" to "verified" before
  spending implementation budget.
- **B's TBN-flip hypothesis is plausible but unverified**, and
  cannot be tested by code-reading alone. The bisection test in
  §3b is the way to isolate it.

---

End of reconciliation. Next session should begin with §3a (shader
probe) and §4 item 2 (RenderDoc capture) before any code edit.
