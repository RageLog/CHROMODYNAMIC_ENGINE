# ADR-20260616 — Band 6 render-misc + foundation_utils: scope seals to 100%

- Status: Accepted
- Date: 2026-06-16
- Scope: `engine/render/{ibl_gpu,gpu_particles,light_shafts,nrc}`,
  `engine/foundation/foundation_utils`
- Related: ROADMAP_ALL_MODULES_TO_100.md Band 6;
  PROJECT_COMPLETION_STATUS.md §1 (foundation) + §5 (render features);
  `cd::ibl` (CPU bake, B3); `cd::rhi` (100%, upload target).

## Context

Five Band-6 libraries (40–48%) each had a real, working *core* and an honest
*documented* gap. The road-to-100 honesty rule requires every documented gap
to be in exactly one terminal state: IMPLEMENTED+tested, or SEALED by a
one-paragraph scope decision with a precise promote-on-need trigger. This ADR
seals the deferred slices and records the test work that lifted the libs;
banners were corrected so a working path is no longer mislabeled and a sealed
path is no longer advertised as a feature.

## Decision

### §1 — cd::ibl_gpu (was 48%, 0 tests): upload-helper-v1 SEALED + tests added

The three upload helpers (`upload_cubemap_rgba16f`, `upload_prefiltered_specular`,
`upload_brdf_lut`) are real RHI code (staging buffer → barrier → copy → barrier
→ view) but shipped **0 tests**, so correctness was unverified — the load-bearing
gap.

**IMPLEMENTED (the missing verification):** `tests/test_ibl_gpu.cpp` now checks
(a) the host-side pure helpers deterministically and everywhere —
`float_to_half` exact bit patterns (0/1/0.5/2, sign, overflow-clamp-to-0x7BFF,
underflow-to-0), the prefiltered mip-chain face-size halving, and the BRDF-LUT
RG half-packing order; and (b) the upload path **end-to-end on a live Vulkan
device** via an `upload_brdf_lut → copy_image_to_buffer → download_buffer`
round-trip that asserts every RG16Float half equals `float_to_half` of the
source, plus mip-count smokes for the cube + prefiltered-specular uploads. The
GPU tests `GTEST_SKIP` when no ICD is present so CI without a GPU stays green
(mirrors `restir_di_dispatch`); they run real on the host RTX 3080.

**SEALED:** `upload-helper-v1` is the complete scope for this lib — upload of
the three cd::ibl baked products. **Boundary (now in the header):** cd::ibl
bakes on the CPU (B3); cd::ibl_gpu uploads. A *GPU-side bake* (compute-shader
prefilter / convolution feeding the upload) is NOT in scope here.
**Trigger to lift the boundary:** when a renderer needs runtime (per-frame /
per-skybox-change) IBL regeneration rather than offline-baked-then-uploaded
environment maps, add a `Bake.hpp` driving the cd::ibl GLSL on-device.

### §2 — cd::gpu_particles (was 45%): sim-CS-v1 SEALED + CPU branch topped up

CPU `advance()` + `compact_alive()` + one `kSimulateCS` compute kernel are real;
the banner over-claimed "emit kernels + indirect draw".

**IMPLEMENTED (CPU-branch test depth):** death-crossing-zero (a particle whose
life < dt dies this step and is not advanced afterward), compaction *payload*
preservation (survivors packed contiguously into the head in stable order, not
merely counted), and the zero/all boundaries.

**SEALED:** `sim-CS-v1` = CPU reference advance/compact + the single in-place
simulate kernel that accumulates the live `instance_count` into the indirect-
draw args layout it declares. **NOT in scope (promote-on-need):** a GPU
emit/spawn kernel and host-side indirect-draw buffer ownership + render-pass
wiring. These need the GPU-driven-particles *consumer* integration, not a fix
to this header. The banner was corrected to say exactly this.
**Trigger to lift:** when a consumer wires a GPU-driven particle render pass,
add an emit kernel + an `IndirectArgs` buffer owner alongside the simulate CS.

### §3 — cd::light_shafts (was 42%): radial-blur-v1 SEALED + banner corrected

