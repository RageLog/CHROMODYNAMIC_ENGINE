// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/WayfinderAgent3D.hpp
// Standalone Concrete C++23 Construct Class #145: `cd::scene::WayfinderAgent3D`
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

// 145: NavigationAgent3D -> WayfinderAgent3D
class WayfinderAgent3D final
    : public ChromaConstructBase<145, ConstructCategory::Spatial3D, ConstructRole::Pilot>
{
public:
    float         patrol_speed_mps     { 3.8F };
    float         pursuit_speed_mps    { 6.2F };
    float         detection_radius_m   { 14.0F };
    float         waypoint_reach_eps_m { 0.35F };
    std::uint32_t active_waypoint_idx  { 0u };
    bool          pursuit_mode_active  { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "WayfinderAgent3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NavigationAgent3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "23:13"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "WayfinderAgent3D + PatrolWaypointAI + Hazard";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<WayfinderAgent3D>(*this);
    }
};

} // namespace cd::scene
