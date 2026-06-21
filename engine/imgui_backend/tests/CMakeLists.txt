# engine/imgui_backend/tests — host-side (device-free) coverage.
#
# cd::imgui_backend's draw path is device-gated (verified by the renderer +
# rhi pixel-parity capstone). These tests cover the host-testable surface:
# the name-hash colour encoder, the profiler flame-graph data/branch logic
# driven through a headless ImGui context, and Context::create's
# null-argument validation. No live Vulkan device is required.
cd_add_test(imgui_backend
  SOURCES test_imgui_backend.cpp
  DEPS    cd::imgui_backend cd::profile cd::core
)

# Device-gated GPU-draw test: drives Context::render(cmd) ->
# ImGui_ImplVulkan_RenderDrawData onto a REAL Vulkan command buffer inside an
# offscreen dynamic-rendering pass (no swapchain / present surface needed).
# SKIPs cleanly when no Vulkan ICD or no platform window is available, so
# headless CI stays green. cd::imgui_backend already PUBLIC-links
# cd::rhi_vulkan + cd::platform; we name them so the symbols this TU calls
# directly (create_vulkan_device, create_window, IDevice/ICommandBuffer) link.
cd_add_test(imgui_backend_device
  SOURCES test_imgui_backend_device.cpp
  DEPS    cd::imgui_backend cd::rhi_vulkan cd::platform cd::rhi cd::core
)
