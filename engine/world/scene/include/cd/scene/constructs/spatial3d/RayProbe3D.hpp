// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RayProbe3D.hpp
// Standalone Concrete C++23 Construct Class #150: `cd::scene::RayProbe3D`
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

// 150: RayCast3D -> RayProbe3D
class RayProbe3D final
    : public ChromaConstructBase<150, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    std::array<float, 3> ray_direction       { 0.0F, -1.0F, 0.0F };
    float                max_ray_distance_m  { 50.0F };
    std::uint32_t        collision_mask      { 0xFFFFFFFFu };
    bool                 use_gpu_bvh_query   { false };
    bool                 hit_detected        { false };
    float                last_hit_distance_m { 50.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RayProbe3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RayCast3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "24:16"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RayProbe3D + CpuAndGpuRayCast";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RayProbe3D>(*this);
    }
};

} // namespace cd::scene
