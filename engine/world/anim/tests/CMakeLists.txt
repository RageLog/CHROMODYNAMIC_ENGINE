cd_add_test(anim
  SOURCES test_anim.cpp
  DEPS    cd::anim cd::math cd::core
)

cd_add_test(dual_quat
  SOURCES test_dual_quat.cpp
  DEPS    cd::anim cd::math cd::core
)
