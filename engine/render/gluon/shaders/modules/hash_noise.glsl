// =============================================================================
// cd::gluon — hash_noise.glsl
// P0-A wave 1 (ADR-20260613-gluon-module-manifest §2.3): hashing + noise.
//
// SOURCE / DERIVATION:
//   * cd_pcg / cd_rand — VERBATIM (math) from the engine's DOMINANT RNG copy,
//     byte-identical in BOTH restir reservoirs:
//       engine/render/restir_di/include/cd/restir_di/Reservoir.hpp:189-197
//       engine/render/restir_gi/include/cd/restir_gi/GiReservoir.hpp:172-180
//     (PCG hash + float rand(inout uint); O'Neill 2014 "PCG: A Family of
//     Simple Fast Space-Efficient Statistically Good Algorithms for RNG").
//     Renamed cd_ only so both restir passes can later #include this verbatim.
//   * cd_hash21 / cd_value_noise / cd_fbm4 — VERBATIM (math) from the engine's
//     DOMINANT GLSL value-noise copy in
//       engine/render/post/include/cd/post/composite/Composite.hpp:382-414
//     (hash21 + quintic-Hermite value noise + 6-octave rotated fBm).
//   * cd_hash11 / cd_hash33 — SOTA-standard scalar/vec3 hash companions
//     (no engine GLSL copy; same sin-fract family as the dominant cd_hash21).
//   * cd_gradient_noise (Perlin-style) + cd_curl_noise — SOTA-standard
//     (Perlin 2002 improved-noise gradients; curl = analytic finite-difference
//     of the value-noise potential field for divergence-free flow).
//
// RULES (ADR-20260612 §2.2): may ONLY #include math_common.glsl; pure fns.
// =============================================================================
#ifndef CD_GLUON_HASH_NOISE_GLSL
#define CD_GLUON_HASH_NOISE_GLSL

#include <cd/gluon/math_common.glsl>

// --- PCG integer hash + canonical RNG (restir DOMINANT) ----------------------

/// PCG output-permutation hash (O'Neill 2014).
uint cd_pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}

/// Advance `state` and return a uniform float in [0,1).
float cd_rand(inout uint state) {
    state = cd_pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

// --- Float hashes (composite DOMINANT cd_hash21 + SOTA companions) -----------

/// 1D → [0,1) hash (sin-fract family, matches cd_hash21's irrational rotation).
float cd_hash11(float p) {
    return fract(sin(p * 127.1 + 311.7) * 43758.5453);
}

/// 2D → [0,1) hash (composite DOMINANT — three irrational rotations).
float cd_hash21(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return fract(sin(p.x + p.y) * 43758.5453);
}

/// 3D → vec3 [0,1) hash (SOTA companion).
vec3 cd_hash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453);
}

// --- Value noise + fBm (composite DOMINANT) ----------------------------------

/// Quintic-Hermite value noise (Perlin "improved" 2002 fade curve).
float cd_value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = cd_hash21(i);
    float b = cd_hash21(i + vec2(1.0, 0.0));
    float c = cd_hash21(i + vec2(0.0, 1.0));
    float d = cd_hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/// 6-octave fractional Brownian motion with per-octave ~37° rotation.
float cd_fbm4(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    const mat2 kRot = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < 6; ++i) {
        s += a * cd_value_noise(p);
        p = kRot * p * 2.07 + vec2(31.7, 17.3);
        a *= 0.5;
    }
    return s;
}

// --- Perlin gradient noise + curl (SOTA-standard) ----------------------------

/// Perlin-style gradient noise in [-1,1] (improved-noise gradients 2002).
float cd_gradient_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    // Per-corner pseudo-gradient unit vectors from the hash.
    vec2 ga = normalize(cd_hash33(vec3(i,                  0.0)).xy * 2.0 - 1.0);
    vec2 gb = normalize(cd_hash33(vec3(i + vec2(1.0, 0.0), 0.0)).xy * 2.0 - 1.0);
    vec2 gc = normalize(cd_hash33(vec3(i + vec2(0.0, 1.0), 0.0)).xy * 2.0 - 1.0);
    vec2 gd = normalize(cd_hash33(vec3(i + vec2(1.0, 1.0), 0.0)).xy * 2.0 - 1.0);
    float va = dot(ga, f - vec2(0.0, 0.0));
    float vb = dot(gb, f - vec2(1.0, 0.0));
    float vc = dot(gc, f - vec2(0.0, 1.0));
    float vd = dot(gd, f - vec2(1.0, 1.0));
    return mix(mix(va, vb, u.x), mix(vc, vd, u.x), u.y);
}

/// Divergence-free curl of the value-noise potential (analytic central
/// difference). Drives flow-map / cloud advection without sources/sinks.
vec2 cd_curl_noise(vec2 p) {
    const float e = 1e-3;
    float n_x1 = cd_value_noise(p + vec2(0.0,  e));
    float n_x0 = cd_value_noise(p - vec2(0.0,  e));
    float n_y1 = cd_value_noise(p + vec2(e,  0.0));
    float n_y0 = cd_value_noise(p - vec2(e,  0.0));
    float dndy = (n_x1 - n_x0) / (2.0 * e);
    float dndx = (n_y1 - n_y0) / (2.0 * e);
    return vec2(dndy, -dndx);
}

#endif  // CD_GLUON_HASH_NOISE_GLSL
