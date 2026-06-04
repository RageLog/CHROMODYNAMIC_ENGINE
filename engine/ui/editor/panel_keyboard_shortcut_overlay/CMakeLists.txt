# engine/ui/editor/panel_keyboard_shortcut_overlay — cd::editor_panel_keyboard_shortcut_overlay
#
# phase688 — Keyboard Shortcut Overlay panel library (M14 W2B).
# Provides cd::editor::panel::keyboard_shortcut_overlay::KeyboardShortcutOverlay
# (DrawBatcher path).
#
# Full-screen semi-transparent overlay that lists all registered editor
# shortcuts grouped by category.  Toggle with '?' (Source 2 SDK convention).
#
# MOMENT: A new editor user presses '?', sees a clean shortcut cheatsheet
# overlaid — no manual learning curve, every key revealed at a glance.
#
# Targets:
#   cd_editor_panel_keyboard_shortcut_overlay   (real target)
#   cd::editor_panel_keyboard_shortcut_overlay  (alias)

cd_add_library(editor_panel_keyboard_shortcut_overlay
  TYPE STATIC
  SOURCES
    src/KeyboardShortcutOverlay.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer  # DrawBatcher
    cd::ui_widgets   # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
