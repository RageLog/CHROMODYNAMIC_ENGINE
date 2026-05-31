# cd::editor_panel_console

**Purpose**: Console panel library. Displays engine diagnostics, log output, and user console commands in the editor.

**Namespace**: `cd::editor::panel::console`.

**Headers**: `cd/editor/panel_console/Console.hpp`.

**Primary types**:
- `cd::editor::panel::console::Console` -- console widget; emits DrawCommands via `cd::ui::DrawBatcher` rendering path.
- Support for log-entry filtering and search.

**Rendering path**: DrawBatcher (CPU-side rendering via `cd::ui_renderer`).

**Usage example**:
```cpp
#include <cd/editor/panel_console/Console.hpp>

cd::editor::panel::console::Console console(log_sink);
auto draw_commands = console.draw();
// Submit to ui_renderer
```

**Test command**: `ctest --preset ninja-debug -R cd_test_editor_panel_console --output-on-failure`.

**Notes**:
- Phase 544: Console panel library.
- Integrates with `cd::log` for diagnostic message filtering.
- DrawBatcher rendering model.

**TODO**: expand coverage (currently <3 test cases).
