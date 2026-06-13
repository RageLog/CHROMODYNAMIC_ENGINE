// =============================================================================
// cd::gluon — brdf_clearcoat.glsl
// P1 wave 1 (ADR-20260613-gluon-module-manifest §2.1(c) brdf_clearcoat):
// exact-text migration of the clearcoat lobe lifted VERBATIM from the
// engine library cd/brdf/sheen_clearcoat/SheenClearcoat.hpp
// (kSheenClearcoatGlsl clearcoat_dv at line 81; kInlineRimApproxGlsl
// clearcoat_inline_lobe at line 96). Function bodies are byte-for-byte the
// engine string so a future consumer migration is pixel-neutral; only a
// cd_ prefix is added (manifest §2.4: cd_-prefixed). Sheen lobe is a
// SEPARATE module (brdf_sheen.glsl) — God-module split per manifest §2.2.
//
// References:
//   * Filament docs — clearcoat model (GGX clearcoat lobe with 0.045
//     hard min-roughness floor so the layer never collapses to a mirror).
// =============================================================================
#ifndef CD_GLUON_BRDF_CLEARCOAT_GLSL
#define CD_GLUON_BRDF_CLEARCOAT_GLSL

#include <cd/gluon/math_common.glsl>

// Filament clearcoat D * V product. GGX with a hard 0.045 min-roughness
// floor. VERBATIM from SheenClearcoat.hpp:81 (clearcoat_dv).
// r = roughness, nh = N·H, nv = N·V, nl = N·L.
float cd_clearcoat_dv(float r, float nh, float nv, float nl) {
  float a  = max(r * r, 0.045 * 0.045);
  float a2 = a * a;
  float d  = (nh * nh) * (a2 - 1.0) + 1.0;
  float D  = a2 / (3.14159265 * d * d);
  float V  = 1.0 / (4.0 * nv * nl + 1e-4);
  return D * V;
}

// Cheap inline clearcoat rim approximation used when a full clearcoat
// lobe with proper half-vector + Fresnel-Schlick isn't running. VERBATIM
// from SheenClearcoat.hpp:96 (clearcoat_inline_lobe).
vec3 cd_clearcoat_inline_lobe(vec3 prefilt_spec, float NoV, float strength) {
  float fres_cc = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
  return prefilt_spec * fres_cc * strength * 0.6;
}

#endif  // CD_GLUON_BRDF_CLEARCOAT_GLSL
