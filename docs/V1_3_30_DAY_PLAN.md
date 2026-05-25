# CHROMODYNAMIC Engine — v1.3 30-Day Visual + Engine Marathon Plan

- **Plan date:** 2026-05-25
- **Starting tag:** `v0.99.93` (post-Faz 1.5 UX batch)
- **Branch:** `dev`
- **Scope policy:** Each day's ship must:
  1. Live as a standalone library under `engine/<sub>/` (consumable
     in isolation, own gtest binary, own CMake target).
  2. Be **missing** from the engine today — items from
     `docs/V1_2_RECLAIM_REPORT.md` and `V1_3_PLAN.md` are skipped
     unless a still-needed extension was identified.
  3. Lift either *visual fidelity* or *engine core strength*. No
     pure refactor or doc-only days.

The list is ordered "easy → hard, general → specific, high → low"
per the marathon discipline rule.

---

## Week 1 — shadow stack + GI primitives (highest visual return)

| Day | Library                              | Ship                                                                              | SOTA reference / why                                                |
|----:|--------------------------------------|-----------------------------------------------------------------------------------|---------------------------------------------------------------------|
|  1  | `cd::render_shadow`                  | **CSM (cascaded shadow maps)** — 3-cascade PSS + PCF 3×3 + depth bias + slope    | Zhang 2006 + Persson 2009 stable cascades. Closes user-flagged "mutual shadow" gap. |
|  2  | `cd::render_shadow`                  | **EVSM (exponential variance SM)** — leak-free filtered shadows on top of Day 1  | Lauritzen 2008. Optional path; the CSM produces VSM-style depth.    |
|  3  | `cd::render_shadow`                  | **Inline RT shadows** (`VK_KHR_ray_query`) — punctual + area lights, raster pass | Bitterli 2020 §B (RT shadows). Closes the spot/point shadow gap.    |
|  4  | `cd::render_ao`                      | **GTAO (Ground-Truth Ambient Occlusion)** — temporal + spatial denoise           | Jiménez 2016 (Activision). State-of-art real-time AO.               |
|  5  | `cd::render_ssr`                     | **Screen-Space Reflections** — hierarchical depth tracing, blue-noise jitter      | Stachowiak 2015 (UE) / McGuire 2014. Stand-in for RT reflections.   |
|  6  | `cd::render_restir`                  | **ReSTIR DI** — reservoir streaming, temporal+spatial reuse                       | Bitterli 2020 SIGGRAPH. Direct-illumination unbiased estimator.     |
|  7  | `cd::render_restir`                  | **ReSTIR GI** — indirect-bounce reservoir reuse, demodulated                     | Ouyang 2021 HPG. Builds on Day 6's reservoir code.                  |

## Week 2 — post-processing + tonemap stack (camera fidelity)

| Day | Library                              | Ship                                                                              | SOTA reference                                                       |
|----:|--------------------------------------|-----------------------------------------------------------------------------------|----------------------------------------------------------------------|
|  8  | `cd::render_post::bloom`             | **5-stage compute downsample/upsample bloom** + Karis "stable" tap                | Karis 2013 (Bethesda talk). Sub-pixel stability.                     |
|  9  | `cd::render_post::motion_blur`       | **Per-object motion-blur** — velocity buffer + jittered tile pass                | McGuire 2012. Needs Day 24's velocity buffer.                        |
| 10  | `cd::render_post::dof`               | **Bokeh DOF** — hexagonal blade kernel, near/far split                            | Sousa 2013 (Crytek).                                                 |
| 11  | `cd::render_post::aa::taa`           | **TAA** — color clamping (Karis 2014) + velocity reproject + neighborhood AABB    | Karis 2014 talk. Most-cited TAA reference.                           |
| 12  | `cd::render_post::aa::smaa`          | **SMAA T2x** — fallback morphological AA for TAA-incompatible content             | Jiménez 2012 (Iryo).                                                 |
| 13  | `cd::render_tonemap`                 | **AGX + Hable + ACES Hill** picker + custom-LUT path                              | AGX (Sobotka 2022) replaces Narkowicz for production look.           |
| 14  | `cd::render_hdr`                     | **HDR display output** — HDR10 / scRGB / linear FP16 swapchain + PQ encode       | Windows + Vulkan HDR colorspace surfaces (Vulkan 1.2 HDR ext).        |

## Week 3 — volumetrics + sky (atmospheric realism)

| Day | Library                              | Ship                                                                              | SOTA reference                                                       |
|----:|--------------------------------------|-----------------------------------------------------------------------------------|----------------------------------------------------------------------|
| 15  | `cd::render_sky::atmosphere`         | **Production-grade atmospheric scattering** — pre-computed transmittance + sky-view LUT | Hillaire 2020 SIGGRAPH. Replaces today's analytical sky.           |
| 16  | `cd::render_volumetric::fog`         | **Volumetric fog** — froxel grid + temporal jitter                                | Wronski 2014 (Frostbite).                                            |
| 17  | `cd::render_volumetric::clouds`      | **Volumetric clouds** — Perlin-Worley density + ray-march + temporal              | Schneider 2017 (Horizon Zero Dawn).                                  |
| 18  | `cd::render_volumetric::lightshafts` | **Light shafts (god-rays)** — analytic single-scattering + epipolar sampling      | Kim & Marsalek 2014 (epipolar).                                      |
| 19  | `cd::render_brdf::ltc`               | **LTC area lighting** — Linearly-Transformed Cosines for rect/disk lights         | Heitz 2016. Closes today's rect-light "no shader yet" gap.           |
| 20  | `cd::render_brdf::sss`               | **Burley SSS** — pre-integrated diffusion profile + separable blur                | Burley 2015 (Disney) + Jiménez 2010 (separable SSS).                 |
| 21  | `cd::render_brdf::sheen_clearcoat`   | **Charlie sheen + Filament clearcoat** layered onto Standard PBR                   | Estevez 2017 + Filament docs.                                        |

