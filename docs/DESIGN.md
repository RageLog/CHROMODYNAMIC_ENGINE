# CHROMODYNAMIC Design Principles & C++23 Standards

This document captures the software engineering, UI/UX, and human-plus-AI workflow rules enforced across all **199 libraries** and applications in CHROMODYNAMIC.

## 1. Modern C++23 Engineering Rules

1. **Explicit Ownership & Zero Leaks**: Heap ownership uses `std::unique_ptr<T>` (unique owner) or `std::shared_ptr<T>` (shared immutable resources) exclusively. Raw pointers (`T*`) are non-owning observers only; `new` and `delete` never appear in application or subsystem logic. Hot-path ECS and resource references use generational `cd::core::Handle<Tag>` values to prevent ABA use-after-free bugs.
2. **Typed Error Handling (`std::expected`)**: Recoverable failures in file I/O, asset parsing, shader compilation, and RHI creation return `cd::core::Result<T>` (`std::expected<T, cd::core::ErrorCode>`). Empty `catch(...) {}` blocks are forbidden.
3. **API Contracts & Const Correctness**: Every query and factory function carries `[[nodiscard]]`. Non-mutating member functions and local values are `const`. Virtual overrides carry `override`, and C-style casts are forbidden (`static_cast`, `std::bit_cast` only) under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wold-style-cast -Werror`.
4. **Zero Global Mutable Engine State**: Engine libraries (`engine/**`) hold zero mutable global state; all state lives in explicit context objects (`EngineContext`, `World`, `Stage2D`, `StudioEngineState`) so multiple worlds and headless tests run concurrently in the same process.
5. **Deterministic Testing Without Sleeps**: Unit and integration tests never call `std::this_thread::sleep_for`. Timing, synchronization, and multi-frame simulation tests step deterministic fixed-delta clocks (`1.0F / 60.0F`) or explicit condition synchronization.

## 2. Valve Source 2 & Commercial UX Design Rules

Usability is treated as a first-class architectural constraint alongside frame time:

1. **Unified Single Top Bar (`32px`)**: Following Valve Source 2 Hammer 5 and Godot 4, Studio uses one `32px` top bar housing left menus (`File`, `Edit`, `Create`, `View`, `Project`, `Help`), center workspace/play pills (`Hub`, `2D`, `3D`, `Graph`, `> Play (F5)`, `Export .cdpak`), and a discrete right FPS/frame-time readout. Stacked secondary ribbon bars are prohibited.
2. **Uncluttered Viewport by Default**: The central 2D/3D viewport reserves over 92% of its area for the scene itself, showing only a compact top-left tool pill (`Move`, `Rotate`, `Scale`, shading mode, `Cam`, `Snap`, `Grid`, `Col`, `Phys`, `Focus`) and a top-right 3D X/Y/Z orientation rose. Engine R&D diagnostic probes are quarantined off by default behind `View -> Diagnostic 3D Render Probes`.
3. **Direct 2-Pane Asset Workflow**: `Assets & FileSystem` uses a direct 2-pane layout (left folder bookmarks + right icon grid). Double-clicking a mesh spawns it into the scene; dragging a mesh or texture onto any viewport object or `Scene Outliner` row assigns it immediately without intermediate staging modals.
4. **Lossless Play-In-Editor (`F5` / `Esc`)**: Pressing `F5` snapshots the authoring state in memory (`Stage2D` or `.cdscene.json` state), runs live physics, character controllers, and Lua 5.4 scripts inside the viewport, and restores exact authoring transforms and selection when `Stop (Esc)` is pressed.

## 3. Symmetric Human + AI Native Architecture

CHROMODYNAMIC is engineered so human creators and AI agents are first-class peers operating on the same data model:

1. **Self-Describing Reflection (`cd::schema`)**: Runtime types register fields, types, units, and numeric bounds once in `cd::schema::SchemaRegistry`. The Studio UI auto-generates Inspector widgets from `TypeSchema`, while `--dump-schema` exports Draft 2020-12 JSON Schema so LLM agents never guess property names or ranges.
2. **Unified Undoable Command Bus (`CommandBus2D` & `EditHistory`)**: Mouse painting in the 2D canvas, Inspector edits, and AI JSON command batches (`--exec-2d-commands`) execute through the same validated command objects, giving human users instant `Ctrl+Z` / `Ctrl+Y` undo over AI-authored edits.
3. **Deterministic Closed-Loop Playtesting (`PlaytestRunner2D` & `--self-test-all`)**: AI agents and CI pipelines verify level solvability, collision integrity, frame budgets, and swapchain visual output headlessly in milliseconds (`--playtest-2d`, `--capture-png`, `--self-test-all`) before presenting results to the user.
