// =============================================================================
// CHROMODYNAMIC — cd/asset/AssetRegistry.hpp
// ADR-006 (Sprint S3.2) — central asset cache + loader dispatch.
//
// The registry composes a `cd::vfs::VirtualFileSystem` with a collection of
// `IAssetLoader` instances. Game code calls `load("tex2d", "shaders/x.vert")`
// and gets back a typed handle (`AssetId`) plus a cached `IAsset*`. Repeat
// requests for the same id are served from the cache.
//
// Thread-safety:
//   * `load` / `find` / `release` are synchronized by a shared_mutex.
//   * Loader registration is expected at engine init time (single-thread).
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/asset/IAssetLoader.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/vfs/VirtualFileSystem.hpp>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cd::asset
{

class AssetRegistry
{
public:
    explicit AssetRegistry(cd::vfs::VirtualFileSystem& vfs) noexcept
        : vfs_ { &vfs }
    {
    }

    /// Register a loader under its declared `tag()`. The registry owns the
    /// loader for its lifetime.
    void register_loader(std::unique_ptr<IAssetLoader> loader)
    {
        if (!loader)
            return;
        std::unique_lock guard { mutex_ };
        loaders_.insert_or_assign(std::string { loader->tag() }, std::move(loader));
    }

    /// Look up a previously-loaded asset; null if not in the cache.
    [[nodiscard]] IAsset* find(AssetId id) const
    {
        std::shared_lock guard { mutex_ };
        auto it = cache_.find(id);
        return it == cache_.end() ? nullptr : it->second.get();
    }

    /// Map a path's file extension to a registered loader tag.
    /// Returns empty string when no extension matches a known loader.
    /// Extensions checked (case-insensitive): .png/.jpg/.jpeg/.bmp/.tga/.hdr → "image",
    /// .obj → "obj", .ktx2 → "ktx2", .gltf/.glb → "gltf", .cdmesh → "cdmesh",
    /// .cdtex → "cdtex", .wav → "wav", .json → "json".
    [[nodiscard]] static std::string_view tag_from_extension(std::string_view path) noexcept
    {
        auto dot = path.rfind('.');
        if (dot == std::string_view::npos)
            return {};
        // Lowercased extension into a small buffer.
        std::string ext;
        ext.reserve(path.size() - dot);
        for (auto c : path.substr(dot + 1))
        {
            ext.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c));
        }
        if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga" || ext == "hdr")
            return "image";
        if (ext == "obj")
            return "obj";
        if (ext == "ktx2")
            return "ktx2";
        if (ext == "gltf" || ext == "glb")
            return "gltf";
        if (ext == "cdmesh")
            return "cdmesh";
        if (ext == "cdtex")
            return "cdtex";
        if (ext == "wav")
            return "wav";
        if (ext == "json")
            return "json";
        return {};
    }

    /// Auto-dispatch: derive the loader tag from the path extension and
    /// forward to `load(tag, path)`. Returns kNoLoaderForTag if the
    /// extension does not match any built-in mapping (caller can still
    /// use the explicit-tag overload for custom extensions).
    [[nodiscard]] cd::core::Result<AssetId> load_auto(std::string_view path)
    {
        const auto tag = tag_from_extension(path);
        if (tag.empty())
        {
            return std::unexpected(asset_errors::make(
                asset_errors::Code::kNoLoaderForTag,
                "load_auto: unknown extension"));
        }
        return load(tag, path);
    }

    /// Synchronous load: VFS read → loader.decode → cache. Idempotent.
    [[nodiscard]] cd::core::Result<AssetId> load(std::string_view tag, std::string_view path)
    {
        const auto id = AssetId::from_path(path);
        if (auto* hit = find(id); hit != nullptr)
        {
            return id;  // already cached
        }
        IAssetLoader* loader {};
        {
            std::shared_lock guard { mutex_ };
            auto it = loaders_.find(std::string { tag });
            if (it == loaders_.end())
            {
                return std::unexpected(asset_errors::make(asset_errors::Code::kNoLoaderForTag, "no loader registered"));
            }
            loader = it->second.get();
        }
        auto bytes = vfs_->read(path);
        if (!bytes.has_value())
        {
            return std::unexpected(asset_errors::make(asset_errors::Code::kVfsReadFailed, "vfs read failed"));
        }
        auto asset = loader->decode(*bytes, path);
        if (!asset.has_value())
        {
            return std::unexpected(asset.error());
        }
        {
            std::unique_lock guard { mutex_ };
            cache_.insert_or_assign(id, std::move(*asset));
        }
        return id;
    }

    /// Insert a pre-decoded asset directly (test fixtures, runtime-generated
    /// resources). Replaces any cached entry for the same id.
    void install(AssetId id, std::unique_ptr<IAsset> asset)
    {
        std::unique_lock guard { mutex_ };
        cache_.insert_or_assign(id, std::move(asset));
    }

    /// Drop a single cached asset.
    void release(AssetId id)
    {
        std::unique_lock guard { mutex_ };
        cache_.erase(id);
    }

    /// Drop everything.
    void clear()
    {
        std::unique_lock guard { mutex_ };
        cache_.clear();
    }

    [[nodiscard]] std::size_t loader_count() const
    {
        std::shared_lock guard { mutex_ };
        return loaders_.size();
    }

    [[nodiscard]] std::size_t cached_count() const
    {
        std::shared_lock guard { mutex_ };
        return cache_.size();
    }

private:
    cd::vfs::VirtualFileSystem* vfs_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<IAssetLoader>> loaders_;
    std::unordered_map<AssetId, std::unique_ptr<IAsset>> cache_;
};

}  // namespace cd::asset
