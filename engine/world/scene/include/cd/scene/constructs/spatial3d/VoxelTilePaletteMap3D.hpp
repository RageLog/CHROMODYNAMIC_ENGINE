// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/VoxelTilePaletteMap3D.hpp
// Standalone Concrete C++23 Construct Class #140: `cd::scene::VoxelTilePaletteMap3D`
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

// 140: GridMap -> VoxelTilePaletteMap3D
class VoxelTilePaletteMap3D final
    : public ChromaConstructBase<140, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> cell_size_m            { 2.0F, 2.0F, 2.0F };
    std::uint32_t        octree_chunk_dim       { 16u };
    std::uint32_t        palette_tile_count     { 32u };
    std::uint32_t        wfc_seed               { 1337u };
    bool                 greedy_face_merge      { true };
    bool                 auto_build_static_blas { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "VoxelTilePaletteMap3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GridMap"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "22:32"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "VoxelTilePaletteMap3D + OctreeChunking";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<VoxelTilePaletteMap3D>(*this);
    }
};

} // namespace cd::scene
