// =============================================================================
// CHROMODYNAMIC — cd/scene_renderer/GlobalIlluminationPass.hpp
// FAZ 4 Wave 4.3 — Global Illumination (GTAO, SSR, DDGI, ReSTIR DI/GI, NRC),
// À-Trous SVGF Denoiser, and GPU IBL Lookup FrameGraph Wiring.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/framegraph/FrameGraph.hpp>

#include <cstdint>

namespace cd::scene_renderer
{

struct GiPipelineConfig
{
    bool enable_gtao { true };
    bool enable_ssr { true };
    bool enable_ddgi { true };
    bool enable_restir_di { true };
    bool enable_restir_gi { true };
    bool enable_nrc { false };        ///< Neural Radiance Cache inference pass
    bool enable_svgf_denoise { true };
    bool enable_ibl_gpu { true };
    std::uint32_t atrous_iterations { 3 };
};

struct GiPipelineHandles
{
    cd::framegraph::ResourceHandle gtao_ao {};
    cd::framegraph::ResourceHandle ssr_reflection {};
    cd::framegraph::ResourceHandle ddgi_irradiance {};
    cd::framegraph::ResourceHandle restir_di_reservoir {};
    cd::framegraph::ResourceHandle restir_gi_reservoir {};
    cd::framegraph::ResourceHandle nrc_radiance {};
    cd::framegraph::ResourceHandle denoised_gi {};
    cd::framegraph::ResourceHandle ibl_specular {};
};

/// Register the full GI + Reflection + Denoiser pass subgraph (`GTAO`, `SSR`,
/// `DDGI`, `ReSTIR DI`, `ReSTIR GI`, `NRC`, `À-Trous SVGF`, `IBL GPU`) into `fg`.
[[nodiscard]] GiPipelineHandles register_gi_passes(
    cd::framegraph::FrameGraph& fg,
    cd::framegraph::ResourceHandle depth,
    cd::framegraph::ResourceHandle gbuffer_normal_roughness,
    cd::framegraph::ResourceHandle& hdr_scene,
    cd::rhi::Extent2D viewport,
    const GiPipelineConfig& config
);

}  // namespace cd::scene_renderer
