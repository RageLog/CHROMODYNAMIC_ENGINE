// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SixDofMotorConstraint3D.hpp
// Standalone Concrete C++23 Construct Class #128: `cd::scene::SixDofMotorConstraint3D`
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

// 128: Generic6DOFJoint3D -> SixDofMotorConstraint3D
class SixDofMotorConstraint3D final
    : public ChromaConstructBase<128, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    std::array<float, 3> linear_spring_stiffness  { 150.0F, 150.0F, 150.0F };
    std::array<float, 3> angular_spring_stiffness { 45.0F, 45.0F, 45.0F };
    std::uint8_t         linear_axis_lock_mask    { 0x07u };
    std::uint8_t         angular_axis_lock_mask   { 0x00u };
    bool                 use_delassus_6x6_solver  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SixDofMotorConstraint3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "Generic6DOFJoint3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "19:55"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SixDofMotorConstraint3D + Delassus6x6";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SixDofMotorConstraint3D>(*this);
    }
};

} // namespace cd::scene
