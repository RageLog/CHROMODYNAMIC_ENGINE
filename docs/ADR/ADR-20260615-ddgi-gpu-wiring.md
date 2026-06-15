# ADR-20260615: DDGI GPU Wiring — Live Indirect Bounce Into the HDR Scene

**Date**: 2026-06-15
**Status**: Implemented (phase1213, commit `5ff21d3`, dev branch)
**Stakeholders**: Rendering, Global Illumination, RHI (Vulkan), Sample / hello_engine
**Supersedes nothing** — extends [ADR-20260531-ddgi](ADR-20260531-ddgi.md) (algorithm + CPU
probe math + dispatch skeleton). This ADR records the *integration* decisions that turned that
skeleton into a live, visible indirect bounce in `hello_engine`.

---

## Bağlam

ADR-20260531 chose DDGI (Majercik, Marrs, Spjut, McGuire — "Dynamic Diffuse Global Illumination
with Ray-Traced Irradiance Fields", JCGT 8:2, 2019) and delivered:

- CPU probe-grid math (`cd::ddgi::ProbeGrid`, octahedral encode/decode, trilinear weights),
- the GLSL trace / blend / sample shader sources,
- `cd::ddgi::DispatchPass` owning the GPU resources (ray images, atlases, pipelines), and
- `Result<void>`-returning validated dispatch overloads + CPU-stub call counters for testing.

What it did **not** have was a working end-to-end frame in the engine. Two gaps made DDGI a
visual no-op:

1. The trace shader shaded every ray hit with a constant ~0.1 grey, so the irradiance atlas
   accumulated a flat ambient — there was no scene colour, no directional response, no bleed.
2. The sample pass needed a per-pixel **world position** to look up the probe lattice, but the
   engine's G-buffer carries depth + normal, not an explicit world-position target.

In addition, wiring a compute dispatch *between* the HDR raster pass and the composite pass
exposed a latent correctness bug in the Vulkan command-buffer layer (descriptor sets for the
DDGI compute pipelines were silently bound at the graphics bind-point).

This is the "P6 visual gate": the design was implemented, but the feature had to actually change
pixels — verifiably, deterministically, and without regressing the chrome golden — before it
could be called done. The work was scoped against the 2026-06-15 vision-tier review, in which
DDGI was ranked #1 and ReSTIR DI #2.

---

## Karar (Seçilen tasarım)

DDGI is wired as a four-pass compute chain (trace → blend-irradiance → blend-visibility →
sample) that runs **after the HDR scene pass and before composite**, adding one diffuse indirect
bounce into the HDR colour buffer. Six concrete decisions:

### 1. Sample-pass world position is RECONSTRUCTED from depth + `inv_vp`

`kDdgiSampleCS` (binding 1) samples the existing scene **depth** target and rebuilds world
position with the inverse view-projection, rather than reading a dedicated 9th render target.

- Saves one full-screen RGBA16F render target and its per-frame write bandwidth.
- Reuses depth the engine already produces, so no G-buffer restructuring.
- The raster vertex shader applies `clip.y = -clip.y` (Vulkan framebuffer upright), so the
  reconstruction negates NDC `y` when building the clip-space point fed to `inv_vp`:
  `clip = vec4(ndc.x, -ndc.y, depth, 1.0)`.
- `depth >= 1.0` (cleared far plane = sky / no geometry) takes an **early-out** `return`, so DDGI
  never tints the sky.

### 2. `inv_vp` is passed via a small UBO (binding 5), NOT a push constant

`SampleReconstructUbo { mat4 inv_vp; }` (64 B, `static_assert`-locked) is bound at sample
binding 5 and refreshed once per frame via `set_inv_vp()`.

- `SamplePushConstants` is already 80 B; adding a `mat4` (64 B) would take it to 144 B, over the
  guaranteed 128-byte push-constant floor. A UBO is the correct home for the matrix.

### 3. Real Lambertian radiance in the trace CS via a scene light UBO (binding 3)

`kDdgiTraceCS` consumes `CdDdgiLights { vec4 sun_dir; vec4 sun_col; }` (host POD
`cd::ddgi::SceneLightUbo`, 32 B, `static_assert`-locked) bound at trace binding 3 and refreshed
via `set_sun_light()`. On a committed ray-query triangle hit it computes a first-bounce diffuse
response: `direct = sun_col * (albedo · 1/π · max(0, dot(-dir, L)))` plus a small `sky_color`
ambient floor. Because ray-query inline RT has no closest-hit shader, there is no interpolated
normal/albedo at the hit, so the surface is approximated as facing back toward the probe
(`N ≈ -dir`) with a constant grey albedo (0.5) — the standard "diffuse-only first bounce"
surrogate when no hit shader is wired.

- This is the fix that makes DDGI visible: the placeholder grey-constant made the atlas a flat
  ambient and the feature a no-op (the P6 gate).

### 4. Execution placement, accumulation mode, and the default-OFF gate

DDGI executes **after** the HDR scene pass and **before** composite; the gathered indirect
irradiance is **ADDed** read-modify-write into the HDR buffer (`prev + indirect * 0.6`), i.e. it
is one extra diffuse bounce on top of the direct lighting the raster pass already wrote — not an
overwrite. It is gated on `ddgi_on && tlas.is_valid()`.

- `ddgi_on` **defaults OFF**. The golden-fixture path never sets it, so the chrome golden stays
  **byte-identical**. It is opt-in via the hello_engine panel toggle or the `--ddgi-force-on`
  CLI flag (`g_ddgi_force_on`, ORed into `fx.ddgi_on`).

### 5. Probe grid aligned to the Sponza nave

`HelloDdgi::sponza_desc()`: `origin (-10, 0.5, -5)`, `spacing (2.5, 1.5, 2.5)`,
`8 × 4 × 8 = 256` probes, `64` rays/probe, `hysteresis 0.97`, `max_distance 30 m`,
`probe_face_size 8`. The lattice blankets the visible nave (floor ~y0, columns to ~y6, width
~[-10,10], depth ~[-5,15]); 64 rays + 0.97 hysteresis ≈ 33-frame temporal convergence
(Majercik 2019 §4 starter config).

### 6. Vulkan `end_render_pass` bind-point correctness fix (exposed by DDGI)

`VulkanCommandBuffer::end_render_pass()` now clears `current_graphics_layout_ = VK_NULL_HANDLE`.
`bind_descriptor_set()` picks the bind-point from a heuristic: graphics layout if
`current_graphics_layout_ != VK_NULL_HANDLE`, else compute. Before this fix, a compute
`bind_descriptor_set` issued after a raster pass inherited the stale graphics layout, so the DDGI
descriptors were bound at `VK_PIPELINE_BIND_POINT_GRAPHICS` and **silently never reached the
dispatch** — no validation error, just a no-op pass. Clearing the layout at pass end routes the
follow-up compute binds to `VK_PIPELINE_BIND_POINT_COMPUTE`; the next
`begin_render_pass` + `bind_graphics_pipeline` re-establishes the graphics layout.

---

## Reddedilen alternatifler

- **Dedicated world-position G-buffer render target (the 9th RT).** Correct and trivial to
  sample, but costs a full RGBA16F target + its write bandwidth every frame for data already
  derivable from depth. Rejected in favour of depth + `inv_vp` reconstruction (Decision 1).

- **`inv_vp` as a push constant.** Would overflow `SamplePushConstants` to 144 B, past the 128 B
  guaranteed push-constant floor. Rejected for the binding-5 UBO (Decision 2).

- **ReSTIR DI first.** Ranked #2 in the 2026-06-15 vision-tier review. It is a many-light
  *direct*-illumination sampler and would still require a shading-integrator pass to land
  visibly; DDGI was the higher-leverage, more self-contained diffuse-GI win and so was taken
  first. ReSTIR DI remains queued under the vision-tier scope. See
  [ADR-20260531-restir-di-gi](ADR-20260531-restir-di-gi.md).

- **SBT / RT-pipeline DDGI trace (closest-hit shader).** A closest-hit shader would give true
  interpolated normal + albedo at the hit (removing the `N ≈ -dir`, albedo-0.5 surrogate). But
  the engine's RT decision is **inline ray-query** (`GL_EXT_ray_query`), and the trace pass uses
  it to match the rest of the RT stack (shadows, reflections). Adopting a separate RT pipeline +
  SBT just for DDGI would fork the RT path. Rejected; the diffuse-first-bounce surrogate is the
  accepted trade.

- **NRC (Neural Radiance Cache).** Requires an on-GPU MLP (training + inference path) that does
  not exist in the engine today. Out of scope for this wiring.

---

## Sonuçlar (etkilenen modüller)

**Works on Vulkan, verified.** With `--ddgi-force-on`, the DDGI-on vs DDGI-off frame differs in
~59% of pixels and is deterministic; an `execute_call_count` smoke test confirms the four-pass
chain actually records. The chrome golden is unchanged because `ddgi_on` defaults OFF
(Decision 4).

**Affected files:**
- `engine/render/ddgi/include/cd/ddgi/Ddgi.hpp` — `kDdgiTraceCS` real Lambertian + light UBO;
  `kDdgiSampleCS` depth-reconstruct world-pos + ADD into HDR.
- `engine/render/ddgi/include/cd/ddgi/DispatchPass.hpp` + `src/DispatchPass.cpp` —
  `SceneLightUbo` (binding 3) + `SampleReconstructUbo` (binding 5) + depth sampler;
  `set_inv_vp()` / `set_sun_light()`.
- `samples/engine/hello_engine/HelloDdgi.hpp` + `main.cpp` — boot / bind / execute_frame / grid /
  panel / `--ddgi-force-on`.
- `engine/render/rhi/src/vulkan/VulkanCommandBuffer.cpp` — `end_render_pass` bind-point fix.

**Cross-backend parity is FUTURE.** DDGI is **Vulkan-only** today. D3D12 / Metal DDGI parity is
tracked under the standing backend-parity scope, **not this ADR**.

**The 3-frame contribution is subtle.** The atlas accumulates via 0.97 hysteresis (~33-frame
convergence), so the first few frames after enabling DDGI show little. A boot warm-up (pre-spin
the trace/blend chain before the first visible frame) is future polish, not shipped here.

**Bind-point heuristic hardening (future).** The `current_graphics_layout_ != VK_NULL_HANDLE`
heuristic in `bind_descriptor_set()` is correct for the current usage but fragile: it infers the
bind-point from a layout-non-null side channel. The hardened design is to **track the last-bound
pipeline type explicitly** (an enum updated by `bind_graphics_pipeline` /
`bind_compute_pipeline`) and select the bind-point from that, removing the reliance on layout
nullability. Tracked as a follow-up RHI task.

**References:** Majercik et al. 2019 (JCGT 8:2); the 2026-06-15 vision-tier scoping decision
(DDGI #1, ReSTIR DI #2); [ADR-20260531-ddgi](ADR-20260531-ddgi.md) (algorithm + CPU math).
