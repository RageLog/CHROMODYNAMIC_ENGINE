cd_add_test(framegraph
  SOURCES test_framegraph.cpp
  DEPS    cd::framegraph cd::rhi cd::core
)

# Real-Vulkan-device integration tests for FrameGraph barrier emission.
# Skips gracefully when no Vulkan ICD is present on the host.
cd_add_test(framegraph_vulkan
  SOURCES test_framegraph_vulkan.cpp
  DEPS    cd::framegraph cd::rhi_vulkan cd::rhi cd::core
)
