// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanDeviceFactory.cpp
// =============================================================================
#include "VulkanInternal.hpp"

#include <cd/rhi_vulkan/VulkanDevice.hpp>

#include <memory>
#include <utility>

namespace cd::rhi_vulkan
{

[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>> create_device(
    std::unique_ptr<VulkanInstance> inst,
    const std::vector<std::string>& device_extensions,
    bool prefer_discrete
);

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>> create_vulkan_device(VulkanCreateInfo info)
{
    auto inst = std::make_unique<VulkanInstance>();
    if (auto r =
            create_instance(*inst, info.app_name, info.app_version, info.enable_validation, info.instance_extensions);
        !r.has_value())
    {
        return std::unexpected(r.error());
    }
    return create_device(std::move(inst), info.device_extensions, info.prefer_discrete_gpu);
}

}  // namespace cd::rhi_vulkan
