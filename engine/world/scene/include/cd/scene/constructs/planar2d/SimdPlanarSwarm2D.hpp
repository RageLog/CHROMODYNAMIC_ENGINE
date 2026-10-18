// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/SimdPlanarSwarm2D.hpp
// Standalone Concrete C++23 Construct Class #40: `cd::scene::SimdPlanarSwarm2D`
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

// [40] CPUParticles2D -> SimdPlanarSwarm2D (05:49)
class SimdPlanarSwarm2D final
    : public ChromaConstructBase<40, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t particle_amount { 256u };
    float         lifetime_sec    { 1.5F };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SimdPlanarSwarm2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "CPUParticles2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "05:49"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "SimdPlanarSwarm2D + SoAIntegrator"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<SimdPlanarSwarm2D>(*this); }
};

} // namespace cd::scene
