// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/DynamicRvoObstacle3D.hpp
// Standalone Concrete C++23 Construct Class #144: `cd::scene::DynamicRvoObstacle3D`
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

// 144: NavigationObstacle3D -> DynamicRvoObstacle3D
class DynamicRvoObstacle3D final
    : public ChromaConstructBase<144, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float                avoidance_radius_m   { 0.90F };
    float                cylinder_height_m    { 1.80F };
    float                time_horizon_sec     { 1.50F };
    std::array<float, 3> obstacle_velocity    { 0.0F, 0.0F, 0.0F };
    bool                 carve_dynamic_navmesh { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "DynamicRvoObstacle3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NavigationObstacle3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "23:13"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "DynamicRvoObstacle3D + RvoAvoidance";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<DynamicRvoObstacle3D>(*this);
    }
};

} // namespace cd::scene
