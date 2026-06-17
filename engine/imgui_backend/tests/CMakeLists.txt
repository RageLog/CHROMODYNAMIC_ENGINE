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
