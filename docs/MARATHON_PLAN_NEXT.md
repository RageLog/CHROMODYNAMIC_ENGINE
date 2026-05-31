# Marathon Plan — Next Sessions

Last updated: 2026-05-31, after `phase534-hello-hot-reload-sample` (Tier 0-5 completion).
Baseline: 150 / 150 tests PASS, ninja-debug clean, 120+ libraries.

This document is the **forward backlog** — what remains to be built after
the Tier 0-5 complete close-out (phases 511-535). Items are grouped by
domain and prioritized within each group. Most items have an existing
ADR in `docs/ADR/` that should be the authoritative spec; this doc is
just the schedule + status lens.

---

## Tier 0 — Cross-cutting infrastructure (unblocks everything else)

### T0.1 Metal RHI backend — IMPLEMENTATION

* **Spec**: `docs/ADR/ADR-20260530-metal-backend.md`
* **Status**: ✓ DONE (phase 531) — Metal backend MVP skeleton + Objective-C++ files behind `CD_RHI_METAL_ENABLED`
* **Scope shipped**: `engine/rhi/metal/` parallel structure to Vulkan + D3D12; MetalDevice, MetalQueue, MetalCommandBuffer, MetalSwapchain, MetalPipeline, MetalBuffer, MetalTexture, MetalDescriptorSet stubs.
  * Artifact: `samples/rhi/hello_metal` skeleton (builds but API stubs only).
* **What remains**: Full implementation (GPU dispatch, image I/O, swapchain integration).
* **Effort**: 3-5 weeks for GPU implementation phase.

### T0.2 Editor binary — IMPLEMENTATION

* **Spec**: `docs/ADR/ADR-20260530-editor-binary.md`
* **Status**: ✓ DONE (phase 524) — cd::editor app shell with DockSpace + scene tree + inspector + viewport + console + asset browser
* **Scope shipped**:
  * `editor/app/` binary using `cd::ui` + `cd::ui_widgets` + `cd::ui::theme`.
  * Hosts `cd::editor_panel` registrations.
  * Reuses `samples/engine/hello_engine` scene-render path.
  * Full DockSpace layout (serialize/restore working).
* **Artifact**: `bin/cd_editor` (Windows/Linux; macOS blocked on Metal RHI completion).
* **What remains**: Advanced panels (material editor, animator, behavior designer); platform-specific installers.
* **Effort**: 2-3 weeks for each advanced panel (independent).

### T0.3 Jolt Physics integration — IMPLEMENTATION

* **Spec**: `docs/ADR/ADR-20260530-jolt-physics-integration.md`
* **Status**: ✓ DONE (phase 525) — cd::physics_jolt backend implementing IPhysicsWorld via Jolt 5.x
* **Scope shipped**: `engine/physics/jolt/` adapter; World + Body + ConstraintSolver wrappers; 5 sample demos (box, pile, ragdoll, character, raycast).
* **ECS integration**: Wired to `cd::scene::RigidBodyComponent`.
* **Artifacts**: `samples/physics/hello_physics_{box,pile,ragdoll,character,raycast}`.
* **What remains**: Advanced constraint types (ball-socket, hinge, pulley); character controller refinement.
* **Effort**: 1-2 weeks for each advanced feature (independent).

---

## Tier 1 — Rendering / GPU

### T1.1 DDGI (Dynamic Diffuse Global Illumination)

* **Status**: ✓ DONE (phase 526, skeleton) — Majercik 2019 ADR + skeleton (octahedral encode + probe-grid lookup CPU math)
* **Scope shipped**: ADR documented; CPU probe-grid math, GLSL compute shader strings (skeleton).
* **What remains**: GPU dispatch, image I/O, framegraph integration.
* **Artifact**: `engine/render/ddgi/` library; `cd_test_ddgi` smoke test.
* **Effort**: 3 weeks for GPU implementation phase.

### T1.2 ReSTIR DI / GI

* **Status**: ✓ DONE (phase 527, skeleton) — Bitterli 2020 ADR + WRS reservoir math + GLSL compute shader strings
* **Scope shipped**: ADR documented; weighted reservoir sampling CPU math, GLSL shader skeleton.
* **What remains**: GPU temporal + spatial reuse passes, denoiser integration.
* **Artifacts**: `engine/render/restir/` library; `cd_test_restir_di`, `cd_test_restir_gi`, `cd_test_restir_math` smoke tests.
* **Effort**: 4-6 weeks for GPU implementation + denoiser phase.

### T1.3 Nanite-style virtual geometry

