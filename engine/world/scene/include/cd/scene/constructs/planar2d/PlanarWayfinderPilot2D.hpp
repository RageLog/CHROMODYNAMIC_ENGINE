// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarWayfinderPilot2D.hpp
// Standalone Concrete C++23 Construct Class #55: `cd::scene::PlanarWayfinderPilot2D`
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

// [55] NavigationAgent2D -> PlanarWayfinderPilot2D (08:15)
class PlanarWayfinderPilot2D final
    : public ChromaConstructBase<55, ConstructCategory::Planar2D, ConstructRole::Pilot>
{
public:
    float max_speed_px        { 180.0F };
    float path_desired_dist   { 8.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarWayfinderPilot2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NavigationAgent2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "08:15"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarWayfinderPilot2D + PatrolAI"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarWayfinderPilot2D>(*this); }
};

} // namespace cd::scene
