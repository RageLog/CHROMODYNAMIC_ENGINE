// =============================================================================
// CHROMODYNAMIC — cd/vfs/IFileSource.hpp
// ADR-017 P4 (Sprint S2.9) — virtual filesystem source interface.
//
// Backing implementations:
//   * cd::vfs::MemorySource          — std::unordered_map<path, bytes>
//   * cd::vfs::FilesystemSource      — backed by std::filesystem
//   * (future) ArchiveSource          — backed by .pak / .zip
//
// Paths are forward-slash-separated, case-sensitive, relative-style strings
// (e.g. "shaders/standard.vert"). The VFS layer normalises before lookup.
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::vfs
{

namespace vfs_errors
{
inline constexpr std::uint32_t kDomain = 0x0008;

enum class Code : std::uint32_t
{
    kOk = 0,
    kNotFound = 1,
    kIoFailed = 2,
    kSourceClosed = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view msg = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), msg };
}
}  // namespace vfs_errors

class IFileSource
{
public:
    IFileSource() noexcept = default;
    virtual ~IFileSource() = default;
    IFileSource(const IFileSource&) = delete;
    IFileSource& operator=(const IFileSource&) = delete;
    IFileSource(IFileSource&&) = delete;
    IFileSource& operator=(IFileSource&&) = delete;

    [[nodiscard]] virtual bool exists(std::string_view path) const = 0;

    /// Read the whole file into a vector of bytes. Returns kNotFound or kIoFailed.
    [[nodiscard]] virtual cd::core::Result<std::vector<std::byte>> read(std::string_view path) const = 0;

    /// Enumerate paths under `prefix`. Optional — sources may return empty if
    /// listing is not supported (e.g., HTTP-backed).
    [[nodiscard]] virtual std::vector<std::string> list(std::string_view prefix) const
    {
        (void)prefix;
        return {};
    }

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

}  // namespace cd::vfs
