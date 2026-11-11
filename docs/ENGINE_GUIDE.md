# CHROMODYNAMIC Developer Quickstart Guide

This guide covers day-to-day workflows for building CHROMODYNAMIC, authoring 2D and 3D games in Studio, automating workflows with AI agents, and adding new modular C++23 libraries. For architectural internals and the full library catalog, see [ARCHITECTURE.md](ARCHITECTURE.md), [DESIGN.md](DESIGN.md), and [LIBRARIES.md](LIBRARIES.md).

## 1. Building & Running Verification Gates

```powershell
# Configure and build Studio + Standalone Player
cmake --preset ide-base
cmake --build build/ide-base --target cd_app_editor cd_player -j 8

# Verify strict downward #include layer DAG (zero cycles / zero upward includes)
python ".agents/skills/engine-sota-adr/scripts/check_layer_dag.py" --engine-root "engine"

# Run unit, integration, and headless smoke suites
ctest --test-dir build/ide-base --output-on-failure

# Run the 8-part end-to-end Studio self-test suite
.\build\ide-base\bin\editor.exe --self-test-all
```

## 2. Studio Controls & Workspace Shortcuts

* **Top Bar Workspaces**: Switch between `Hub` (Project Manager), `2D` (2D Engine Studio), `3D` (3D Scene Studio), and `Graph` (Node & Shader Workspace) from the unified `32px` top bar.
* **3D Camera**: Hold `RMB` + `WASD`/`QE` to fly; `MMB` drag or scroll wheel to pan/zoom; `F` to focus the selected node.
* **Gizmo & Snapping**: Press `1` (`Move`), `2` (`Rotate`), or `3` (`Scale`); toggle `Snap` (`0.25m` / `0.50m`) or press `G` to snap the selected object onto the ground collider.
* **Play-In-Editor & Packaging**: Press `F5` to play live 2D or 3D physics and scripts inside the viewport, `Esc` (`F8`) to restore exact authoring state, and `F9` (`Export .cdpak`) to build a portable standalone folder with `cd_player.exe` and `game.cdpak`.

## 3. Symmetric Human + AI CLI Reference

| Flag | Example | Description |
| :-- | :-- | :-- |
| `--dump-schema <file>` | `editor.exe --dump-schema schema.json` | Exports Draft 2020-12 JSON Schema for all registered `cd::schema` types and exits cleanly (`0`). |
| `--2d-studio` | `editor.exe --2d-studio` | Boots directly into the 2D Engine Studio workspace. |
| `--exec-2d-commands <json>` | `editor.exe --2d-studio --exec-2d-commands patch.json` | Executes undoable `CommandBus2D` JSON operations (`paint_tile_2d`, `paint_rect_2d`, `spawn_entity_2d`, `set_field_2d`). |
| `--playtest-2d <frames>` | `editor.exe --2d-studio --playtest-2d 240 --telemetry-out rep.json` | Runs deterministic headless 2D physics/Lua playtesting and writes structured JSON telemetry. |
| `--export-standalone <dir>` | `editor.exe --2d-studio --export-standalone build/export/Game` | Packages `game.cdpak` + `cd_player.exe` + runtime DLLs into `<dir>`. |
| `--capture-png <path>` | `editor.exe --template 1 --capture-png out.png --frames 10` | Captures a verified Vulkan swapchain PNG on the final frame for visual regression inspection. |

## 4. Adding a New Engine Module

1. Choose the lowest valid tier (`foundation`, `asset`, `render`, `world`, `script`, `game`, `ui`, or `runtime`) that satisfies the downward DAG in [ARCHITECTURE.md](ARCHITECTURE.md).
2. Declare the target with `cd_add_library(<name> ...)` in `CMakeLists.txt`, placing public headers under `include/cd/<name>/` with `[[nodiscard]]`, `const` correctness, `std::expected` error reporting, and zero global mutable state.
3. Add a focused GTest binary (`tests/test_<name>.cpp`) with zero `sleep_for` calls and verify both `ctest` and `check_layer_dag.py` pass before committing.
