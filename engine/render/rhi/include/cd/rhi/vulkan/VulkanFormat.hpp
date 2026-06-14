// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/VulkanFormat.hpp
// Vulkan V1 (depth-aware barrier fix) — testable format helpers.
//
// Kept free of <volk.h> / <vulkan/vulkan.h> (mirrors VulkanDevice.hpp policy):
// the aspect mask is returned as the raw VkImageAspectFlags value widened to
// std::uint32_t. Callers that already include volk can compare against the
// symbolic VK_IMAGE_ASPECT_* constants (they are the same underlying values).
// =============================================================================
#pragma once

#include <cd/rhi/Format.hpp>

#include <cstdint>

namespace cd::rhi::vulkan
{

/// Map a cd::rhi::Format to the VkImageAspectFlags value that must be used in
/// every image-subresource range / layers struct (barriers, copies, views).
///   * depth-only  (e.g. kD32Float)            -> VK_IMAGE_ASPECT_DEPTH_BIT
///   * depth+stencil (kD24UnormS8Uint, kD32FloatS8Uint)
///                                              -> DEPTH_BIT | STENCIL_BIT
///   * stencil-only (kS8Uint)                   -> VK_IMAGE_ASPECT_STENCIL_BIT
///   * everything else (colour)                 -> VK_IMAGE_ASPECT_COLOR_BIT
///
/// Returns the raw bit value (a VkImageAspectFlags is an alias of uint32_t) so
/// this declaration does not pull in the Vulkan headers. This is the single
/// source of truth shared by the command-buffer barrier/copy path and the
/// device-level readback path — the two must never drift.
[[nodiscard]] std::uint32_t vk_aspect_for_format(cd::rhi::Format f) noexcept;

}  // namespace cd::rhi::vulkan
