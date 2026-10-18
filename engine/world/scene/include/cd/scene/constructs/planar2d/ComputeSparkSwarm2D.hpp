// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/planar2d/ComputeSparkSwarm2D.hpp
// Standalone Concrete C++23 Construct Class #41: `cd::scene::ComputeSparkSwarm2D`
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

// [41] GPUParticles2D -> ComputeSparkSwarm2D (05:49)
class ComputeSparkSwarm2D final
    : public ChromaConstructBase<41, ConstructCategory::Planar2D, ConstructRole::Element>
{
public:
    std::uint32_t max_gpu_particles { 16384u };
    bool          sdf_collision     { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "ComputeSparkSwarm2D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticles2D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "05:49"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override { return "ComputeSparkSwarm2D + IndirectCompute"; }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override { return std::make_unique<ComputeSparkSwarm2D>(*this); }
};

} // namespace cd::scene
