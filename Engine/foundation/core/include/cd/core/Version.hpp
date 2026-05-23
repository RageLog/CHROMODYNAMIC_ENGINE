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

// Engine version stamped from CMake `project(... VERSION X.Y.Z ...)`. The
// build wires `CD_ENGINE_VERSION_MAJOR/MINOR/PATCH` as PUBLIC compile
// definitions on cd_core (see Engine/foundation/core/CMakeLists.txt) so
// every translation unit that includes this header sees the same numbers
// the CMake project carries. Fallback defaults below match the v0.0.0
// "unconfigured" pattern — if the build forgot to set the macros, the
// engine reports a clearly-wrong version instead of pretending to be
// 0.1.0 (the previous hard-coded value, which drifted from PROJECT_VERSION
// repeatedly during the marathon).
#ifndef CD_ENGINE_VERSION_MAJOR
    #define CD_ENGINE_VERSION_MAJOR 0
#endif
#ifndef CD_ENGINE_VERSION_MINOR
    #define CD_ENGINE_VERSION_MINOR 0
#endif
#ifndef CD_ENGINE_VERSION_PATCH
    #define CD_ENGINE_VERSION_PATCH 0
#endif

/// Compile-time engine version. Synchronized with the top-level CMake
/// project(... VERSION ...) declaration at configure time.
inline constexpr Version kEngineVersion {
    CD_ENGINE_VERSION_MAJOR,
    CD_ENGINE_VERSION_MINOR,
    CD_ENGINE_VERSION_PATCH,
    ""
};

inline constexpr std::string_view kEngineName = "CHROMODYNAMIC";

namespace detail
{
/// ODR anchor for cd_core (keeps the TU live in the static archive). Returns
/// `kEngineName.data()`; primarily useful as a linkable symbol that bin
/// tools (nm, dumpbin) can confirm to validate a build artifact.
CD_CORE_API [[nodiscard]] const char* engine_name() noexcept;
}  // namespace detail

}  // namespace cd::core
