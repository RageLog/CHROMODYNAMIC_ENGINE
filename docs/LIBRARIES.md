# Library catalogue

CHROMODYNAMIC ships **55 libraries**. Each one builds and tests in
isolation; the table below lists the target name, alias, public namespace,
include root, and primary responsibility.

The libraries are organised into seven groups. Cross-group dependencies
flow strictly downward (foundation → asset → render → world → ui →
runtime, with `script` and `imgdiff` as cross-cutting auxiliaries);
circular references are a build error.

> The cross-tier ordering is enforced at `engine/CMakeLists.txt` —
> `add_subdirectory()` calls are bottom-up, and `cd_add_library()` in
> `cmake/CDProject.cmake` validates the dependency DAG at configure time.

---

## Foundation tier (15)

The "always-on" platform abstraction layer. Anything above this tier
may depend on these; nothing here depends on anything above.

| Target        | Alias            | Namespace               | Include root           | Responsibility |
|---------------|------------------|-------------------------|------------------------|----------------|
| `cd_core`     | `cd::core`       | `cd::core`              | `cd/core/`             | `Result`, `ErrorCode`, `span`, primitive utilities. Zero deps. |
| `cd_mem`      | `cd::mem`        | `cd::mem`               | `cd/mem/`              | Slot-map handles, pool / monotonic / stack allocators. |
| `cd_concurrency` | `cd::concurrency` | `cd::concurrency`  | `cd/concurrency/`      | Thread pool, work-stealing queues, hazard pointers, deterministic executor. |
| `cd_time`     | `cd::time`       | `cd::time`              | `cd/time/`             | Monotonic & wall clocks, frame timing, sleep utilities. |
| `cd_io`       | `cd::io`         | `cd::io`                | `cd/io/`               | File I/O, byte streams, memory-mapped readers. |
| `cd_log`      | `cd::log`        | `cd::log`               | `cd/log/`              | Structured logging with sink abstraction (stdout, file, ImGui console). |
| `cd_events`   | `cd::events`     | `cd::events`            | `cd/events/`           | Strongly typed signal/slot, event bus. |
| `cd_diag`     | `cd::diag`       | `cd::diag`              | `cd/diag/`             | Assert, panic, stack traces. |
| `cd_profile`  | `cd::profile`    | `cd::profile`           | `cd/profile/`          | Sample ring, stats aggregator, CSV + Chrome Tracing sinks. |
| `cd_platform` | `cd::platform`   | `cd::platform`          | `cd/platform/`         | Windows, message pump, OS events, keyboard / mouse. |
| `cd_plugin`   | `cd::plugin`     | `cd::plugin`            | `cd/plugin/`           | Dynamic library load / unload, interface discovery. |
| `cd_config`   | `cd::config`     | `cd::config`            | `cd/config/`           | TOML-like configuration values, runtime overrides. |
| `cd_math`     | `cd::math`       | `cd::math`              | `cd/math/`             | Vec / Mat / Quat / AABB / Plane / Ray, SIMD-friendly. |
| `cd_vfs`      | `cd::vfs`        | `cd::vfs`               | `cd/vfs/`              | Virtual file system, mount points, package layers. |
| `cd_bench`    | `cd::bench`      | `cd::bench`             | `cd/bench/`            | Header-only microbench runner (warmup, p50/p99, CSV). |

> A `cd::serialization` library was scoped early but never materialised —
> binary-serialization needs are met inline by individual asset
> libraries (`cd::asset_cdmesh`, `cd::asset_cdtex`) and by
> `cd::asset_pak`. Older docs that named it can be ignored.

---

## Asset tier (10)

Static-data import/export. Tier-internal dependencies are explicit
(e.g. `asset_cdtex` needs `asset_image` for the cook path); runtime
asset reading depends on no other asset library.

