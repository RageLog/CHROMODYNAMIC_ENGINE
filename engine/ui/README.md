# cd::ui

**Purpose**: retained-mode widget tree for building rich user interfaces. Renderer-agnostic; widgets emit DrawCommands that frontends (editor, apps) consume and render via `cd::ui_renderer` or `cd::ui_renderer_rhi`.

**Namespace**: `cd::ui`.

**Headers**: `cd/ui/{Anchor,ContextMenu,ProgressBar,Spinner,TabBar,...}.hpp` -- concrete widget types; base Widget class in core.

**Primary types**:
- `cd::ui::Widget` -- base class for all retained-mode widgets.
- `cd::ui::ProgressBar` -- progress indicator widget.
- `cd::ui::Spinner` -- animated spinner/loader.
- `cd::ui::TabBar` -- tabbed interface container.
- `cd::ui::ContextMenu` -- right-click context menu.
- `cd::ui::Anchor` -- positioning and alignment helpers.
- `cd::ui::DrawCommand` -- renderer-independent draw opcode emitted by widgets.

**Sub-libraries**:
- `cd::ui_layout` -- Phase 442: Flex layout solver for widget arrangement.
- `cd::ui_font` -- Phase 443: stb_truetype glyph atlas.
- `cd::ui_renderer` -- Phase 444: CPU-side draw batcher.
- `cd::ui_renderer_rhi` -- Phase 453: RHI bridge for GPU rendering.
- `cd::ui_input` -- Phase 457: hit-testing, focus chain, tab navigation, modal capture.
- `cd::ui_animation` -- Phase 458: easing, Tweener, Timeline animations.
- `cd::ui_widgets` -- Phase 460: concrete widget catalog (Button, Slider, etc.).
- `cd::ui_theme` -- Phase 474: design token system (Material 3 shape).
- `cd::ui_a11y` -- Phase 478: accessibility baseline.
- `cd::ui_renderer_webgpu` -- Phase 522: WebGPU rendering skeleton.

**Usage example**:
```cpp
#include <cd/ui/ProgressBar.hpp>

cd::ui::ProgressBar bar;
bar.set_value(0.75f);
std::vector<cd::ui::DrawCommand> commands = bar.draw();
```

**Test command**: `ctest --preset ninja-debug -R cd_test_ui --output-on-failure`.

**Notes**:
- Depends on `cd::core` only; fully renderer-agnostic.
- Phase 430 flattened `ui/ui` into umbrella.
- See `ADR-20260530-phase-1-0-flex-layout-solver.md` for layout engine design.

**TODO**: expand coverage (currently <3 test cases).
