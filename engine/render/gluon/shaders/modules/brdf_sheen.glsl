// =============================================================================
// cd::gluon — brdf_sheen.glsl
// P1 wave 1 (ADR-20260613-gluon-module-manifest §2.1(c) brdf_sheen):
// exact-text migration of the sheen lobe lifted VERBATIM from the engine
// library cd/brdf/sheen_clearcoat/SheenClearcoat.hpp (kSheenClearcoatGlsl
// charlie_d + v_neubelt at line 71; kInlineRimApproxGlsl sheen_inline_lobe
// at line 100). Function bodies are byte-for-byte the engine string so a
// future consumer migration is pixel-neutral; only a cd_ prefix is added
// (manifest §2.4: cd_-prefixed). Clearcoat lobe is a SEPARATE module
// (brdf_clearcoat.glsl) — God-module split per manifest §2.2.
//
// References:
//   * Estevez & Kulla 2017 — "Production Friendly Microfacet Sheen BRDF"
//     (Imageworks) — the Charlie distribution.
//   * Neubelt & Pettineo 2013 — the low-cost sheen visibility fit.
// =============================================================================
#ifndef CD_GLUON_BRDF_SHEEN_GLSL
#define CD_GLUON_BRDF_SHEEN_GLSL

#include <cd/gluon/math_common.glsl>

// Charlie sheen distribution (Estevez-Kulla 2017). VERBATIM from
// SheenClearcoat.hpp:72 (charlie_d). r = roughness, nh = N·H.
float cd_charlie_d(float r, float nh) {
  float a = max(r, 0.05);
  float i = 1.0 / a;
  float s2 = max(0.0, 1.0 - nh * nh);
  return (2.0 + i) * pow(s2, 0.5 * i) / 6.28318530;
}

// Neubelt visibility for the sheen layer. VERBATIM from
// SheenClearcoat.hpp:78 (v_neubelt).
float cd_v_neubelt(float nv, float nl) {
  return 1.0 / (4.0 * (nl + nv - nl * nv) + 1e-4);
}

// Cheap inline sheen rim approximation used when a full sheen lobe with
// proper half-vector isn't running. VERBATIM from
// SheenClearcoat.hpp:100 (sheen_inline_lobe).
vec3 cd_sheen_inline_lobe(float NoV, float strength) {
  float rim = pow(1.0 - NoV, 4.0);
  vec3 col = vec3(0.95, 0.92, 0.88);
  return col * rim * strength * 1.2;
}

#endif  // CD_GLUON_BRDF_SHEEN_GLSL
