// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanDevice.hpp
// ADR-001 (Sprint S3.3) — Vulkan backend for cd::rhi.
//
// Only the factory and a tiny VulkanInstance configuration struct are
// publicly visible. The concrete VulkanDevice class is hidden in the .cpp
// to keep this header free of <vulkan/vulkan.h>.
//
// Use:
//   auto dev = cd::rhi_vulkan::create_vulkan_device({.app_name = "demo"});
//   if (dev.has_value()) { (*dev)->wait_idle(); }
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cd::rhi_vulkan
{

struct VulkanCreateInfo
{
    std::string app_name { "CHROMODYNAMIC" };
    std::uint32_t app_version { 0 };
    bool enable_validation { true };
    /// Optional explicit instance / device extension lists. Validation layer
    /// extensions (VK_EXT_debug_utils) are added automatically when validation
    /// is enabled.
    std::vector<std::string> instance_extensions {};
    std::vector<std::string> device_extensions {};
    /// If true, prefer a discrete GPU when more than one device is available.
    bool prefer_discrete_gpu { true };
};

/// Construct a Vulkan-backed IDevice. Returns an error when:
///   * the Vulkan loader (volk) cannot find any ICD on the system,
///   * `vkCreateInstance` fails (driver mismatch, headless CI),
///   * no physical device satisfies the queue requirements.
///
/// The returned device is fully owned by the caller. wait_idle() is called on
/// destruction.
[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>> create_vulkan_device(VulkanCreateInfo info = {});

}  // namespace cd::rhi_vulkan
