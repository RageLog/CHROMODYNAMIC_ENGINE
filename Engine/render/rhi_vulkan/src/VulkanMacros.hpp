// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanMacros.hpp (private)
//
// Vulkan headers expose API-version constants and pack-version helpers as
// C macros that expand to C-style casts. Including those expansions in our
// translation units would trip `-Wold-style-cast`, so we re-implement them
// here as inline constexpr helpers that use static_cast. Vulkan's own header
// remains the source of truth for the bit layout (variant:3, major:7,
// minor:10, patch:12), which has been stable since Vulkan 1.0.
// =============================================================================
#pragma once

#include <cstdint>

namespace cd::rhi_vulkan::vkv
{

[[nodiscard]] constexpr std::uint32_t
make_version(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) noexcept
{
    return (major << 22U) | (minor << 12U) | patch;
}

[[nodiscard]] constexpr std::uint32_t
make_api_version(std::uint32_t variant, std::uint32_t major, std::uint32_t minor, std::uint32_t patch) noexcept
{
    return (variant << 29U) | (major << 22U) | (minor << 12U) | patch;
}

inline constexpr std::uint32_t kApiVersion10 = make_api_version(0, 1, 0, 0);
inline constexpr std::uint32_t kApiVersion11 = make_api_version(0, 1, 1, 0);
inline constexpr std::uint32_t kApiVersion12 = make_api_version(0, 1, 2, 0);
inline constexpr std::uint32_t kApiVersion13 = make_api_version(0, 1, 3, 0);

}  // namespace cd::rhi_vulkan::vkv
