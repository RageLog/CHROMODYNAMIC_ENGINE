// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SparkSwarm3D.hpp
// Standalone Concrete C++23 Construct Class #85: `cd::scene::SparkSwarm3D`
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
class SparkSwarm3D final
    : public ChromaConstructBase<85, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t max_gpu_particles    { 16384u };
    float         emission_rate_hz     { 2400.0F };
    float         depth_bounce_restit  { 0.6F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SparkSwarm3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticles3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:26"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SparkSwarm3D + ComputeParticles + PointLight";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SparkSwarm3D>(*this);
    }
};

} // namespace cd::scene
