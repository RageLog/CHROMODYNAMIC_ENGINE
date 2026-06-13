// =============================================================================
// cd::gluon — brdf_anisotropy.glsl
// P1 wave 2 (ADR-20260613-gluon-module-manifest §2.1(c) brdf_anisotropy):
// anisotropic microfacet terms the engine does NOT yet have in GLSL form
// (manifest §1.3 grep BOŞ — the engine prim path is isotropic GGX only).
// SOTA-standard implementation (no DOMINANT engine copy to lift verbatim).
//
// SEPARATE extension module — does NOT #include brdf.glsl (isotropic GGX-D /
// Smith-V live there; manifest §2.2 forbids module→module include). The
// roughness inputs are the ANISOTROPIC ALPHAS (at = roughness² along the
// tangent, ab = roughness² along the bitangent) — the caller derives them
// from a perceptual roughness + anisotropy parameter (Burley 2012 mapping).
//
// References:
//   * Burley 2012 — "Physically Based Shading at Disney" (the aspect-ratio
//     anisotropic roughness split at/ab and the anisotropic GGX NDF).
//   * Kulla & Conty 2017 / Filament `surface_brdf` — the production form
//     of the anisotropic GGX distribution used here.
//   * Heitz 2014 — "Understanding the Masking-Shadowing Function" (the
//     height-correlated Smith visibility, anisotropic extension).
// =============================================================================
#ifndef CD_GLUON_BRDF_ANISOTROPY_GLSL
#define CD_GLUON_BRDF_ANISOTROPY_GLSL

#include <cd/gluon/math_common.glsl>

/// Anisotropic GGX / Trowbridge-Reitz normal distribution (Burley 2012,
/// Kulla-Conty/Filament form). `at` / `ab` are the anisotropic alphas
/// (tangent / bitangent roughness²). `toh` = T·H, `boh` = B·H, `noh` = N·H.
float cd_d_ggx_aniso(float at, float ab, float toh, float boh, float noh)
{
    float a2 = at * ab;
    vec3  d  = vec3(ab * toh, at * boh, a2 * noh);
    float d2 = dot(d, d);
    float k  = a2 / max(d2, CD_EPSILON);
    return a2 * k * k * CD_INV_PI;
}

/// Height-correlated anisotropic Smith visibility (Heitz 2014). Combines
/// the masking-shadowing G with the 1/(4 NoV NoL) BRDF denominator — the
/// result multiplies D * V * F directly. `at`/`ab` are the anisotropic
/// alphas; the tangent/bitangent view & light cosines are passed explicitly.
float cd_v_smith_ggx_aniso(float at, float ab,
                           float tov, float bov, float nov,
                           float tol, float bol, float nol)
{
    float v = max(nov, CD_EPSILON);
    float l = max(nol, CD_EPSILON);
    float lambda_v = l * length(vec3(at * tov, ab * bov, v));
    float lambda_l = v * length(vec3(at * tol, ab * bol, l));
    return 0.5 / max(lambda_v + lambda_l, CD_EPSILON);
}

#endif  // CD_GLUON_BRDF_ANISOTROPY_GLSL
