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
endif()
