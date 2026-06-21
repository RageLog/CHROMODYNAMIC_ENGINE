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

#include <cd/math/Functions.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
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

/// Henyey-Greenstein phase function — delegates to the canonical
/// cd::math::henyey_greenstein (guarded, singularity-safe).
/// Re-exported here so cd::atmosphere consumers remain unaffected.
using cd::math::henyey_greenstein;

/// Rayleigh phase (isotropic-ish, cos²-dependent).
[[nodiscard]] inline float rayleigh_phase(float cos_theta) noexcept
{
    return 3.0F / (16.0F * std::numbers::pi_v<float>) * (1.0F + cos_theta * cos_theta);
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

/// Sample the transmittance LUT with the same parameterisation the baker
/// uses: `x` ← (mu + 1) / 2, `y` ← altitude / atmosphere-thickness. Nearest-
/// texel fetch (the offline reference + multi-scatter integral do not need
/// the bilinear filter the GPU sampler provides). Clamps to the texel grid.
/// Does NOT alter the baked LUT — read-only consumer of the sealed table.
[[nodiscard]] inline cd::math::Vec3f
sample_transmittance(const Lut2D& lut, const Parameters& p, float mu,
                     float altitude_km) noexcept
{
    const float thickness = p.top_radius_km - p.bottom_radius_km;
    const float u = (mu + 1.0F) * 0.5F;
    const float v = thickness > 0.0F ? altitude_km / thickness : 0.0F;
    const float fx = std::clamp(u, 0.0F, 1.0F) * static_cast<float>(lut.w) - 0.5F;
    const float fy = std::clamp(v, 0.0F, 1.0F) * static_cast<float>(lut.h) - 0.5F;
    const auto x = static_cast<std::uint32_t>(
        std::clamp(fx, 0.0F, static_cast<float>(lut.w - 1)));
    const auto y = static_cast<std::uint32_t>(
        std::clamp(fy, 0.0F, static_cast<float>(lut.h - 1)));
    return lut.at(x, y);
}

/// Bake the multiple-scattering LUT on the CPU (Hillaire 2020 §5.3 — the
/// second of the four LUTs). Parameterised by `width` ← cos(sun-zenith),
/// `height` ← altitude. For each texel we sample `kSphereSamples` uniformly
/// distributed directions over the sphere and ray-march single scattering,
/// accumulating two quantities exactly as in the paper:
///   * `L_2nd`  — 2nd-order in-scattered luminance under unit medium lighting.
///   * `f_ms`   — the multiple-scattering transfer factor.
/// The infinite higher-order series collapses to the geometric sum
/// `Ψ = L_2nd / (1 - f_ms)` (paper Eq. 10) which is what we store. Reads the
/// sealed transmittance LUT via `sample_transmittance`; the transmittance
/// baker math/constants are untouched. Production size: 32×32. OPT-IN — no
/// renderer bakes this today (see README LUT roadmap), so it cannot alter the
/// default rendered frame.
[[nodiscard]] inline Lut2D bake_multiscatter_lut(const Parameters& p,
                                                 const Lut2D& transmittance,
                                                 std::uint32_t width,
                                                 std::uint32_t height)
{
    Lut2D out { width, height, {} };
    out.texels.resize(static_cast<std::size_t>(width) * height);
    constexpr std::uint32_t kSphereSamples = 64;   // Hillaire uses 64 dirs.
    constexpr std::uint32_t kMarchSteps    = 20;
    const float inv_samples = 1.0F / static_cast<float>(kSphereSamples);
    // Isotropic phase (multiple scattering is treated as uniform — Hillaire
    // §5.3 drops the directional phase for the 2nd+ orders).
    const float uniform_phase = 1.0F / (4.0F * std::numbers::pi_v<float>);
    // Golden angle for the deterministic Fibonacci-sphere sampling.
    const float golden_angle = std::numbers::pi_v<float> *
                               (3.0F - std::sqrt(5.0F));
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const float alt_frac = (static_cast<float>(y) + 0.5F) /
                               static_cast<float>(height);
        const float altitude = alt_frac * (p.top_radius_km - p.bottom_radius_km);
        const float r = p.bottom_radius_km + altitude;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const float mu_sun = (static_cast<float>(x) + 0.5F) /
                                 static_cast<float>(width) * 2.0F - 1.0F;
            const cd::math::Vec3f sun_dir { 0.0F,
                                            std::sqrt(std::max(0.0F,
                                                               1.0F - mu_sun * mu_sun)),
                                            mu_sun };
            cd::math::Vec3f l_2nd { 0.0F, 0.0F, 0.0F };  // 2nd-order luminance
            cd::math::Vec3f f_ms  { 0.0F, 0.0F, 0.0F };  // transfer factor
            for (std::uint32_t s = 0; s < kSphereSamples; ++s)
            {
                // Fibonacci-sphere uniform direction (deterministic, no RNG).
                const float u_s = (static_cast<float>(s) + 0.5F) * inv_samples;
                const float cos_t = 1.0F - 2.0F * u_s;
                const float sin_t = std::sqrt(std::max(0.0F, 1.0F - cos_t * cos_t));
                const float phi = static_cast<float>(s) * golden_angle;
                const cd::math::Vec3f dir { sin_t * std::cos(phi),
                                            sin_t * std::sin(phi),
                                            cos_t };
                const float mu_view = dir.z;
                const float disc = std::max(r * r * (mu_view * mu_view - 1.0F) +
                                            p.top_radius_km * p.top_radius_km,
                                            0.0F);
                const float dist = std::max(-r * mu_view + std::sqrt(disc), 0.0F);
                const float seg = dist / static_cast<float>(kMarchSteps);
                cd::math::Vec3f trans_accum { 1.0F, 1.0F, 1.0F };
                for (std::uint32_t i = 0; i < kMarchSteps; ++i)
                {
                    const float t = (static_cast<float>(i) + 0.5F) * seg;
                    // r²+t²+2rt·mu == |r_vec + t·dir|² is mathematically >= 0,
                    // but float error near a planet-grazing ray can make it a
                    // tiny negative -> sqrt(NaN); clamp the radicand to 0. AND
                    // clamp the altitude itself to >= 0: the march length uses
                    // only the top-of-atmosphere intersection, so a ray can pass
                    // BELOW the surface where a negative h would make
                    // exp(-h/scale) overflow to +inf -> inf*0 = NaN downstream.
                    const float h = std::max(0.0F,
                                    std::sqrt(std::max(0.0F,
                                              r * r + t * t +
                                              2.0F * r * t * mu_view)) -
                                    p.bottom_radius_km);
                    const float rayleigh_d = std::exp(-h / p.rayleigh_scale_h);
                    const float mie_d      = std::exp(-h / p.mie_scale_h);
                    const float oz_d = std::max(0.0F,
                                                1.0F - std::abs(h - 25.0F) / 15.0F);
                    const cd::math::Vec3f scatter {
                        p.rayleigh_scattering.x * rayleigh_d + p.mie_scattering.x * mie_d,
                        p.rayleigh_scattering.y * rayleigh_d + p.mie_scattering.y * mie_d,
                        p.rayleigh_scattering.z * rayleigh_d + p.mie_scattering.z * mie_d };
                    const cd::math::Vec3f extinction {
                        scatter.x + p.mie_absorption.x * mie_d + p.ozone_absorption.x * oz_d,
                        scatter.y + p.mie_absorption.y * mie_d + p.ozone_absorption.y * oz_d,
                        scatter.z + p.mie_absorption.z * mie_d + p.ozone_absorption.z * oz_d };
                    // Step transmittance along the march (exp of -extinction*seg).
                    const cd::math::Vec3f step_trans {
                        std::exp(-extinction.x * seg),
                        std::exp(-extinction.y * seg),
                        std::exp(-extinction.z * seg) };
                    // Transmittance from this sample to the sun (sealed LUT).
                    // A degenerate atmosphere (e.g. all scattering zeroed -> the
                    // transmittance LUT can be non-finite where extinction also
                    // vanishes) must not poison L_2nd via 0 * NaN = NaN; treat a
                    // non-finite sun transmittance as zero transmission.
                    cd::math::Vec3f sun_trans =
                        sample_transmittance(transmittance, p, dot(dir, sun_dir), h);
                    sun_trans.x = std::isfinite(sun_trans.x) ? sun_trans.x : 0.0F;
                    sun_trans.y = std::isfinite(sun_trans.y) ? sun_trans.y : 0.0F;
                    sun_trans.z = std::isfinite(sun_trans.z) ? sun_trans.z : 0.0F;
                    // f_ms accumulates scattered fraction (unit incoming light).
                    f_ms.x += trans_accum.x * scatter.x * uniform_phase * seg;
                    f_ms.y += trans_accum.y * scatter.y * uniform_phase * seg;
                    f_ms.z += trans_accum.z * scatter.z * uniform_phase * seg;
                    // L_2nd: in-scattered sunlight at this sample.
                    l_2nd.x += trans_accum.x * scatter.x * uniform_phase *
                               sun_trans.x * seg;
                    l_2nd.y += trans_accum.y * scatter.y * uniform_phase *
                               sun_trans.y * seg;
                    l_2nd.z += trans_accum.z * scatter.z * uniform_phase *
                               sun_trans.z * seg;
                    trans_accum.x *= step_trans.x;
                    trans_accum.y *= step_trans.y;
                    trans_accum.z *= step_trans.z;
                }
            }
            // Average over the sphere (4π / N · 1/4π = 1/N for the directions).
            l_2nd.x *= inv_samples;
            l_2nd.y *= inv_samples;
            l_2nd.z *= inv_samples;
            f_ms.x  *= inv_samples;
            f_ms.y  *= inv_samples;
            f_ms.z  *= inv_samples;
            // Geometric series Ψ = L_2nd / (1 - f_ms) (Hillaire Eq. 10). f_ms
            // is < 1 for any physical atmosphere; guard keeps it finite.
            out.at(x, y) = {
                l_2nd.x / std::max(1.0F - f_ms.x, 1.0e-4F),
                l_2nd.y / std::max(1.0F - f_ms.y, 1.0e-4F),
                l_2nd.z / std::max(1.0F - f_ms.z, 1.0e-4F) };
        }
    }
    return out;
}