The header claimed an "Analytic single-scattering (Kim & Marsalek 2014 epipolar)"
path that does not exist; only the Mitchell screen-space radial blur is real.

**IMPLEMENTED (test depth):** off-screen-sun projection (sun in front but far
off-axis → uv outside [0,1], which the consumer must clamp/skip), the
`fz == 0` camera-plane degenerate (returns the (-1,-1) sentinel, no divide-by-
zero), default-settings tuning lock, and presence of both GLSL strings.

**SEALED:** `radial-blur-v1` = `sun_screen_pos()` projection + `kRadialBlurCS`
(occlusion-aware Mitchell radial blur) + the `kInlineConeShaftGlsl` fallback —
the functional, shipped look. Analytic **epipolar** sampling (Engelhardt &
Dachsbacher 2010 / Kim & Marsalek 2014) is a *separate algorithm*, not a TODO
of the above, and is NOT implemented. The banner was corrected to mark
radial-blur as the implemented path and epipolar as deferred-by-design.
**Trigger to lift:** when sunset-grade finely-detailed shafts through complex
occluders are required, add an epipolar-sampling pass as a second path
selectable alongside (not replacing) the radial blur.

### §4 — cd::nrc (was 40%): research-skeleton-v1 SEALED HONESTLY + test hardened

A tiny single-hidden-layer CPU MLP with a weak test that allowed *zero gain*
(`err1 < err0 + eps`), so a non-learning skeleton would have passed.

**IMPLEMENTED (test strengthened):** `TrainingStrictlyReducesErrorOnFittableTarget`
now asserts the SGD MLP **strictly** reduces L1 error on a fittable constant
target — `err1 < err0`, at least a 50% reduction, and convergence below an
absolute 0.05 floor — so a broken/no-op backward pass trips the test.

**SEALED:** `research-skeleton-v1`. This is honestly a **CPU reference / research
skeleton, NOT a production radiance cache.** A usable NRC needs an on-GPU
trainable MLP (16-wide fully-fused tensor-core layers, Adam, frequency
encoding) — a multi-month subsystem. The CUDA / OneAPI / SPIR-V backends behind
`CD_NRC_BACKEND` are NOT implemented. The banner now says research-skeleton, not
"feature".
**Trigger to lift:** when a path tracer actually integrates indirect-bounce
radiance caching for shipping, stand up a `CD_NRC_BACKEND=tinycudann` (or a
custom SPIR-V fully-fused MLP) translation unit with Adam + frequency encoding;
the CPU MLP stays as the reference oracle for that backend's tests.

### §5 — cd::foundation_utils (was 40%): aggregator, 100%-by-definition

A pure umbrella header that only `#include`s its eight sibling foundation libs
(bench/config/diag/events/frame_timing/plugin/profile/mem). It has **no own
logic** — there is nothing to implement; completeness = it includes and
re-exports the right siblings.

**SEALED as aggregator-100-by-definition.** **IMPLEMENTED (the missing proof):**
the smoke test was upgraded from compile-only to a reachability check that names
one symbol from EVERY re-exported member lib (pointer-typedef references +
config's format constant/entry-point), so a dropped `#include` in the umbrella
becomes a build break rather than a silent aggregation gap (fail-on-revert).
**Trigger to revisit:** if a future foundation lib is added/removed, the
umbrella's member list and this reachability test are updated together.

## Consequences

- All five libs reach a defensible 100% under the honesty rule: real impl or
  sealed scope, each with a dedicated gtest (fail-on-revert) and a precise
  promote-on-need trigger.
- The `0 tests` state of cd::ibl_gpu is corrected, including a real on-GPU
  byte-correctness round-trip on the RTX 3080.
- Three misleading banners (light_shafts epipolar, gpu_particles emit/indirect,
  nrc "feature") are corrected; the ibl_gpu CPU↔GPU boundary is documented.
- No out-of-scope library, rhi/, samples/ or hello_* was touched. No GPU bake,
  emit kernel, epipolar pass, or trainable-MLP backend was added — those are
  sealed with triggers, not stubbed.
