# CHROMODYNAMIC Engine — Comprehensive Architecture & Developer Guide (`v1.0`)

> **CHROMODYNAMIC** is a cross-platform, cross-API, library-oriented C++23 hybrid 2D+3D game engine and rendering framework built from **120+ independent, single-responsibility `cd_<lib>` libraries**. It unifies the approachable, composable scene-element ergonomics of lightweight engines with the deep gameplay, rendering, and authoring architecture of AAA engines — using **100% original terminology** and a **100% custom graphics-backed UI toolkit (`cd::ui::*`)**.

---

## 1. Architectural DNA & Strict Layer DAG

Every subsystem in `engine/` is a self-contained CMake target (`cd::<lib>`) that can be linked individually or composed into the full runtime (`cd::runtime`) and editor (`cd_editor`). Dependencies flow strictly bottom-up (verified by `check_layer_dag.py --root engine` with **0 violations**):

| Layer Rank | Directory | Representative `cd::<lib>` Targets |
| :--- | :--- | :--- |
| **0. Foundation** | `engine/foundation/` | `cd::core`, `cd::math`, `cd::mem`, `cd::concurrency`, `cd::io`, `cd::vfs`, `cd::log`, `cd::diag`, `cd::platform`, `cd::config` |
| **1. Asset & Data** | `engine/asset/` | `cd::asset`, `cd::asset_pak`, `cd::asset_gltf`, `cd::asset_image`, `cd::asset_ktx2`, `cd::asset_cdmesh`, `cd::asset_cdtex`, `cd::asset_wav`, `cd::asset_texture_streamer` |
| **2. Render & RHI** | `engine/render/` | `cd::rhi`, `cd::rhi_vulkan`, `cd::rhi_d3d12`, `cd::rhi_metal`, `cd::rhi_metal_shader`, `cd::rhi_webgpu`, `cd::rhi_opengl`, `cd::shader`, `cd::gluon`, `cd::material`, `cd::framegraph`, `cd::cluster`, `cd::ibl`, `cd::volumetric`, `cd::scene_renderer` |
| **3. World Domains** | `engine/world/` | `cd::ecs`, `cd::scene`, `cd::physics`, `cd::anim`, `cd::audio`, `cd::input`, `cd::net` |
| **4. Script** | `engine/script/` | `cd::script` (Lua 5.4 / Luau bindings) |
| **5. Gameplay & Elements** | `engine/game/` | `cd::element_core`, `cd::elements_domain`, `cd::gameplay_framework`, `cd::ai_behavior_tree`, `cd::ai_navigation`, `cd::gameplay_ability`, `cd::gameplay_save` |
| **6. Custom UI & Editor** | `engine/ui/` | `cd::ui`, `cd::ui_renderer_rhi`, `cd::editor_ui`, `cd::editor`, `cd::editor_panel_inspector`, `cd::editor_gizmo`, `cd::editor_picking` |
| **7. Runtime & Apps** | `engine/runtime/`, `apps/` | `cd::runtime` (`EngineLoop`), `cd_editor` (`apps/editor`), `cd_player` (`apps/player`) |

---

## 2. Original CHROMODYNAMIC Scene & Gameplay Taxonomy

CHROMODYNAMIC avoids borrowed terminology (`Node2D`, `Node3D`, `Pawn`, `Controller`, `GameMode`) in favor of its own cohesive domain vocabulary:

### 2.1 Core Hierarchy (`cd::element_core`)
- **`Element`**: Base scene graph building block with `ElementId`, `TagSet`, `Signal<Args...>` pub/sub slots, property reflection (`PropertyBag`), and deterministic lifecycle hooks (`on_enter_stage`, `on_leave_stage`, `on_tick`, `on_fixed_tick`).
- **`Planar2D`**: 2D transform base element (`pos`, `rot`, `scale`, `z_index`).
- **`Spatial3D`**: 3D transform base element (`TRS`, local/world matrix propagation, world AABB).
- **`Stage` (`.cdstage`)**: Composable, nestable scene tree (`mount_substage` / `instantiate_stage`) with bidirectional ECS synchronization (`ElementEcsBridge`) and JSON serialization (`StageSerializer`).

### 2.2 Domain Scene Elements (`cd::elements_domain`)
- **Atmosphere & Lighting**: `SolarLight3D`, `PointLight3D`, `SpotLight3D`, `AreaLight3D`, `CelestialDome3D`, `RadianceProbe3D`, `ReflectionProbe3D`, `IrradianceField3D` (DDGI + ReSTIR), `AetherFog3D`, `FogPocket3D`, `CloudStrata3D`, `SunBeams3D`, `OpticsVolume3D`.
- **3D Geometry & Nature**: `StaticMesh3D`, `RiggedMesh3D`, `SwarmMesh3D`, `ProjectionDecal3D`, `TerraClipmap3D`, `OceanWave3D`, `SparkSwarm3D`, `CameraLens3D`.
- **Physics & Destruction**: `KineticBody3D`, `TriggerZone3D`, `JointLink3D`, `RagdollChain3D`, `ClothSurface3D`, `ShatterMesh3D`, `RoverRig3D`.
- **2D Planar Suite**: `QuadSprite2D`, `Flipbook2D`, `TileCanvas2D`, `KineticBody2D`, `Camera2D`, `RadialLight2D`, `SparkEmitter2D`, `PathCurve2D`.
- **Audio, AI & Narrative**: `SoundSource3D`, `SoundEar3D`, `EchoChamber3D`, `NavMeshZone3D`, `WayfinderAgent3D`, `CutsceneElement`, `DialogueElement`, `AiDirectorElement`.

