// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/PhysicalVesselBase3D.hpp
// Standalone Concrete C++23 Construct Class #111: `cd::scene::PhysicalVesselBase3D`
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

// -----------------------------------------------------------------------------
class PhysicalVesselBase3D final
    : public ChromaConstructBase<111, ConstructCategory::Spatial3D, ConstructRole::Vessel>
{
public:
    std::uint8_t motion_regime    { 2u }; ///< 0=Static, 1=Kinematic, 2=Dynamic
    float        friction_coeff   { 0.65F };
    float        restitution_coef { 0.15F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PhysicalVesselBase3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PhysicsBody3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "16:27"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "PhysicalVesselBase3D + UnifiedRigidBody";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<PhysicalVesselBase3D>(*this);
    }
};

} // namespace cd::scene
