// =============================================================================
// cd::shader_lib — brdf.glsl
// Microfacet BRDF building blocks. Canonical replacements for the Fresnel
// ×9 / GGX-D ×5 / Smith-G ×6 copies the phase1128 inventory found across
// engine libs and samples.
//
// CONVENTIONS (ADR-20260612-shader-library-architecture §2.2):
//   * direct-lighting diffuse terms carry the /PI and a `_pi` suffix;
//     IBL/split-sum variants are PI-free;
//   * `roughness` is PERCEPTUAL (artist-facing); `alpha = roughness²`
//     is derived inside — pass perceptual roughness, never alpha;
//   * all NdotX inputs are expected unclamped; functions clamp locally.
// References: Karis 2013 (UE4 course notes), Lagarde & de Rousiers 2014
// (Frostbite PBR), Heitz 2014 (height-correlated Smith).
// =============================================================================
#ifndef CD_SL_BRDF_GLSL
#define CD_SL_BRDF_GLSL

#include <cd/shader_lib/math_common.glsl>

/// Schlick Fresnel with configurable grazing reflectance f90.
vec3 cd_f_schlick(vec3 f0, float f90, float voh)
{
    return f0 + (vec3(f90) - f0) * cd_pow5(1.0 - cd_saturate(voh));
}

/// Schlick Fresnel, f90 = 1 (the common dielectric/metal case).
vec3 cd_f_schlick(vec3 f0, float voh)
{
    return cd_f_schlick(f0, 1.0, voh);
}

/// GGX / Trowbridge-Reitz normal distribution.
float cd_d_ggx(float noh, float perceptual_roughness)
{
    float a  = perceptual_roughness * perceptual_roughness;
    float a2 = a * a;
    float nh = cd_saturate(noh);
    float d  = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / max(CD_PI * d * d, CD_EPSILON);
}

/// Height-correlated Smith visibility term (Heitz 2014). Combines the
/// geometry term G with the 1/(4 NoV NoL) BRDF denominator — multiply
/// D * V * F directly.
float cd_v_smith_ggx_correlated(float nov, float nol, float perceptual_roughness)
{
    float a  = perceptual_roughness * perceptual_roughness;
    float a2 = a * a;
    float v  = max(nov, CD_EPSILON);
    float l  = max(nol, CD_EPSILON);
    float lv = l * sqrt(v * v * (1.0 - a2) + a2);
    float ll = v * sqrt(l * l * (1.0 - a2) + a2);
    return 0.5 / max(lv + ll, CD_EPSILON);
}

/// Fast approximation of the height-correlated visibility (Hammon 2017
/// / Lagarde mobile path). Use when ALU is the bottleneck.
float cd_v_smith_ggx_fast(float nov, float nol, float perceptual_roughness)
{
    float a = perceptual_roughness * perceptual_roughness;
    float v = max(nov, CD_EPSILON);
    float l = max(nol, CD_EPSILON);
    return 0.5 / mix(2.0 * l * v, l + v, a);
}

/// Lambert diffuse, energy-normalised for direct lighting (carries /PI).
vec3 cd_fd_lambert_pi(vec3 diffuse_color)
{
    return diffuse_color * CD_INV_PI;
}

/// Disney/Burley diffuse with retro-reflection, /PI included.
/// (Burley 2012; the Frostbite-renormalised form.)
vec3 cd_fd_burley_pi(vec3 diffuse_color, float nov, float nol, float loh,
                     float perceptual_roughness)
{
    float f90 = 0.5 + 2.0 * perceptual_roughness * loh * loh;
    float lf  = 1.0 + (f90 - 1.0) * cd_pow5(1.0 - cd_saturate(nol));
    float vf  = 1.0 + (f90 - 1.0) * cd_pow5(1.0 - cd_saturate(nov));
    return diffuse_color * (lf * vf * CD_INV_PI);
}

/// Full specular lobe for direct lighting: D * V * F.
vec3 cd_specular_ggx(vec3 f0, float noh, float nov, float nol, float voh,
                     float perceptual_roughness)
{
    float d = cd_d_ggx(noh, perceptual_roughness);
    float v = cd_v_smith_ggx_correlated(nov, nol, perceptual_roughness);
    vec3  f = cd_f_schlick(f0, voh);
    return d * v * f;
}

#endif  // CD_SL_BRDF_GLSL
