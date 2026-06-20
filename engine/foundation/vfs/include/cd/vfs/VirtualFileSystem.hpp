// =============================================================================
// CHROMODYNAMIC — cd/vfs/VirtualFileSystem.hpp
// ADR-017 P4 (Sprint S2.9) — composite filesystem with overlay layers.
//
// Layers are queried in mount order: the FIRST source that has the path wins.
// This lets callers stack: [mod_pack] → [user_overrides] → [base_assets], so
// a mod can override a base asset without touching it on disk.
// =============================================================================
#pragma once

#include <cd/vfs/IFileSource.hpp>
#include <cd/vfs/PathUtil.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cd::vfs
{

class VirtualFileSystem
{
public:
    VirtualFileSystem() = default;
    ~VirtualFileSystem() = default;
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;
    VirtualFileSystem(VirtualFileSystem&&) noexcept = default;
    VirtualFileSystem& operator=(VirtualFileSystem&&) noexcept = default;

    /// Push a layer onto the *front* (highest priority).
    void mount_front(std::shared_ptr<IFileSource> source)
    {
        layers_.insert(layers_.begin(), std::move(source));
    }

    /// Push a layer onto the *back* (lowest priority — typical for base assets).
    void mount_back(std::shared_ptr<IFileSource> source)
    {
        layers_.push_back(std::move(source));
    }

    /// Remove the first layer whose name() equals `source_name`.
    /// Returns true if a layer was removed, false if no matching layer existed.
    bool unmount(std::string_view source_name)
    {
        auto it = std::ranges::find_if(layers_,
            [source_name](const auto& l) { return l->name() == source_name; });
        if (it == layers_.end())
            return false;
        layers_.erase(it);
        return true;
    }

    [[nodiscard]] bool exists(std::string_view path) const
    {
        const std::string norm = normalize_path(path);
        return std::ranges::any_of(
            layers_, [&](const auto& l) { return l->exists(norm); });
    }

    [[nodiscard]] cd::core::Result<std::vector<std::byte>> read(std::string_view path) const
    {
        const std::string norm = normalize_path(path);
        for (const auto& l : layers_)
        {
            if (l->exists(norm))
                return l->read(norm);
        }
        return std::unexpected(vfs_errors::make(vfs_errors::Code::kNotFound, "vfs: no layer has path"));
    }

    /// Return the name of the layer that would serve `path`, or empty string.
    [[nodiscard]] std::string_view resolving_layer(std::string_view path) const
    {
        const std::string norm = normalize_path(path);
        for (const auto& l : layers_)
        {
            if (l->exists(norm))
                return l->name();
        }
        return {};
    }

    /// Union of all layers' listings (de-duplicated).
    [[nodiscard]] std::vector<std::string> list(std::string_view prefix) const
    {
        // Normalise but preserve a trailing slash so directory-prefix filtering
        // (e.g. "shaders/") is not widened to match "shaders_extra/…".
        std::string norm_prefix = normalize_path(prefix);
        const bool had_sep = !prefix.empty() && (prefix.back() == '/' || prefix.back() == '\\');
        if (had_sep && !norm_prefix.empty())
            norm_prefix += '/';

        std::unordered_set<std::string> set;
        for (const auto& l : layers_)
        {
            for (auto& p : l->list(norm_prefix))
                set.insert(std::move(p));
        }
        return { set.begin(), set.end() };
    }

    [[nodiscard]] std::size_t layer_count() const noexcept
    {
        return layers_.size();
    }

private:
    std::vector<std::shared_ptr<IFileSource>> layers_;
};

}  // namespace cd::vfs
