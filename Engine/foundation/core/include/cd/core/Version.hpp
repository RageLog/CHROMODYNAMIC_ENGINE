// =============================================================================
// CHROMODYNAMIC — cd/core/Version.hpp
// ADR-014 §D — SemVer for engine; CalVer for asset (future)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string_view>

namespace cd::core
{

struct Version
{
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
    std::string_view tag {};  // e.g., "alpha", "rc1", ""

    CD_NODISCARD constexpr std::uint32_t packed() const noexcept
    {
        return (static_cast<std::uint32_t>(major) << 16) | (static_cast<std::uint32_t>(minor) << 8) |
               static_cast<std::uint32_t>(patch);
    }
};

/// Compile-time engine version (synchronized with top-level CMakeLists.txt).
inline constexpr Version kEngineVersion { 0, 1, 0, "" };

inline constexpr std::string_view kEngineName = "CHROMODYNAMIC";

namespace detail
{
/// ODR anchor for cd_core (keeps the TU live in the static archive). Returns
/// `kEngineName.data()`; primarily useful as a linkable symbol that bin
/// tools (nm, dumpbin) can confirm to validate a build artifact.
CD_CORE_API [[nodiscard]] const char* engine_name() noexcept;
}  // namespace detail

}  // namespace cd::core
