cd_add_test(ui
  SOURCES test_ui.cpp
  DEPS    cd::ui cd::core
)

cd_add_test(ui_phase1_integration
  SOURCES test_ui_phase1_integration.cpp
  DEPS    cd::ui_layout cd::ui_font cd::ui_renderer cd::core
)
