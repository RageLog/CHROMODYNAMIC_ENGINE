// =============================================================================
// CHROMODYNAMIC — cd/asset/cdtex/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_cdtex.
// Tag: "cdtex". Forwards the byte buffer to `decode()`.
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset/cdtex/CdTex.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace cd::asset::cdtex
{

class CdTexAsset final : public cd::asset::IAsset
{
public:
    explicit CdTexAsset(CdTex c) noexcept
        : tex_ { std::move(c) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "cdtex";
    }

    [[nodiscard]] const CdTex& tex() const noexcept
    {
        return tex_;
    }

private:
    CdTex tex_;
};

class CdTexAssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "cdtex";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        const auto* u8 = reinterpret_cast<const std::uint8_t*>(bytes.data());
        auto r = cd::asset::cdtex::decode(u8, bytes.size());
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> { std::make_unique<CdTexAsset>(std::move(*r)) };
    }
};

}  // namespace cd::asset::cdtex
