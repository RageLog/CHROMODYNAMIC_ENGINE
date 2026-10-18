// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BooleanCylinderSolid3D.hpp
// Standalone Concrete C++23 Construct Class #104: `cd::scene::BooleanCylinderSolid3D`
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
class BooleanCylinderSolid3D final
    : public ChromaConstructBase<104, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         radius_m       { 0.425F };
    float         height_m       { 1.6F };
    std::uint32_t radial_sides   { 24u };
    bool          cone_taper     { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BooleanCylinderSolid3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CSGCylinder3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "15:14"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BooleanCylinderSolid3D + ColumnPillar";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BooleanCylinderSolid3D>(*this);
    }
};

} // namespace cd::scene
