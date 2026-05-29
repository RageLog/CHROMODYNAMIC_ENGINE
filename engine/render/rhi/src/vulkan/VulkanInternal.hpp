// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/VulkanInternal.hpp (private)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>
#include <volk.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi::vulkan
{

struct VulkanInstance
{
    VkInstance instance { VK_NULL_HANDLE };
    VkDebugUtilsMessengerEXT debug_messenger { VK_NULL_HANDLE };
    bool volk_initialized { false };
    bool validation_enabled { false };

    ~VulkanInstance();
    VulkanInstance() = default;
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    VulkanInstance(VulkanInstance&&) = delete;
    VulkanInstance& operator=(VulkanInstance&&) = delete;
};

[[nodiscard]] cd::core::Result<void> create_instance(
    VulkanInstance& out,
    const std::string& app_name,
    std::uint32_t app_version,
    bool enable_validation,
    const std::vector<std::string>& extra_extensions
);

/// Bridge for `cd::rhi::vulkan::get_native(IDevice&)`. Lives in
/// VulkanDevice.cpp because that's where the concrete VulkanDevice class
/// is defined; NativeHandles.cpp would otherwise need its full layout.
/// Returns false when the IDevice is not Vulkan-backed (out parameters
/// untouched).
[[nodiscard]] bool try_fill_native_handles(
    cd::rhi::IDevice& dev,
    VkInstance* out_instance,
    VkPhysicalDevice* out_physical,
    VkDevice* out_device,
    VkQueue* out_graphics_queue,
    std::uint32_t* out_graphics_family
) noexcept;

}  // namespace cd::rhi::vulkan
