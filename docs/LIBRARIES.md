# CHROMODYNAMIC Library Catalog

The active CMake build declares **199 modular targets** across 8 strict DAG tiers (`L0` through `L6`). Every engine library uses the `cd::<name>` CMake alias and compiles cleanly under C++23 `-Wall -Wextra -Werror`.

## 1. L0 Foundation & Reflection (`engine/foundation/`)

| Target | Header Root | Summary |
| :-- | :-- | :-- |
| `cd::core` | `cd/core/` | Generational `Handle<Tag>`, `ErrorCode`, `Result<T>`, `FixedString`, `Span`, environment compatibility helpers. |
| `cd::math` | `cd/math/` | Right-handed **Y-up** `Vec2/3/4`, `Mat3/4`, `Quat`, `Transform`, `AABB`, `Frustum`, Vulkan perspective and orthographic projections. |
| `cd::schema` | `cd/schema/` | Self-describing C++23 `FieldDesc`, `TypeSchema`, `SchemaRegistry`, value clamping, and Draft 2020-12 JSON Schema generator. |
| `cd::memory` | `cd/memory/` | Arena, linear frame, pool, and slab allocators with alignment guarantees. |
| `cd::concurrency` | `cd/concurrency/` | Work-stealing job system, task graph dependencies, and lock-free queues. |
| `cd::io` / `cd::vfs` | `cd/io/`, `cd/vfs/` | Binary/text streams, virtual filesystem mount points (`res://`, disk directories, `.cdpak` archives), file watchers. |
| `cd::platform` | `cd/platform/` | Cross-platform windowing, high-DPI cursors, keyboard/mouse/gamepad `OSEvent` pumping. |
| `cd::log` / `cd::diag` / `cd::frame_timing` | `cd/log/`, `cd/diag/` | Structured logging, counters, `chrome://tracing` profiler export, crash minidump/symbolizer support. |

## 2. L1 Asset & Packaging Pipeline (`engine/asset/`)

| Target | Header Root | Summary |
| :-- | :-- | :-- |
| `cd::asset` | `cd/asset/` | Async handle-based asset registry, reference counting, and hot-reload lifecycle. |
| `cd::asset_gltf` | `cd/asset/gltf/` | glTF 2.0 `.gltf`/`.glb` mesh, PBR material, texture, skeleton, and animation clip importer. |
| `cd::asset_obj` | `cd/asset/obj/` | Canonical Wavefront `.obj` parser with fan triangulation and smooth normal synthesis. |
| `cd::asset_ktx2` | `cd/asset/ktx2/` | KTX2 / Basis / BCn mipmapped GPU texture container loader. |
| `cd::asset_pak` | `cd/asset/pak/` | `PakBuilder` and `PakReader` for deterministic `.cdpak` standalone distribution archives. |

## 3. L2-L3 Rendering, GI & Post-FX (`engine/render/`)

| Target | Header Root | Summary |
| :-- | :-- | :-- |
| `cd::rhi` / `cd::rhi_null` / `cd::rhi_vulkan` | `cd/rhi/` | Explicit GPU device, swapchain, dynamic rendering passes, bindless descriptors, ray-tracing BLAS/TLAS, and headless null backend. |
| `cd::shader` / `cd::material` | `cd/shader/`, `cd/material/` | Runtime GLSL-to-SPIR-V compilation (`glslang`), reflection, pipeline caching, and `MaterialInstance` descriptor binding. |
| `cd::framegraph` | `cd/framegraph/` | Transient resource lifetimes, automatic image/buffer barrier insertion, and pass culling. |
| `cd::render2d` | `cd/render2d/` | 64-bit Z/Y-sorted `SpriteBatch2D`, `Camera2D`, sparse `Tilemap2D` + 16-tile Wang auto-tiling + greedy colliders, soft-shadow `Lighting2D`, `.aseprite` & `.ldtk` loaders, `Canvas2D`. |
| `cd::scene_renderer` | `cd/scene_renderer/` | Multi-pass 3D FrameGraph orchestrator (depth prepass, G-buffer, CSM shadows, clustered PBR, sky, post-FX). |
| `cd::brdf` / `cd::ibl` / `cd::ibl_gpu` / `cd::light` / `cd::cluster` | `cd/brdf/`, `cd/light/` | Cook-Torrance GGX, Turquin multi-scattering, Belcour-Barla iridescence, Kelvin blackbody lights, 3D clustered light culling, split-sum IBL. |
| `cd::ddgi` / `cd::restir_di` / `cd::restir_gi` / `cd::nrc` | `cd/ddgi/`, `cd/restir_di/` | Ray-traced probe irradiance/visibility volumes, Reservoir Spatio-Temporal Importance Resampling, and radiance caching. |
| `cd::atmosphere` / `cd::volumetric` / `cd::light_shafts` | `cd/atmosphere/`, `cd/volumetric/` | Physically based sky/aerial perspective, volumetric cloud layers, and Wronski froxel volumetric fog. |
| `cd::virtual_geometry` / `cd::virtual_textures` / `cd::mesh_shader` | `cd/virtual_geometry/` | Meshlet cluster DAG culling, virtual texture page tables, and GPU-driven indirect submission. |
| `cd::post` / `cd::post_exposure` / `cd::velocity` | `cd/post/` | Karis 6-mip bloom, histogram/compute auto-exposure, temporal anti-aliasing history, motion vectors, AgX/ACES tonemapping. |
| `cd::debug_draw` / `cd::debug_line` | `cd/debug_draw/` | Immediate 3D world-space debug lines, grids, frustums, and contact vectors. |

