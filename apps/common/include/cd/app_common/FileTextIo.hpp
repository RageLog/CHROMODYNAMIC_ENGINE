// =============================================================================
// CHROMODYNAMIC — cd/app_common/FileTextIo.hpp
//
// Canonical UTF-8 BOM-safe text file reading and atomic directory-creating
// text/binary file writing shared across cd_editor, cd_player, and tools.
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::app_common
{

namespace io_errors
{
inline constexpr std::uint32_t kDomain = 0x0A10U;

enum class Code : std::uint32_t
{
    kOk            = 0U,
    kOpenFailed    = 1U,
    kWriteFailed   = 2U,
    kInvalidPath   = 3U,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view msg = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), msg };
}
}  // namespace io_errors

/// Strip leading UTF-8 Byte Order Mark (0xEF 0xBB 0xBF) if present.
[[nodiscard]] std::string_view strip_utf8_bom_view(std::string_view text) noexcept;

/// Read a UTF-8 text file from disk and strip any leading UTF-8 BOM.
[[nodiscard]] cd::core::Result<std::string> read_text_file_utf8(std::string_view path);

/// Write a UTF-8 text file to disk (creating parent directories if needed).
[[nodiscard]] cd::core::Result<void> write_text_file_utf8(
    std::string_view path,
    std::string_view content);

/// Read raw binary bytes from disk.
[[nodiscard]] cd::core::Result<std::vector<std::uint8_t>> read_binary_file(std::string_view path);

/// Write raw binary bytes to disk (creating parent directories if needed).
[[nodiscard]] cd::core::Result<void> write_binary_file(
    std::string_view            path,
    std::span<const std::byte>  bytes);

}  // namespace cd::app_common
