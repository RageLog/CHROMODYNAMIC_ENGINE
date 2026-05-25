// =============================================================================
// CHROMODYNAMIC — cd/atmosphere/Atmosphere.hpp
// Day 15 — Production atmospheric scattering (Hillaire 2020).
//
// Pre-computed LUTs:
//   * transmittance(view-zenith, altitude) — view-ray attenuation.
//   * multi-scattering(view-zenith, altitude) — 2nd+ bounces.
//   * sky-view(view-azimuth, view-zenith) — fully-sampled sky.
//   * aerial-perspective(view-x, view-y, depth) — in-scattering vs. depth.
//
// CPU bakes for tests + a reference implementation. GLSL compute
// kernels for production runtime baking. Layered above each other so
// the renderer can pick any subset (e.g. just the sky-view LUT for a
// cheap static sky, or all four for full aerial-perspective fog).
//
// References:
//   * Hillaire 2020 — "A Scalable and Production-Ready Sky and
//     Atmosphere Rendering Technique" (EGSR 2020).
//   * Bruneton & Neyret 2008 — original pre-computed LUT formulation.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::atmosphere
{

/// Earth-scale parameters (Hillaire 2020 defaults). Units are kilometres.
struct Parameters
{
    float bottom_radius_km { 6360.0F };
    float top_radius_km    { 6460.0F };
    /// Rayleigh scale height (km) — air-density falloff.
    float rayleigh_scale_h { 8.0F };
    /// Mie scale height (km) — aerosol-density falloff.
    float mie_scale_h      { 1.2F };
    /// Rayleigh scattering coefficients per RGB at sea level (km^-1).
    cd::math::Vec3f rayleigh_scattering { 0.0058F, 0.01356F, 0.0331F };
    /// Mie scattering / absorption (km^-1).
    cd::math::Vec3f mie_scattering { 0.003996F, 0.003996F, 0.003996F };
    cd::math::Vec3f mie_absorption { 0.000444F, 0.000444F, 0.000444F };
    /// Ozone absorption layer (km^-1). Peaks ~25 km altitude.
    cd::math::Vec3f ozone_absorption { 0.00065F, 0.001881F, 0.000085F };
    /// Mie phase asymmetry. 0.8 = production default (forward-scatter).
    float mie_g { 0.8F };
};

/// Henyey-Greenstein phase function. Forward-asymmetry parameter `g`.
[[nodiscard]] inline float henyey_greenstein(float cos_theta, float g) noexcept
{
    const float g2 = g * g;
    const float denom = std::pow(1.0F + g2 - 2.0F * g * cos_theta, 1.5F);
    return (1.0F - g2) / (4.0F * 3.14159265F * denom);
}

/// Rayleigh phase (isotropic-ish, cos²-dependent).
[[nodiscard]] inline float rayleigh_phase(float cos_theta) noexcept
{
    return 3.0F / (16.0F * 3.14159265F) * (1.0F + cos_theta * cos_theta);
}

/// 2D float LUT — used by the CPU bakes + offline tests. Production
/// stores these as RGBA16F textures.
struct Lut2D
{
    std::uint32_t w { 0 };
    std::uint32_t h { 0 };
    std::vector<cd::math::Vec3f> texels;

    [[nodiscard]] const cd::math::Vec3f&
    at(std::uint32_t x, std::uint32_t y) const noexcept
    {
        return texels[y * w + x];
    }
    cd::math::Vec3f& at(std::uint32_t x, std::uint32_t y) noexcept
    {
        return texels[y * w + x];
    }
};

/// Bake the transmittance LUT on the CPU. `width` parameterises view-
/// zenith cos, `height` parameterises altitude above the planet
/// surface. Production size: 256×64.
[[nodiscard]] inline Lut2D bake_transmittance_lut(const Parameters& p,
                                                   std::uint32_t width,
                                                   std::uint32_t height)
{
    Lut2D out { width, height, {} };
    out.texels.resize(static_cast<std::size_t>(width) * height);
    constexpr std::uint32_t kIntegrationSteps = 40;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const float alt_frac = (static_cast<float>(y) + 0.5F) /
                               static_cast<float>(height);
        const float altitude = alt_frac * (p.top_radius_km - p.bottom_radius_km);
        const float r = p.bottom_radius_km + altitude;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const float mu_frac = (static_cast<float>(x) + 0.5F) /
                                  static_cast<float>(width);
            const float mu = mu_frac * 2.0F - 1.0F;  // cos(view-zenith)
            // Distance through atmosphere along the view ray.
            const float disc = std::max(r * r * (mu * mu - 1.0F) +
                                        p.top_radius_km * p.top_radius_km,
                                        0.0F);
            const float dist = std::max(-r * mu + std::sqrt(disc), 0.0F);
            cd::math::Vec3f optical { 0.0F, 0.0F, 0.0F };
            for (std::uint32_t i = 0; i < kIntegrationSteps; ++i)
            {
                const float t = (static_cast<float>(i) + 0.5F) /
                                static_cast<float>(kIntegrationSteps) * dist;
                const float h = std::sqrt(r * r + t * t + 2.0F * r * t * mu) -
                                p.bottom_radius_km;
                const float rayleigh_d = std::exp(-h / p.rayleigh_scale_h);
                const float mie_d      = std::exp(-h / p.mie_scale_h);
                const auto rs = p.rayleigh_scattering;
                const auto ms = p.mie_scattering;
                const auto ma = p.mie_absorption;
                const auto oa = p.ozone_absorption;
                const float oz_d = std::max(0.0F, 1.0F - std::abs(h - 25.0F) / 15.0F);
                optical.x += (rs.x * rayleigh_d + (ms.x + ma.x) * mie_d + oa.x * oz_d) *
                             (dist / static_cast<float>(kIntegrationSteps));
                optical.y += (rs.y * rayleigh_d + (ms.y + ma.y) * mie_d + oa.y * oz_d) *
                             (dist / static_cast<float>(kIntegrationSteps));
                optical.z += (rs.z * rayleigh_d + (ms.z + ma.z) * mie_d + oa.z * oz_d) *
                             (dist / static_cast<float>(kIntegrationSteps));
            }
            out.at(x, y) = {
                std::exp(-optical.x),
                std::exp(-optical.y),
                std::exp(-optical.z) };
        }
    }
    return out;
}

