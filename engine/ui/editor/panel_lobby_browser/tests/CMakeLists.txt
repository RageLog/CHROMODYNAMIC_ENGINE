cd_add_test(editor_panel_lobby_browser
  SOURCES test_lobby_browser.cpp
  DEPS
    cd::editor_panel_lobby_browser
    cd::net_lobby
    cd::ui_renderer
    cd::ui_widgets
)
