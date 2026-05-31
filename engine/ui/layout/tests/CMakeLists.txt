cd_add_test(flex
  SOURCES test_flex.cpp
  DEPS    cd::ui_layout cd::core
)

cd_add_test(ui_layout_constraint
  SOURCES test_constraint.cpp
  DEPS    cd::ui_layout cd::core
)
