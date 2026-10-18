// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/HiZOccluderHull3D.hpp
// Standalone Concrete C++23 Construct Class #147: `cd::scene::HiZOccluderHull3D`
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

// 147: OccluderInstance3D -> HiZOccluderHull3D
class HiZOccluderHull3D final
    : public ChromaConstructBase<147, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float         depth_bias_epsilon     { 0.0005F };
    std::uint32_t min_mip_level          { 0u };
    bool          contribute_phase1_hiz  { true };
    bool          two_phase_gpu_culling  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "HiZOccluderHull3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "OccluderInstance3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "23:39"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "HiZOccluderHull3D + AutoGpuHiZ";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<HiZOccluderHull3D>(*this);
    }
};

} // namespace cd::scene
