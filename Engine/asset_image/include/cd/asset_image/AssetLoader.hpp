// =============================================================================
// CHROMODYNAMIC — cd/asset_image/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_image. Tag:
// "image".
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset_image/Image.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace cd::asset_image
{

class ImageAsset final : public cd::asset::IAsset
{
public:
    explicit ImageAsset(Image i) noexcept : image_ { std::move(i) } {}
    [[nodiscard]] std::string_view tag() const noexcept override { return "image"; }
    [[nodiscard]] const Image& image() const noexcept { return image_; }
private:
    Image image_;
};

class ImageAssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override { return "image"; }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        // load_image_from_memory wants std::uint8_t; reinterpret because
        // std::byte → unsigned char round-trip is a well-defined no-op.
        const auto* u8 = reinterpret_cast<const std::uint8_t*>(bytes.data());
        auto r = cd::asset_image::load_image_from_memory(
            std::span<const std::uint8_t> { u8, bytes.size() }
        );
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> {
            std::make_unique<ImageAsset>(std::move(*r))
        };
    }
};

}  // namespace cd::asset_image
