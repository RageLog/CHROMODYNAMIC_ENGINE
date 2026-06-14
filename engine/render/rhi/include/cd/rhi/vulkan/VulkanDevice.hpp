// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/VulkanDevice.hpp
// ADR-001 (Sprint S3.3) — Vulkan backend for cd::rhi.
//
// Only the factory and a tiny VulkanInstance configuration struct are
// publicly visible. The concrete VulkanDevice class is hidden in the .cpp
// to keep this header free of <vulkan/vulkan.h>.
//
// Use:
//   auto dev = cd::rhi::vulkan::create_vulkan_device({.app_name = "demo"});
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

namespace cd::rhi::vulkan
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

/// Vulkan V1 (depth-aware barrier fix) — in-process validation-error net.
///
/// The default debug messenger (installed when `enable_validation` is true)
/// counts every ERROR-severity validation message it receives, process-wide.
/// A wrong image-subresource aspect on a depth image is a validation VUID, not
/// a device-lost; before this counter the only signal was an out-of-band stderr
/// print that no test assertion could observe. Tests call
/// `reset_validation_error_count()`, exercise a real wiring path, `wait_idle()`,
/// then assert `validation_error_count() == 0`.
///
/// Thread-safe (backed by a relaxed atomic). The count is global, not
/// per-device, because the messenger callback has no device context.
[[nodiscard]] std::uint32_t validation_error_count() noexcept;

/// Reset the process-wide validation-error counter to zero. Call this at the
/// start of a test that asserts a wiring path emits no validation errors.
void reset_validation_error_count() noexcept;

/// Vulkan V2 (multi-queue model) — read-only view of the queue families a
/// device selected. Family indices may legitimately ALIAS (overlap): on a GPU
/// exposing a single graphics+compute+transfer family, all indices are equal
/// and `*_dedicated` is false. The smoke test asserts graphics is valid and
/// compute/transfer are either dedicated or aliased to graphics.
struct QueueFamilyInfo
{
    std::uint32_t graphics { 0 };
    std::uint32_t compute { 0 };
    std::uint32_t transfer { 0 };
    std::uint32_t present { 0 };
    bool compute_dedicated { false };
    bool transfer_dedicated { false };
};

/// Fill `out` with the queue-family selection of a Vulkan-backed device.
/// Returns false (out untouched) when `dev` is not a Vulkan device. Internal
/// introspection for tests/tooling — the IDevice interface stays queue-agnostic.
[[nodiscard]] bool query_queue_families(cd::rhi::IDevice& dev, QueueFamilyInfo& out) noexcept;

/// True when VK_LAYER_KHRONOS_validation is discoverable by the Vulkan loader,
/// i.e. when `enable_validation` actually installs a debug messenger. The
/// validation-error counter only catches VUIDs while this is true, so a test
/// that relies on it must SKIP (not pass) when this returns false — otherwise a
/// host without the layer reports a meaningless green. Bootstraps volk if
/// needed; returns false when no ICD/loader is present.
[[nodiscard]] bool validation_layer_available() noexcept;

}  // namespace cd::rhi::vulkan
