// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BillboardQuadBase3D.hpp
// Standalone Concrete C++23 Construct Class #80: `cd::scene::BillboardQuadBase3D`
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
class BillboardQuadBase3D final
    : public ChromaConstructBase<80, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float alpha_scissor_threshold { 0.5F };
    bool  y_axis_locked_billboard { false };
    bool  cast_depth_shadows      { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BillboardQuadBase3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SpriteBase3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "41:31"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BillboardQuadBase3D + SphericalBasis";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BillboardQuadBase3D>(*this);
    }
};

} // namespace cd::scene