### 2.3 High-Level Gameplay Framework (`cd::gameplay_framework`)
- **`Vessel`** (`Avatar`, `RoverVessel`): Physical controllable entity in the world that can be boarded or vacated by a `Pilot`.
- **`Pilot`** (`HumanPilot`, `SynthPilot`): Decision-making brain driving a `Vessel` (`board(Vessel&)` / `vacate()`).
- **`StageDirector` & `WorldLedger`**: Stage rules referee, `SpawnBeacon3D` selection, wave/match orchestration, and network-serializable score/quest ledger.

---

## 3. 19-Pass FrameGraph & `cd::scene_renderer`

`cd::render::SceneRenderer` (`engine/render/scene_renderer`) automatically translates active `Stage` elements into a single 19-pass `FrameGraph` execution pipeline:
1. `ShadowCascadePass` $\rightarrow$ 2. `DepthPrePass` $\rightarrow$ 3. `FroxelClusterBuildPass` $\rightarrow$ 4. `ClusterLightCullPass` $\rightarrow$ 5. `GBufferVelocityPass` $\rightarrow$ 6. `GtaoPass` $\rightarrow$ 7. `DdgiRestirUpdatePass` $\rightarrow$ 8. `DeferredDirectIblPass` $\rightarrow$ 9. `ForwardOpaqueSssHairPass` $\rightarrow$ 10. `TerrainWaterPass` $\rightarrow$ 11. `AtmosphereSkyCloudsPass` $\rightarrow$ 12. `VolumetricFroxelFogPass` $\rightarrow$ 13. `SsrPass` $\rightarrow$ 14. `SparkParticleTransparentPass` $\rightarrow$ 15. `TaaResolvePass` $\rightarrow$ 16. `DofMotionBlurPass` $\rightarrow$ 17. `BloomAutoExposurePass` $\rightarrow$ 18. `AgxTonemapColorGradePass` $\rightarrow$ 19. `CustomUiOverlayPass`.

---

## 4. 100% Custom Graphics-Backed UI (`cd::ui::*`) & Editor (`cd_editor`)

Both in-game HUDs (`HudLayer`) and the 27-panel `CHROMODYNAMIC Editor` run on `cd::ui::renderer_rhi` with zero Dear ImGui dependency:
- **Typography & Layout**: SDF/MSDF font atlas, UTF-8 + HarfBuzz-style BiDi/RTL shaping (`measure_text`), Flexbox + CSS Grid (`solve_rtl`, `solve_grid`), DPI scaling, and `CubicBezier` / `SpringDamper` animations.
- **Full-Fledged Editor Suite (`FullFledgedEditorSuite.hpp`)**:
  - `EditorCockpitShell` (dockable panels, splitters, workspace presets, command palette)
  - `AddElementCatalog` & `ElementInspectorBinder` (one-click element spawning & live property editing with Undo/Redo)
  - `ViewportGizmoController` & `TerrainFoliageBrush` (ray/GPU picking, translate/rotate/scale snapping, terrain sculpting)
  - `PieWorkbenchController` (Play / Pause / Step / Stop Play-In-Editor with instant `.cdstage` snapshot restore)
  - `VisualGraphWorkbench` (Behavior Tree, Dialogue, AnimGraph/IK, and Material node graphs)
  - `GpuFrameProfilerExporter` (`chrome://tracing` JSON export)

---

## 5. Showcase Projects (`showcases/01..12`) & Production Tooling

All legacy `samples/` have been replaced by **12 Editor-native Showcase Projects** under `showcases/`:
1. `01_pbr_materials_sss_hair`
2. `02_celestial_aether_fog_clouds`
3. `03_sponza_clustered_ddgi_restir`
4. `04_optics_cinematic_studio`
5. `05_avatar_locomotion_anim_ik`
6. `06_jolt_dynamics_rover_shatter`
7. `07_terra_ocean_nature_world`
8. `08_synth_pilot_squad_narrative`
9. `09_acoustics_hrtf_echo_chamber`
10. `10_spark_swarm_and_decals`
11. `11_custom_ui_hud_and_menus`
12. `12_planar_2d_platformer_net`

### Production CLI Tools & Verification
- **Asset Bundler (`cd_assetc`)**: Packs project directories into `.cdpak` archives (`cd_assetc --pack <dir> <out.cdpak>`) or CMake `add_cdpack()`.
- **Standalone Game Player (`cd_player`)**: Boots `.cdproj` / `.cdpak` bundles directly into `EngineLoop`.
- **Crash Reporter (`cd::diag::CrashReporter`)**: Captures symbolicated stack backtraces (`capture_stack_backtrace`) and writes `.cdcrash` minidump reports (`write_minidump_report`).
- **Stress & Benchmark Harnesses**: `cd_stress` (`tests/stress/`) and `cd_bench_archetype` (`tools/bench_archetype/`).
