# engine/ui/editor/panel_animator — cd::editor_panel_animator
#
# phase556-557-558 — Animator panel library.
# Provides cd::editor::panel::animator::Animator (DrawBatcher path).
# Animation clip browser + timeline scrubber:
#   * List of ClipId entries with the active clip highlighted.
#   * Current-time scrubber bar (normalised playback position).
#   * Play/pause and loop toggle indicator strips.
#   * Bind via set_clip(ClipId) + set_time(seconds).
#
# Targets:
#   cd_editor_panel_animator   (real target)
#   cd::editor_panel_animator  (alias)

cd_add_library(editor_panel_animator
  TYPE STATIC
  SOURCES
    src/Animator.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
