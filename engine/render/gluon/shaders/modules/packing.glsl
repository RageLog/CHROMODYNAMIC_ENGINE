// =============================================================================
// cd::gluon — packing.glsl
// P0-A wave 1 (ADR-20260613-gluon-module-manifest §2.3): encode/decode helpers.
//
// SOURCE / DERIVATION:
//   * cd_oct_encode / cd_oct_decode — VERBATIM (math) from the engine's
//     DOMINANT octahedral copy in engine/render/ddgi/include/cd/ddgi/Ddgi.hpp
//     (oct_encode @ :392, oct_decode @ :462; Cigolle et al. 2014, JCGT
//     "A Survey of Efficient Representations for Independent Unit Vectors").
//     Renamed cd_ only — body byte-for-math identical so a future ddgi
//     migration to #include <cd/gluon/packing.glsl> is a pure rename.
//   * cd_normal_reconstruct_z — SOTA-standard 2-channel normal rebuild
//     (z = sqrt(1 - x² - y²)), the canonical hemisphere reconstruct used
//     when only the tangent-space XY of a unit normal is stored.
//   * cd_pack/unpack unorm/snorm — SOTA-standard fixed-point helpers.
//
// RULES (ADR-20260612 §2.2): may ONLY #include math_common.glsl; pure fns.
// =============================================================================
#ifndef CD_GLUON_PACKING_GLSL
#define CD_GLUON_PACKING_GLSL

#include <cd/gluon/math_common.glsl>

// --- Octahedral unit-vector packing (Cigolle 2014; ddgi DOMINANT) ------------

/// Encode a unit vector into octahedral [0,1]² (rg16f / unorm friendly).
vec2 cd_oct_encode(vec3 n) {
    float l = abs(n.x) + abs(n.y) + abs(n.z);
    vec2  p = n.xy / l;
    if (n.z < 0.0) p = (1.0 - abs(p.yx)) * sign(p);
    return p * 0.5 + 0.5;
}

/// Decode an octahedral [0,1]² pair back to a unit vector.
vec3 cd_oct_decode(vec2 e) {
    vec2  p = e * 2.0 - 1.0;
    vec3  n = vec3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return normalize(n);
}

// --- 2-channel normal reconstruct --------------------------------------------

/// Rebuild a (near-)unit normal from its stored tangent-space XY, assuming a
/// +Z hemisphere: z = sqrt(1 - x² - y²). The classic two-channel normal-map
/// reconstruct; clamps the radicand so a slightly-overlong XY can't NaN.
vec3 cd_normal_reconstruct_z(vec2 xy) {
    float z = sqrt(cd_saturate(1.0 - dot(xy, xy)));
    return vec3(xy, z);
}

// --- unorm / snorm fixed-point pack helpers ----------------------------------

/// [0,1] float → N-bit unorm integer (rounded).
float cd_pack_unorm(float x, float bits) {
    float maxv = exp2(bits) - 1.0;
    return floor(cd_saturate(x) * maxv + 0.5);
}

/// N-bit unorm integer → [0,1] float.
float cd_unpack_unorm(float q, float bits) {
    return q / (exp2(bits) - 1.0);
}

/// [-1,1] float → N-bit snorm integer (rounded).
float cd_pack_snorm(float x, float bits) {
    float maxv = exp2(bits - 1.0) - 1.0;
    return floor(clamp(x, -1.0, 1.0) * maxv + (x >= 0.0 ? 0.5 : -0.5));
}

/// N-bit snorm integer → [-1,1] float.
float cd_unpack_snorm(float q, float bits) {
    float maxv = exp2(bits - 1.0) - 1.0;
    return clamp(q / maxv, -1.0, 1.0);
}

#endif  // CD_GLUON_PACKING_GLSL
