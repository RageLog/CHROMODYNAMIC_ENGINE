# Marathon Plan — Next Sessions

Last updated: 2026-05-30, after `phase509-marathon-wave2-summary`.
Baseline: 143 / 143 tests PASS, ninja-debug clean, 110+ libraries.

This document is the **forward backlog** — what remains to be built on
top of the 2026-05-30 wave (phases 425-508). Items are grouped by
domain and prioritized within each group. Most items have an existing
ADR in `docs/ADR/` that should be the authoritative spec; this doc is
just the schedule + status lens.

---

## Tier 0 — Cross-cutting infrastructure (unblocks everything else)

### T0.1 Metal RHI backend — IMPLEMENTATION

* **Spec**: `docs/ADR/ADR-20260530-metal-backend.md`
* **Status**: ADR only. Zero implementation code committed.
* **Scope**: `engine/rhi/metal/` parallel to `rhi/vulkan` + `rhi/d3d12`.
  Needs `MetalDevice`, `MetalQueue`, `MetalCommandBuffer`,
  `MetalSwapchain`, `MetalPipeline`, `MetalBuffer`, `MetalTexture`,
  `MetalDescriptorSet` (Argument Buffer Tier 2), and a per-API
  smoke binary at `samples/rhi/hello_metal/`.
* **Prerequisites**:
  * macOS host or self-hosted CI runner (current CI is Win + Linux).
  * `SPIRV-Cross` Metal target wired into the shader pipeline (already
    in vcpkg manifest; needs Objective-C++ glue in the shader cooker).
  * MoltenVK NOT used — first-class Metal, per ADR.
* **Tests required**: golden-image diff against Vulkan reference on
  Sponza Atrium + 3 hello_engine fixed-camera frames.
* **Effort**: 3-5 weeks single-engineer.

### T0.2 Editor binary — IMPLEMENTATION

* **Spec**: `docs/ADR/ADR-20260530-editor-binary.md`
* **Status**: ADR only. The `editor/` library + `cd::editor_panel`
  shipped earlier; what's missing is the actual editor *binary* —
  scene tree + inspector + viewport + asset browser + console all
  hosted by a dockable shell.
* **Scope**:
  * `editor/app/` binary using `cd::ui` + `cd::ui_widgets` + the new
    `cd::ui::a11y` + `cd::ui::theme` V2 token system.
  * Hosts `cd::editor_panel` registrations.
  * Reuses `samples/engine/hello_engine` scene-render path via
    library hand-off (no duplicated GL/Vulkan code).
* **Prerequisites**: T2.1 dock-space widget (below) is a hard blocker.
* **Tests required**: layout-restore round-trip, panel registration
  table round-trip, golden screenshot of stock layout.
* **Effort**: 2-3 weeks (largely UI plumbing, no new rendering).

### T0.3 Jolt Physics integration — IMPLEMENTATION

* **Spec**: `docs/ADR/ADR-20260530-jolt-physics-integration.md`
* **Status**: ADR only.
* **Scope**: `engine/physics/jolt/` adapter library, vcpkg `jolt-physics`
  dependency, `cd::physics::World` + `cd::physics::Body` +
  `cd::physics::ConstraintSolver` thin wrappers + 5+ samples
  (`samples/physics/hello_*`).
* **Prerequisites**: vcpkg manifest update; ECS integration via
  existing `cd::scene` `RigidBodyComponent` (already stubbed).
* **Tests required**: deterministic step regression test, broad-phase
  fuzz test, character-controller smoke test.
* **Effort**: 2 weeks (Jolt has good C++ ergonomics; mostly glue).

---

## Tier 1 — Rendering / GPU

### T1.1 DDGI (Dynamic Diffuse Global Illumination)

* **Status**: Not started. No ADR yet.
* **Scope**: probe-grid GI with radiance + irradiance octahedral atlases,
  Vulkan RT shader for trace, blend with existing IBL bake.
* **Prerequisites**: existing RT path (TLAS + reflection) is already
  live as of Run-29; can reuse its pipeline layout.
* **Tests required**: golden-image diff on Sponza + cornell box.
* **Effort**: 3 weeks. Needs an ADR first
  (`docs/ADR/ADR-YYYYMMDD-ddgi.md`).

### T1.2 ReSTIR DI / GI

* **Status**: Not started.
* **Scope**: streaming reservoir sampling for direct + indirect light,
  temporal + spatial reuse passes, denoiser tie-in.
* **Prerequisites**: T1.1 DDGI as fallback (ReSTIR converges slowly
  in dark regions); GPU motion vectors (already shipped).
* **Effort**: 4-6 weeks. Research-grade — needs literature ADR pass.

### T1.3 Nanite-style virtual geometry

* **Status**: Not started. Mentioned in `docs/REALISM_ROADMAP.md`.
* **Scope**: cluster builder, LOD chain, software rasterizer for tiny
  triangles + hardware path for large, visibility buffer.
