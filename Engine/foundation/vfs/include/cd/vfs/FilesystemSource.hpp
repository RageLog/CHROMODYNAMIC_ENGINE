// =============================================================================
// CHROMODYNAMIC — cd/vfs/FilesystemSource.hpp
// ADR-017 P4 (Sprint S2.9) — std::filesystem-backed file source.
// =============================================================================
#pragma once

#include <cd/vfs/IFileSource.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cd::vfs
{

class FilesystemSource final : public IFileSource
{
public:
    explicit FilesystemSource(std::filesystem::path root, std::string name = "fs");

    [[nodiscard]] bool exists(std::string_view path) const override;
    [[nodiscard]] cd::core::Result<std::vector<std::byte>> read(std::string_view path) const override;
    [[nodiscard]] std::vector<std::string> list(std::string_view prefix) const override;

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return name_;
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept
    {
        return root_;
    }

private:
    std::filesystem::path resolve(std::string_view path) const;

    std::filesystem::path root_;
    std::string name_;
};

}  // namespace cd::vfs
