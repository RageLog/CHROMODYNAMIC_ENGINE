// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/NavMeshZone3D.hpp
// Standalone Concrete C++23 Construct Class #143: `cd::scene::NavMeshZone3D`
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

// 143: NavigationRegion3D -> NavMeshZone3D
class NavMeshZone3D final
    : public ChromaConstructBase<143, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         voxel_cell_size_m    { 0.25F };
    float         voxel_cell_height_m  { 0.15F };
    float         agent_max_slope_deg  { 45.0F };
    float         agent_max_climb_m    { 0.40F };
    float         agent_radius_m       { 0.40F };
    std::uint32_t tile_size_voxels     { 64u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "NavMeshZone3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "NavigationRegion3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "23:13"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "NavMeshZone3D + TiledNavMesh";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<NavMeshZone3D>(*this);
    }
};

} // namespace cd::scene
