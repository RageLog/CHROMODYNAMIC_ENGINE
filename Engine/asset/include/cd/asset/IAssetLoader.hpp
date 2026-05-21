// =============================================================================
// CHROMODYNAMIC — cd/asset/IAssetLoader.hpp
// ADR-006 (Sprint S3.2) — pluggable per-type asset loader interface.
//
// A loader maps raw bytes (from the VFS) to a typed asset object. Loaders
// register themselves with the `AssetRegistry` against a short string tag
// ("tex2d", "shader", "mesh", "scene"). The registry picks the loader by tag
// at load time; multi-codec dispatch (e.g. PNG vs KTX2 under "tex2d") happens
// inside the loader.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::asset
{

namespace asset_errors
{
inline constexpr std::uint32_t kDomain = 0x000A;

enum class Code : std::uint32_t
{
    kOk = 0,
    kNoLoaderForTag = 1,
    kDecodeFailed = 2,
    kVfsReadFailed = 3,
    kAlreadyLoaded = 4,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace asset_errors

/// Type-erased decoded asset. Each concrete loader returns a unique_ptr to a
/// subclass; callers downcast based on the loader tag they requested.
class IAsset
{
public:
    IAsset() noexcept = default;
    virtual ~IAsset() = default;
    IAsset(const IAsset&) = delete;
    IAsset& operator=(const IAsset&) = delete;
    IAsset(IAsset&&) = delete;
    IAsset& operator=(IAsset&&) = delete;

    [[nodiscard]] virtual std::string_view tag() const noexcept = 0;
};

class IAssetLoader
{
public:
    IAssetLoader() noexcept = default;
    virtual ~IAssetLoader() = default;
    IAssetLoader(const IAssetLoader&) = delete;
    IAssetLoader& operator=(const IAssetLoader&) = delete;
    IAssetLoader(IAssetLoader&&) = delete;
    IAssetLoader& operator=(IAssetLoader&&) = delete;

    [[nodiscard]] virtual std::string_view tag() const noexcept = 0;

    /// Decode `bytes` into a typed asset. `path_hint` is the source path (for
    /// diagnostics only).
    [[nodiscard]] virtual cd::core::Result<std::unique_ptr<IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view path_hint) = 0;
};

// ---- Built-in: text + raw bytes -------------------------------------------

class TextAsset final : public IAsset
{
public:
    explicit TextAsset(std::string text) noexcept
        : text_ { std::move(text) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "text";
    }

    [[nodiscard]] const std::string& text() const noexcept
    {
        return text_;
    }

private:
    std::string text_;
};

class TextAssetLoader final : public IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "text";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        std::string s;
        s.resize(bytes.size());
        if (!bytes.empty())
        {
            std::memcpy(s.data(), bytes.data(), bytes.size());
        }
        return std::unique_ptr<IAsset> { std::make_unique<TextAsset>(std::move(s)) };
    }
};

class BytesAsset final : public IAsset
{
public:
    explicit BytesAsset(std::vector<std::byte> b) noexcept
        : bytes_ { std::move(b) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "bytes";
    }

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept
    {
        return bytes_;
    }

private:
    std::vector<std::byte> bytes_;
};

class BytesAssetLoader final : public IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "bytes";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        return std::unique_ptr<IAsset> { std::make_unique<BytesAsset>(std::vector<std::byte> { bytes.begin(),
                                                                                               bytes.end() }) };
    }
};

}  // namespace cd::asset
