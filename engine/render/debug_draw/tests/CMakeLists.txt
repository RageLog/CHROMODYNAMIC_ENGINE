cd_add_test(debug_draw
  SOURCES test_debug_draw.cpp
  DEPS    cd::debug_draw
)

# band2-render-core — real-Vulkan-device integration tests for the VB
# growth / park / reclaim contract (phase-1034 use-after-free guard). The
# NullDevice cannot compile GLSL, so this is the first CI coverage of the
# real flush path. Skips gracefully without a Vulkan ICD / glslang.
if(CD_ENABLE_VULKAN)
  cd_add_test(debug_draw_vulkan
    SOURCES test_debug_draw_vulkan.cpp
    DEPS    cd::debug_draw cd::rhi_vulkan cd::rhi cd::shader cd::core cd::math cd::debug_line
  )
endif()
