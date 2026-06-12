# phase1133 (SL-C step 2): catalogue + headless compile-all gate.
cd_add_test(gluon
  SOURCES test_gluon.cpp
  DEPS    cd::gluon cd::shader cd::core
)