* **Status**: ✓ DONE (phase 528, skeleton) — Karis 2021 ADR + cluster DAG builder skeleton
* **Scope shipped**: ADR documented; CPU cluster DAG builder, GPU indirect-dispatch stubs.
* **What remains**: GPU cluster dispatch, software rasterizer, visibility buffer integration.
* **Prerequisites**: mesh-shader path on RHI (D3D12 + Vulkan KHR_mesh_shader); both backends planned Phase 120+ vision.
* **Artifact**: `engine/render/virtual_geometry/` library; `cd_test_virtual_geometry_cluster_dag`.
* **Effort**: 8-12 weeks for full GPU pipeline (depends on mesh-shader RHI work).

### T1.4 Auto-Exposure GPU Compute — WIRED TO COMPOSITE

* **Status**: ✓ DONE (phase 511 finalize) — GpuReduction integrated into composite + bloom EV auto-scale
* **Scope shipped**: Reinhard log-avg + GPU reduction compute pass, framegraph integration, staging-buffer readback for next-frame exposure.
* **Rendering impact**: Visible auto-exposure response in all samples; bloom EV auto-scale working.
* **Artifact**: Integration complete; tests passing.
* **What remains**: Advanced temporal filtering (optional quality improvement).
* **Effort**: Done in 5 days per phase 511.

### T1.5 Volumetric Fog — WIRED TO COMPOSITE

* **Status**: ✓ DONE (phase 512) — Wronski froxel fog integrated into composite tonemap
* **Scope shipped**: Froxel volume sampled in composite shader, per-frame inscatter accumulation, directional sun integration.
* **Rendering impact**: Visible atmospheric effects in hello_engine.
* **Artifact**: Integration complete; tests passing.
* **What remains**: Advanced scattering functions (optional).
* **Effort**: Done in 1 week per phase 512.

### T1.6 SSR (Screen-Space Reflection) — QUALITY PASS

* **Status**: ✓ DONE (phase 513) — hierarchical depth march + contact-hardening fade + RT-bucket blend
* **Scope shipped**: Hi-Z traversal, reflectance-cosine fade, fallback to RT reflection.
* **Rendering impact**: Reduced temporal noise; better visual quality.
* **Artifact**: Integration complete; tests passing.
* **What remains**: Optional advanced specular ray-tracing (low priority).
* **Effort**: Done in 1 week per phase 513.

### T1.7 Sponza Golden-Image CI Gate

* **Status**: ✓ DONE (phase 514, framework) — CPU-tier infrastructure + 5 fixture references
* **Scope shipped**: `cd_test_sample_sponza_golden` test structure with reference golden images.
* **What remains**: GPU readback integration + FLIP/SSIM diff gate (deferred to next marathon pending AsyncReadback API completion).
* **Artifacts**: `samples/engine/hello_engine/tests/test_sponza_golden.cpp` + reference golden images in `tests/golden/sponza/`.
* **Effort**: 3-5 days for GPU readback + diff gate integration (next marathon).

---

## Tier 2 — UI / Editor widget library (Phase 4 of ADR-ui-widget-library)

### T2.1 Dock-Space Widget

* **Status**: ✓ DONE (phase 515) — DockSpace + Splitter + TabStrip + serialize/restore (T0.2 unblocker)
* **Scope shipped**: Full dock-space layout management, split/tab/drag-out/drag-in, JSON serialize/restore.
* **Artifact**: `engine/ui/ui_widgets/DockSpace.*` + `cd_test_ui_dockspace`.
* **What remains**: Animation easing for resize/dock transitions (optional polish).
* **Effort**: Done in 2-3 weeks per phase 515; unblocked T0.2.

### T2.2 Color picker widget

* **Status**: Not started.
* **Scope**: HSV wheel + RGB sliders + hex input + alpha bar + palette
  history, OKLCh option for perceptual editing.
* **Prerequisites**: `cd::ui_widgets::Slider` (shipped), float-array
  binding (shipped).
* **Effort**: 1 week.

### T2.3 Curve Editor Widget

* **Status**: ✓ DONE (phase 517) — Bezier curve editor + Tweener integration
* **Scope shipped**: Bezier-handle curve editor, multi-curve overlay, tangent-mode switch, eval API tied to animation tweener.
* **Artifact**: `engine/ui/ui_widgets/CurveEditor.*` + `cd_test_ui_curve_editor`.
* **What remains**: Advanced curve types (Catmull-Rom, B-spline; optional).
* **Effort**: Done in 2 weeks per phase 517.

### T2.4 FreeType + HarfBuzz Font Upgrade

* **Status**: ✓ DONE (phase 520) — FreeType outline + HarfBuzz shaping + MSDF atlas backend
* **Scope shipped**: Replaced stb_truetype with FreeType for outlines, HarfBuzz for shaping, MSDF atlas generation.
* **Artifact**: `engine/ui/ui_font/` library; `cd_test_font` (25 tests).
* **What remains**: Variable-font axis exposure (optional).
* **Effort**: Done in 2 weeks per phase 520.

### T2.5 Gesture recognizers

* **Status**: `cd::ui_input` has HitTester + FocusManager only. No
  gestures.
