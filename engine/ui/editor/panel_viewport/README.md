# cd::editor_panel_viewport

**Purpose**: Viewport panel library. Displays the 3D scene render target and handles viewport interaction (pan, zoom, object selection).

**Namespace**: `cd::editor::panel::viewport`.

**Headers**: `cd/editor/panel_viewport/Viewport.hpp`.

**Primary types**:
- `cd::editor::panel::viewport::Viewport` -- viewport widget; accepts a `cd::rhi::TextureHandle` for the scene color render target and emits a textured quad via `cd::ui::DrawBatcher`.

**Input handling**: Receives viewport interaction events (mouse, keyboard) and translates them to camera/selection commands.

**Rendering path**: DrawBatcher textured-quad rendering (`cd::ui_renderer`).

**Usage example**:
```cpp
#include <cd/editor/panel_viewport/Viewport.hpp>

cd::rhi::TextureHandle scene_color = /* render target from renderer */;
cd::editor::panel::viewport::Viewport viewport(scene_color);
auto draw_commands = viewport.draw();
// Submit to ui_renderer
```

**Test command**: `ctest --preset ninja-debug -R cd_test_editor_panel_viewport --output-on-failure`.

**Notes**:
- Phase 546: Viewport panel library.
- Minimal; focuses on display + input routing, not scene management.
- Scene rendering is external (provided as texture handle).

**TODO**: expand coverage (currently <3 test cases).
