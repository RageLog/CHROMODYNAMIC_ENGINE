add_executable(cd_stress main.cpp)
cd_apply_warnings(cd_stress)
cd_apply_sanitizers(cd_stress)
target_link_libraries(cd_stress PRIVATE
  cd::core
  cd::ecs
  cd::math
  cd::runtime
  cd::gameplay_framework
)
set_target_properties(cd_stress PROPERTIES FOLDER "tests/stress")

add_executable(cd_tier5_stress test_tier5_stress.cpp)
cd_apply_warnings(cd_tier5_stress)
target_link_libraries(cd_tier5_stress PRIVATE
  cd::core
  cd::math
  cd::render
  cd::render2d
  cd::ui_renderer
  cd::asset_pak
  cd::world2d
  cd::platform
  cd::rhi
  cd::rhi_vulkan
)
set_target_properties(cd_tier5_stress PROPERTIES FOLDER "tests/stress")

if(CD_ENABLE_TESTING)
  add_test(
    NAME cd_stress_smoke
    COMMAND cd_stress --frames 32
  )
  add_test(
    NAME cd_tier5_stress_empirical
    COMMAND cd_tier5_stress
  )
endif()
