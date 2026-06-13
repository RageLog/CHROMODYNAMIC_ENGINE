// =============================================================================
// cd::gluon — brdf_diffuse_ext.glsl
// P1 wave 2 (ADR-20260613-gluon-module-manifest §2.1(c) brdf_diffuse_ext):
// rough-diffuse extension lobes the engine does NOT yet have in GLSL form
// (manifest §1.3 grep BOŞ — the engine prim path is Lambert/Burley only).
// SOTA-standard implementation (no DOMINANT engine copy to lift verbatim).
//
// This is a SEPARATE extension module — it does NOT #include brdf.glsl
// (Lambert/Burley live there; manifest §2.2 forbids module→module include).
// Per the /PI convention (manifest §brdf / brdf.glsl line 9): direct-lighting
// diffuse terms carry the 1/PI normalisation and a `_pi` suffix.
//
// References:
//   * Oren & Nayar 1994 — "Generalization of Lambert's Reflectance Model"
//     (SIGGRAPH) — the qualitative (A,B) rough-diffuse model.
//   * Fujii — "A tiny improvement of Oren-Nayar reflectance model"
//     (the optimised single-fit `cd_fd_oren_nayar_fast`).
// =============================================================================
#ifndef CD_GLUON_BRDF_DIFFUSE_EXT_GLSL
#define CD_GLUON_BRDF_DIFFUSE_EXT_GLSL

#include <cd/gluon/math_common.glsl>

/// Oren-Nayar qualitative rough-diffuse model (1994), energy-normalised for
/// direct lighting (carries /PI, `_pi` suffix per manifest §brdf). `sigma`
/// is the surface-slope standard deviation in radians (0 collapses to
/// Lambert). NdotL / NdotV are expected unclamped; clamped locally.
/// `gamma` is cos of the azimuth difference between the projected view and
/// light directions: gamma = cos(phi_v - phi_l).
vec3 cd_fd_oren_nayar(vec3 diffuse_color, float nol, float nov, float gamma,
                      float sigma)
{
    float s2    = sigma * sigma;
    float a     = 1.0 - 0.5 * (s2 / (s2 + 0.33));
    float b     = 0.45 * (s2 / (s2 + 0.09));

    float l     = cd_saturate(nol);
    float v     = cd_saturate(nov);
    float theta_l = acos(l);
    float theta_v = acos(v);
    float alpha = max(theta_l, theta_v);
    float beta  = min(theta_l, theta_v);

    float c     = max(0.0, gamma);
    float term  = a + b * c * sin(alpha) * tan(beta);
    return diffuse_color * (l * term * CD_INV_PI);
}

/// Fujii's optimised Oren-Nayar fit — a single-pass approximation that
/// avoids the acos/tan transcendentals of the qualitative form while
/// staying close to the reference. `roughness` is the perceptual (0..1)
/// surface roughness. Carries /PI (`_pi` convention). LdotV is the cosine
/// between the light and view directions (= dot(L, V)).
vec3 cd_fd_oren_nayar_fast(vec3 diffuse_color, float nol, float nov,
                           float lov, float roughness)
{
    float l  = cd_saturate(nol);
    float v  = cd_saturate(nov);
    // s = L·V - (N·L)(N·V) is the (unnormalised) azimuth term.
    float s  = lov - l * v;
    // t normalises s into the inter-reflection geometry (Fujii).
    float t  = (s <= 0.0) ? 1.0 : (1.0 / max(max(l, v), CD_EPSILON));
    float s2 = roughness * roughness;
    float a  = 1.0 / (CD_PI + (CD_PI * 0.5 - 2.0 / 3.0) * s2);
    float b  = s2 * a;
    return diffuse_color * (l * (a * CD_PI + b * s * t));
}

#endif  // CD_GLUON_BRDF_DIFFUSE_EXT_GLSL