/// Sample the multiple-scattering LUT with the same parameterisation
/// `bake_multiscatter_lut` writes: `x` <- (mu_sun + 1) / 2, `y` <- altitude /
/// atmosphere-thickness. Nearest-texel, clamped to the grid; read-only. Mirror
/// of `sample_transmittance` so the sky-view baker consumes the sealed table the
/// same way the GPU sampler would.
[[nodiscard]] inline cd::math::Vec3f
sample_multiscatter(const Lut2D& lut, const Parameters& p, float mu_sun,
                    float altitude_km) noexcept
{
    const float thickness = p.top_radius_km - p.bottom_radius_km;
    const float u = (mu_sun + 1.0F) * 0.5F;
    const float v = thickness > 0.0F ? altitude_km / thickness : 0.0F;
    const float fx = std::clamp(u, 0.0F, 1.0F) * static_cast<float>(lut.w) - 0.5F;
    const float fy = std::clamp(v, 0.0F, 1.0F) * static_cast<float>(lut.h) - 0.5F;
    const auto x = static_cast<std::uint32_t>(
        std::clamp(fx, 0.0F, static_cast<float>(lut.w - 1)));
    const auto y = static_cast<std::uint32_t>(
        std::clamp(fy, 0.0F, static_cast<float>(lut.h - 1)));
    return lut.at(x, y);
}