| Target              | Alias                  | Reads / writes | Format invariants |
|---------------------|------------------------|----------------|-------------------|
| `cd_asset`          | `cd::asset`            | (interface)    | `AssetId`, `AssetRegistry`. |
| `cd_asset_image`    | `cd::asset_image`      | PNG / JPG / HDR / `generate_mips` | stb_image header-only, 2x2 box-filter mip chain. |
| `cd_asset_obj`      | `cd::asset_obj`        | OBJ            | Hand-rolled parser, vertex/index/normal/uv. |
| `cd_asset_gltf`     | `cd::asset_gltf`       | glTF 2.0       | tinygltf vendor, per-mesh AABB pre-compute. |
| `cd_asset_cdmesh`   | `cd::asset_cdmesh`     | `.cdmesh` (cooked) | Engine-internal binary, mmap-ready. |
| `cd_asset_cdtex`    | `cd::asset_cdtex`      | `.cdtex` v1/v2 | BC7 blocks + optional mip chain. |
| `cd_asset_ktx2`     | `cd::asset_ktx2`       | KTX2           | Khronos KTX2 v2; single-2D-texture short-list (RGBA8, BGRA8, BC7). |
| `cd_asset_wav`      | `cd::asset_wav`        | WAV (RIFF)     | PCM (i8/i16/i24/i32) + IEEE float; chunk-walking, skip-unknown. |
| `cd_asset_json`     | `cd::asset_json`       | JSON           | Hand-rolled subset parser + AST + serialize (compact & pretty). |
| `cd_asset_pak`      | `cd::asset_pak`        | `.cdpak` bundle | Header-only single-file asset bundle (directory + payload). |

---

## Render tier (16)

Vulkan-first rendering stack. RHI is abstract; `rhi_vulkan` is the
mature backend, `rhi_d3d12` and `rhi_metal` are platform-gated
implementations (Vulkan-only consumers stay portable via
`cd::rhi_native` selecting at runtime).

| Target           | Alias               | Responsibility |
|------------------|---------------------|----------------|
| `cd_rhi`         | `cd::rhi`           | Abstract device, swapchain, command buffer, pipeline, descriptor set interfaces. |
| `cd_rhi_vulkan`  | `cd::rhi_vulkan`    | Vulkan 1.3 + dynamic rendering, volk loader, VMA allocator. `get_native()` for opt-in raw-handle interop. |
| `cd_rhi_d3d12`   | `cd::rhi_d3d12`     | Direct3D 12 backend (Windows). Boot + idle fence shipped in v0.27.0; buffer/texture/swapchain in Phase 13.C. |
| `cd_rhi_metal`   | `cd::rhi_metal`     | Metal backend (macOS / iOS). Public factory always linkable; concrete code gated by `__APPLE__`. |
| `cd_rhi_native`  | `cd::rhi_native`    | Cross-platform dispatcher: `make_native_device()` selects the best available backend (Vulkan / D3D12 / Metal). |
| `cd_shader`      | `cd::shader`        | GLSL → SPIR-V compile (glslang), reflection, cache. |
| `cd_material`    | `cd::material`      | Pipeline state object, descriptor binding model. |
| `cd_render`      | `cd::render`        | Renderer composition; per-view, per-frame submission. |
| `cd_framegraph`  | `cd::framegraph`    | Pass declaration, resource lifetimes, barrier scheduling. |
| `cd_camera`      | `cd::camera`        | View / projection, Gribb-Hartmann frustum extraction, AABB visibility tests. |
| `cd_imgui_backend`| `cd::imgui_backend`| Dear ImGui docking + Vulkan dynamic-rendering backend, profile HUD. |
| `cd_async_submit`| `cd::render::AsyncSubmit` | Header-only "one frame deep" render-thread primitive; backend-agnostic. |
| `cd_cluster`     | `cd::render::cluster` | CPU baseline for clustered Forward+ light culling (config + reference output). |
| `cd_cluster_gpu` | `cd::cluster_gpu`   | Vulkan compute pipeline binding `cluster_assign.comp` to the cd::cluster reference. |
| `cd_cluster_pbr` | `cd::cluster_pbr`   | GLSL helper + C++ binding-layout describing the PBR fragment-shader consumer of cluster buffers. |
| `cd_ibl`         | `cd::ibl`           | Image-based lighting CPU bakes (irradiance / radiance / BRDF LUT). GPU port deferred. |
| `cd_volumetric`  | `cd::render::volumetric` | CPU baseline for volumetric fog / atmospheric scattering. GPU froxel grid deferred. |

