Transform CHROMODYNAMIC into a commercial-grade, high-efficiency, general-purpose 2D game engine, complete all remaining open roadmap phases, and maintain clean modular architecture and tooling.

Integrity mode: [OPEN: Which shortcuts should the team NOT be allowed to do?]

## Requirements

### R1. Complete All Open Roadmap Phases
Complete all open roadmap phases in `docs/PLAN_NEXT_PHASES.md` and related roadmap documents, specifically FAZ 36 (Wave 36.1 unified `SceneRenderer` GPU passes and 3D split-brain elimination between `apps/editor` and `apps/player`, Wave 36.2 `StageMode::HD2D` hybrid rendering) and FAZ 37 (Wave 37.1 live `--mcp-server` JSON-RPC/MCP server and hot-reload scene sync, Wave 37.2 `tools/assetc` offline BC7/KTX2 + SPIR-V cooker and distribution packaging). [OPEN: Should open phases be executed before or alongside the 2D commercial overhaul?]

### R2. Commercial High-Efficiency General-Purpose 2D Game Engine Evolution
Evolve the existing prototypical 2D modules (`cd::render2d`, `cd::world2d`, and `[2D Studio]` editor workspace) into a production-grade commercial 2D engine with real GPU texture-atlas sprites, animated flipbooks, 2D particle systems, rich tilemap and prefab authoring tools, high performance, and standalone runtime distribution via `cd_player.exe`.

### R3. Clean Modular Architecture and Code Quality
Maintain clean modular architecture with zero unnecessary code duplication, adhering to Valve Source 2 `toolframework2` and Godot 4 UI/UX principles, strict C++23 standards, layer DAG invariants, and clean documentation.

## Acceptance Criteria

### Compilation & Architectural Integrity
- [ ] Entire engine and toolchain compile under Windows Clang C++23 with zero compiler errors and zero warnings under `-Wall -Wextra -Wpedantic -Wconversion -Werror`.
- [ ] Architectural layer DAG verification (`check_layer_dag.py`) reports zero `#include` cycle or upward violations.
- [ ] Academic iron rule / human voice checks (`check_human_voice.py`) pass with zero AI clichés on updated documentation.

### Roadmap & Test Suite Pass
- [ ] FAZ 36 and FAZ 37 are fully implemented and marked complete in `docs/PLAN_NEXT_PHASES.md`.
- [ ] All unit and regression tests pass unconditionally under `ctest`.
- [ ] [OPEN: Are there specific external test suites or benchmarks required?]

### Visual & Runtime Quality
- [ ] Live Vulkan 1.3 frame captures verify production-grade 2D rendering (textured sprites, lighting/shadows, tilesets, particles) in both `editor.exe` and `cd_player.exe` without prototypical solid-colored rectangles.
