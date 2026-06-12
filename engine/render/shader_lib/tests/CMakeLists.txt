# phase1133 (SL-C step 2): catalogue + headless compile-all gate.
cd_add_test(shader_lib
  SOURCES test_shader_lib.cpp
  DEPS    cd::shader_lib cd::shader cd::core
)
