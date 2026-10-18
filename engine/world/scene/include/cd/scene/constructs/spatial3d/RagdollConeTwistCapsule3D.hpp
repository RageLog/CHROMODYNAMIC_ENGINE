// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RagdollConeTwistCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #125: `cd::scene::RagdollConeTwistCapsule3D`
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

// 125: ConeTwistJoint3D -> RagdollConeTwistCapsule3D
class RagdollConeTwistCapsule3D final
    : public ChromaConstructBase<125, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float swing_span_y_deg  { 45.0F };
    float swing_span_z_deg  { 35.0F };
    float twist_span_x_deg  { 25.0F };
    float softness          { 0.85F };
    float relaxation_factor { 1.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RagdollConeTwistCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "ConeTwistJoint3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "19:24"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RagdollConeTwistCapsule3D + EllipticalCone";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RagdollConeTwistCapsule3D>(*this);
    }
};

} // namespace cd::scene
