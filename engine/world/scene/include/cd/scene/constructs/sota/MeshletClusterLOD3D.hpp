// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/sota/MeshletClusterLOD3D.hpp
// Standalone Concrete C++23 Construct Class #222: `cd::scene::MeshletClusterLOD3D`
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

// 222: MeshletClusterLOD3D
class MeshletClusterLOD3D final
    : public ChromaConstructBase<222, ConstructCategory::NextGenSOTA, ConstructRole::Element>
{
public:
    std::uint32_t max_triangles_per_meshlet { 64u };
    float         pixel_error_threshold     { 1.0F };
    bool          gpu_hiz_occlusion_cull    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "MeshletClusterLOD3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "MeshletClusterLOD3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "42:00+ (Beyond Godot SOTA)"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "MeshletClusterLOD3D + ClusterDAG + HiZCull"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<MeshletClusterLOD3D>(*this); }
};

} // namespace cd::scene
