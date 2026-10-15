// =============================================================================
// CHROMODYNAMIC — cd/scene_renderer/SurfaceAndPostFxPipeline.hpp
// FAZ 4 Wave 4.4 — Advanced Surface Shading (SSS, Kajiya-Kay Hair, Clearcoat/
// Sheen, POM, Deferred Decals, FFT Ocean Water, GPU Clipmap Terrain, GPU
// Particles) and Full Camera & PostFX FrameGraph Chain.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/scene_renderer/GlobalIlluminationPass.hpp>
#include <cd/scene_renderer/GpuDrivenPass.hpp>
#include <cd/scene_renderer/PhotometricAndAtmospherePass.hpp>

#include <array>
#include <cstdint>

namespace cd::scene_renderer
{

/// Kajiya-Kay hair/fur dual-highlight specular evaluation (primary + secondary
/// shifted Marschner/Kajiya-Kay lobes).
[[nodiscard]] std::array<float, 3> eval_kajiya_kay_hair(
    std::array<float, 3> tangent,
    std::array<float, 3> light_dir,
    std::array<float, 3> view_dir,
    std::array<float, 3> base_color,
    float primary_exp = 80.0F,
    float secondary_exp = 20.0F,
    float shift = 0.1F
) noexcept;

/// Parallax Occlusion Mapping (POM) UV offset solver with linear + secant step refinement.
[[nodiscard]] std::array<float, 2> eval_pom_uv_offset(
    std::array<float, 2> base_uv,
    std::array<float, 3> view_dir_ts,
    float height_scale,
    std::span<const float> height_samples
) noexcept;

struct SurfaceAndWorldPassConfig
{
    bool enable_sss_skin { true };
    bool enable_kajiya_kay_hair { true };
    bool enable_sheen_clearcoat { true };
    bool enable_pom { true };
    bool enable_deferred_decals { true };
    bool enable_fft_ocean_water { true };
    bool enable_clipmap_terrain { true };
    bool enable_gpu_particles { true };
};

enum class TonemapOperator : std::uint8_t
{
    kAcesFitted = 0,
    kAgX = 1,
    kAgXPunchy = 2,
    kUchimura = 3,
    kHdr10Pq = 4,
};

struct PostFxChainConfig
{
    bool enable_velocity { true };
    bool enable_taa { true };
    bool enable_smaa { false };
    bool enable_auto_exposure { true };
    bool enable_bloom { true };
    bool enable_dof { true };
    bool enable_motion_blur { true };
    bool enable_color_lut_3d { true };
    TonemapOperator tonemap { TonemapOperator::kAgX };
};

struct SceneRendererConfig
{
    cd::rhi::Extent2D viewport { 1920, 1080 };
    GpuCullingConfig culling {};
    SurfaceAndWorldPassConfig surface {};
    AtmosphereVolumetricConfig atmosphere {};
    GiPipelineConfig gi {};
    PostFxChainConfig postfx {};
};

struct SceneFrameGraphHandles
{
    cd::framegraph::ResourceHandle depth {};
    cd::framegraph::ResourceHandle gbuffer_albedo {};
    cd::framegraph::ResourceHandle gbuffer_normal_roughness {};
    cd::framegraph::ResourceHandle velocity {};
    cd::framegraph::ResourceHandle hdr_scene {};
    cd::framegraph::ResourceHandle post_aa {};
    cd::framegraph::ResourceHandle bloom_chain {};
    cd::framegraph::ResourceHandle dof_coc {};
    cd::framegraph::ResourceHandle final_backbuffer {};
    AtmosphereVolumetricHandles atmosphere_handles {};
    GiPipelineHandles gi_handles {};
};

/// Build the complete CHROMODYNAMIC photorealistic GPU-driven `FrameGraph` for
/// one frame:
///   1. `gpu_cull_and_gbuffer` (+ SSS/Hair/Clearcoat/POM)
///   2. `deferred_decals`, `clipmap_terrain`, `fft_ocean_water`, `gpu_particles`
///   3. `atmosphere_and_volumetrics` (Transmittance/SkyView/Aerial LUTs, Froxel Fog, Clouds, Light Shafts)
///   4. `gi_and_reflections` (GTAO, SSR, DDGI, ReSTIR DI/GI, NRC, SVGF Denoise, IBL GPU)
///   5. `velocity` -> `taa`/`smaa` -> `auto_exposure` -> `bloom` -> `dof` -> `motion_blur` -> `composite_tonemap`
[[nodiscard]] SceneFrameGraphHandles build_scene_renderer_framegraph(
    cd::framegraph::FrameGraph& fg,
    cd::rhi::TextureHandle backbuffer_texture,
    const SceneRendererConfig& config
);

}  // namespace cd::scene_renderer
