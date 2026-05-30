cd_add_test(ui_widgets
  SOURCES test_widgets.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::core
)

cd_add_test(ui_widgets_animation
  SOURCES test_widgets_animation.cpp
  DEPS    cd::ui_widgets cd::ui_renderer cd::ui_animation cd::core
)
