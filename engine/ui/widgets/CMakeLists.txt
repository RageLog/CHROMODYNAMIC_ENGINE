# engine/ui/widgets -- Concrete widget catalog (ADR-20260530, Phase 2.2)
#
# cd::ui_widgets -- concrete widgets (Button, TextInput, Slider, Toggle,
# Checkbox, Dropdown, Modal) sitting on top of the existing UI stack:
#   * cd::ui            (retained-mode tree primitives -- Rect / Color)
#   * cd::ui_layout     (Flex layout solver -- caller-set rects)
#   * cd::ui_font       (glyph atlas + TTF rasterizer)
#   * cd::ui_renderer   (CPU draw batcher -- quad / glyph)
#   * cd::ui_input      (hit-test + focus chain + modal capture)
#   * cd::core          (foundation)
#
# Each widget exposes `set_rect`, `tick(input_state)`, and `draw(batcher,
# font, theme)` so headless callers (tests / tooling) can drive state
# transitions without touching the GPU.

cd_add_library(ui_widgets
  TYPE STATIC
  SOURCES
    src/Widgets.cpp
    src/ColorPicker.cpp
    src/CurveEditor.cpp
    src/DockSpace.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui
    cd::ui_layout
    cd::ui_font
    cd::ui_renderer
    cd::ui_input
    cd::ui_animation
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
