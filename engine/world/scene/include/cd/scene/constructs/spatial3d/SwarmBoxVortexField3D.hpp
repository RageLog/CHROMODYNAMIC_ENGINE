// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmBoxVortexField3D.hpp
// Standalone Concrete C++23 Construct Class #88: `cd::scene::SwarmBoxVortexField3D`
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
class SwarmBoxVortexField3D final
    : public ChromaConstructBase<88, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::array<float, 3> box_extents_m    { 1.4F, 0.8F, 1.4F };
    float                axial_wind_mps   { 12.0F };
    float                swirl_torque     { 4.5F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmBoxVortexField3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesAttractorBox3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmBoxVortexField3D + WindTunnel";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmBoxVortexField3D>(*this);
    }
};

} // namespace cd::scene
