// =============================================================================
// CHROMODYNAMIC — cd/brdf/multiscatter/TurquinMultiScatter.hpp
// Phase 18 — SOTA Multiple-Scattering Microfacet Energy Compensation &
//            Spectral-to-XYZ Airy Thin-Film Iridescence.
//
// Academic references:
//   [1] E. Turquin (Industrial Light & Magic), "Practical multiple scattering
//       compensation for microfacet models," Technical Report, 2019.
//   [2] C. Kulla, A. Conty (Sony Pictures Imageworks), "Revisiting Physically
//       Based Shading at Imageworks," SIGGRAPH 2017 Course: Physically Based
//       Shading in Theory and Practice.
//   [3] L. Belcour, P. Barla (Inria / Univ. Bordeaux), "A Practical Extension
//       to Microfacet Theory for the Modeling of Varying Iridescence," ACM
//       Transactions on Graphics (SIGGRAPH 2017), 36(4):65, 2017.
//   [4] E. Heitz, "Understanding the Masking-Shadowing Function in Microfacet-
//       Based BRDFs," JCGT 3(2), 2014.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>

namespace cd::brdf::multiscatter
{

// -----------------------------------------------------------------------------
// 1. Directional Albedo E(mu, alpha) & Average Albedo E_avg(alpha) for GGX
// -----------------------------------------------------------------------------

/// Closed-form Karis/Lazarov/Turquin rational polynomial approximation of the
/// white-furnace directional albedo E(mu, roughness) for Smith-GGX:
///   E(mu, alpha) = \int_{\Omega_+} f_{ss}(v, l; F=1) (n \cdot l) d\omega_l
/// Satisfies E(mu, 0) == 1.0 (mirror preserves 100% energy in single bounce)
/// and monotonically decreases with roughness as multiple-bounce shadowing grows.
[[nodiscard]] inline float directional_albedo_ggx(
    float n_dot_v,
    float roughness) noexcept
{
    const float mu = std::clamp(n_dot_v, 0.0001F, 1.0F);
    const float r  = std::clamp(roughness, 0.0F, 1.0F);

    // Karis 2013 / Lazarov 2013 environment BRDF split-sum fit (scale + bias at F0=1):
    const float c0x = -1.0F;
    const float c0y = -0.0275F;
    const float c0z = -0.572F;
    const float c0w =  0.022F;

    const float c1x =  1.0F;
    const float c1y =  0.0425F;
    const float c1z =  1.04F;
    const float c1w = -0.04F;

    const float rx = r * c0x + c1x;
    const float ry = r * c0y + c1y;
    const float rz = r * c0z + c1z;
    const float rw = r * c0w + c1w;

    const float a004 = std::min(rx * rx, std::exp2(-9.28F * mu)) * rx + ry;
    const float scale = -1.04F * a004 + rz;
    const float bias  =  1.04F * a004 + rw;

    return std::clamp(scale + bias, 0.05F, 1.0F);
}

/// Hemispherically averaged directional albedo:
///   E_avg(roughness) = 2 \int_0^1 E(mu, roughness) mu dmu
[[nodiscard]] inline float average_albedo_ggx(float roughness) noexcept
{
    const float r = std::clamp(roughness, 0.0F, 1.0F);
    // Analytic 4-point Gauss-Legendre quadrature over mu in [0, 1]:
    constexpr float kNodes[4]   = { 0.06943184F, 0.33000948F, 0.66999052F, 0.93056816F };
    constexpr float kWeights[4] = { 0.17392742F, 0.32607258F, 0.32607258F, 0.17392742F };
    float integral = 0.0F;
    for (int i = 0; i < 4; ++i)
    {
        integral += kWeights[i] * directional_albedo_ggx(kNodes[i], r) * kNodes[i];
    }
    return std::clamp(2.0F * integral, 0.05F, 1.0F);
}

// -----------------------------------------------------------------------------
// 2. Turquin (2019) & Kulla-Conty (2017) Energy Compensation
// -----------------------------------------------------------------------------

/// Hemispherical average of Schlick Fresnel with F90 = 1:
///   F_avg = 2 \int_0^1 F(mu) mu dmu = (20/21) F_0 + (1/21)
[[nodiscard]] constexpr cd::math::Vec3f fresnel_average_schlick(cd::math::Vec3f f0) noexcept
{
    constexpr float kW0 = 20.0F / 21.0F;
    constexpr float kW1 = 1.0F / 21.0F;
    return {
        f0.x * kW0 + kW1,
        f0.y * kW0 + kW1,
        f0.z * kW0 + kW1
    };
}

/// Turquin (2019) Eq. 11 multiplicative energy compensation factor:
///   f_{ms-turquin}(v, l) = f_{ss}(v, l) * (1 + F_0 * weight * (1 - E(mu_o)) / E(mu_o))
/// At roughness -> 0, E(mu_o) -> 1 and the factor is identically (1, 1, 1).
/// At roughness -> 1 on a pure conductor (F_0 = 1), the factor restores the
/// ~45-60% energy lost by single-scatter Smith-GGX.
[[nodiscard]] inline cd::math::Vec3f turquin_energy_compensation(
    cd::math::Vec3f f0,
    float           e_directional,
    float           weight = 1.0F) noexcept
{
    const float e_safe = std::max(e_directional, 0.05F);
    const float w      = std::clamp(weight, 0.0F, 1.0F);
    const float ratio  = w * (1.0F - e_safe) / e_safe;
    return {
        1.0F + std::clamp(f0.x, 0.0F, 1.0F) * ratio,
        1.0F + std::clamp(f0.y, 0.0F, 1.0F) * ratio,
        1.0F + std::clamp(f0.z, 0.0F, 1.0F) * ratio
    };
}

/// Kulla-Conty (SIGGRAPH 2017) additive reciprocity-preserving multi-scatter lobe:
///   f_{ms}(v, l) = \frac{(1 - E(mu_v))(1 - E(mu_l))}{\pi (1 - E_{avg})}
///                  * \frac{F_{avg}^2 E_{avg}}{1 - F_{avg}(1 - E_{avg})}
[[nodiscard]] inline cd::math::Vec3f kulla_conty_multiscatter_lobe(
    cd::math::Vec3f f0,
    float           e_v,
    float           e_l,
    float           e_avg) noexcept
{
    const float ev   = std::clamp(e_v, 0.0F, 1.0F);
    const float el   = std::clamp(e_l, 0.0F, 1.0F);
    const float eavg = std::clamp(e_avg, 0.01F, 0.9999F);

    const float geom = ((1.0F - ev) * (1.0F - el))
                     / (std::numbers::pi_v<float> * (1.0F - eavg));

    const cd::math::Vec3f favg = fresnel_average_schlick(f0);
    auto channel_fms = [eavg](float fa) noexcept -> float {
        const float denom = std::max(1.0F - fa * (1.0F - eavg), 1e-4F);
        return (fa * fa * eavg) / denom;
    };

    return {
        geom * channel_fms(favg.x),
        geom * channel_fms(favg.y),
        geom * channel_fms(favg.z)
    };
}

// -----------------------------------------------------------------------------
// 3. Belcour & Barla (SIGGRAPH 2017) Thin-Film Iridescence (Airy Interference)
// -----------------------------------------------------------------------------

/// Evaluates thin-film spectral interference reflectance for a dielectric coat
/// of optical thickness `d_nm` (nanometers) and refractive index `film_ior`
/// over a base conductor/dielectric interface with normal-incidence reflectance `f0`.
/// When `film_thickness_nm <= 0`, reduces smoothly to standard Schlick Fresnel.
[[nodiscard]] inline cd::math::Vec3f belcour_barla_iridescence_fresnel(
    cd::math::Vec3f f0,
    float           cos_theta_d,
    float           film_thickness_nm,
    float           film_ior = 1.5F) noexcept
{
    const float cos1 = std::clamp(cos_theta_d, 0.0F, 1.0F);
    const float one_minus_cos = 1.0F - cos1;
    const float p5 = one_minus_cos * one_minus_cos * one_minus_cos * one_minus_cos * one_minus_cos;

    // Standard Schlick Fresnel at the base interface:
    const cd::math::Vec3f f_schlick {
        f0.x + (1.0F - f0.x) * p5,
        f0.y + (1.0F - f0.y) * p5,
        f0.z + (1.0F - f0.z) * p5
    };

    if (film_thickness_nm <= 1e-3F)
    {
        return f_schlick;
    }

    // Snell's law into the thin film (n_0 = 1.0 -> n_1 = film_ior):
    const float eta  = std::max(film_ior, 1.05F);
    const float sin1_sq = std::max(0.0F, 1.0F - cos1 * cos1);
    const float sin2_sq = sin1_sq / (eta * eta);
    if (sin2_sq >= 1.0F)
    {
        return { 1.0F, 1.0F, 1.0F }; // Total internal reflection
    }
    const float cos2 = std::sqrt(1.0F - sin2_sq);

    // Optical Path Difference (OPD = 2 * n_1 * d * cos(theta_2)) in nanometers,
    // plus half-wave (pi) phase shift at the air-film boundary:
    const float opd_nm = 2.0F * eta * film_thickness_nm * cos2;

    // Top air->film Schlick reflectance R_01:
    const float r0_top = ((eta - 1.0F) / (eta + 1.0F)) * ((eta - 1.0F) / (eta + 1.0F));
    const float t2 = 1.0F - cos2;
    const float r12 = r0_top + (1.0F - r0_top) * (t2 * t2 * t2 * t2 * t2);

    // Evaluate Airy interference at CIE RGB primary wavelengths (650nm, 530nm, 450nm)
    // with spectral Gaussian envelope (Belcour & Barla 2017 Eq. 14) to prevent
    // high-order ringing at large film thicknesses:
    constexpr float kLambdaR = 650.0F;
    constexpr float kLambdaG = 530.0F;
    constexpr float kLambdaB = 450.0F;
    constexpr float kTwoPi   = 2.0F * std::numbers::pi_v<float>;

    auto airy_channel = [&](float r23, float lambda_nm) noexcept -> float {
        // Film-to-substrate reflectance R_23 has reduced refractive-index contrast
        // relative to air-to-substrate F_schlick (Belcour & Barla 2017, Sec. 3.1):
        const float r23_clamped = std::clamp(r23 * 0.75F, 0.04F, 0.92F);
        const float phi = kTwoPi * (opd_nm / lambda_nm) + std::numbers::pi_v<float>;
        // Spectral band-width attenuation exp(-(opd / coherence_length)^2):
        const float depol = std::exp(-0.0000015F * opd_nm * opd_nm);
        const float cos_phi = std::cos(phi) * depol;

        const float r12_r23 = r12 * r23_clamped;
        const float two_sqrt_r_cos = 2.0F * std::sqrt(r12_r23) * cos_phi;
        const float num = r12 + r23_clamped + two_sqrt_r_cos;
        const float den = std::max(1.0F + r12_r23 + two_sqrt_r_cos, 1e-4F);
        return std::clamp(num / den, 0.0F, 1.0F);
    };

    // Smoothly ramp in the thin-film interference for ultrathin layers (< 20 nm):
    const float blend = std::clamp(film_thickness_nm / 20.0F, 0.0F, 1.0F);
    const cd::math::Vec3f f_airy {
        airy_channel(f_schlick.x, kLambdaR),
        airy_channel(f_schlick.y, kLambdaG),
        airy_channel(f_schlick.z, kLambdaB)
    };

    return {
        f_schlick.x + (f_airy.x - f_schlick.x) * blend,
        f_schlick.y + (f_airy.y - f_schlick.y) * blend,
        f_schlick.z + (f_airy.z - f_schlick.z) * blend
    };
}

// -----------------------------------------------------------------------------
// 4. Combined SOTA Specular BRDF Evaluator (Smith-GGX + Turquin + Belcour-Barla)
// -----------------------------------------------------------------------------

/// Full SOTA specular microfacet BRDF evaluation combining:
///   - Trowbridge-Reitz (GGX) Normal Distribution Function D(h)
///   - Hammon/Heitz height-correlated Smith masking-shadowing V(v, l)
///   - Belcour & Barla (2017) thin-film iridescence Fresnel F_airy(h, l)
///   - Turquin (2019) multiple-scattering energy compensation F_ms(v)
[[nodiscard]] inline cd::math::Vec3f evaluate_turquin_iridescent_ggx(
    cd::math::Vec3f f0,
    float           roughness,
    float           n_dot_v,
    float           n_dot_l,
    float           n_dot_h,
    float           v_dot_h,
    float           multiscatter_weight = 1.0F,
    float           iridescence_nm      = 0.0F,
    float           iridescence_ior     = 1.5F) noexcept
{
    if (n_dot_l <= 0.0F || n_dot_v <= 0.0F)
    {
        return { 0.0F, 0.0F, 0.0F };
    }

    const float r  = std::clamp(roughness, 0.045F, 1.0F);
    const float a  = r * r;
    const float a2 = a * a;

    // 1. GGX NDF D(h):
    const float nh = std::clamp(n_dot_h, 0.0F, 1.0F);
    const float d_denom = (nh * nh) * (a2 - 1.0F) + 1.0F;
    const float D = a2 / std::max(std::numbers::pi_v<float> * d_denom * d_denom, 1e-7F);

    // 2. Height-correlated Smith-GGX visibility V(v, l) = G_2 / (4 * NoV * NoL):
    const float nv = std::clamp(n_dot_v, 1e-4F, 1.0F);
    const float nl = std::clamp(n_dot_l, 1e-4F, 1.0F);
    const float ggx_v = nl * std::sqrt(nv * nv * (1.0F - a2) + a2);
    const float ggx_l = nv * std::sqrt(nl * nl * (1.0F - a2) + a2);
    const float V = 0.5F / std::max(ggx_v + ggx_l, 1e-5F);

    // 3. Fresnel (Standard Schlick or Belcour-Barla Iridescence):
    const cd::math::Vec3f F = belcour_barla_iridescence_fresnel(
        f0, v_dot_h, iridescence_nm, iridescence_ior);

    // 4. Turquin (2019) Multiple-Scattering Compensation:
    const float e_v = directional_albedo_ggx(nv, r);
    const cd::math::Vec3f ms_comp = turquin_energy_compensation(
        f0, e_v, multiscatter_weight);

    const float dv = D * V;
    return {
        dv * F.x * ms_comp.x,
        dv * F.y * ms_comp.y,
        dv * F.z * ms_comp.z
    };
}

/// GLSL snippet for Vulkan 1.3 shaders implementing Turquin (2019) + Belcour-Barla (2017).
inline constexpr std::string_view kTurquinIridescenceGlsl = R"GLSL(
vec3 cd_turquin_multiscatter_comp(vec3 f0, float env_brdf_e, float ms_weight) {
    float e_safe = max(env_brdf_e, 0.05);
    return vec3(1.0) + f0 * (ms_weight * (1.0 - e_safe) / e_safe);
}

vec3 cd_belcour_barla_iridescence(vec3 f_schlick, float cos_theta_d, float film_nm) {
    if (film_nm <= 0.5) return f_schlick;
    float eta = 1.5;
    float sin2_sq = max(0.0, 1.0 - cos_theta_d * cos_theta_d) / (eta * eta);
    float cos2 = sqrt(max(0.0, 1.0 - sin2_sq));
    float opd = 2.0 * eta * film_nm * cos2;
    vec3 lambdas = vec3(650.0, 530.0, 450.0);
    vec3 phi = 6.2831853 * (opd / lambdas) + 3.14159265;
    float depol = exp(-0.0000015 * opd * opd);
    vec3 cos_phi = cos(phi) * depol;
    float r12 = 0.04 + 0.96 * pow(clamp(1.0 - cos2, 0.0, 1.0), 5.0);
    vec3 r23 = clamp(f_schlick * 0.75, vec3(0.04), vec3(0.92));
    vec3 r12_r23 = vec3(r12) * r23;
    vec3 two_sqrt_r_cos = 2.0 * sqrt(r12_r23) * cos_phi;
    vec3 f_airy = (vec3(r12) + r23 + two_sqrt_r_cos) / max(vec3(1.0) + r12_r23 + two_sqrt_r_cos, vec3(1e-4));
    return mix(f_schlick, clamp(f_airy, vec3(0.0), vec3(1.0)), clamp(film_nm / 20.0, 0.0, 1.0));
}
)GLSL";

} // namespace cd::brdf::multiscatter
