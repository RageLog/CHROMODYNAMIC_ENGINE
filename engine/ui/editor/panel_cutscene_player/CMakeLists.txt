# engine/ui/editor/panel_cutscene_player — cd::editor_panel_cutscene_player
#
# phase617 — CutscenePlayer editor panel library.
# phase703 — Save/Load JSON round-trip wired into the panel (M15 W4B).
# phase741 — Pan/zoom Sprint-2: zoom_factor + pan_offset_x state, tick_input
#             (mouse-wheel zoom + shift-drag/middle-drag pan), scrollbar.
#
# Provides cd::editor::panel::cutscene_player::CutscenePlayerPanel
# (DrawBatcher path).
# Timeline scrubber + per-phase block layout + event marker dots:
#   * Per-phase coloured blocks sized proportionally to duration_ms,
#     scaled by zoom_factor and offset by pan_offset_x.
#   * Event marker dots within each phase block at zoomed+panned offsets.
#   * Global playhead scrubber showing the current position (zoom+pan applied).
#   * Scrollbar at bottom showing the visible viewport within the zoomed timeline.
#   * Current-offset display strip (progress within the active phase).
#   * Play/Pause/Stop/Skip button strips.
#   * Save/Load button strips — round-trip via save_to_json/load_from_json.
#   * Bind via set_cutscene(Cutscene) + set_player(CutscenePlayer*).
#   * tick_input(pointer, middle_down, wheel_delta, shift_held, bounds) for pan/zoom.
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
