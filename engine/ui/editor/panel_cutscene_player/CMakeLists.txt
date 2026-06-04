# engine/ui/editor/panel_cutscene_player — cd::editor_panel_cutscene_player
#
# phase617 — CutscenePlayer editor panel library.
# phase703 — Save/Load JSON round-trip wired into the panel (M15 W4B).
#
# Provides cd::editor::panel::cutscene_player::CutscenePlayerPanel
# (DrawBatcher path).
# Timeline scrubber + per-phase block layout + event marker dots:
#   * Per-phase coloured blocks sized proportionally to duration_ms.
#   * Event marker dots within each phase block at scaled offsets.
#   * Global playhead scrubber showing the current position.
#   * Current-offset display strip (progress within the active phase).
#   * Play/Pause/Stop/Skip button strips.
#   * Save/Load button strips — round-trip via save_to_json/load_from_json.
#   * Bind via set_cutscene(Cutscene) + set_player(CutscenePlayer*).
#
# Targets:
#   cd_editor_panel_cutscene_player   (real target)
#   cd::editor_panel_cutscene_player  (alias)

cd_add_library(editor_panel_cutscene_player
  TYPE STATIC
  SOURCES
    src/CutscenePlayerPanel.cpp
  PUBLIC_DEPS
    cd::core
    cd::game_cutscene_player  # includes CutsceneJson + CutscenePlayer; pulls in cd::asset_json
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
