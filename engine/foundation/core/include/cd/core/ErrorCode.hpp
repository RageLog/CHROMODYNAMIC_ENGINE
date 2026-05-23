// =============================================================================
// CHROMODYNAMIC — cd/core/ErrorCode.hpp
// ADR-005 §B Error policy + ADR-017 P0 (DtForHil Result port)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

/// @addtogroup cd_foundation_core
/// @{
namespace cd::core
{

/// Typed error code with optional domain + numeric code + diagnostic message.
///
/// Two construction modes for the message:
///   * **Non-owning** (constexpr ctors below). The `message` `string_view`
///     refers to a literal or to memory the caller guarantees outlives
///     the ErrorCode. Zero allocation. Use when the message is a string
///     literal or a static buffer.
///   * **Owning** (`make_owning` factory). Use when the message is built
///     at run time (e.g. concatenated diagnostic from a compiler / loader).
///     The factory allocates an owning `std::shared_ptr<const std::string>`
///     so the `message` `string_view` is always backed by storage that
///     lives as long as the ErrorCode itself. Avoid in tight loops.
///
/// Designed to round-trip through `std::expected<T, ErrorCode>` without
/// dangling. The owning storage adds one pointer (16 bytes on 64-bit) to
/// the struct but lets us return rich diagnostics from arbitrary
/// fallible boundaries without bug-prone lifetime contracts.
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
    /// Owning back-store for the message. Non-null only when constructed
    /// via `make_owning`. The `message` view is set to refer to `*owned`.
    /// Copying the ErrorCode shares the storage via shared_ptr refcount.
    std::shared_ptr<const std::string> owned {};

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

    /// Factory for owning-message variant. The message is moved into a
    /// shared_ptr and the `message` view points at that storage, so the
    /// view stays valid for the full lifetime of every copy of the
    /// returned ErrorCode.
    [[nodiscard]] static ErrorCode make_owning(std::uint32_t d, std::uint32_t c, std::string m)
    {
        ErrorCode ec;
        ec.domain = d;
        ec.code = c;
        ec.owned = std::make_shared<const std::string>(std::move(m));
        ec.message = std::string_view { *ec.owned };
        return ec;
    }

    /// Re-tag an upstream ErrorCode with a new (domain, code) while
    /// preserving its message storage. If the upstream owns its message
    /// (via `make_owning`), the returned ErrorCode shares that owning
    /// `shared_ptr` (refcount bump, no copy). If the upstream's message
    /// is a non-owning view, this just forwards the view — caller is
    /// responsible for ensuring its lifetime.
    ///
    /// Use at library boundaries when you want to translate an error
    /// code domain without losing the original diagnostic text — for
    /// example: `material_errors::make` wrapping a `shader_errors`
    /// failure with the full glslang infoLog intact.
    [[nodiscard]] static ErrorCode rewrap(std::uint32_t d, std::uint32_t c, const ErrorCode& src) noexcept
    {
        ErrorCode ec;
        ec.domain = d;
        ec.code = c;
        if (src.owned)
        {
            ec.owned = src.owned;
            ec.message = std::string_view { *ec.owned };
        }
        else
        {
            ec.message = src.message;
        }
        return ec;
    }

    CD_NODISCARD constexpr bool ok() const noexcept
    {
        return domain == 0 && code == 0;
    }

    CD_NODISCARD constexpr explicit operator bool() const noexcept
    {
        return !ok();
    }

    friend bool operator==(const ErrorCode& a, const ErrorCode& b) noexcept
    {
        return a.domain == b.domain && a.code == b.code;
    }
};

/// Canonical "no error" sentinel.
inline const ErrorCode kNoError {};

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
