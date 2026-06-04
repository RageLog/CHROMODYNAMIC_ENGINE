// =============================================================================
// CHROMODYNAMIC — cd/core/Bytes.hpp
// Phase 68.A / Wave 236 — byte-size human format + multipliers.
//
// `format_bytes(n)` returns a short human-readable string (e.g. "1.5 MB")
// for editor labels, ProfilerView memory bars, asset cache UI.
//
// Constants `kKB`, `kMB`, `kGB`, `kTB` are binary multipliers (1024-
// based). Use `kKBd` / `kMBd` / ... for decimal (1000-based) when you
// want disk-vendor convention.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace cd::core
{

inline constexpr std::uint64_t kKB = 1024ULL;
inline constexpr std::uint64_t kMB = 1024ULL * 1024ULL;
inline constexpr std::uint64_t kGB = 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kTB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;

inline constexpr std::uint64_t kKBd = 1000ULL;
inline constexpr std::uint64_t kMBd = 1000ULL * 1000ULL;
inline constexpr std::uint64_t kGBd = 1000ULL * 1000ULL * 1000ULL;

[[nodiscard]] inline std::string format_bytes(std::uint64_t n) noexcept
{
    auto two_decimal = [](double d) noexcept
    {
        char buf[32] {};
        const auto v = std::lround(d * 100.0);
        const auto whole = v / 100;
        const auto frac  = v % 100;
        std::snprintf(buf, sizeof(buf), "%ld.%02ld", whole, frac);
        return std::string { buf };
    };
    if (n >= kTB) return two_decimal(static_cast<double>(n) / static_cast<double>(kTB)) + " TB";
    if (n >= kGB) return two_decimal(static_cast<double>(n) / static_cast<double>(kGB)) + " GB";
    if (n >= kMB) return two_decimal(static_cast<double>(n) / static_cast<double>(kMB)) + " MB";
    if (n >= kKB) return two_decimal(static_cast<double>(n) / static_cast<double>(kKB)) + " KB";
    return std::to_string(n) + " B";
}

}  // namespace cd::core
