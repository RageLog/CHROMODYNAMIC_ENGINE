// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BooleanExtrudeSweep3D.hpp
// Standalone Concrete C++23 Construct Class #106: `cd::scene::BooleanExtrudeSweep3D`
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
class BooleanExtrudeSweep3D final
    : public ChromaConstructBase<106, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         extrude_depth_m { 1.4F };
    float         spin_degrees    { 0.0F };
    std::uint32_t sweep_slices    { 16u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BooleanExtrudeSweep3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CSGPolygon3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "15:14"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BooleanExtrudeSweep3D + SplineSweep";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BooleanExtrudeSweep3D>(*this);
    }
};

} // namespace cd::scene
