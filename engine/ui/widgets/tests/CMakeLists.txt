cd_add_test(ui_widgets
  SOURCES
    test_widgets.cpp
    test_color_picker.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::core
)

cd_add_test(ui_widgets_animation
  SOURCES test_widgets_animation.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::ui_animation cd::core
)

cd_add_test(ui_curve_editor
  SOURCES test_curve_editor.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::ui_animation cd::core
)

cd_add_test(ui_dockspace
  SOURCES test_dockspace.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::core
)

cd_add_test(ui_table
  SOURCES test_table.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::core
)

cd_add_test(ui_tree_view
  SOURCES test_tree_view.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::core
)

cd_add_test(ui_popout_dock
  SOURCES test_popout_dock.cpp
  DEPS    cd::ui_widgets cd::core
)

cd_add_test(ui_native_window_adapter
  SOURCES test_native_window_adapter.cpp
  DEPS    cd::ui_widgets cd::platform cd::core
)
