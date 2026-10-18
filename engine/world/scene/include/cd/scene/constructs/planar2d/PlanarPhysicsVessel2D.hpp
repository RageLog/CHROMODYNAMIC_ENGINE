// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/PlanarPhysicsVessel2D.hpp
// Standalone Concrete C++23 Construct Class #26: `cd::scene::PlanarPhysicsVessel2D`
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

// [26] PhysicsBody2D -> PlanarPhysicsVessel2D (03:17)
class PlanarPhysicsVessel2D final
    : public ChromaConstructBase<26, ConstructCategory::Planar2D, ConstructRole::Vessel>
{
public:
    float friction    { 0.6F };
    float restitution { 0.1F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "PlanarPhysicsVessel2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "PhysicsBody2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "03:17"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "PlanarPhysicsVessel2D + RigidBody"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<PlanarPhysicsVessel2D>(*this); }
};

} // namespace cd::scene
