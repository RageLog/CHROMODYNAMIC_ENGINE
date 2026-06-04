# engine/ui/editor/panel_auto_save_indicator — cd::editor_panel_auto_save_indicator
#
# phase745 — Auto-Save Indicator panel (status-bar widget).
# Provides cd::editor::panel::auto_save_indicator::AutoSaveIndicator
# (DrawBatcher path; no ImGui).
#
# Five-state Status enum (Idle / PendingDirty / Saving / JustSaved / Error)
# drives a colour-coded icon + label background strip (~50 px high).
# Fits in a status bar or right-corner of the editor window.
#
# MOMENT: user edits scene → "Unsaved changes" badge (amber) appears
# immediately; auto-save fires → "Saving..." → "Saved just now" (green).
# Trust signal that work isn't lost.
#
# Targets:
#   cd_editor_panel_auto_save_indicator   (real target)
#   cd::editor_panel_auto_save_indicator  (alias)

cd_add_library(editor_panel_auto_save_indicator
  TYPE STATIC
  SOURCES
    src/AutoSaveIndicator.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer   # DrawBatcher + Color
    cd::ui_theme      # Theme + PaletteSlot
    cd::ui_widgets    # Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