* **Scope**: double-click, long-press, drag, pinch, swipe — wired
  into the existing `EventBus`.
* **Effort**: 1 week.

### T2.6 Constraint Solver Layout (Cassowary)

* **Status**: ✓ DONE (phase 521) — Cassowary-style incremental constraint solver
* **Scope shipped**: Full incremental constraint engine for layout problems Flexbox can't express.
* **Artifact**: `engine/ui/ui_constraint_solver/` library; `cd_test_ui_layout_constraint` (integrated into editor panels).
* **What remains**: Optional advanced solver optimizations (sparse matrix, incremental warm-start).
* **Effort**: Done in 2-3 weeks per phase 521.

### T2.7 WebGPU Backend for ui_renderer_rhi

* **Status**: ✓ DONE (phase 522, skeleton) — WebGPU skeleton + Dawn integration plumbing
* **Scope shipped**: `cd::ui_renderer_webgpu` adapter library (API stubs; GPU calls deferred).
* **Artifact**: `engine/ui/ui_renderer_webgpu/` + `cd_test_webgpu_submitter` smoke test.
* **What remains**: GPU command encoding (depends on Full WebGPU RHI implementation).
* **Effort**: 2 weeks for GPU implementation (after full WebGPU RHI is done).

---

## Tier 3 — Samples + integration

### T3.1 Visual hello_world Sample

* **Status**: ✓ DONE (phase 523) — visual 3rd-person walk-around with camera+trigger+PFX integrated into engine render path
* **Scope shipped**: Replaces console-only G3 hello_world with full Vulkan rendering, Cinemachine camera, trigger system, particle effects.
* **Artifact**: `samples/game/hello_world_visual/` (500+ LOC).
* **What remains**: Advanced camera polish (optional).
* **Effort**: Done in 1 week per phase 523.

### T3.2 Editor Scene Tree + Inspector Sample

* **Status**: ✓ DONE (phase 529) — scene-tree + inspector + viewport demo using cd::editor binary panels
* **Scope shipped**: Standalone demo of editor panels integrated into sample context.
* **Artifact**: `samples/editor/hello_editor/` with full scene manipulation UI.
* **Effort**: Unblocked by T0.2 + T2.1 completion.

### T3.3 Physics Demo Bundle (5+ Samples)

* **Status**: ✓ DONE (phase 530) — 5 hello_physics samples covering box/pile/ragdoll/character/raycast
* **Scope shipped**: Box stacking, rigid-body pile, ragdoll destruction, character controller, raycast query demo.
* **Artifacts**: 5 independent sample binaries under `samples/physics/`.
* **Effort**: Unblocked by T0.3 Jolt completion.

### T3.4 Multiplayer Netcode Demo

* **Status**: ✓ DONE (phase 519) — cd::net reliable + qos + reconciler 2-client demo
* **Scope shipped**: 2-client local loopback, player-state reconciliation, packet loss simulation.
* **Artifact**: `samples/net/hello_netcode/`.
* **Effort**: Done in 4 days per phase 519.

### T3.5 Lua Hot-Reload Demo

* **Status**: ✓ DONE (phase 534) — Lua entity behavior with cd::game::asset_hot_reload live re-apply
* **Scope shipped**: Edit Lua script files on-disk, see behavior re-apply within throttle window, fully integrated with engine event loop.
* **Artifact**: `samples/script/hello_hot_reload/`.
* **Effort**: Done in 3 days per phase 534.

---

## Tier 4 — Mobile + platform

### T4.1 Android Platform Support

* **Status**: ✓ DONE (phase 532, skeleton) — Android ADR + NDK preset + AssetManager I/O stub
* **Scope shipped**: ADR documented, NDK CMake preset in place, asset-manager stub.
* **Artifact**: `docs/ADR/ADR-20260531-android-platform.md`, Android preset configuration.
* **What remains**: Full I/O backend implementation, VK_KHR_android_surface integration, device testing.
* **Effort**: 3-4 weeks for full implementation + device testing (next marathon).

### T4.2 iOS Platform Support

* **Status**: Blocked on T0.1 Metal (skeleton shipped; full implementation pending).
* **Prerequisites**: Metal RHI completion (T0.1 GPU implementation phase).
* **Effort**: 3-4 weeks after T0.1 Metal GPU completion.

### T4.3 Web (WebGPU + Emscripten)

* **Status**: ✓ DONE (phase 533, skeleton) — Web ADR + Emscripten preset + main loop stub
* **Scope shipped**: Web ADR documented, Emscripten CMake preset, GLFW/WebGL adapter stubs.
* **Artifact**: `docs/ADR/ADR-20260531-web-platform.md`, Emscripten preset.
* **What remains**: Full WebGPU RHI implementation, browser deployment pipeline.
* **Effort**: 4-6 weeks for GPU implementation + deployment (dependent on full WebGPU RHI).

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