* **Prerequisites**: needs mesh-shader path on RHI (D3D12 + Vulkan
  KHR_mesh_shader); currently both backends are vertex-shader only.
* **Effort**: 8-12 weeks. Major research + impl pass.

### T1.4 Auto-exposure GPU compute reduction — wire to composite

* **Status**: CPU path live (phase454). GPU skeleton API only
  (phase461). `phase507-auto-exposure-wire` added a Setup helper but
  was SCOPED-DOWN — the compute pass is not yet recording into the
  composite framegraph.
* **Scope**: insert `cd::post::exposure::reduce_luminance` between
  HDR scene resolve and tonemap; pull `EV100` back through staging buf
  for next-frame composite.
* **Prerequisites**: SSBO ring buffer in composite (exists).
* **Effort**: 3-5 days.

### T1.5 Volumetric fog — composite integration

* **Status**: Library shipped (phase469) with HG phase function. Not
  yet sampled by the composite shader.
* **Scope**: wire the froxel volume to the composite tonemap pass,
  per-frame inscatter accumulation, integration with directional sun.
* **Effort**: 1 week.

### T1.6 SSR — quality pass

* **Status**: per-prim SSR gate live (phase447). Quality is
  hi-temporal-noise; no contact-hardening.
* **Scope**: hierarchical depth march, fade by reflection-vector cosine,
  combine with RT reflection fallback (already gated by surface_flag).
* **Effort**: 1 week.

### T1.7 Sponza golden-image CI gate

* **Status**: Sponza renders well after phases 425-456 fix-up wave
  but there is NO golden-image diff gate.
* **Scope**: `samples/engine/hello_engine` headless camera-script
  mode, capture 5 fixed angles, FLIP / SSIM diff in CI.
* **Prerequisites**: NVIDIA self-hosted CI runner (exists; see
  `docs/CI_SELF_HOSTED.md`).
* **Effort**: 3 days for plumbing, 2 days for golden-set curation.

---

## Tier 2 — UI / Editor widget library (Phase 4 of ADR-ui-widget-library)

### T2.1 Dock-space widget

* **Status**: Not started. Hard blocker for T0.2 editor binary.
* **Scope**: split + tab + drag-out + drag-in, layout serialize/restore,
  ratio-driven resize, ImGui dock-space semantic parity.
* **Effort**: 2-3 weeks. Single biggest unbuilt UI primitive.

### T2.2 Color picker widget

* **Status**: Not started.
* **Scope**: HSV wheel + RGB sliders + hex input + alpha bar + palette
  history, OKLCh option for perceptual editing.
* **Prerequisites**: `cd::ui_widgets::Slider` (shipped), float-array
  binding (shipped).
* **Effort**: 1 week.

### T2.3 Curve editor widget

* **Status**: Not started.
* **Scope**: bezier-handle curve editor for animation curves, multi-
  curve overlay, tangent-mode switch (auto/linear/stepped), eval API
  exposed to `cd::ui_animation::Tweener`.
* **Effort**: 2 weeks.

### T2.4 FreeType + HarfBuzz font upgrade

* **Status**: stb_truetype shipped (phase443). FT + HB upgrade is
  Phase 4 of the UI ADR.
* **Scope**: replace stb backend with FreeType for outlines, HarfBuzz
  for shaping (BiDi-aware), MSDF atlas generation, variable-font axis
  exposure.
* **Prerequisites**: vcpkg manifest update (freetype + harfbuzz +
  icu-bidi or similar).
* **Effort**: 2 weeks.

### T2.5 Gesture recognizers

* **Status**: `cd::ui_input` has HitTester + FocusManager only. No
  gestures.
* **Scope**: double-click, long-press, drag, pinch, swipe — wired
  into the existing `EventBus`.
* **Effort**: 1 week.

### T2.6 Constraint solver layout (Cassowary)

* **Status**: Flex (Yoga semantics) is shipped. Phase 5 per the ADR.
* **Scope**: Cassowary / Auto-Layout style constraint engine for
  cases Flex can't express (right-align + min-width chain).
* **Effort**: 2-3 weeks. Lower priority — Flex covers 95% of
  realistic editor layouts.

### T2.7 WebGPU backend for `cd::ui_renderer_rhi`

* **Status**: Not started. Phase 5.
* **Effort**: 2 weeks after Dawn vcpkg integration.

---

## Tier 3 — Samples + integration

### T3.1 Phase G3 hello_world visual variant

* **Status**: `samples/game/hello_world` shipped phase504 in console
  form (text-based 3rd-person walkthrough). Visual variant pending.
* **Scope**: replace console output with real Vulkan rendering — use
  `samples/engine/hello_engine`'s scene-render path, swap player
  control to `cd::game::camera::Cinemachine` + `cd::game::trigger`,
  spawn PFX from `cd::game::particles_event` via
  `cd::post::particles` (which exists).
* **Prerequisites**: none — all libraries shipped, just integration.
* **Effort**: 1 week. Highest-payoff "make it visual" item in the
  gameplay tier.

