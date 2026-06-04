# engine/ui/editor/panel_scene_palette — cd::editor_panel_scene_palette
#
# phase717 — Scene Palette panel library.
# Provides cd::editor::panel::scene_palette::ScenePalette (DrawBatcher path).
#
# Live theme token preview: a grid of labelled colour swatches, one per
# semantic token in cd::ui::widgets::Theme (15 tokens: background, surface,
# surface_subtle, surface_hover, surface_press, divider, text, text_dim,
# dim_overlay, accent, accent_hover, focus_ring, accent_warning,
# accent_error, accent_success). Click any swatch to record its token name
# (for future copy-to-clipboard use).
#
# MOMENT: A UI designer sees all 12+ palette tokens at-a-glance with actual
# colour swatches — not memorised hex values.  Theme reskins are validated
# visually in seconds.
#
# Targets:
#   cd_editor_panel_scene_palette   (real target)
#   cd::editor_panel_scene_palette  (alias)

cd_add_library(editor_panel_scene_palette
  TYPE STATIC
  SOURCES
    src/ScenePalette.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer   # DrawBatcher
    cd::ui_widgets    # Theme + Rect + Color
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