/// Bake the sky-view LUT on the CPU (Hillaire 2020 §5.4 — the third of the four
/// LUTs). For a fixed camera altitude `view_altitude_km` and sun direction
/// `sun_dir` (already normalised, planet-up = +Z so `sun_dir.z` == cos(sun-
/// zenith)), each texel stores the total in-scattered luminance reaching the
/// eye along the view ray (view-azimuth `width`, view-zenith `height`).
///
/// Per step of the single-scattering march we add
///   * direct sunlight in-scatter: `trans_to_eye * (rayleigh_s * P_R +
///     mie_s * P_HG) * sun_transmittance * step`, and
///   * the multiple-scattering term: `trans_to_eye * (rayleigh_s + mie_s) *
///     Psi(mu_sun, h) * step`,
/// exactly the two contributions of Hillaire's `RaymarchScattering`. Reads the
/// sealed transmittance LUT (sun visibility) and the multi-scatter LUT (`Psi`)
/// read-only; neither baker's math/constants are touched. Production size:
/// 192x108. OPT-IN — no renderer bakes this today (see README LUT roadmap), so
/// it cannot alter the default rendered frame.
[[nodiscard]] inline Lut2D bake_skyview_lut(const Parameters& p,
                                            const Lut2D& transmittance,
                                            const Lut2D& multiscatter,
                                            const cd::math::Vec3f& sun_dir,
                                            float view_altitude_km,
                                            std::uint32_t width,
                                            std::uint32_t height)
{
    Lut2D out { width, height, {} };
    out.texels.resize(static_cast<std::size_t>(width) * height);
    constexpr std::uint32_t kMarchSteps = 30;
    const float r0 = p.bottom_radius_km + std::max(0.0F, view_altitude_km);
    // Camera at (0, 0, r0): planet-up is +Z, so a view direction's z component
    // is its cos(view-zenith). The sun's z component is cos(sun-zenith).
    const float mu_sun_global = std::clamp(sun_dir.z, -1.0F, 1.0F);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        // Non-linear zenith parameterisation (Hillaire §5.4 packs more detail
        // near the horizon): v in [0,1] -> view-zenith angle via a squared map
        // around the horizon. Here a simpler uniform cos map keeps the CPU
        // reference legible; the GPU kernel mirrors it 1:1.
        const float v_frac = (static_cast<float>(y) + 0.5F) /
                             static_cast<float>(height);
        const float cos_view = 1.0F - 2.0F * v_frac;  // +1 up .. -1 down
        const float sin_view = std::sqrt(std::max(0.0F,
                                                  1.0F - cos_view * cos_view));
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const float az_frac = (static_cast<float>(x) + 0.5F) /
                                  static_cast<float>(width);
            const float azimuth = az_frac * 2.0F * std::numbers::pi_v<float>;
            const cd::math::Vec3f view_dir { sin_view * std::cos(azimuth),
                                             sin_view * std::sin(azimuth),
                                             cos_view };
            const float mu_view = view_dir.z;
            // Top-of-atmosphere intersection along the view ray.
            const float disc = std::max(r0 * r0 * (mu_view * mu_view - 1.0F) +
                                        p.top_radius_km * p.top_radius_km,
                                        0.0F);
            const float dist = std::max(-r0 * mu_view + std::sqrt(disc), 0.0F);
            const float seg = dist / static_cast<float>(kMarchSteps);
            const float cos_vs = std::clamp(dot(view_dir, sun_dir),
                                            -1.0F, 1.0F);
            const float phase_r = rayleigh_phase(cos_vs);
            const float phase_m = henyey_greenstein(cos_vs, p.mie_g);
            cd::math::Vec3f l { 0.0F, 0.0F, 0.0F };      // accumulated luminance
            cd::math::Vec3f trans { 1.0F, 1.0F, 1.0F };  // eye->sample transmit
            for (std::uint32_t i = 0; i < kMarchSteps; ++i)
            {
                const float t = (static_cast<float>(i) + 0.5F) * seg;
                // Altitude of this march sample (clamped >= 0 — same NaN guard
                // the multi-scatter baker uses for planet-grazing rays).
                const float h = std::max(0.0F,
                                std::sqrt(std::max(0.0F,
                                          r0 * r0 + t * t +
                                          2.0F * r0 * t * mu_view)) -
                                p.bottom_radius_km);
                const float rayleigh_d = std::exp(-h / p.rayleigh_scale_h);
                const float mie_d      = std::exp(-h / p.mie_scale_h);
                const float oz_d = std::max(0.0F,
                                            1.0F - std::abs(h - 25.0F) / 15.0F);
                const cd::math::Vec3f rayleigh_s {
                    p.rayleigh_scattering.x * rayleigh_d,
                    p.rayleigh_scattering.y * rayleigh_d,
                    p.rayleigh_scattering.z * rayleigh_d };
                const cd::math::Vec3f mie_s {
                    p.mie_scattering.x * mie_d,
                    p.mie_scattering.y * mie_d,
                    p.mie_scattering.z * mie_d };
                const cd::math::Vec3f extinction {
                    rayleigh_s.x + mie_s.x + p.mie_absorption.x * mie_d +
                        p.ozone_absorption.x * oz_d,
                    rayleigh_s.y + mie_s.y + p.mie_absorption.y * mie_d +
                        p.ozone_absorption.y * oz_d,
                    rayleigh_s.z + mie_s.z + p.mie_absorption.z * mie_d +
                        p.ozone_absorption.z * oz_d };
                const cd::math::Vec3f step_trans {
                    std::exp(-extinction.x * seg),
                    std::exp(-extinction.y * seg),
                    std::exp(-extinction.z * seg) };
                // Sun visibility at this altitude (sealed transmittance LUT);
                // guard a non-finite degenerate fetch to 0 (same as §5.3).
                cd::math::Vec3f sun_trans =
                    sample_transmittance(transmittance, p, mu_sun_global, h);
                sun_trans.x = std::isfinite(sun_trans.x) ? sun_trans.x : 0.0F;
                sun_trans.y = std::isfinite(sun_trans.y) ? sun_trans.y : 0.0F;
                sun_trans.z = std::isfinite(sun_trans.z) ? sun_trans.z : 0.0F;
                // Multiple-scattering term Psi(mu_sun, h) (sealed MS LUT).
                cd::math::Vec3f psi =
                    sample_multiscatter(multiscatter, p, mu_sun_global, h);
                psi.x = std::isfinite(psi.x) ? psi.x : 0.0F;
                psi.y = std::isfinite(psi.y) ? psi.y : 0.0F;
                psi.z = std::isfinite(psi.z) ? psi.z : 0.0F;
                // Direct phased single-scatter from the sun + the isotropic
                // multiple-scatter term, both attenuated by eye->sample trans.
                const cd::math::Vec3f in_scatter {
                    (rayleigh_s.x * phase_r + mie_s.x * phase_m) * sun_trans.x +
                        (rayleigh_s.x + mie_s.x) * psi.x,
                    (rayleigh_s.y * phase_r + mie_s.y * phase_m) * sun_trans.y +
                        (rayleigh_s.y + mie_s.y) * psi.y,
                    (rayleigh_s.z * phase_r + mie_s.z * phase_m) * sun_trans.z +
                        (rayleigh_s.z + mie_s.z) * psi.z };
                l.x += trans.x * in_scatter.x * seg;
                l.y += trans.y * in_scatter.y * seg;
                l.z += trans.z * in_scatter.z * seg;
                trans.x *= step_trans.x;
                trans.y *= step_trans.y;
                trans.z *= step_trans.z;
            }
            out.at(x, y) = l;
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

// ---- GLSL compute kernel (multiple-scattering LUT bake) ---------------------
// Mirrors bake_multiscatter_lut (Hillaire 2020 §5.3). Samples the sealed
// transmittance LUT (binding 1) and accumulates the L_2nd / f_ms pair, storing
// the geometric-series result Psi = L_2nd / (1 - f_ms). OPT-IN runtime bake.

constexpr std::string_view kMultiScatterCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D ms_lut;
layout(set = 0, binding = 1) uniform sampler2D transmittance_lut;
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
const int   kSphereSamples = 64;
const int   kMarchSteps    = 20;
const float kPi            = 3.14159265358979323846;
const float kUniformPhase  = 1.0 / (4.0 * kPi);
vec3 sample_t(float mu, float h) {
  float u = (mu + 1.0) * 0.5;
  float v = h / (pc.top_r - pc.bottom_r);
  return texture(transmittance_lut, vec2(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0))).rgb;
}
void main() {
  uvec2 px = gl_GlobalInvocationID.xy;
  if (px.x >= uint(pc.size.x) || px.y >= uint(pc.size.y)) return;
  float alt    = (float(px.y) + 0.5) / pc.size.y * (pc.top_r - pc.bottom_r);
  float r      = pc.bottom_r + alt;
  float mu_sun = (float(px.x) + 0.5) / pc.size.x * 2.0 - 1.0;
  vec3  sun_dir = vec3(0.0, sqrt(max(0.0, 1.0 - mu_sun * mu_sun)), mu_sun);
  float inv_n   = 1.0 / float(kSphereSamples);
  vec3  l_2nd   = vec3(0.0);
  vec3  f_ms    = vec3(0.0);
  for (int s = 0; s < kSphereSamples; ++s) {
    float u_s   = (float(s) + 0.5) * inv_n;
    float cos_t = 1.0 - 2.0 * u_s;
    float sin_t = sqrt(max(0.0, 1.0 - cos_t * cos_t));
    float phi   = float(s) * (kPi * (3.0 - sqrt(5.0)));
    vec3  dir   = vec3(sin_t * cos(phi), sin_t * sin(phi), cos_t);
    float mu_v  = dir.z;
    float disc  = max(r * r * (mu_v * mu_v - 1.0) + pc.top_r * pc.top_r, 0.0);
    float dist  = max(-r * mu_v + sqrt(disc), 0.0);
    float seg   = dist / float(kMarchSteps);
    vec3  ta    = vec3(1.0);
    for (int i = 0; i < kMarchSteps; ++i) {
      float t  = (float(i) + 0.5) * seg;
      float h  = sqrt(r * r + t * t + 2.0 * r * t * mu_v) - pc.bottom_r;
      float rd = exp(-h / pc.ray_h);
      float md = exp(-h / pc.mie_h);
      float od = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
      vec3  scatter    = pc.ray_s.rgb * rd + pc.mie_s.rgb * md;
      vec3  extinction = scatter + pc.mie_a.rgb * md + pc.ozone_a.rgb * od;
      vec3  step_trans = exp(-extinction * seg);
      vec3  sun_trans  = sample_t(dot(dir, sun_dir), h);
      f_ms  += ta * scatter * kUniformPhase * seg;
      l_2nd += ta * scatter * kUniformPhase * sun_trans * seg;
      ta    *= step_trans;
    }
  }
  l_2nd *= inv_n;
  f_ms  *= inv_n;
  vec3 psi = l_2nd / max(vec3(1.0) - f_ms, vec3(1.0e-4));
  imageStore(ms_lut, ivec2(px), vec4(psi, 1.0));
}
)glsl";

