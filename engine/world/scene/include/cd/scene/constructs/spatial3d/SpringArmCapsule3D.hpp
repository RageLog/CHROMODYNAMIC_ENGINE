// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SpringArmCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #120: `cd::scene::SpringArmCapsule3D`
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

// 120: SpringArm3D -> SpringArmCapsule3D
class SpringArmCapsule3D final
    : public ChromaConstructBase<120, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float target_arm_length    { 3.5F };
    float current_arm_length   { 3.5F };
    float spring_stiffness     { 7.5F };
    float probe_sphere_radius  { 0.22F };
    float yaw_orbit_deg        { 0.0F };
    float pitch_orbit_deg      { -18.0F };
    bool  obstacle_sweep_active { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SpringArmCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "SpringArm3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "24:37"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SpringArmCapsule3D + ObstacleSweepLag";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SpringArmCapsule3D>(*this);
    }

    void on_tick(Element& host, float dt) noexcept override
    {
        IChromaConstruct::on_tick(host, dt);
        if (!enabled_ || dt <= 0.0F)
        {
            return;
        }
        const float alpha = std::clamp(dt * spring_stiffness, 0.05F, 1.0F);
        current_arm_length += (target_arm_length - current_arm_length) * alpha;
    }
};

} // namespace cd::scene
