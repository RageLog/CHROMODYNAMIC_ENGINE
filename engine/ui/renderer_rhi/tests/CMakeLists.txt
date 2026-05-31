cd_add_test(submitter
  SOURCES test_submitter.cpp
  DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::core
)

# Phase 554 / M3 W1A -- Route B inline GLSL pipeline factory tests.
# Gated on CD_ENABLE_VULKAN because the inline-shader path is best
# exercised against a real backend; the test itself GTEST_SKIPs when
# no Vulkan ICD is present so headless CI keeps passing.
if(CD_ENABLE_VULKAN)
  cd_add_test(submitter_inline
    SOURCES test_submitter_inline.cpp
    DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::rhi_vulkan cd::core
  )

  # Phase 608 / M7 W3 -- Route A cd::material::UiVariant pipeline factory
  # tests. Same gate + GTEST_SKIP fallback as `submitter_inline`. The
  # test additionally needs cd::material (the variant factory) + cd::shader
  # (the glslang compiler probe used in the skip guard).
  cd_add_test(submitter_material_route
    SOURCES test_submitter_material_route.cpp
    DEPS    cd::ui_renderer_rhi cd::ui_renderer cd::rhi cd::rhi_vulkan
            cd::material cd::shader cd::core
  )
endif()
