# cd::editor_panel_inspector

**Purpose**: Inspector panel library. Displays and allows editing of scene object properties, component data, and asset metadata.

**Namespace**: `cd::editor::panel::inspector`.

**Headers**: `cd/editor/panel_inspector/Inspector.hpp`.

**Primary types**:
- `cd::editor::panel::inspector::Inspector` -- inspector widget; supports both DrawBatcher and ImGui rendering paths.

**Rendering paths**: Both DrawBatcher (CPU-side via `cd::ui_renderer`) and ImGui (immediate-mode GUI) backends.

**Usage example**:
```cpp
#include <cd/editor/panel_inspector/Inspector.hpp>

cd::editor::panel::inspector::Inspector inspector(scene_object);
// Choose rendering backend:
// - DrawBatcher: auto draw_commands = inspector.draw_batcher();
// - ImGui: inspector.draw_imgui();
```

**Test command**: `ctest --preset ninja-debug -R cd_test_editor_panel_inspector --output-on-failure`.

**Notes**:
- Phase 543: Inspector panel library.
- Dual rendering support (DrawBatcher + ImGui) for flexibility.
- Property inspection driven by reflection metadata.

**TODO**: expand coverage (currently <3 test cases).
