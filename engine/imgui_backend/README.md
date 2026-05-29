# cd::imgui_backend

## Purpose
Dear ImGui integration with Vulkan dynamic-rendering backend and Win32 input bridge. Wraps Dear ImGui docking branch, compiles Vulkan/DX12/Win32 backends directly, and provides thin `cd::imgui::Context` API for editor and samples.

## Namespace
`cd::<imgui>::`

## Public headers
- `include/cd/imgui/Context.hpp` — Frame lifecycle (begin, render, end)
- `include/cd/imgui/ProfilerView.hpp` — Built-in profiler overlay (integrates cd::profile::Sample)
- `include/cd/imgui/ImGuiD3D12.hpp` — Optional D3D12 backend selection (Windows only)

## Primary types
- `Imgui::Context` — ImGui initialization, frame begin/end, command list generation
- `Imgui::ProfilerView` — Real-time frame timer display (CPU/GPU times)

## Usage example
```cpp
#include <cd/imgui/Context.hpp>

// Initialize ImGui backend.
cd::imgui::Context imgui_ctx;
imgui_ctx.initialize({
  .display_size = glm::vec2(1920, 1080),
  .font_scale = 1.5f
});

// Main loop:
while (running) {
  imgui_ctx.begin_frame();
  
  // User code draws ImGui widgets.
  if (ImGui::Button("Click me")) {
    on_button_click();
  }
  ImGui::Text("Frame time: %.2f ms", delta_time_ms);
  
  imgui_ctx.end_frame();
  
  // Render ImGui draw lists to GPU.
  imgui_ctx.render(framegraph, backbuffer);
}

imgui_ctx.shutdown();
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_imgui_backend
ctest --preset ninja-debug -R imgui_backend
```

## Dependencies
- `cd::core` — engine types
- `cd::platform` — window/input abstraction
- `cd::profile` — frame timing for ProfilerView
- `cd::rhi` — render hardware interface
- `cd::rhi_vulkan` — Vulkan backend (primary)
- **ImGui** (FetchContent: docking branch, compiled inline)
- **Windows SDK** (Win32 input, DWM, D3D12 — Windows only)

## References
- **Dear ImGui** (ocornut/imgui) — retained-mode GUI framework
- Vulkan dynamic rendering: VK_KHR_dynamic_rendering extension

## Notes
- STATIC library (linked once per editor/tool).
- ImGui compiled directly into the static lib (no separate binary).
- Vulkan backend uses volk for runtime function pointer loading (no libvulkan linking).
- Supports Win32 input; macOS/Linux input routing via cd::platform abstraction.
- D3D12 backend compiled but typically unused in favor of Vulkan (cross-platform).
- Optional ProfilerView integrates with cd::profile for frame timing overlay.
