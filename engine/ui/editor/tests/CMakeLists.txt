cd_add_test(editor
  SOURCES test_editor.cpp
  DEPS    cd::editor cd::editor_ui cd::ui cd::scene cd::ecs cd::input cd::math cd::core
)

cd_add_test(viewport_gizmo_active_axis
  SOURCES test_viewport_gizmo_active_axis.cpp
  DEPS    cd::editor cd::editor_ui cd::ui cd::scene cd::ecs cd::math cd::core
)
