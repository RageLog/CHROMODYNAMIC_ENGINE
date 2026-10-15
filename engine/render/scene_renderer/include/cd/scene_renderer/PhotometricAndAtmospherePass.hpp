// =============================================================================
// CHROMODYNAMIC — cd/scene_renderer/PhotometricAndAtmospherePass.hpp
// FAZ 4 Wave 4.2 — Physical Light Units (Lumen/Lux/Candela/Kelvin), IES & LTC
// Area Lights, PCSS + Contact Shadows, and FrameGraph Atmosphere/Volumetrics.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/framegraph/FrameGraph.hpp>

#include <array>
#include <cstdint>

namespace cd::scene_renderer
{

/// Convert luminous power (Lumens) of an isotropic point light to luminous
/// intensity (Candela = lm / (4 * pi)).
[[nodiscard]] float lumens_to_point_candela(float lumens) noexcept;

/// Convert luminous power (Lumens) of a spot light with outer cone half-angle
/// `outer_half_angle_rad` to luminous intensity (Candela).
[[nodiscard]] float lumens_to_spot_candela(float lumens, float outer_half_angle_rad) noexcept;

/// Convert correlated color temperature in Kelvin `[1000, 40000]` to linear RGB.
[[nodiscard]] std::array<float, 3> kelvin_to_linear_rgb(float kelvin) noexcept;

/// Evaluate a 1D polar IES candela profile (`samples` covering `[0, pi]` radians)
/// at polar angle `theta_rad`.
[[nodiscard]] float eval_ies_profile(std::span<const float> polar_candela, float theta_rad) noexcept;

/// Compute PCSS (Percentage-Closer Soft Shadows) penumbra filter radius:
/// `w_penumbra = (d_receiver - d_blocker) * light_size / d_blocker`.
[[nodiscard]] float eval_pcss_penumbra_radius(
    float receiver_depth,
    float avg_blocker_depth,
    float light_source_radius
) noexcept;

/// Screen-space contact shadow ray marcher over a linear depth buffer.
/// Returns `0.0F` if occluded within `max_steps`, `1.0F` if lit.
[[nodiscard]] float trace_contact_shadow(
    std::span<const float> depth_buffer,
    std::uint32_t width,
    std::uint32_t height,
    float start_uv_x,
    float start_uv_y,
    float start_depth,
    float step_uv_x,
    float step_uv_y,
    float step_depth,
    std::uint32_t max_steps = 16,
    float thickness = 0.01F
) noexcept;

struct AtmosphereVolumetricConfig
{
    bool enable_atmosphere_luts { true };     ///< Transmittance + MultiScatter + SkyView + AerialPerspective 3D LUT
    bool enable_volumetric_fog { true };      ///< Froxel volumetric fog (160x90x64)
    bool enable_volumetric_clouds { true };   ///< Raymarched cloud strata
    bool enable_light_shafts { true };        ///< Radial occlusion god rays
};

struct AtmosphereVolumetricHandles
{
    cd::framegraph::ResourceHandle transmittance_lut {};
    cd::framegraph::ResourceHandle sky_view_lut {};
    cd::framegraph::ResourceHandle aerial_perspective_lut {};
    cd::framegraph::ResourceHandle froxel_fog_volume {};
    cd::framegraph::ResourceHandle cloud_color {};
    cd::framegraph::ResourceHandle light_shafts_mask {};
};

/// Register the Atmosphere LUTs, Froxel Volumetric Fog, Volumetric Clouds, and
/// Light Shafts passes into `fg`, reading `depth` and `hdr_scene` and producing
/// updated `hdr_scene`.
[[nodiscard]] AtmosphereVolumetricHandles register_atmosphere_and_volumetric_passes(
    cd::framegraph::FrameGraph& fg,
    cd::framegraph::ResourceHandle depth,
    cd::framegraph::ResourceHandle& hdr_scene,
    cd::rhi::Extent2D viewport,
    const AtmosphereVolumetricConfig& config
);

}  // namespace cd::scene_renderer
