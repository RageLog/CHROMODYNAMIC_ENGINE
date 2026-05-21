// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanInternal.hpp (private)
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

namespace cd::rhi_vulkan
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

}  // namespace cd::rhi_vulkan
