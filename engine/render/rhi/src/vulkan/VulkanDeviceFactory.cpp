// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/VulkanDeviceFactory.cpp
// =============================================================================
#include "VulkanInternal.hpp"

#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace cd::rhi::vulkan
{

[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>> create_device(
    std::unique_ptr<VulkanInstance> inst,
    const std::vector<std::string>& device_extensions,
    bool prefer_discrete,
    std::vector<std::byte> pipeline_cache_blob
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
    // V-PIPECACHE: thread the optional seed blob into device init.
    return create_device(std::move(inst), info.device_extensions, info.prefer_discrete_gpu,
                         std::move(info.pipeline_cache_blob));
}

}  // namespace cd::rhi::vulkan
