// =============================================================================
// cd::gluon — sampling.glsl
// P0-A wave 1 (ADR-20260613-gluon-module-manifest §2.3): low-discrepancy
// sequences + importance sampling for Monte-Carlo BRDF / IBL integration.
//
// SOURCE / DERIVATION:
//   No DOMINANT GLSL copy exists in the engine (manifest §1.3: the
//   Hammersley / radical-inverse / importance-GGX trio lives only as CPU
//   C++ in ibl/BrdfLut.hpp + ibl/PrefilteredSpecular.hpp + material/BrdfLut.hpp,
//   and the IBL bake is NOT regenerated — ADR-20260612 §2.6). These are
//   therefore SOTA-standard GLSL whose MATH mirrors the dominant CPU form:
//   * cd_radical_inverse_vdc — van der Corput base-2 bit-reverse, identical
//     constant 2.3283064365386963e-10 as ibl/PrefilteredSpecular.hpp:45-53.
//   * cd_hammersley — same (i/N, vdC) pair as ibl/BrdfLut.hpp:51-61
//     (Hammersley low-discrepancy sequence).
//   * cd_importance_sample_ggx — Karis 2013 "Real Shading in Unreal Engine 4"
//     GGX half-vector importance sample; cos_theta formula matches
//     ibl/BrdfLut.hpp:64-96 sample_ggx (a = roughness², the canonical
//     Karis/Frostbite split-sum form).
//   * cd_cosine_sample_hemisphere / cd_uniform_sample_sphere /
//     cd_uniform_sample_disk — SOTA-standard (PBR concentric / cosine maps).
//   Tangent→world uses cd_onb (Duff 2017) from math_common for a consistent
//   branchless basis.
//
// RULES (ADR-20260612 §2.2): may ONLY #include math_common.glsl; pure fns.
// =============================================================================
#ifndef CD_GLUON_SAMPLING_GLSL
#define CD_GLUON_SAMPLING_GLSL

#include <cd/gluon/math_common.glsl>

// --- Low-discrepancy sequences (van der Corput / Hammersley) -----------------

/// Radical inverse base 2 (van der Corput) of `bits` via bit reversal.
float cd_radical_inverse_vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

/// i-th of N points of the 2D Hammersley sequence: (i/N, vdC(i)).
vec2 cd_hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), cd_radical_inverse_vdc(i));
}

// --- GGX importance sampling (Karis split-sum) -------------------------------

/// GGX importance-sampled half-vector around `n` for the given roughness,
/// driven by a 2D low-discrepancy sample `xi` ∈ [0,1)². a = roughness².
vec3 cd_importance_sample_ggx(vec2 xi, float roughness, vec3 n) {
    float a   = roughness * roughness;
    float phi = CD_TWO_PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));

    // Tangent-space half-vector → world via the Duff ONB.
    vec3 h = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    vec3 b1; vec3 b2;
    cd_onb(n, b1, b2);
    return b1 * h.x + b2 * h.y + n * h.z;
}

// --- Cosine / uniform mappings -----------------------------------------------

/// Cosine-weighted hemisphere direction around `n` from `xi` ∈ [0,1)²
/// (Malley's method: uniform disk then project to the hemisphere).
vec3 cd_cosine_sample_hemisphere(vec2 xi, vec3 n) {
    float r   = sqrt(xi.x);
    float phi = CD_TWO_PI * xi.y;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - xi.x));
    vec3 b1; vec3 b2;
    cd_onb(n, b1, b2);
    return b1 * x + b2 * y + n * z;
}

/// Uniform direction on the unit sphere from `xi` ∈ [0,1)².
vec3 cd_uniform_sample_sphere(vec2 xi) {
    float z   = 1.0 - 2.0 * xi.x;
    float r   = sqrt(max(0.0, 1.0 - z * z));
    float phi = CD_TWO_PI * xi.y;
    return vec3(r * cos(phi), r * sin(phi), z);
}

/// Concentric uniform point on the unit disk from `xi` ∈ [0,1)²
/// (Shirley–Chiu concentric map: low distortion vs. polar).
vec2 cd_uniform_sample_disk(vec2 xi) {
    vec2 o = 2.0 * xi - 1.0;
    if (o.x == 0.0 && o.y == 0.0) return vec2(0.0);
    float r;
    float theta;
    if (abs(o.x) > abs(o.y)) {
        r     = o.x;
        theta = (CD_PI * 0.25) * (o.y / o.x);
    } else {
        r     = o.y;
        theta = CD_HALF_PI - (CD_PI * 0.25) * (o.x / o.y);
    }
    return r * vec2(cos(theta), sin(theta));
}

#endif  // CD_GLUON_SAMPLING_GLSL
