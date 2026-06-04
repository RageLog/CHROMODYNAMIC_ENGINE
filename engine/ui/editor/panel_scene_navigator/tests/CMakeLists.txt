cd_add_test(editor_panel_scene_navigator
  SOURCES test_scene_navigator.cpp
  DEPS
    cd::editor_panel_scene_navigator
    cd::ecs
    cd::ui_renderer
    cd::ui_widgets
)
