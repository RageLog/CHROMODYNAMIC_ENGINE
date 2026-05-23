// =============================================================================
// CHROMODYNAMIC — cd/asset_obj/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_obj. Tag:
// "obj". OBJ is a text format; the adapter reinterprets the byte buffer
// as a string_view and forwards to parse_obj().
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset_obj/ObjLoader.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace cd::asset_obj
{

class ObjAsset final : public cd::asset::IAsset
{
public:
    explicit ObjAsset(ObjMesh m) noexcept
        : mesh_ { std::move(m) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "obj";
    }

    [[nodiscard]] const ObjMesh& mesh() const noexcept
    {
        return mesh_;
    }

private:
    ObjMesh mesh_;
};

class ObjAssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "obj";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        const std::string_view text { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        auto r = cd::asset_obj::parse_obj(text);
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> { std::make_unique<ObjAsset>(std::move(*r)) };
    }
};

}  // namespace cd::asset_obj