// ---- GLSL compute kernel (sky-view LUT bake) --------------------------------
// Mirrors bake_skyview_lut (Hillaire 2020 §5.4). Reads the sealed transmittance
// LUT (binding 1, sun visibility) and the multi-scatter LUT (binding 2, Psi)
// read-only, accumulating the phased single-scatter + isotropic multi-scatter
// pair along a 30-step view-ray march. The camera altitude + sun direction are
// pushed per-bake. OPT-IN runtime bake — off the default rendered path.

constexpr std::string_view kSkyViewCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D sky_lut;
layout(set = 0, binding = 1) uniform sampler2D transmittance_lut;
layout(set = 0, binding = 2) uniform sampler2D multiscatter_lut;
layout(push_constant) uniform PC {
  vec2  size;
  float bottom_r;
  float top_r;
  float ray_h;
  float mie_h;
  float mie_g;
  float view_alt;
  vec4  ray_s;     // rayleigh scattering RGB + pad
  vec4  mie_s;     // mie scattering RGB + pad
  vec4  mie_a;     // mie absorption RGB + pad
  vec4  ozone_a;   // ozone RGB + pad
  vec4  sun_dir;   // sun direction XYZ + pad (planet-up = +Z)
} pc;
const int   kMarchSteps = 30;
const float kPi         = 3.14159265358979323846;
vec3 sample_lut(sampler2D lut, float mu, float h) {
  float u = (mu + 1.0) * 0.5;
  float v = h / (pc.top_r - pc.bottom_r);
  return texture(lut, vec2(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0))).rgb;
}
float rayleigh_phase(float c) { return 3.0 / (16.0 * kPi) * (1.0 + c * c); }
float hg_phase(float c, float g) {
  float d = 1.0 + g * g - 2.0 * g * c;
  return (1.0 - g * g) / (4.0 * kPi * max(d, 1.0e-4) * sqrt(max(d, 1.0e-4)));
}
void main() {
  uvec2 px = gl_GlobalInvocationID.xy;
  if (px.x >= uint(pc.size.x) || px.y >= uint(pc.size.y)) return;
  float r0       = pc.bottom_r + max(0.0, pc.view_alt);
  float mu_sun   = clamp(pc.sun_dir.z, -1.0, 1.0);
  float v_frac   = (float(px.y) + 0.5) / pc.size.y;
  float cos_view = 1.0 - 2.0 * v_frac;
  float sin_view = sqrt(max(0.0, 1.0 - cos_view * cos_view));
  float az       = (float(px.x) + 0.5) / pc.size.x * 2.0 * kPi;
  vec3  view_dir = vec3(sin_view * cos(az), sin_view * sin(az), cos_view);
  float mu_view  = view_dir.z;
  float disc     = max(r0 * r0 * (mu_view * mu_view - 1.0) + pc.top_r * pc.top_r, 0.0);
  float dist     = max(-r0 * mu_view + sqrt(disc), 0.0);
  float seg      = dist / float(kMarchSteps);
  float cos_vs   = clamp(dot(view_dir, pc.sun_dir.xyz), -1.0, 1.0);
  float phase_r  = rayleigh_phase(cos_vs);
  float phase_m  = hg_phase(cos_vs, pc.mie_g);
  vec3  l        = vec3(0.0);
  vec3  trans    = vec3(1.0);
  for (int i = 0; i < kMarchSteps; ++i) {
    float t  = (float(i) + 0.5) * seg;
    float h  = max(0.0, sqrt(max(0.0, r0 * r0 + t * t + 2.0 * r0 * t * mu_view)) - pc.bottom_r);
    float rd = exp(-h / pc.ray_h);
    float md = exp(-h / pc.mie_h);
    float od = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    vec3  rayleigh_s = pc.ray_s.rgb * rd;
    vec3  mie_sc     = pc.mie_s.rgb * md;
    vec3  extinction = rayleigh_s + mie_sc + pc.mie_a.rgb * md + pc.ozone_a.rgb * od;
    vec3  step_trans = exp(-extinction * seg);
    vec3  sun_trans  = sample_lut(transmittance_lut, mu_sun, h);
    vec3  psi        = sample_lut(multiscatter_lut, mu_sun, h);
    vec3  in_scatter = (rayleigh_s * phase_r + mie_sc * phase_m) * sun_trans +
                       (rayleigh_s + mie_sc) * psi;
    l     += trans * in_scatter * seg;
    trans *= step_trans;
  }
  imageStore(sky_lut, ivec2(px), vec4(l, 1.0));
}
)glsl";

}  // namespace cd::atmosphere
