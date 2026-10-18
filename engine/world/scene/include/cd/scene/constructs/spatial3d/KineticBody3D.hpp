// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/KineticBody3D.hpp
// Standalone Concrete C++23 Construct Class #116: `cd::scene::KineticBody3D`
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

// 116: RigidBody3D -> KineticBody3D
class KineticBody3D final
    : public ChromaConstructBase<116, ConstructCategory::Spatial3D, ConstructRole::Vessel>
{
public:
    float                mass_kg            { 10.0F };
    float                gravity_scale      { 1.0F };
    float                restitution_bounce { 0.35F };
    float                coulomb_friction   { 0.60F };
    float                linear_damping     { 0.05F };
    std::array<float, 3> linear_velocity    { 0.0F, 0.0F, 0.0F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "KineticBody3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "RigidBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "17:28"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "KineticBody3D + DynamicRigidBody + SphereCollider";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<KineticBody3D>(*this);
    }
};

} // namespace cd::scene
