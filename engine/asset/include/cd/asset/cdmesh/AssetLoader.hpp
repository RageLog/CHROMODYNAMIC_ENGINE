// =============================================================================
// CHROMODYNAMIC — cd/asset/cdmesh/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_cdmesh.
// Tag: "cdmesh".
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset/cdmesh/CdMesh.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace cd::asset::cdmesh
{

class CdMeshAsset final : public cd::asset::IAsset
{
public:
    explicit CdMeshAsset(CdMesh m) noexcept
        : mesh_ { std::move(m) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "cdmesh";
    }

    [[nodiscard]] const CdMesh& mesh() const noexcept
    {
        return mesh_;
    }

private:
    CdMesh mesh_;
};

class CdMeshAssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "cdmesh";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        const auto* u8 = reinterpret_cast<const std::uint8_t*>(bytes.data());
        auto r = cd::asset::cdmesh::decode(u8, bytes.size());
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> { std::make_unique<CdMeshAsset>(std::move(*r)) };
    }
};

}  // namespace cd::asset::cdmesh
