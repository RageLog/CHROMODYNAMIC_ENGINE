# Library catalogue

CHROMODYNAMIC ships **48 libraries**. Each one builds and tests in
isolation; the table below lists the target name, alias, public namespace,
include root, and primary responsibility.

The libraries are organised into five tiers. Cross-tier dependencies
flow strictly downward (foundation → asset → render → world → runtime →
ui); circular references are a build error.

> The cross-tier ordering is enforced at `engine/CMakeLists.txt` —
> `add_subdirectory()` calls are bottom-up, and `cd_add_library()` in
> `cmake/CDProject.cmake` validates the dependency DAG at configure time.

---

## Foundation tier (16)

The "always-on" platform abstraction layer. Anything above this tier
may depend on these; nothing here depends on anything above.

| Target        | Alias            | Namespace               | Include root           | Responsibility |
|---------------|------------------|-------------------------|------------------------|----------------|
| `cd_core`     | `cd::core`       | `cd::core`              | `cd/core/`             | `Result`, `ErrorCode`, `span`, primitive utilities. Zero deps. |
| `cd_mem`      | `cd::mem`        | `cd::mem`               | `cd/mem/`              | Slot-map handles, pool / monotonic / stack allocators. |
| `cd_concurrency` | `cd::concurrency` | `cd::concurrency`  | `cd/concurrency/`      | Thread pool, work-stealing queues, hazard pointers, deterministic executor. |
| `cd_time`     | `cd::time`       | `cd::time`              | `cd/time/`             | Monotonic & wall clocks, frame timing, sleep utilities. |
| `cd_io`       | `cd::io`         | `cd::io`                | `cd/io/`               | File I/O, byte streams, memory-mapped readers. |
| `cd_serialization` | `cd::serialization` | `cd::serialization` | `cd/serialization/` | Binary serialization primitives + endianness helpers. |
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

---

## Asset tier (9)

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
| `cd_asset_cdtex`    | `cd::asset_cdtex`      | `.cdtex` v1/v2  | BC7 blocks + optional mip chain. |
| `cd_asset_ktx2`     | `cd::asset_ktx2`       | KTX2           | Khronos KTX2 v2; single-2D-texture short-list (RGBA8, BGRA8, BC7). |
| `cd_asset_wav`      | `cd::asset_wav`        | WAV (RIFF)     | PCM (i8/i16/i24/i32) + IEEE float; chunk-walking, skip-unknown. |
| `cd_asset_json`     | `cd::asset_json`       | JSON           | Hand-rolled subset parser + AST + serialize (compact & pretty). |

---

## Render tier (8)

Vulkan-first rendering stack. RHI is abstract; only `rhi_vulkan` is
implemented. Add a `cd::rhi_d3d12` or `cd::rhi_metal` here later
without touching consumers.

| Target           | Alias               | Responsibility |
|------------------|---------------------|----------------|
| `cd_rhi`         | `cd::rhi`           | Abstract device, swapchain, command buffer, pipeline, descriptor set interfaces. |
| `cd_rhi_vulkan`  | `cd::rhi_vulkan`    | Vulkan 1.3 + dynamic rendering, volk loader, VMA allocator. `get_native()` for opt-in raw-handle interop. |
| `cd_shader`      | `cd::shader`        | GLSL → SPIR-V compile (glslang), reflection, cache. |
| `cd_material`    | `cd::material`      | Pipeline state object, descriptor binding model. |
| `cd_render`      | `cd::render`        | Renderer composition; per-view, per-frame submission. |
| `cd_framegraph`  | `cd::framegraph`    | Pass declaration, resource lifetimes, barrier scheduling. |
| `cd_camera`      | `cd::camera`        | View / projection, Gribb-Hartmann frustum extraction, AABB visibility tests. |
| `cd_imgui_backend`| `cd::imgui_backend`| Dear ImGui docking + Vulkan dynamic-rendering backend, profile HUD. |

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
| `cd_audio`   | `cd::audio`     | Mixer, voice graph (output backend deferred per ADR-007). |
| `cd_input`   | `cd::input`     | Input mapping, action sets, device polling. |
| `cd_net`     | `cd::net`       | Reliable / unreliable channels, snapshot replication (v2). |

---

## UI tier (3)

| Target          | Alias              | Responsibility |
|-----------------|--------------------|----------------|
| `cd_ui`         | `cd::ui`           | Layout engine, widget primitives (per ADR-009). |
| `cd_editor_ui`  | `cd::editor_ui`    | Editor-specific widgets atop `cd::ui` + ImGui. |
| `cd_editor`     | `cd::editor`       | Top-level editor composition (scene tree, inspector, asset browser). |

---

## Runtime tier (1)

| Target       | Alias        | Responsibility |
|--------------|--------------|----------------|
| `cd_runtime` | `cd::runtime`| Subsystem startup / shutdown / tick orchestration. The "engine main()" library — consumed by `samples/hello_runtime`, by every full-engine sample, and by application binaries shipping CHROMODYNAMIC. |

---

## Tools (2)

Offline cookers; not part of the runtime DAG.

| Binary           | Purpose |
|------------------|---------|
| `cd_cook_mesh`   | OBJ / glTF → `.cdmesh` (engine binary mesh layout). |
| `cd_cook_texture`| PNG/JPG/HDR → `.cdtex` (BC7 blocks + optional mip chain via `--mips`). |

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
