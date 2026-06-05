cd_add_test(rhi_vulkan
  SOURCES test_rhi_vulkan.cpp
  DEPS    cd::rhi_vulkan cd::rhi cd::core cd::shader
)

# Phase 765 W2A — F5: KHR/EXT mesh-shader pipeline + draw_mesh_tasks.
# Gated test — skips when no Vulkan ICD or when the device lacks the
# VK_EXT_mesh_shader / VK_NV_mesh_shader extension.
cd_add_test(rhi_mesh_shader
  SOURCES test_rhi_mesh_shader.cpp
  DEPS    cd::rhi_vulkan cd::rhi cd::core cd::shader
)