> The render tier is listed alphabetically by intent (interface →
> backends → consumers). The actual count above is 17 lines but
> `cd_imgui_backend` is the ImGui integration, not a render-core
> library — keep it in render-tier topology, omit it from the
> "core renderer" subset.

---

## World tier (7)

Simulation. Each world subsystem is independently driven by `cd::runtime`
in production; samples can pull just the pieces they need.

| Target       | Alias           | Responsibility |
|--------------|-----------------|----------------|
| `cd_ecs`     | `cd::ecs`       | Archetype storage, query system, `Scheduler` with declared reads/writes. |
| `cd_scene`   | `cd::scene`     | Scene graph, parent/child transforms, sibling iteration. |
| `cd_physics` | `cd::physics`   | Broad-phase / narrow-phase primitives (Jolt integration deferred). |
| `cd_anim`    | `cd::anim`      | Skeletal hierarchy, sampled tracks (curve API — playback v2). |
| `cd_audio`   | `cd::audio`     | Mixer, voice graph, WASAPI output backend (Windows). |
| `cd_input`   | `cd::input`     | Input mapping, action sets, device polling. |
| `cd_net`     | `cd::net`       | Reliable / unreliable channels, snapshot replication (v2). |

---

## Game tier (13)

Gameplay-facing libraries: behavior authoring, narrative, persistence,
interaction, and developer ergonomics. Each library is independently
buildable; they depend on the world tier but never reverse. Per
[ADR-20260530-gameplay-library-family](ADR/ADR-20260530-gameplay-library-family.md).

| Target                  | Alias                      | Responsibility |
|-------------------------|----------------------------|----------------|
| `cd_game_save`          | `cd::game::save`           | Slot-based persistence with atomic on-disk writes (temp + rename). |
| `cd_game_settings`      | `cd::game::settings`       | Persistent preferences store with live change broadcast. |
| `cd_game_fsm`           | `cd::game::fsm`            | Hierarchical finite state machine (Harel statecharts, UML regions). |
| `cd_game_ai_bt`         | `cd::game::ai_bt`          | Behavior tree (Sequence / Selector / Parallel + Decorators + Blackboard). |
| `cd_game_anim_graph`    | `cd::game::anim_graph`     | Animation graph with 1D/2D blend trees over `SkinnedClip` / `Skeleton`. |
| `cd_game_query`         | `cd::game::query`          | Gameplay spatial queries (raycast, sphere, box, frustum). |
| `cd_game_trigger`       | `cd::game::trigger`        | Trigger volumes (AABB/Sphere, on_enter/on_stay/on_exit dispatch). |
| `cd_game_camera`        | `cd::game::camera`         | Cinemachine-style virtual camera stack with priority blending + damping. |
| `cd_game_dialogue`      | `cd::game::dialogue`       | Branching dialogue VM (Ink/Yarn Spinner-style graph + predicate gates). |
| `cd_game_quest`         | `cd::game::quest`          | Quest log tracker (4-bucket partitioning, auto-complete/fail, serialization). |
| `cd_game_l10n`          | `cd::game::l10n`           | Localization runtime (CLDR plural rules, RTL helper, locale fallback). |
| `cd_game_particles_event` | `cd::game::particles_event` | Recipe-driven gameplay particle dispatcher (named bursts + callbacks). |
| `cd_game_asset_hot_reload` | `cd::game::asset_hot_reload` | Editor-mode asset hot-reload bus with categorized subscribers. |

---

## UI tier (3)

| Target          | Alias              | Responsibility |
|-----------------|--------------------|----------------|
| `cd_ui`         | `cd::ui`           | Layout engine, widget primitives (per ADR-009). |
| `cd_editor_ui`  | `cd::editor_ui`    | Editor-specific widgets atop `cd::ui` + ImGui. |
| `cd_editor`     | `cd::editor`       | Top-level editor composition (scene tree, inspector, asset browser, `EditHistory` undo/redo). |

---

## Runtime tier (1)

| Target       | Alias        | Responsibility |
|--------------|--------------|----------------|
| `cd_runtime` | `cd::runtime`| Subsystem startup / shutdown / tick orchestration. The "engine main()" library — consumed by `samples/hello_runtime`, by every full-engine sample, and by application binaries shipping CHROMODYNAMIC. |

