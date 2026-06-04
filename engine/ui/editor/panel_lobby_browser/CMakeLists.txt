# engine/ui/editor/panel_lobby_browser — cd::editor_panel_lobby_browser
#
# phase718 — Lobby Browser panel library.
# Provides cd::editor::panel::lobby_browser::LobbyBrowser (DrawBatcher path).
# Visualizes cd::network::lobby::Lobby active room list:
#   * One row per RoomState: room_name + game_mode + player_count bar + lock icon.
#   * Click-to-select row emits selected_room_id().
# MOMENT: multiplayer dev sees rooms appear/disappear in real-time — no external
# network monitor needed.
#
# Targets:
#   cd_editor_panel_lobby_browser   (real target)
#   cd::editor_panel_lobby_browser  (alias)

cd_add_library(editor_panel_lobby_browser
  TYPE STATIC
  SOURCES
    src/LobbyBrowser.cpp
  PUBLIC_DEPS
    cd::core
    cd::net_lobby       # cd::network::lobby::Lobby + RoomState
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