### T3.2 Editor scene tree + inspector sample

* **Status**: Blocked on T0.2 + T2.1.

### T3.3 Physics demo bundle (5+ samples)

* **Status**: Blocked on T0.3 Jolt integration.

### T3.4 Multiplayer netcode demo

* **Status**: `cd::net::reliable / qos / reconciler` shipped phase468.
  No sample yet exercises them.
* **Scope**: `samples/net/hello_netcode/` — 2-client local-loopback
  movement reconciliation demo.
* **Effort**: 4 days.

### T3.5 Lua hot-reload demo

* **Status**: `cd::script` Lua bindings shipped phase467;
  `cd::game::asset_hot_reload` shipped phase502. No sample ties them.
* **Scope**: `samples/script/hello_hot_reload/` — edit a Lua entity-
  behavior script while running, see it re-apply within the throttle
  window.
* **Effort**: 3 days.

---

## Tier 4 — Mobile + platform

### T4.1 Android platform support

* **Status**: Not started.
* **Scope**: NDK build preset, GLFW -> SDL2 swap on Android, Android
  asset-manager I/O backend, Vulkan via VK_KHR_android_surface.
* **Effort**: 3-4 weeks. Needs ADR
  (`docs/ADR/ADR-YYYYMMDD-android-platform.md`).

### T4.2 iOS platform support

* **Status**: Not started. Hard-dependent on T0.1 Metal.
* **Effort**: 3-4 weeks after T0.1.

### T4.3 Web (WebGPU + Emscripten)

* **Status**: Not started. Hard-dependent on T2.7 WebGPU RHI.
* **Effort**: 4-6 weeks.

---

## Tier 5 — Audit / cleanup follow-ups

### T5.1 RHI parity audit refresh

* **Status**: DONE (2026-05-31).
  * `docs/D3D12_PARITY_AUDIT.md` refreshed post-phase466: updated baseline
    to Phase 466 + current, confirmed 127/127 tests PASS on both Vulkan + D3D12.
  * Matrix expanded: 74 virtuals (IDevice + ICommandBuffer) with per-method status.
  * Phase 466 impact documented: 5 kNotImpl → shipped items, bringing D3D12 from
    9 down to 2 remaining stubs (shared with Vulkan: RT pipeline + dispatch_rays).
  * Next-priority gaps documented: mesh shaders, work graphs, sampler feedback,
    GPU upload heap defragmentation (all low-priority, Phase 120+ vision).
  * Summary table + feature matrix added for quick reference.
* **Effort**: 1 day (completed).

### T5.2 Gameplay-tier README sweep

* **Status**: DONE (2026-05-31).
  * `engine/game/README.md` created with 1-paragraph rationale + table of
    all 13 cd::game::* libraries (purpose, path, deps) + Mermaid DAG +
    samples section with 4 examples.
  * `docs/LIBRARIES.md` refreshed: Game tier (13 libs) inserted between
    World and UI, library count updated to 67 public + 68 total.
* **Effort**: 0.5 days (completed).

### T5.3 Volumetric-fog + auto-exposure-GPU ADR backfill

* **Status**: DONE (2026-05-31).
* Two ADRs backfilled for phases shipped earlier:
  * `docs/ADR/ADR-20260531-volumetric-fog.md` (phase469, froxel volume + HG phase).
  * `docs/ADR/ADR-20260531-auto-exposure-gpu.md` (phase461/507, Reinhard log-avg + GPU reduction).
* **Effort**: 0.5 days each (completed).

---

## Suggested ordering (next 1-3 marathons)

1. **Marathon N+1 (visual / sample close-out)**: T3.1 visual
   hello_world, T1.4 auto-exposure wire, T1.5 vol-fog wire, T1.7
   Sponza golden-image gate, T5.1 + T5.3 audit/ADR backfill.
   *(All within existing infra; no new vendored deps.)*
2. **Marathon N+2 (editor)**: T2.1 dock-space, T2.2 color picker,
   T2.3 curve editor, T0.2 editor binary. *(Pure UI + editor; no GPU
   backend changes.)*
3. **Marathon N+3 (Metal foundation)**: T0.1 Metal RHI MVP +
   Sponza render on Metal. *(Requires macOS CI.)*
4. **Long-running parallel track**: T0.3 Jolt + T3.3 physics demos
   can be picked up by a second engineer in parallel any time.

---

## Reference: ADRs landed 2026-05-30 (resume wave)

* `docs/ADR/ADR-20260530-editor-binary.md`
* `docs/ADR/ADR-20260530-metal-backend.md`
* `docs/ADR/ADR-20260530-jolt-physics-integration.md`
* `docs/ADR/ADR-20260530-gameplay-library-family.md`
* `docs/ADR/ADR-20260530-generic-gltf-scene-loading.md`

Each of T0.1, T0.2, T0.3 above maps 1-to-1 onto one of these ADRs;
the ADR is the spec, this doc is the schedule.
