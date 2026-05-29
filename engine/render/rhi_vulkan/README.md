# cd::rhi_vulkan

**Purpose**: Vulkan 1.3+ backend implementation for the GPU hardware abstraction layer (RHI). Provides instance, device, command buffer, and memory management using Khronos-standard APIs, VMA for suballocation, and volk for runtime symbol loading (graceful degradation on systems without Vulkan drivers).

**Namespace**: `cd::rhi_vulkan`.

**Key Dependencies**:
- `Khronos Vulkan-Headers` (SDK 1.3.290+) — core API definitions.
- `volk` — runtime symbol loader; avoids hard link against vulkan-1.dll.
- `VulkanMemoryAllocator (VMA)` — AMD GPUOpen; efficient GPU memory pooling.

**Primary Types** (Internal; exposed via `cd::rhi::make_native_device()`):
- `VulkanInstance` — wraps `VkInstance`; manages driver enumeration + layer validation.
- `VulkanDevice` — concrete `IDevice` impl; GPU queue + render pass + PSO cache.
- `VulkanCommandBuffer` — concrete `ICommandBuffer` impl; command recording + submission.
- `VulkanVma` — VMA pool wrapper; sub-allocation + fragmentation tracking.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_rhi_vulkan
ctest --preset ninja-debug -R rhi_vulkan --output-on-failure
```

**Platform Support**:
- **Windows**: VK_USE_PLATFORM_WIN32_KHR (via vkCreateWin32SurfaceKHR).
- **macOS**: VK_USE_PLATFORM_METAL_EXT (Metal under MoltenVK); user supplies `CAMetalLayer*` via SwapchainDesc.
- **Linux**: VK_USE_PLATFORM_WAYLAND_KHR (default) or Xlib (via `CD_RHI_PREFER_X11`).

**Forward Compatibility**:
- `CD_RHI_VULKAN_14` option gates Vulkan 1.4 features (push_descriptor, maintenance5, dynamic_rendering_local_read).

**Notes**:
- volk gates gracefully: if no Vulkan ICD is installed, `VulkanInstance::create()` returns an error rather than crashing the loader.
- VMA is header-only; implementation compiled once in VulkanVma.cpp.
- volk + Vulkan-Headers are marked as SYSTEM includes to silence third-party warnings under strict flags.
