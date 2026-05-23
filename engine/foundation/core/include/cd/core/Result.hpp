// =============================================================================
// CHROMODYNAMIC — cd/core/Result.hpp
// ADR-005 §B + ADR-017 P0 (DtForHil port: dfh::common::error::Result)
//
// Wraps std::expected<T, ErrorCode> with a stable engine alias. When the host
// toolchain lacks <expected> (CD_HAS_STD_EXPECTED == 0), this header refuses to
// compile to surface the toolchain mismatch early; a tl::expected polyfill can
// be added behind CD_USE_TL_EXPECTED in a future sprint if needed.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>

#if !CD_HAS_STD_EXPECTED
    #error "CHROMODYNAMIC requires <expected>. Update libstdc++ >= 14 or libc++ >= 17."
#endif

#include <expected>
#include <utility>

/// @addtogroup cd_foundation_core
/// @{
namespace cd::core
{

/// Engine-wide Result alias. Use this on every fallible boundary.
///
/// Conventions:
///   - Hot path: return `Result<T>` instead of throwing.
///   - Sentinel: `Result<void>` is permitted; `return {};` on success.
///   - Compose: `value_or`, `transform`, `and_then` (C++23 monadic).
///
/// Example:
///   Result<Texture> load_texture(std::string_view path) {
///     if (path.empty()) return std::unexpected(core_errors::make(
///         core_errors::Code::kInvalidArgument, "empty path"));
///     ...
///     return Texture{...};
///   }
template <class T>
using Result = std::expected<T, ErrorCode>;

/// Convenience: construct an unexpected ErrorCode from a core_errors::Code.
[[nodiscard]] inline auto fail(core_errors::Code c, std::string_view message = {}) noexcept
{
    return std::unexpected(core_errors::make(c, message));
}

/// Convenience: construct an unexpected ErrorCode from any domain/code pair.
[[nodiscard]] inline auto fail(std::uint32_t domain, std::uint32_t code, std::string_view message = {}) noexcept
{
    return std::unexpected(ErrorCode { domain, code, message });
}

}  // namespace cd::core
/// @}
