# cd::ui

## Purpose
Retained-mode widget tree system with renderer-agnostic DrawCommand emission. Enables decoupling of UI layout/logic from rendering backend, supporting multiple frontends (editor, samples, runtime) with different rasterization paths.

## Namespace
`cd::<ui>::ui::`

## Public headers
- `include/cd/ui/Widget.hpp` — Base widget type and tree composition
- `include/cd/ui/Anchor.hpp` — Layout anchoring (dock sides, pivot points)
- `include/cd/ui/Theme.hpp` — Styling (colors, fonts, spacing)
- `include/cd/ui/ContextMenu.hpp` — Right-click menu builder
- `include/cd/ui/TabBar.hpp` — Tabbed panel container
- `include/cd/ui/ProgressBar.hpp` — Progress indication widget
- `include/cd/ui/Spinner.hpp` — Loading spinner animation
- `include/cd/ui/Toast.hpp` — Temporary notification overlay
- `include/cd/ui/Tooltip.hpp` — Hover hints

## Primary types
- `Ui::Widget` — Base class with layout constraints, children, event handlers
- `Ui::DrawCommand` — Opaque rendering instruction emitted per-frame
- `Ui::Theme` — Global style palette (colors, fonts, margins)
- `Ui::InputEvent` — Mouse/keyboard/focus events

## Usage example
```cpp
#include <cd/ui/Widget.hpp>
#include <cd/ui/Theme.hpp>

// Build widget tree.
auto root = cd::ui::Widget::create<Panel>();
auto button = root->add_child<Button>("Click Me");
auto label = root->add_child<Label>("Status");

// Theme applies globally.
cd::ui::Theme theme{
  .primary_color = glm::vec3(0.0f, 0.5f, 1.0f),
  .font_size = 14
};

// Emit draw commands (consumed by any renderer).
std::vector<cd::ui::DrawCommand> cmds = root->draw(theme);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_ui
ctest --preset ninja-debug -R "^ui$"
```

## Dependencies
- `cd::core` — engine types

## References
- Dear ImGui — retained-mode UI reference
- Architectural patterns: MTV (Model-View-Theme) separation

## Notes
- No external UI library dependency — pure abstraction.
- DrawCommand is backend-agnostic (ImGui, VkDraw, Canvas, etc. can consume).
- Input routing via event delegation pattern.
- Used by cd::editor_ui for scene inspector, and cd::imgui_backend for editor panels.
