// =============================================================================
// cd::gluon — brdf_sss.glsl
// P1 wave 1 (ADR-20260613-gluon-module-manifest §2.1(c) brdf_sss):
// exact-text migration of the FRAGMENT-usable subsurface-scattering lobe
// lifted VERBATIM from the engine library cd/brdf/sss/Sss.hpp
// (kInlineBurleyWrapGlsl sss_inline_wrap at line 126). Function body is
// byte-for-byte the engine string so a future consumer migration is
// pixel-neutral; only a cd_ prefix is added (manifest §2.4: cd_-prefixed).
//
// SCOPE NOTE: the separable Jiménez 2010 blur (kSssSeparableBlurCS,
// Sss.hpp:84) is a COMPUTE-shader dispatch body (#version 460 +
// local_size + image2D + push_constant + SSBO bindings); it is a full CS
// program, NOT a reusable fragment lobe — so it is INTENTIONALLY NOT
// migrated here (manifest brings only fragment-usable BRDF lobe helpers;
// the CS dispatch stays in the cd::brdf::sss library, separate concern).
//
// References:
//   * Burley 2015 — "Extending the Disney BRDF to a BSDF with Integrated
//     Subsurface Scattering" (SIGGRAPH 2015) — the diffusion profile this
//     wrap term approximates inline.
//   * Jiménez & Gutierrez 2010 — "Screen-Space Perceptual Rendering of
//     Human Skin" (separable SSS — the full CS path, not migrated).
// =============================================================================
#ifndef CD_GLUON_BRDF_SSS_GLSL
#define CD_GLUON_BRDF_SSS_GLSL

#include <cd/gluon/math_common.glsl>

// Cheap fragment-shader wrap-diffusion approximation used inline when a
// full Burley separable-blur pass isn't available. Backlit pixels receive
// a warm subsurface bleed. VERBATIM from Sss.hpp:126 (sss_inline_wrap).
vec3 cd_sss_inline_wrap(vec3 N, vec3 L, float sun_intensity, float strength) {
  float backlit = clamp(dot(-N, L), 0.0, 1.0);
  vec3 tint = vec3(0.95, 0.55, 0.45);
  return tint * pow(backlit, 1.5) * strength * sun_intensity * 0.8;
}

#endif  // CD_GLUON_BRDF_SSS_GLSL