---

## Cross-cutting (2)

Auxiliary libraries that don't fit a tier but are part of the install
set. They are intentionally narrow-scope and depend only on
`cd::core`.

| Target       | Alias        | Responsibility |
|--------------|--------------|----------------|
| `cd_imgdiff` | `cd::imgdiff`| Header-only golden-image pixel diff (FLIP / per-pixel epsilon). Used by every render-side test. |
| `cd_script`  | `cd::script` | Embedded Lua 5.4 binding tier (vendored Lua, internal static lib). |

---

## Library count (FAZ 1–10 Close-Out: 193 Libraries)

CHROMODYNAMIC declares **193 modular `cd_<lib>` targets** across 7 strictly ordered DAG tiers (`foundation`, `asset`, `render`, `world`, `game`, `ui`, `runtime`), including:
- **Foundation & Profiling (20)**: `cd::core`, `cd::math`, `cd::mem`, `cd::concurrency`, `cd::time`, `cd::frame_timing`, `cd::io`, `cd::log`, `cd::events`, `cd::diag` (with `CrashReporter` minidump + stack symbolizer), `cd::profile`, `cd::profile_cpu_marker_overlay`, `cd::profile_frame_graph_timeline`, `cd::profile_gpu_marker`, `cd::platform` (Win32, X11, Cocoa, `AndroidSurfaceAdapter`, `IosMetalLayerAdapter`), `cd::plugin`, `cd::config`, `cd::vfs`, `cd::bench`, `cd::foundation_utils`.
- **Asset & Authoring (15)**: `cd::asset`, `cd::asset_image`, `cd::asset_obj`, `cd::asset_gltf`, `cd::asset_cdmesh`, `cd::asset_cdtex`, `cd::asset_ktx2`, `cd::asset_wav`, `cd::asset_json`, `cd::asset_pak`, `cd::asset_streaming`, `cd::asset_texture_compress`, `cd::asset_material_authoring`, `cd::asset_shader_cache`, `cd::asset_vfx_authoring`, `cd::asset_streamer_pool`, `cd::asset_validator`, `cd::asset_texture_streamer`, `cd::asset_scene_streamer`, `cd::asset_audio_streamer`.
- **Render & RHI (40)**: `cd::rhi`, `cd::rhi_vulkan`, `cd::rhi_d3d12`, `cd::rhi_metal`, `cd::rhi_opengl`, `cd::rhi_webgpu`, `cd::rhi_native`, `cd::gluon` (27 shader modules), `cd::shader`, `cd::spirv_cross_glue`, `cd::material`, `cd::brdf` (`ltc`, `sheen_clearcoat`, `sss`), `cd::framegraph`, `cd::scene_renderer` (`SurfaceAndPostFxPipeline` 19-pass orchestrator), `cd::ddgi`, `cd::restir_di`, `cd::restir_gi`, `cd::nrc`, `cd::virtual_geometry`, `cd::virtual_textures`, `cd::atmosphere`, `cd::volumetric`, `cd::light_shafts`, `cd::post`, `cd::denoise`, `cd::decal`, `cd::velocity`, `cd::gpu_particles`, `cd::hdr_display`, `cd::debug_line`, `cd::debug_draw`, etc.
- **World & Simulation (21)**: `cd::ecs`, `cd::scene`, `cd::world_container`, `cd::physics`, `cd::physics_jolt`, `cd::physics_soft_body`, `cd::physics_vehicle`, `cd::anim`, `cd::anim_ik`, `cd::audio`, `cd::audio_spatial` (`HrtfMixer`), `cd::audio_dsp_fx`, `cd::input`, `cd::gameplay_input_binding`, `cd::gameplay_time`, `cd::net`, `cd::net_lobby`, `cd::net_matchmaker`, `cd::net_session_replay`, `cd::particle_system`.
- **Game & Original CHROMODYNAMIC Element Architecture (23)**: `cd::element_core` (`Element`, `Planar2D`, `Spatial3D`, `Stage`, `Pulse<Args...>`), `cd::elements_domain` (20+ concrete 2D/3D Elements), `cd::gameplay_framework` (`Vessel`, `Avatar`, `RoverVessel`, `Pilot`, `HumanPilot`, `SynthPilot`, `StageDirector`, `WorldLedger`, `SpawnBeacon3D` + 12 Showcase Stage builders), plus `cd::game::*` (`save`, `save_compression`, `settings`, `fsm`, `ai_bt`, `ai_director`, `ai_pathfinding`, `ai_squad`, `anim_graph`, `query`, `trigger`, `camera`, `dialogue`, `dialog_tree`, `quest`, `l10n`, `particles_event`, `asset_hot_reload`, `cutscene_player`, `input_recorder`).
- **Custom Graphics-Backed UI & Editor (74)**: `cd::ui`, `cd::ui_layout`, `cd::ui_font`, `cd::ui_renderer`, `cd::ui_renderer_rhi`, `cd::ui_renderer_webgpu`, `cd::ui_input`, `cd::ui_animation`, `cd::ui_widgets` (`DockSpace`, `Splitter`, `ColorPicker`, `CurveEditor`, `Table`, `TreeView`, `PopoutDock`, `EditorAndHudWidgets`, `HudLayer`), `cd::ui_theme`, `cd::ui_a11y`, `cd::editor` (`FullFledgedEditorSuite`), `cd::editor_ui`, `cd::editor_cdproj`, and 27 `cd::editor_panel_*` libraries.
- **Runtime (1)**: `cd::runtime` (`EngineContext` + `EngineLoop` + PIE snapshot/restore).

