// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BillboardSprite3D.hpp
// Standalone Concrete C++23 Construct Class #81: `cd::scene::BillboardSprite3D`
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::scene
{

// -----------------------------------------------------------------------------
class BillboardSprite3D final
    : public ChromaConstructBase<81, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t albedo_texture_slot { 1u };
    float         hdr_emissive_boost  { 1.0F };
    bool          receive_csm_shadows { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BillboardSprite3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Sprite3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "12:40"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BillboardSprite3D + LitHdrPBR";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BillboardSprite3D>(*this);
    }
};

} // namespace cd::scene
