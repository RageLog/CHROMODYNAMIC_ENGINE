cd_add_test(editor_panel_vehicle_editor
  SOURCES test_vehicle_editor.cpp
  DEPS
    cd::editor_panel_vehicle_editor
    cd::physics_vehicle
    cd::ui_renderer
    cd::ui_widgets
)
