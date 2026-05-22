// =============================================================================
// CHROMODYNAMIC — cd/asset_gltf/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_gltf.
// Tag: "gltf". Auto-detects .gltf JSON vs .glb binary by magic header.
//
// Limitation: this adapter cannot resolve external URI references
// (linked .bin, image files) from a VFS path — tinygltf wants a
// filesystem base directory. For glTF assets with external resources,
// stay with `cd::asset_gltf::load_gltf(path)` until a VFS-backed
// resource resolver lands in v2.
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset_gltf/GltfLoader.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace cd::asset_gltf
{

class GltfAsset final : public cd::asset::IAsset
{
public:
    explicit GltfAsset(GltfScene s) noexcept : scene_ { std::move(s) } {}
    [[nodiscard]] std::string_view tag() const noexcept override { return "gltf"; }
    [[nodiscard]] const GltfScene& scene() const noexcept { return scene_; }
private:
    GltfScene scene_;
};

class GltfAssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override { return "gltf"; }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        const auto* u8 = reinterpret_cast<const std::uint8_t*>(bytes.data());
        auto r = cd::asset_gltf::load_gltf_from_memory(u8, bytes.size());
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> {
            std::make_unique<GltfAsset>(std::move(*r))
        };
    }
};

}  // namespace cd::asset_gltf
