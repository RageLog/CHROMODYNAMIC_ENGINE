# tools/stress — Production v2.0.7: 24-Hour / Deterministic Showcase Soak & Perturbation Harness (cd_showcase_soak)

add_executable(cd_showcase_soak main.cpp)
target_link_libraries(cd_showcase_soak PRIVATE
  cd::runtime
  cd::gameplay_framework
  cd::elements_domain
  cd::element_core
  cd::scene_renderer
  cd::framegraph
  cd::rhi
  cd::diag
  cd::core
)
target_compile_features(cd_showcase_soak PUBLIC cxx_std_23)
cd_apply_warnings(cd_showcase_soak)
cd_apply_sanitizers(cd_showcase_soak)
cd_apply_coverage(cd_showcase_soak)
set_target_properties(cd_showcase_soak PROPERTIES
  FOLDER "tools"
  OUTPUT_NAME "cd_showcase_soak"
)

if(CD_ENABLE_TESTING)
  add_test(
    NAME cd_showcase_soak_smoke
    COMMAND cd_showcase_soak --iterations 24
  )
endif()
