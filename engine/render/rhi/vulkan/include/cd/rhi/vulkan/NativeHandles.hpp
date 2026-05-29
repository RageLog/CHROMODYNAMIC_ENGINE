// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/NativeHandles.hpp
//
// Opt-in raw Vulkan handle accessors. Only consumers that NEED VkInstance /
// VkDevice / VkQueue / VkCommandBuffer directly should include this header —
// the regular cd::rhi public API hides them on purpose so engine code stays
// API-agnostic.
//
// Typical clients:
//   * cd::imgui — the Vulkan backend wants every native handle for setup
//   * Third-party debuggers (RenderDoc programmatic capture, NSight scripts)
//   * Custom Vulkan-specific render passes that the engine doesn't yet wrap
//
// Including this header pulls in volk.h, so it carries the same compile-
// cost penalty as the rest of the Vulkan backend. Don't include it in
// public engine headers.
//
// Design (per ADR-20260522-imgui-integration):
//   * Free functions in cd::rhi_vulkan namespace.
//   * dynamic_cast under the hood; returns nullopt / VK_NULL_HANDLE when
//     the supplied IDevice / ICommandBuffer is from a different backend
//     (e.g. future cd::rhi_dx12). This makes the function safe to call
//     from cross-backend client code.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <volk.h>

#include <cstdint>
#include <optional>

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}  // namespace cd::rhi

namespace cd::rhi::vulkan
{

/// Bundle of every Vulkan handle a "raw" client typically needs at init
/// time. Returned by value (cheap; handles are pointer-sized).
struct NativeHandles
{
    VkInstance instance { VK_NULL_HANDLE };
    VkPhysicalDevice physical_device { VK_NULL_HANDLE };
    VkDevice device { VK_NULL_HANDLE };
    VkQueue graphics_queue { VK_NULL_HANDLE };
    std::uint32_t graphics_family { 0 };
};

/// Extract native handles from an IDevice. Returns nullopt when the
/// supplied IDevice is not a Vulkan-backed device (e.g. a NullDevice
/// or a future DX12 device).
[[nodiscard]] std::optional<NativeHandles> get_native(cd::rhi::IDevice& dev) noexcept;

/// Extract the underlying VkCommandBuffer for the current recording.
/// Returns VK_NULL_HANDLE when the supplied ICommandBuffer is not from
/// the Vulkan backend.
[[nodiscard]] VkCommandBuffer get_native(cd::rhi::ICommandBuffer& cmd) noexcept;

}  // namespace cd::rhi::vulkan
