// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/RevoluteHingeCapsule3D.hpp
// Standalone Concrete C++23 Construct Class #124: `cd::scene::RevoluteHingeCapsule3D`
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

// 124: HingeJoint3D -> RevoluteHingeCapsule3D
class RevoluteHingeCapsule3D final
    : public ChromaConstructBase<124, ConstructCategory::Spatial3D, ConstructRole::TraitCapsule>
{
public:
    float lower_limit_deg     { -90.0F };
    float upper_limit_deg     { 90.0F };
    float motor_target_rpm    { 0.0F };
    float motor_max_torque_nm { 180.0F };
    bool  use_angle_limits    { true };
    bool  motor_enabled       { false };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "RevoluteHingeCapsule3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "HingeJoint3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "19:13"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "RevoluteHingeCapsule3D + SwingTwistHinge";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<RevoluteHingeCapsule3D>(*this);
    }
};

} // namespace cd::scene
