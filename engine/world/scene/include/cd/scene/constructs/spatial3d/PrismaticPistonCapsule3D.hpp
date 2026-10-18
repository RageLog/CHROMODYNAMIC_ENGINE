// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/PrismaticPistonCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #126: `cd::scene::PrismaticPistonCapsule3D`
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

// 126: SliderJoint3D -> PrismaticPistonCapsule3D
class PrismaticPistonCapsule3D final
    : public ChromaConstructBase<126, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float linear_limit_min_m { -1.5F };
    float linear_limit_max_m { 1.5F };
    float servo_target_pos_m { 0.0F };
    float max_servo_force_n  { 1200.0F };
    bool  oscillate_auto     { true };
    bool  carry_riders       { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PrismaticPistonCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SliderJoint3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "19:32"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "PrismaticPistonCapsule3D + LinearServo";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<PrismaticPistonCapsule3D>(*this);
    }
};

} // namespace cd::scene
