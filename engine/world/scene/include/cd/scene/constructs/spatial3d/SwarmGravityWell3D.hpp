// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmGravityWell3D.hpp
// Standalone Concrete C++23 Construct Class #89: `cd::scene::SwarmGravityWell3D`
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
class SwarmGravityWell3D final
    : public ChromaConstructBase<89, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float horizon_radius_m    { 4.5F };
    float radial_pull_accel   { 28.0F };
    float orbital_spin_factor { 0.35F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmGravityWell3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesAttractorSphere3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmGravityWell3D + RadialAttractor";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmGravityWell3D>(*this);
    }
};

} // namespace cd::scene
