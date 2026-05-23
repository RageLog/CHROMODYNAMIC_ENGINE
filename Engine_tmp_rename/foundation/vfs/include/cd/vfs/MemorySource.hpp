// =============================================================================
// CHROMODYNAMIC — cd/vfs/MemorySource.hpp
// ADR-017 P4 (Sprint S2.9) — in-memory file source (test fixtures, embedded
// assets, hot-patched overrides).
// =============================================================================
#pragma once

#include <cd/vfs/IFileSource.hpp>

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
        files_.insert_or_assign(std::move(path), std::move(bytes));
    }

    /// Convenience for tests / small embedded blobs.
    void put_text(std::string path, std::string_view text)
    {
        std::vector<std::byte> b(text.size());
        if (!text.empty())
            std::memcpy(b.data(), text.data(), text.size());
        files_.insert_or_assign(std::move(path), std::move(b));
    }

    void erase(std::string_view path)
    {
        files_.erase(std::string { path });
    }

    [[nodiscard]] bool exists(std::string_view path) const override
    {
        return files_.find(std::string { path }) != files_.end();
    }

    [[nodiscard]] cd::core::Result<std::vector<std::byte>> read(std::string_view path) const override
    {
        auto it = files_.find(std::string { path });
        if (it == files_.end())
        {
            return std::unexpected(vfs_errors::make(vfs_errors::Code::kNotFound, "memory: path not found"));
        }
        return it->second;
    }

    [[nodiscard]] std::vector<std::string> list(std::string_view prefix) const override
    {
        std::vector<std::string> out;
        out.reserve(files_.size());
        for (const auto& [p, _] : files_)
        {
            if (prefix.empty() || (p.size() >= prefix.size() && std::string_view { p.data(), prefix.size() } == prefix))
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
