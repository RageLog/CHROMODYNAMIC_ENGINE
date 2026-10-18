// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/BallSocketPinCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #127: `cd::scene::BallSocketPinCapsule3D`
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

// 127: PinJoint3D -> BallSocketPinCapsule3D
class BallSocketPinCapsule3D final
    : public ChromaConstructBase<127, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    std::array<float, 3> local_pivot_a       { 0.0F, 0.5F, 0.0F };
    std::array<float, 3> local_pivot_b       { 0.0F, -0.5F, 0.0F };
    float                positional_stiffness { 1.0F };
    float                angular_damping      { 0.12F };
    bool                 zero_stretch_mode    { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "BallSocketPinCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PinJoint3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "19:42"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "BallSocketPinCapsule3D + PointConstraint";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<BallSocketPinCapsule3D>(*this);
    }
};

} // namespace cd::scene
