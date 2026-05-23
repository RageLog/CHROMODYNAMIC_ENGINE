// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/NativeHandles.cpp
//
// VulkanDevice's class definition lives in VulkanDevice.cpp (anonymous-
// namespace-style detail file). Its accessor methods are public on the
// class itself; we just need the dynamic_cast bridge. To avoid pulling
// the full VulkanDevice class definition here, we add tiny extern "C++"
// query helpers in VulkanDevice.cpp that this TU calls — keeps the
// dynamic_cast cost localised to the place that knows the concrete type.
// =============================================================================
#include "VulkanCommandBuffer.hpp"
#include "VulkanInternal.hpp"  // dynamic_cast helpers

#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/NativeHandles.hpp>

namespace cd::rhi_vulkan
{

std::optional<NativeHandles> get_native(cd::rhi::IDevice& dev) noexcept
{
    NativeHandles out {};
    if (!try_fill_native_handles(
            dev,
            &out.instance,
            &out.physical_device,
            &out.device,
            &out.graphics_queue,
            &out.graphics_family
        ))
    {
        return std::nullopt;
    }
    return out;
}

VkCommandBuffer get_native(cd::rhi::ICommandBuffer& cmd) noexcept
{
    auto* concrete = dynamic_cast<VulkanCommandBuffer*>(&cmd);
    if (concrete == nullptr)
        return VK_NULL_HANDLE;
    return concrete->native();
}

}  // namespace cd::rhi_vulkan
