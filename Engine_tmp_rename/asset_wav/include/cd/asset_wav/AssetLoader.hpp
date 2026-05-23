// =============================================================================
// CHROMODYNAMIC — cd/asset_wav/AssetLoader.hpp
//
// Header-only `cd::asset::IAssetLoader` adapter for cd::asset_wav. Lets
// the central `cd::asset::AssetRegistry` resolve a WAV file through the
// VFS and cache the decoded Wav by AssetId.
//
// Registration:
//   registry.register_loader(std::make_unique<cd::asset_wav::WavAssetLoader>());
//   auto id = registry.load("wav", "sfx/hit.wav");
//   auto* a = static_cast<const cd::asset_wav::WavAsset*>(registry.find(id));
//   audio_mixer.play(a->wav());
//
// Including this header pulls cd::asset's `IAssetLoader.hpp`. The consumer's
// CMakeLists must `target_link_libraries(... cd::asset_wav cd::asset)`.
// cd::asset_wav itself does not link cd::asset (the runtime decoder stays
// dependency-free); only consumers who want the registry integration pay
// the cost.
// =============================================================================
#pragma once

#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset_wav/Wav.hpp>

#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace cd::asset_wav
{

/// Wraps a decoded `Wav` as an `IAsset`. Tag: "wav".
class WavAsset final : public cd::asset::IAsset
{
public:
    explicit WavAsset(Wav w) noexcept
        : wav_ { std::move(w) }
    {
    }

    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "wav";
    }

    [[nodiscard]] const Wav& wav() const noexcept
    {
        return wav_;
    }

private:
    Wav wav_;
};

/// Loader bridge so `cd::asset::AssetRegistry::load("wav", path)` works.
class WavAssetLoader final : public cd::asset::IAssetLoader
{
public:
    [[nodiscard]] std::string_view tag() const noexcept override
    {
        return "wav";
    }

    [[nodiscard]] cd::core::Result<std::unique_ptr<cd::asset::IAsset>>
    decode(std::span<const std::byte> bytes, std::string_view /*path_hint*/) override
    {
        auto r = cd::asset_wav::decode(bytes.data(), bytes.size());
        if (!r.has_value())
            return std::unexpected(r.error());
        return std::unique_ptr<cd::asset::IAsset> { std::make_unique<WavAsset>(std::move(*r)) };
    }
};

}  // namespace cd::asset_wav
