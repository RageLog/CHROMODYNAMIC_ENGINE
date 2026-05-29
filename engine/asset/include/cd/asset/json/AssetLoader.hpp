// =============================================================================
// CHROMODYNAMIC — cd/asset/json/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_json. Lets
// the central `cd::asset::AssetRegistry` resolve a JSON file via the VFS
// and cache the parsed Value by AssetId.
//
// Including this header pulls cd::asset's `IAssetLoader.hpp`. Consumer
// `target_link_libraries(... cd::asset_json cd::asset)`.
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset/json/Json.hpp>

#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace cd::asset::json
{

class JsonAsset final : public cd::asset::IAsset
{
public:
    explicit JsonAsset(Value v) noexcept
        : value_ { std::move(v) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "json";
    }

    [[nodiscard]] const Value& value() const noexcept
    {
        return value_;
    }

private:
    Value value_;
};

class JsonAssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "json";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        // JSON is text; the cd::asset_json parser takes a string_view.
        const std::string_view text { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        auto r = parse(text);
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> { std::make_unique<JsonAsset>(std::move(*r)) };
    }
};

}  // namespace cd::asset::json
