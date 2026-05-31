# cd::editor_panel_asset_browser

**Purpose**: Asset Browser panel library. Provides a hierarchical asset browser widget that displays project assets, supports drag-drop, and enables asset preview and metadata inspection.

**Namespace**: `cd::editor::panel::asset_browser`.

**Headers**: `cd/editor/panel_asset_browser/AssetBrowser.hpp`.

**Primary types**:
- `cd::editor::panel::asset_browser::AssetBrowser` -- main widget; emits DrawCommands via `cd::ui::DrawBatcher` rendering path.

**Rendering path**: DrawBatcher (CPU-side rendering via `cd::ui_renderer`).

**Usage example**:
```cpp
#include <cd/editor/panel_asset_browser/AssetBrowser.hpp>

cd::editor::panel::asset_browser::AssetBrowser browser(asset_context);
auto draw_commands = browser.draw();
// Submit to ui_renderer
```

**Test command**: `ctest --preset ninja-debug -R cd_test_editor_panel_asset_browser --output-on-failure`.

**Notes**:
- Phase 545: Asset Browser panel library.
- Standalone; depends on `cd::ui` and `cd::core`.
- DrawBatcher rendering model supports both software (CPU) and GPU paths.

**TODO**: expand coverage (currently <3 test cases).
