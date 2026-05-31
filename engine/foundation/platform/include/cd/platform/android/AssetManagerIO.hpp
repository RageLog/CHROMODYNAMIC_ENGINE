// =============================================================================
// CHROMODYNAMIC — cd/platform/android/AssetManagerIO.hpp
// Phase 532 — Android AAssetManager I/O wrapper.
//
// Thin C++ wrapper around Android NDK <android/asset_manager.h>.
// Provides async I/O interface for asset loading (glTF, shaders, textures)
// from the APK's assets/ folder via cd::asset::Pipeline IAssetLoader plugin.
//
// Ownership: ANativeActivity::assetManager is external; we cache the pointer
// at app init and hold non-owning observer references.
//
// Error handling: Returns cd::core::Result<std::vector<uint8_t>> on all reads.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

// Forward declare Android types to avoid dependency on <android/asset_manager.h>
// in the public header. Implementation includes the real headers.
struct AAssetManager;

namespace cd::platform::android
{

// ---- Error domain -----------------------------------------------------------

namespace asset_manager_errors
{
inline constexpr std::uint32_t kDomain = 0x0012;

enum class Code : std::uint32_t
{
    kOk = 0,
    kInvalidArgument = 1,
    kAssetManagerNull = 2,
    kAssetNotFound = 3,
    kReadFailed = 4,
    kAllocFailed = 5,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace asset_manager_errors

// ---- Asset Manager wrapper --------------------------------------------------

/// Thin wrapper around Android NDK AAssetManager for synchronous asset reads.
/// Phase 1: synchronous I/O only (thread pool integration Phase 2).
class AssetManagerIO
{
public:
    /// Initialize with a pointer to ANativeActivity::assetManager.
    /// Returns error if manager is null.
    [[nodiscard]] static cd::core::Result<AssetManagerIO> create(AAssetManager* manager) noexcept;

    /// Non-owning; initialization happens at app startup.
    AssetManagerIO() noexcept = default;

    AssetManagerIO(const AssetManagerIO&) = delete;
    AssetManagerIO& operator=(const AssetManagerIO&) = delete;

    AssetManagerIO(AssetManagerIO&&) = default;
    AssetManagerIO& operator=(AssetManagerIO&&) = default;

    /// Synchronously read asset from assets/ folder.
    /// Path is relative to assets/ root (e.g., "shaders/pbr.spv").
    /// Returns entire file contents as byte vector.
    [[nodiscard]] cd::core::Result<std::vector<uint8_t>> read_asset(std::string_view path) const noexcept;

    /// Check if asset exists without reading.
    [[nodiscard]] bool asset_exists(std::string_view path) const noexcept;

    /// Asset file size in bytes. Returns 0 if not found.
    [[nodiscard]] std::uint32_t asset_size(std::string_view path) const noexcept;

    /// Query whether manager is valid (non-null).
    [[nodiscard]] bool is_valid() const noexcept { return manager_ != nullptr; }

private:
    explicit AssetManagerIO(AAssetManager* manager) noexcept : manager_(manager) {}

    AAssetManager* manager_ = nullptr;
};

}  // namespace cd::platform::android
