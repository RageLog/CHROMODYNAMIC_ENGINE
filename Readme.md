# CHROMODYNAMIC Engine & Studio

**CHROMODYNAMIC** (`cd::`) is a modular C++23 hybrid 2D and 3D game engine, standalone runtime player, and human-plus-AI studio built on an explicit library-oriented architecture modeled after **Valve Source 2** (`tier0` through `toolframework2`). Every subsystem compiles as an independent CMake target (`cd::<module>`, **199 libraries** configured in the active build graph) enforcing a strict acyclic layer hierarchy with zero upward `#include` dependencies.

## Commercial Progression & Architectural North Star

CHROMODYNAMIC targets commercial engine usability and engineering quality in three deliberate stages while preserving Valve Source 2 modularity across every layer:

1. **Stage 1 (Godot 4 Class Usability & 2D Mastery)**: Instant boot, subpixel-accurate 2D camera, 64-bit Z/Y-sorted sprite and flipbook batching (`cd::render2d`), sparse `16x16` chunked tilemaps with 16-tile Wang auto-tiling, greedy AABB collider meshing, 2D analytical soft-shadow lighting, `.aseprite` and `.ldtk` native importers, sandboxed Lua 5.4 live scripting, and one-click `.cdpak` standalone game export (`cd::world2d`).
2. **Stage 2 (Unity 6 Class Multi-Genre Pipeline)**: Self-describing C++23 reflection (`cd::schema`), schema-driven Inspector cards, undoable command bus (`CommandBus2D` and `cd::editor::EditHistory`), multi-world/level/layer containers (`cd::world_container`), skeletal glTF 2.0 animation, Jolt/XPBD/active-ragdoll physics (`cd::physics`), and portable runtime playback (`cd_player`).
3. **Stage 3 (Unreal Engine 5 Class High-End 3D Rendering)**: Vulkan 1.3 bindless PBR (`cd::rhi_vulkan`), Turquin multi-scattering energy compensation, Belcour-Barla thin-film iridescence, cascaded shadow maps, ray-traced DDGI irradiance volumes (`cd::ddgi`), ReSTIR DI/GI (`cd::restir_di`, `cd::restir_gi`), Wronski froxel volumetric fog, Karis 6-mip bloom, and AgX/ACES tone mapping.

## Valve Source 2 Layer Correspondence

| Source 2 Subsystem | CHROMODYNAMIC Layer & Libraries | Responsibility |
| :-- | :-- | :-- |
| `tier0` / `tier1` | **L0 Foundation** (`cd::core`, `cd::math`, `cd::memory`, `cd::concurrency`, `cd::io`, `cd::vfs`, `cd::platform`) | Allocators, generational `Handle<Tag>`, SIMD math (right-handed **Y-up**), virtual filesystem, OS windowing, logging, diagnostics. |
| `schemasystem` | **L0 Reflection** (`cd::schema`) | Compile-time and runtime type/field descriptors, range clamping, JSON serialization, Draft 2020-12 JSON Schema export for AI agents. |
| `resourcesystem` | **L1 Asset** (`cd::asset`, `cd::asset_gltf`, `cd::asset_obj`, `cd::asset_ktx2`, `cd::asset_pak`) | Streaming glTF 2.0, Wavefront OBJ, KTX2/BCn textures, and compressed `.cdpak` archives. |
| `rendersystem` / `materialsystem2` | **L2-L3 Render Core** (`cd::rhi`, `cd::rhi_vulkan`, `cd::shader`, `cd::material`, `cd::framegraph`) | Explicit RHI command buffers, SPIR-V compilation and hot-reload, bindless descriptor layouts, render-graph barrier scheduling. |
| `worldrenderer` | **L3 World Renderers** (`cd::render2d`, `cd::scene_renderer`, `cd::ddgi`, `cd::post`) | 2D sorted sprite/tilemap/soft-shadow renderer and 3D clustered PBR, shadow, GI, volumetric, and post-processing passes. |
| `vphysics2` | **L3-L4 Simulation** (`cd::physics`, `cd::ecs`, `cd::scene`, `cd::anim`, `cd::audio`) | Archetype ECS, transform hierarchy, 2D/3D rigid-body and character-controller solvers, skeletal skinning, spatial audio. |
| `engine2` | **L5-L6 Runtime & App Common** (`cd::runtime`, `cd::world2d`, `cd::app_common`, `cd_player`) | Fixed-timestep simulation loop, camera math, UTF-8 file I/O, GPU swapchain PNG readback, standalone `.cdpak` runtime player (`15`-line `apps/player/main.cpp`). |
| `toolframework2` / **Hammer 5** | **L6 Studio Tooling** (`cd::ui`, `cd::editor`, `cd::editor_lib`, `cd_app_editor`) | GPU-accelerated `ChromaUi` docking studio (`23`-line `apps/editor/main.cpp`), unified `32px` top bar, Scene Outliner, Inspector, 2-pane Asset Browser, PIE. |