## 4. L3 World, ECS, Physics & Audio (`engine/world/`)

| Target | Header Root | Summary |
| :-- | :-- | :-- |
| `cd::ecs` | `cd/ecs/` | Cache-friendly archetype Entity-Component-System (`World`, `Entity`, component queries, command buffers). |
| `cd::scene` | `cd/scene/` | Local-to-world transform hierarchy (`SceneGraph`), parent-child propagation, and dirty-subtree updates. |
| `cd::world_container` | `cd/world_container/` | Multi-Project, `World`, `Level`, and named `Layer` (`LayerMember`) organization with `.cdproj` serialization. |
| `cd::physics` | `cd/physics/` | 2D (`v2d::PhysicsWorld2D`, `CharacterController2D` with coyote time & jump buffering) and 3D (`v3d::PhysicsWorld3D`, OBB/Sphere/Capsule solver, XPBD cloth, Voronoi fracture, active ragdoll, vehicle dynamics). |
| `cd::anim` | `cd/anim/` | Skeletal joint hierarchies, keyframe sampling, linear/spherical blending, and GPU skinning palettes. |
| `cd::audio` | `cd/audio/` | Mixing buses, compressor, reverb, low-pass filter, limiter, and HRTF binaural spatialization. |
| `cd::input` / `cd::net` | `cd/input/`, `cd/net/` | Action mapping, deterministic input recording/playback, and snapshot/transport networking primitives. |

## 5. L4 Scripting & Gameplay (`engine/script/`, `engine/game/`)

| Target | Header Root | Summary |
| :-- | :-- | :-- |
| `cd::script` | `cd/script/` | Sandboxed Lua 5.4 VM with instruction-count budgets and live reload. |
| `cd::world2d` | `cd/world2d/` | Complete 2D stage runtime (`Stage2D`, `Entity2D`, `CommandBus2D`, `PlaytestRunner2D`, `.cd2d.json` persistence). |
| `cd::element_core` / `cd::elements_domain` / `cd::gameplay_framework` | `cd/gameplay_framework/` | Composable `Element`, `Vessel`, `Pilot`, `Stage`, and showcase director (`cd::gameplay_director`). |
| `cd::ai_bt` / `cd::query` / `cd::trigger` / `cd::camera_rig` | `cd/ai_bt/`, `cd/trigger/` | Behavior trees, spatial gameplay queries, trigger volumes, and spring-arm camera rigs. |

## 6. L5-L6 UI, Runtime & Applications (`engine/ui/`, `engine/runtime/`, `apps/`)

| Target | Location | Summary |
| :-- | :-- | :-- |
| `cd::ui` / `cd::ui_widgets` (`ChromaUi`) | `engine/ui/` | Zero-external-bloat GPU docking UI (`ChromaUi`), flexbox layout, FreeType/HarfBuzz/MSDF font atlas, theme palettes, vector icons, node graph, and `cd::ui::renderer_rhi::Submitter`. |
| `cd::editor` | `engine/ui/editor/` | `EditHistory` (`Undo`/`Redo`), `TransformCommands`, `AxisGizmo`, `CommandPalette`, `SelectionOutline`, `Bookmark`. |
| `cd::runtime` | `engine/runtime/` | `EngineContext` and deterministic fixed-step `EngineLoop`. |
| `cd::app_common` | `apps/common/` | Canonical shared application utilities (`FileTextIo`, `CameraMath3D`, `FrameCapture`, `PackageExporter`). |
| `cd::editor_lib` + `cd_app_editor` | `apps/editor/` | Source 2 `toolframework2`-modeled 2D/3D Studio (`core/`, `render/`, `scene/`, `physics/`, `pipeline/`, `workspace_2d/`, `ui/`) + `23`-line `main.cpp`. |
| `cd_player` | `apps/player/` | Standalone `.cdpak` / `Stage2D` / showcase runtime executable + `15`-line `main.cpp`. |
