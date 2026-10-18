// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/SwarmCurlNoiseField3D.hpp
// Standalone Concrete C++23 Construct Class #90: `cd::scene::SwarmCurlNoiseField3D`
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
class SwarmCurlNoiseField3D final
    : public ChromaConstructBase<90, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    float         curl_frequency_hz { 0.85F };
    float         turbulence_amp    { 6.0F };
    std::uint32_t simplex_octaves   { 3u };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "SwarmCurlNoiseField3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "GPUParticlesAttractorVectorField3D"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "13:50"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "SwarmCurlNoiseField3D + ProceduralCurl";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<SwarmCurlNoiseField3D>(*this);
    }
};

} // namespace cd::scene
