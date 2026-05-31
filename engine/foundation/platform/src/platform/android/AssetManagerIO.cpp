// =============================================================================
// CHROMODYNAMIC — platform/android/AssetManagerIO.cpp
// =============================================================================
#include <cd/platform/android/AssetManagerIO.hpp>

#include <android/asset_manager.h>

namespace cd::platform::android
{

cd::core::Result<AssetManagerIO> AssetManagerIO::create(AAssetManager* manager) noexcept
{
    if (manager == nullptr)
    {
        return asset_manager_errors::make(asset_manager_errors::Code::kAssetManagerNull,
                                           "ANativeActivity::assetManager is null");
    }
    return AssetManagerIO(manager);
}

cd::core::Result<std::vector<uint8_t>> AssetManagerIO::read_asset(std::string_view path) const noexcept
{
    if (!is_valid())
    {
        return asset_manager_errors::make(asset_manager_errors::Code::kAssetManagerNull,
                                           "Asset manager not initialized");
    }

    if (path.empty())
    {
        return asset_manager_errors::make(asset_manager_errors::Code::kInvalidArgument,
                                           "Asset path is empty");
    }

    // Phase 532: synchronous read. Phase 2: async via worker thread pool.
    AAsset* asset = AAssetManager_open(manager_, path.data(), AASSET_MODE_BUFFER);
    if (asset == nullptr)
    {
        return asset_manager_errors::make(asset_manager_errors::Code::kAssetNotFound,
                                           "Asset not found");
    }

    off_t size = AAsset_getLength(asset);
    if (size < 0)
    {
        AAsset_close(asset);
        return asset_manager_errors::make(asset_manager_errors::Code::kReadFailed,
                                           "Failed to query asset size");
    }

    std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
    int read_bytes = AAsset_read(asset, buffer.data(), static_cast<int>(size));
    AAsset_close(asset);

    if (read_bytes != size)
    {
        return asset_manager_errors::make(asset_manager_errors::Code::kReadFailed,
                                           "Asset read incomplete");
    }

    return buffer;
}

bool AssetManagerIO::asset_exists(std::string_view path) const noexcept
{
    if (!is_valid() || path.empty())
    {
        return false;
    }

    AAsset* asset = AAssetManager_open(manager_, path.data(), AASSET_MODE_BUFFER);
    if (asset == nullptr)
    {
        return false;
    }
    AAsset_close(asset);
    return true;
}

std::uint32_t AssetManagerIO::asset_size(std::string_view path) const noexcept
{
    if (!is_valid() || path.empty())
    {
        return 0;
    }

    AAsset* asset = AAssetManager_open(manager_, path.data(), AASSET_MODE_BUFFER);
    if (asset == nullptr)
    {
        return 0;
    }

    off_t size = AAsset_getLength(asset);
    AAsset_close(asset);

    return size >= 0 ? static_cast<std::uint32_t>(size) : 0;
}

}  // namespace cd::platform::android