## Repository Layout

```text
CHROMODYNAMIC_ENGINE/
  engine/
    foundation/     L0: core, math, platform, io, vfs, schema, concurrency, diag, log
    asset/          L1: gltf, obj, ktx2, pak, streaming resource loaders
    render/         L2-L3: rhi, rhi_vulkan, shader, material, framegraph, render2d, ddgi, post
    world/          L3: ecs, scene, physics, anim, audio, net, input, world_container
    script/         L4: sandboxed Lua 5.4 VM and hot-reload bindings
    game/           L4: world2d, element_core, gameplay_framework, ai_bt, camera, trigger
    ui/             L5: ChromaUi immediate/retained widgets, flex layout, FreeType/MSDF font, editor
    runtime/        L5: EngineContext, EngineLoop, fixed-step orchestration
  apps/
    common/         L6: cd::app_common (FileTextIo, CameraMath3D, FrameCapture, PackageExporter)
    editor/         L6: cd::editor_lib (core/, render/, scene/, physics/, pipeline/, workspace_2d/, ui/) + 23-line main.cpp
    player/         L6: cd_player standalone game & showcase runner + 15-line main.cpp
  tools/            Offline asset compiler (cd_assetc) and soak/stress harnesses
  tests/            Deterministic GTest unit, integration, golden-image, and headless PIE smoke suites
  docs/             Architecture, design principles, library catalog, roadmap, and ADRs
```

## Quick Build & Verification

### Prerequisites

* **Compiler**: Clang `21+` (Windows MSVC ABI, Linux, or macOS) with full C++23 support (`-std=c++23 -Wall -Wextra -Werror`)
* **Build System**: CMake `3.28+` and Ninja
* **Graphics SDK**: Vulkan SDK `1.3+`

### Configure, Build, and Test

```powershell
cmake --preset ide-base
cmake --build build/ide-base --target cd_app_editor cd_player -j 8
ctest --test-dir build/ide-base --output-on-failure
```

## Running CHROMODYNAMIC Studio & Standalone Player

### Interactive Studio (`editor.exe`)

```powershell
# Launch Studio into the Project Hub (2D & 3D starter templates + disk showcases)
.\build\ide-base\bin\editor.exe --project-manager

# Launch directly into the 2D Engine Studio (TileBrush, Wang Auto-Tile, Lua 5.4, Schema Inspector)
.\build\ide-base\bin\editor.exe --2d-studio

# Launch directly into a 3D Physics & Skinned Avatar Stage
.\build\ide-base\bin\editor.exe --template 1
```

### Symmetric Human + AI Headless Workflows

Human authors and AI coding agents execute through the exact same `cd::schema` types and undoable `CommandBus2D` / `EditHistory` operations:

```powershell
# Export Draft 2020-12 JSON Schema for zero-hallucination AI tool calling
.\build\ide-base\bin\editor.exe --dump-schema build/schema_2d.json

# Apply a batch of AI JSON commands, run a 240-frame deterministic playtest, and emit JSON telemetry
.\build\ide-base\bin\editor.exe --2d-studio --exec-2d-commands ai_patch.json --playtest-2d 240 --telemetry-out build/report.json

# Package a standalone .cdpak bundle and run it in cd_player.exe
.\build\ide-base\bin\editor.exe --2d-studio --export-standalone build/export/My2DGame
.\build\ide-base\bin\cd_player.exe --pak build/export/My2DGame/game.cdpak --autopilot --frames 60

# Run the 8-part autonomous end-to-end Studio self-test suite
.\build\ide-base\bin\editor.exe --self-test-all
```

## Documentation Index

* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): Layer DAG, Source 2 correspondence, coordinate conventions, frame lifecycle, and `cd::app_common` / `cd::editor_lib` boundaries.
* [docs/DESIGN.md](docs/DESIGN.md): Core C++23 engineering rules, memory ownership, error handling (`std::expected`), UI/UX principles, and symmetric Human + AI design.
* [docs/LIBRARIES.md](docs/LIBRARIES.md): Directory and dependency guide for the `199` CMake libraries across `engine/` and `apps/`.
* [docs/ENGINE_GUIDE.md](docs/ENGINE_GUIDE.md): Concise developer quickstart for adding modules, authoring scenes, and running verification gates.
* [docs/ADR/](docs/ADR/): Architecture Decision Records, including [ADR-20261111](docs/ADR/ADR-20261111-commercial-2d-3d-ai-native-architecture.md) and [ADR-20261112](docs/ADR/ADR-20261112-source2-clean-modular-architecture.md).