## Week 4 — geometry + scale + streaming (engine strength)

| Day | Library                              | Ship                                                                              | SOTA reference                                                       |
|----:|--------------------------------------|-----------------------------------------------------------------------------------|----------------------------------------------------------------------|
| 22  | `cd::render_decal`                   | **Deferred screen-space decals** — depth-projected box volumes                    | Persson 2009 / Filion 2012 cluster-decal.                            |
| 23  | `cd::render_particles::gpu`          | **GPU particle system** — compute simulation + indirect draw                      | Riccio 2014 (AMD) GPU particles.                                     |
| 24  | `cd::render_velocity`                | **Velocity buffer pass** — per-pixel motion vectors for TAA / blur / ReSTIR       | Necessary scaffold; unlocks Days 9/11/6 to actually function.        |
| 25  | `cd::render_mesh_shader`             | **Mesh shader cluster culling** — meshlets + per-cluster cone+AABB cull           | Akenine-Möller 2018 §10.5.  `VK_EXT_mesh_shader` path.               |
| 26  | `cd::render_lod::virtual_geometry`   | **Nanite-style virtual geometry** — DAG hierarchy + GPU cluster pick              | Karis 2021 (UE5 Nanite SIGGRAPH talk). Scope-aware: stop at MVP.     |
| 27  | `cd::render_vt`                      | **Virtual textures** — feedback buffer + page-allocator + on-the-fly transcode    | Mittring 2008 (Crytek megatextures) + Hollander 2013.                |
| 28  | `cd::asset::streaming`               | **Mesh + texture streaming** — async I/O + priority queue + LOD demotion          | Sevenich 2017 (Cyberpunk talk).                                      |
| 29  | `cd::render_gi::ddgi`                | **DDGI (Dynamic Diffuse GI)** — irradiance probe grid + RT update                 | Majercik 2019. Scope-aware fallback if Day 6/7 ReSTIR GI insufficient. |
| 30  | `cd::render_post::oidn`              | **OpenImageDenoise integration** in `hello_path_trace`                            | Intel OIDN 2.x. Closes Faz 2 J.                                      |

---

## Sequencing rationale

- **Days 1-3** close the user-flagged shadow gap (mutual + spot/point
  shadow). These are the highest-priority items because the user
  explicitly named them.
- **Days 4-7** add SOTA GI primitives on top of the existing path
  tracer. Each is a separate library so any team can take one.
- **Days 8-14** wrap the post + tonemap stack — this is what makes
  the engine *look like* a modern AAA renderer in the framebuffer.
- **Days 15-21** add volumetrics + BRDF layers — this is what makes
  it look *real*.
- **Days 22-30** ship engine-strength features (geometry pipeline,
  streaming, particles, decals, DDGI, denoiser). These are where the
  engine starts competing with Filament / bgfx / UE on architecture
  rather than visuals.

## Library-discipline rule (recap)

Per `CLAUDE.md` §7:
- Every day's ship is a new `engine/<sub>/` directory with its own
  `include/cd/<sub>/`, `src/`, `tests/`, `CMakeLists.txt`.
- Cross-library dependency must hold the DAG: nothing in `render_*`
  may depend on `editor_*`, etc.
- Standalone gtest binary, own preset entry, own CMake target with
  `cd::<sub>` alias.

## Scope guardrails

- Each ship is *MVP-shaped*. Day 26 (Nanite) is a working DAG +
  cluster-pick pipeline, not a full UE5 feature parity.
- Each ship pins state with at least 4 gtest cases (1 happy path,
  3 edge cases).
- Each ship gets one ADR in `docs/ADR/`.
- No day's ship may break the `ctest` green count (currently
  59/59).

## Outside scope (deliberately deferred)

These are valuable but don't make the visuals/engine list:
- Editor UX polish (selection painters, gizmo themes, dockspace
  layouts) — not a library-grade ship.
- Audio + networking features — strong story today, focus elsewhere.
- D3D12 DXR step 4-5 — already in `V1_3_PLAN.md` and not visual.
- Linux/macOS/mobile hardware validation — needs reviewer-side
  hardware, not implementer effort.

## Marathon mode discipline

Per `feedback_long_autonomous_marathon`:
- Each ship gets one `v0.99.x` tag.
- `v1.x.0` major bumps remain user-only.
- Auto-mode permitted to chain days; clarifying questions only when
  the user explicitly redirects.

---

**This list is the input to the next marathon plan.** When the user
says "go", we follow it in order, treating each day as a marathon
chain link — research, design, implement, test, ADR, tag.
