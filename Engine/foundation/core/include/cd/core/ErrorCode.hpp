// =============================================================================
// CHROMODYNAMIC — cd/core/ErrorCode.hpp
// ADR-005 §B Error policy + ADR-017 P0 (DtForHil Result port)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string_view>

/// @addtogroup cd_foundation_core
/// @{
namespace cd::core
{

/// Lightweight typed error code with optional domain + numeric code.
/// Designed to fit in a register (16-byte struct). For richer error payloads,
/// use std::expected<T, ErrorCode> from cd/core/Result.hpp.
///
/// Domain is a 32-bit ID owned by a subsystem. Convention:
///   0x0000_xxxx — cd::core
///   0x0001_xxxx — cd::mem
///   0x0002_xxxx — cd::concurrency
///   ...
/// Subsystems define their own enum class with `: std::uint32_t` underlying.
struct ErrorCode
{
    std::uint32_t domain = 0;
    std::uint32_t code = 0;
    std::string_view message {};

    constexpr ErrorCode() noexcept = default;

    constexpr ErrorCode(std::uint32_t d, std::uint32_t c) noexcept
        : domain(d)
        , code(c)
    {
    }

    constexpr ErrorCode(std::uint32_t d, std::uint32_t c, std::string_view m) noexcept
        : domain(d)
        , code(c)
        , message(m)
    {
    }

    CD_NODISCARD constexpr bool ok() const noexcept
    {
        return domain == 0 && code == 0;
    }

    CD_NODISCARD constexpr explicit operator bool() const noexcept
    {
        return !ok();
    }

    friend constexpr bool operator==(const ErrorCode& a, const ErrorCode& b) noexcept
    {
        return a.domain == b.domain && a.code == b.code;
    }
};

/// Canonical "no error" sentinel.
inline constexpr ErrorCode kNoError {};

// Core domain (0x0000) error codes
namespace core_errors
{
inline constexpr std::uint32_t kDomain = 0x0000;

enum class Code : std::uint32_t
{
    kOk = 0,
    kUnknown = 1,
    kInvalidArgument = 2,
    kOutOfRange = 3,
    kOutOfMemory = 4,
    kNotImplemented = 5,
    kPermissionDenied = 6,
    kNotFound = 7,
    kAlreadyExists = 8,
    kAborted = 9,
    kTimeout = 10,
};

constexpr ErrorCode make(Code c, std::string_view message = {}) noexcept
{
    return ErrorCode { kDomain, static_cast<std::uint32_t>(c), message };
}
}  // namespace core_errors

}  // namespace cd::core
/// @}
