# engine/ui/editor/panel_input_recorder — cd::editor_panel_input_recorder
#
# phase665 — InputRecorder editor panel library.
# Provides cd::editor::panel::input_recorder::InputRecorderPanel
# (DrawBatcher path).
# Record / Stop / Replay button strips + timestamp/progress display:
#   * Record button — red when actively recording, grey otherwise.
#   * Stop button   — active orange when any operation in flight, dim grey otherwise.
#   * Replay button — green when replayer is in flight, grey otherwise.
#   * Progress strip — red fill (recording event count) or green fill (replay).
#   * Bind via set_recorder(Recorder*) + set_replayer(Replayer*).
#
# Targets:
#   cd_editor_panel_input_recorder   (real target)
#   cd::editor_panel_input_recorder  (alias)

cd_add_library(editor_panel_input_recorder
  TYPE STATIC
  SOURCES
    src/InputRecorderPanel.cpp
  PUBLIC_DEPS
    cd::core
    cd::game_input_recorder
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
