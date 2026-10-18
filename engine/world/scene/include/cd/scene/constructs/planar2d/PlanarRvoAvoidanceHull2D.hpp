// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarRvoAvoidanceHull2D.hpp
// Standalone Concrete C++23 Construct Class #54: `cd::scene::PlanarRvoAvoidanceHull2D`
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

// [54] NavigationObstacle2D -> PlanarRvoAvoidanceHull2D (08:15)
class PlanarRvoAvoidanceHull2D final
    : public ChromaConstructBase<54, ConstructCategory::Planar2D, ConstructRole::TraitCapsule>
{
public:
    float avoidance_radius_px { 24.0F };
    float time_horizon_sec    { 1.5F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarRvoAvoidanceHull2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NavigationObstacle2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:15"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarRvoAvoidanceHull2D + ORCA"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarRvoAvoidanceHull2D>(*this); }
};

} // namespace cd::scene
