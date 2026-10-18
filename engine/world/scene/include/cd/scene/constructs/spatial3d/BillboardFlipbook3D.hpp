// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BillboardFlipbook3D.hpp
// Standalone Concrete C++23 Construct Class #82: `cd::scene::BillboardFlipbook3D`
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
class BillboardFlipbook3D final
    : public ChromaConstructBase<82, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t atlas_columns        { 8u };
    std::uint32_t atlas_rows           { 4u };
    float         frames_per_second    { 24.0F };
    bool          motion_vector_interp { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BillboardFlipbook3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "AnimatedSprite3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "12:40"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BillboardFlipbook3D + MotionVectors";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BillboardFlipbook3D>(*this);
    }
};

} // namespace cd::scene
