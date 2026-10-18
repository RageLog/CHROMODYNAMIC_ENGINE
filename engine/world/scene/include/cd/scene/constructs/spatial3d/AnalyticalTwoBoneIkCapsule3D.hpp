// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/AnalyticalTwoBoneIkCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #132: `cd::scene::AnalyticalTwoBoneIkCapsule3D`
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

// 132: SkeletonIK3D -> AnalyticalTwoBoneIkCapsule3D
class AnalyticalTwoBoneIkCapsule3D final
    : public ChromaConstructBase<132, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    std::string          root_bone_name      { "thigh_l" };
    std::string          mid_bone_name       { "calf_l" };
    std::string          tip_bone_name       { "foot_l" };
    std::array<float, 3> target_world_pos    { 0.0F, 0.0F, 0.0F };
    std::array<float, 3> pole_vector_dir     { 0.0F, 0.0F, 1.0F };
    float                blend_weight        { 1.0F };
    bool                 foot_ground_raycast { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "AnalyticalTwoBoneIkCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SkeletonIK3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "40:43"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "AnalyticalTwoBoneIkCapsule3D + FootPlacementIK";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<AnalyticalTwoBoneIkCapsule3D>(*this);
    }
};

} // namespace cd::scene