---

## Shipping Applications (2) & Offline Tools (8)

| Binary | Category | Purpose |
|---|---|---|
| `editor` (`cd_sample_editor`) | Shipping App (`apps/editor`) | 24-panel Dear ImGui-free CHROMODYNAMIC Editor with Project Hub, Add Element Catalog (60 classes), Viewport Gizmo, Terrain/Foliage Brush, Live PIE (`Play/Pause/Stop`), and Chrome Tracing export. |
| `cd_player` | Shipping App (`apps/player`) | Standalone game runtime player (`--showcase <slug>` or `--all-showcases`). |
| `cd_assetc` | Tool (`tools/assetc`) | Offline `.cdpak` asset cooker, directory bundler, and TOC verifier (`add_cdpack()` CMake macro). |
| `cd_stress` | Tool (`tools/stress`) | 24-hour / deterministic soak & scene perturbation harness (`v2.0.7`). |
| `cd_cook_mesh` | Tool (`tools/cook_mesh`) | OBJ / glTF → `.cdmesh` binary mesh cooker. |
| `cd_cook_texture` | Tool (`tools/cook_texture`) | PNG/JPG/HDR → `.cdtex` BC7 texture & mip-chain cooker. |
| `cd_bench_compare` | Tool (`tools/bench_compare`) | Benchmark regression comparator. |
| `cd_bench_archetype` | Tool (`tools/bench_archetype`) | ECS archetype vs sparse-set throughput benchmark. |
| `cd_render_thread_soak` | Tool (`tools/render_thread_soak`) | Multi-threaded `AsyncSubmitN` render-thread soak verifier. |
| `cd_changelog_gen` | Tool (`tools/changelog_gen`) | Conventional-commit release changelog generator. |

---

## Dependency rules

1. **Tier order is strict.** `engine/CMakeLists.txt` adds subdirectories
   bottom-up; `cd_add_library()` would emit a warning if a downstream
   target tried to `PUBLIC_DEPS` something above its tier.
2. **No global state.** Every library exposes an explicit `Context` or
   registry; consumers pass that in.
3. **Standalone test binary per library.** `cd_add_test()` builds a
   per-library gtest binary; CI runs `ctest --output-on-failure`. Tests
   may consume their own library plus `cd::core`, nothing else from
   the engine, so each library's test suite is self-contained.
4. **Public include layout.** `engine/<lib>/include/cd/<lib>/...`. Anything
   under `engine/<lib>/src/` is `PRIVATE`. Adding `#include` paths past
   another library's `src/` is a build error by configuration.
5. **Export macros.** `CD_<LIB>_API` per library, generated by
   `cd_add_library`. Switch to `CD_BUILD_SHARED_LIBS=ON` to get `.dll/.so`
   per library — symbol visibility is wired up.
