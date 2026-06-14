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

/// Vulkan V2 (multi-queue model) — resolved queue-family selection for a
/// physical device. Every field is a queue-family INDEX into the device's
/// VkQueueFamilyProperties array. Families OVERLAP/ALIAS legitimately: on a GPU
/// that exposes a single graphics+compute+transfer family, all four indices are
/// equal and that is VALID, not an error. `*_dedicated` records whether the
/// async-compute / transfer family is a distinct family (true) or aliased back
/// to graphics (false), which is what the smoke test asserts.
struct QueueFamilySelection
{
    std::uint32_t graphics { 0 };           ///< graphics+compute+transfer (required)
    std::uint32_t compute { 0 };            ///< async-compute (dedicated or = graphics)
    std::uint32_t transfer { 0 };           ///< dedicated transfer (or = graphics)
    std::uint32_t present { 0 };            ///< surface-present (or = graphics fallback)
    bool compute_dedicated { false };       ///< compute is a distinct family
    bool transfer_dedicated { false };      ///< transfer is a distinct family
    bool present_resolved { false };        ///< a surface was probed (else = graphics)
};

/// Query VkQueueFamilyProperties for `pd` and select graphics / async-compute /
/// dedicated-transfer / present families per the Vulkan V2 policy. `surface`
/// may be VK_NULL_HANDLE (headless): present then aliases to graphics. Returns
/// kNoSuitableAdapter only when NO graphics-capable family exists (a device the
/// engine cannot render with at all).
[[nodiscard]] cd::core::Result<QueueFamilySelection>
select_queue_families(VkPhysicalDevice pd, VkSurfaceKHR surface) noexcept;

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