// ---- GLSL compute kernel (transmittance LUT bake) ---------------------------

constexpr std::string_view kTransmittanceCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D lut;
layout(push_constant) uniform PC {
  vec2 size;
  float bottom_r;
  float top_r;
  float ray_h;
  float mie_h;
  vec4  ray_s;     // rayleigh scattering RGB + pad
  vec4  mie_s;     // mie scattering RGB + pad
  vec4  mie_a;     // mie absorption RGB + pad
  vec4  ozone_a;   // ozone RGB + pad
} pc;
const int kSteps = 40;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  float alt = (float(p.y) + 0.5) / pc.size.y * (pc.top_r - pc.bottom_r);
  float r   = pc.bottom_r + alt;
  float mu  = (float(p.x) + 0.5) / pc.size.x * 2.0 - 1.0;
  float disc = max(r * r * (mu * mu - 1.0) + pc.top_r * pc.top_r, 0.0);
  float dist = max(-r * mu + sqrt(disc), 0.0);
  vec3 optical = vec3(0);
  for (int i = 0; i < kSteps; ++i) {
    float t  = (float(i) + 0.5) / float(kSteps) * dist;
    float h  = sqrt(r * r + t * t + 2.0 * r * t * mu) - pc.bottom_r;
    float rd = exp(-h / pc.ray_h);
    float md = exp(-h / pc.mie_h);
    float od = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    optical += (pc.ray_s.rgb * rd + (pc.mie_s.rgb + pc.mie_a.rgb) * md +
                pc.ozone_a.rgb * od) * (dist / float(kSteps));
  }
  imageStore(lut, ivec2(p), vec4(exp(-optical), 1.0));
}
)glsl";

}  // namespace cd::atmosphere
