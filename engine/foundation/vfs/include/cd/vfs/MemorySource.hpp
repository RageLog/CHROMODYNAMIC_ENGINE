// =============================================================================
// CHROMODYNAMIC — cd/vfs/MemorySource.hpp
// ADR-017 P4 (Sprint S2.9) — in-memory file source (test fixtures, embedded
// assets, hot-patched overrides).
// =============================================================================
#pragma once

#include <cd/vfs/IFileSource.hpp>
#include <cd/vfs/PathUtil.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::vfs
{

class MemorySource final : public IFileSource
{
public:
    explicit MemorySource(std::string name = "memory")
        : name_ { std::move(name) }
    {
    }

    void put(std::string path, std::vector<std::byte> bytes)
    {
        files_.insert_or_assign(normalize_path(path), std::move(bytes));
    }

    /// Convenience for tests / small embedded blobs.
    void put_text(std::string path, std::string_view text)
    {
        std::vector<std::byte> b(text.size());
        if (!text.empty())
            std::memcpy(b.data(), text.data(), text.size());
        files_.insert_or_assign(normalize_path(path), std::move(b));
    }

    void erase(std::string_view path)
    {
        files_.erase(normalize_path(path));
    }

    [[nodiscard]] bool exists(std::string_view path) const override
    {
        return files_.contains(normalize_path(path));
    }

    [[nodiscard]] cd::core::Result<std::vector<std::byte>> read(std::string_view path) const override
    {
        const auto it = files_.find(normalize_path(path));
        if (it == files_.end())
        {
            return std::unexpected(vfs_errors::make(vfs_errors::Code::kNotFound, "memory: path not found"));
        }
        return it->second;
    }

    [[nodiscard]] std::vector<std::string> list(std::string_view prefix) const override
    {
        // Normalise but preserve a trailing slash so "shaders/" stays distinct
        // from a path that merely starts with "shaders".
        std::string norm_prefix = normalize_path(prefix);
        const bool had_sep = !prefix.empty() && (prefix.back() == '/' || prefix.back() == '\\');
        if (had_sep && !norm_prefix.empty())
            norm_prefix += '/';

        std::vector<std::string> out;
        out.reserve(files_.size());
        for (const auto& [p, ignored_val] : files_)
        {
            if (norm_prefix.empty() ||
                (p.size() >= norm_prefix.size() &&
                 std::string_view { p.data(), norm_prefix.size() } == norm_prefix))
            {
                out.push_back(p);
            }
        }
        return out;
    }

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return name_;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return files_.size();
    }

private:
    std::string name_;
    std::unordered_map<std::string, std::vector<std::byte>> files_;
};

}  // namespace cd::vfs
