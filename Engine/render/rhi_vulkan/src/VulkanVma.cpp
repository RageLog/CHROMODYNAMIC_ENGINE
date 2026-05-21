// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanVma.cpp
//
// Single translation unit that defines VMA_IMPLEMENTATION. Every other TU
// that includes <vk_mem_alloc.h> sees only the declarations; this file is
// where the function bodies live (header-only-with-impl-in-one-TU pattern).
//
// VMA is configured to use volk's dynamically-loaded function pointers via
// VMA_VULKAN_VERSION + VMA_STATIC_VULKAN_FUNCTIONS=0 + VMA_DYNAMIC_VULKAN_
// FUNCTIONS=1 — the actual function-table pointer is passed at allocator
// creation time inside VulkanDevice.cpp.
// =============================================================================
#include <volk.h>  // volk first so its types satisfy VMA's Vulkan symbols

#define VMA_IMPLEMENTATION
// VMA resolves its own function table from vkGetInstanceProcAddr +
// vkGetDeviceProcAddr at allocator creation time. We feed volk's loaded
// pointers via VmaVulkanFunctions in VulkanDevice.cpp; this avoids both
// static linking and VMA's runtime dlopen path.
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
// Vulkan 1.3 baseline (matches the engine's RHI target).
#define VMA_VULKAN_VERSION 1003000

// VMA itself is vendor code and trips most of our -Werror diagnostics. Wrap
// the include in a diagnostic push/pop so warnings inside vk_mem_alloc.h
// are dropped without disabling them for our own code.
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#if defined(__clang__)
    #pragma clang diagnostic ignored "-Wnullability-completeness"
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#include <vk_mem_alloc.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif
