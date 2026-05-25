# CHROMODYNAMIC Engine — v1.3 Plan

- **Plan date:** 2026-05-25
- **Starting tag:** `v0.99.93`
- **Branch:** `dev`

This plan picks up the user-flagged gaps after the v1.2 reclaim push,
ordered "easy → hard, general → specific, high → low" per the marathon
rule.

## Shadow pipeline track (user flagged, 2026-05-25)

Direct user feedback during the marathon: *"isik ile gorunum boyle cokta
hos degil. sanirim shadow eksik birde bunuda plana ekle"* — the lit
view feels flat because casters don't drop shadows on the floor or on
each other. Three escalating phases:

### Faz 1.5 — planar projective shadow + hemisphere AO (SHIPPED)

Minimal-infrastructure visual lift while the descriptor-set refactor
needed for proper shadow maps is still pending.

- `make_planar_shadow_matrix(sun_dir, plane_y, lift)` flattens any
  caster onto the floor plane along the directional-light ray.
- `kPrimFS` gained a `tint.w < 0.5` sentinel that bypasses lighting
  and outputs a flat dark RGB blob — used by the shadow re-draw of
  every entity and PBR sphere.
- Hemisphere ambient (`sky_c` + `gnd_c` blended by `N.y`) replaces
  the prior flat ambient term so unlit faces respond to orientation.
- Real floor mesh (80×80 m at y = -0.55) added so projected shadows
  have something to land on; ImGui grid lowered to match.

Limitations (resolved in 1.6+):
- Hard shadow only — no penumbra. Alpha blending isn't wired in
  `MaterialDesc` yet.
- One directional caster — point/spot/area lights don't project
  shadows in this path.
- No self-shadowing — the projection collapses 3D casters to 2D, so
  occlusion between casters lying along the same sun ray is lost.

### Faz 1.6 — cascaded shadow maps (CSM)

Proper directional shadows requiring a descriptor-set wire-up.

- New depth render pass (depth-only pipeline, single attachment,
  back-face polygon offset).
- 3-4 cascades, practical split scheme (PSS) blended uniform+log
  (Zhang 2006 + Persson 2009 stable cascades) — `cd::light::Cascaded
  Shadow` already exists in the codebase from Phase 165 and is
  unused. Hook it in.
- PCF 3×3 + Poisson-disk softening + screen-space depth biasing
  (Persson "shadow acne" guidelines).
- New descriptor binding on the prim/PBR pipelines: combined image
  sampler for the depth atlas + UBO for the per-cascade
  `mat4 light_vp` + split distances.
- Wire EVSM (Lauritzen) as an opt-in for higher-quality leak-free
  results on the directional caster.

Blocker dependency: `MaterialDesc` doesn't expose
`descriptor_bindings` for the existing prim shader. The
`LitPbrMaterial` (Phase 171) already uses descriptors; teach the
hello_engine path to consume it OR add a parallel
`prim_lit_material` with a single combined-sampler binding.

### Faz 1.7 — inline ray-traced shadows (`VK_KHR_ray_query`)

The Faz 1.5/1.6 paths handle the sun. Spot, point, and area lights
need real RT shadows.

- Build a small TLAS over the visible scene each frame (reuse the
  hello_path_trace BLAS upload path).
- Bind it as a `kAccelerationStructure` descriptor on the prim
  shader pipeline.
- For each non-sun light fired per fragment: `rayQueryEXT` to test
  whether the shading point sees the light source. If occluded,
  zero its NdotL contribution.
- Optional: only fire the ray every N pixels (checkerboard) +
  spatial denoise (a frame-coherent A-trous wavelet filter).

Blocker: same descriptor refactor as Faz 1.6, plus a per-frame TLAS
rebuild path that doesn't exist on the raster side yet.

## Render path-tracing track (continuation of Faz 2/3)

The path-tracer sample (`hello_path_trace`) shipped in v1.2 with NEE
+ MIS + accumulation. Outstanding:

### Faz 2 J — denoiser

OpenImageDenoise (Intel OIDN) 2.x integration:
- HDR color + albedo + normal aux feature buffers from the PT pass.
- Run the U-Net filter after `frame_count` reaches a threshold.
- Optional GPU device: OIDN 2.x supports CUDA + SYCL + HIP; default
  to the OIDN_DEVICE_TYPE_DEFAULT (CPU) for portability and let an
  env-var override pick GPU.

### Faz 3 K — ReSTIR DI (Bitterli et al. SIGGRAPH 2020)

Spatiotemporal reservoir resampling for direct illumination. Plug
into the existing PT raygen as a replacement for the brute-force
NEE loop:
- Per-pixel `Reservoir` of M candidate light samples.
- WRS (Weighted Reservoir Sampling) initial pass.
- Temporal reuse via reprojected reservoir from the previous frame
  (motion vector required — currently absent).
- Spatial reuse via N-pixel neighborhood pass.

### Faz 3 L — ReSTIR GI (Ouyang et al. HPG 2021)

Reservoir resampling extended to global illumination (indirect
bounces). Builds directly on Faz 3 K's reservoir infrastructure.

### Faz 3 M — Neural Radiance Cache (Müller et al. SIGGRAPH 2021)

Online-trained MLP that caches incoming radiance at each ray's
last-bounce point. Tiny CUDA/HIP MLP backend or a CPU fallback for
portability. Lowest priority — large surface area.

## Cross-track v1.3+ backlog (carried from v1.2)

1. **D3D12 DXR step 4-5** — `CreateStateObject` + `DispatchRays`.
2. **GPU IBL bake passes** — compute-shader convolution.
3. **LTC area-light shader** — Heitz et al. linearly-transformed
   cosines for the area-light record.
4. **OpenGL `ICommandBuffer`** — state-replay command buffer to
   unify the GL backend with Vulkan/D3D12.
5. **Linux/macOS/mobile hardware validation** — Phase 158-160 ship
   the platform windowing code; needs reviewer-side bring-up.
6. **IES profile parser**.
7. **Wayland window backend**.

## Sign-off discipline

`v1.0.0` and higher major tags remain user-only per the marathon
feedback policy. The implementer ships `v0.99.x` patches plus this
plan; the user decides when to cut a major.
