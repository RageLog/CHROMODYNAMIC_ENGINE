// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/spatial3d/GpuBakedLightmapField3D.hpp
// Standalone Concrete C++23 Construct Class #135: `cd::scene::GpuBakedLightmapField3D`
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

// 135: LightmapGI -> GpuBakedLightmapField3D
class GpuBakedLightmapField3D final
    : public ChromaConstructBase<135, ConstructCategory::Spatial3D, ConstructRole::Element>
{
public:
    std::uint32_t texels_per_meter     { 32u };
    std::uint32_t samples_per_texel    { 256u };
    std::uint32_t max_bounces          { 4u };
    float         denoiser_sigma_color { 1.5F };
    bool          svgf_atrous_denoise  { true };
    bool          progressive_preview  { true };

    [[nodiscard]] std::string_view chroma_class_name() const noexcept override { return "GpuBakedLightmapField3D"; }
    [[nodiscard]] std::string_view godot_equivalent_name() const noexcept override { return "LightmapGI"; }
    [[nodiscard]] std::string_view video_timestamp() const noexcept override { return "21:30"; }
    [[nodiscard]] std::string_view bundled_traits_summary() const noexcept override
    {
        return "GpuBakedLightmapField3D + SvgfDenoise";
    }
    [[nodiscard]] std::unique_ptr<IChromaConstruct> clone() const override
    {
        return std::make_unique<GpuBakedLightmapField3D>(*this);
    }
};

} // namespace cd::scene
