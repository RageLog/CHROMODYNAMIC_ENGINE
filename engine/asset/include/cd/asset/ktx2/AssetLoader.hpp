// =============================================================================
// CHROMODYNAMIC — cd/asset/ktx2/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_ktx2. Tag:
// "ktx2".
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset/ktx2/Ktx2.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace cd::asset::ktx2
{

class Ktx2Asset final : public cd::asset::IAsset
{
public:
    explicit Ktx2Asset(Ktx2 k) noexcept
        : ktx_ { std::move(k) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "ktx2";
    }

    [[nodiscard]] const Ktx2& ktx2() const noexcept
    {
        return ktx_;
    }

private:
    Ktx2 ktx_;
};

class Ktx2AssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "ktx2";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        const auto* u8 = reinterpret_cast<const std::uint8_t*>(bytes.data());
        auto r = cd::asset::ktx2::load_from_memory(std::span<const std::uint8_t> { u8, bytes.size() });
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> { std::make_unique<Ktx2Asset>(std::move(*r)) };
    }
};

}  // namespace cd::asset::ktx2
