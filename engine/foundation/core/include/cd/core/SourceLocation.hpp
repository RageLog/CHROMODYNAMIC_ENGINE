// =============================================================================
// CHROMODYNAMIC — cd/core/SourceLocation.hpp
// ADR-005 §B + ADR-017 (DtForHil pattern salvage)
//
// Thin alias over <source_location> with helpers for logging/assert hot paths.
// Sprint S2.1.a (Foundation P0)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <source_location>
#include <string_view>

namespace cd::core
{

/// Engine-wide alias for C++20 std::source_location.
/// Captured implicitly at call sites via default-argument; cheap to copy.
using SourceLocation = std::source_location;

/// Default-construction-friendly capture helper.
/// Use when an API takes a SourceLocation parameter:
///   void log(std::string_view msg, SourceLocation loc = cd::core::here()) noexcept;
[[nodiscard]] consteval SourceLocation here(SourceLocation loc = SourceLocation::current()) noexcept
{
    return loc;
}

/// Extract just the file name (no path) from a SourceLocation.
[[nodiscard]] constexpr std::string_view file_name_only(const SourceLocation& loc) noexcept
{
    std::string_view full { loc.file_name() };
    if (const auto pos = full.find_last_of("/\\"); pos != std::string_view::npos)
    {
        return full.substr(pos + 1);
    }
    return full;
}

/// Compact one-line tag (e.g., "Result.hpp:42:in_function"). Useful for log prefixes.
struct CompactLocation
{
    std::string_view file;
    std::uint_least32_t line;
    std::string_view function;

    explicit constexpr CompactLocation(const SourceLocation& loc = SourceLocation::current()) noexcept
        : file { file_name_only(loc) }
        , line { loc.line() }
        , function { loc.function_name() }
    {
    }
};

}  // namespace cd::core
