// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmSphereDeflector3D.hpp
// Standalone Concrete C++23 Construct Class #93: `cd::scene::SwarmSphereDeflector3D`
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
class SwarmSphereDeflector3D final
    : public ChromaConstructBase<93, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float shield_radius_m       { 1.1F };
    float specular_bounce_ratio { 0.92F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmSphereDeflector3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesCollisionSphere3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmSphereDeflector3D + ShieldSphere";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmSphereDeflector3D>(*this);
    }
};

} // namespace cd::scene
