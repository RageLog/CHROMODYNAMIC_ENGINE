// =============================================================================
// CHROMODYNAMIC — cd/restir_gi/GiReservoir.hpp
// Day 7/L — ReSTIR GI (Ouyang 2021).
//
// Reservoir-based indirect illumination. A GI sample is one full
// bounce (origin + direction + cached incoming radiance). The
// per-pixel reservoir streams candidate bounces and reuses them
// spatially / temporally — same shape as DI's reservoir (Day 6) but
// the survivors carry the world-space geometry of the bounce point
// instead of a light index.
//
// Reference: Ouyang, Liu, Toth, Ramani, Whaley, Lefohn —
// "ReSTIR GI: Path Resampling for Real-Time Path Tracing" (HPG 2021).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::restir_gi
{

/// One indirect-bounce sample. `point` + `normal` describe the
/// secondary hit; `incoming` is the cached radiance arriving from
/// further bounces (computed once when the sample was generated).
struct Sample
{
    cd::math::Vec3f point     { 0, 0, 0 };
    cd::math::Vec3f normal    { 0, 0, 1 };
    cd::math::Vec3f incoming  { 0, 0, 0 };
    /// Visibility marker (1 = sample is currently reachable from
    /// the receiver pixel; 0 = recheck on reuse).
    std::uint8_t    valid     { 1 };
};

struct Reservoir
{
    Sample        selected   {};
    float         weight_sum { 0.0F };
    std::uint32_t M          { 0 };
    /// Age in frames — incremented each frame the reservoir is carried
    /// forward without a new candidate. Used by temporal passes to
    /// invalidate stale reservoirs.
    std::uint32_t age        { 0 };

    [[nodiscard]] float final_weight(float target_pdf) const noexcept
    {
        if (M == 0 || target_pdf <= 0.0F) return 0.0F;
        return weight_sum / (static_cast<float>(M) * target_pdf);
    }

    /// Reset to zero / invalid state.
    void invalidate() noexcept
    {
        *this = Reservoir{};
    }
};

/// WRS streaming update. `target_pdf` is f_r(receiver -> sample)
/// |cos| / dist² — typical RIS target for GI reuse.
inline void
update(Reservoir& r, const Sample& s, float weight, float rand_01) noexcept
{
    r.weight_sum += weight;
    r.M += 1;
    if (r.weight_sum > 0.0F && rand_01 < weight / r.weight_sum)
        r.selected = s;
}

/// Combine two GI reservoirs. `eval_target` returns the donor
/// sample's target PDF evaluated at the receiver's geometry —
/// re-projecting the cached incoming radiance through the new
/// geometric factor (BRDF, cosines, distance²).
template <typename F>
inline void combine(Reservoir& dst,
                    const Reservoir& other,
                    float            other_target_pdf,
                    float            rand_01,
                    F&&              eval_target) noexcept
{
    if (other.M == 0) return;
    const float p_hat = std::forward<F>(eval_target)(other.selected);
    const float w     = p_hat * other.final_weight(other_target_pdf)
                                  * static_cast<float>(other.M);
    dst.weight_sum += w;
    dst.M += other.M;
    if (dst.weight_sum > 0.0F && rand_01 < w / dst.weight_sum)
    {
        dst.selected = other.selected;
        dst.selected.valid = 0;  // Ouyang: visibility must be re-tested
    }
}

inline void clamp_history(Reservoir& r, std::uint32_t cap) noexcept
{
    if (r.M > cap) { r.weight_sum *= static_cast<float>(cap) / static_cast<float>(r.M); r.M = cap; }
}

/// Temporal blend of two GI reservoirs with blending factor alpha in [0,1].
/// alpha = 0 -> pure current, alpha = 1 -> pure previous.
/// Used as a smoothing heuristic in biased temporal mode.
[[nodiscard]] inline Reservoir
temporal_blend(const Reservoir& current,
               const Reservoir& previous,
               float alpha) noexcept
{
    Reservoir result{};
    result.selected   = (alpha <= 0.5F) ? current.selected : previous.selected;
    result.weight_sum = (1.0F - alpha) * current.weight_sum
                      +          alpha  * previous.weight_sum;
    result.M          = static_cast<std::uint32_t>(
                            (1.0F - alpha) * static_cast<float>(current.M)
                          +          alpha  * static_cast<float>(previous.M));
    result.age        = current.age;
    return result;
}

// ---- GLSL helpers -----------------------------------------------------------

constexpr std::string_view kGiReservoirGlsl = R"glsl(
struct GiSample {
  vec3 point;    float pad0;
  vec3 normal;   float pad1;
  vec3 incoming; uint  valid;
};
struct GiReservoir {
  GiSample selected;
  float    weight_sum;
  uint     M;
};
float gi_reservoir_final_weight(GiReservoir r, float target_pdf) {
  if (r.M == 0 || target_pdf <= 0.0) return 0.0;
  return r.weight_sum / (float(r.M) * target_pdf);
}
void gi_reservoir_update(inout GiReservoir r, GiSample s, float w, float rnd) {
  r.weight_sum += w;
  r.M          += 1u;
  if (r.weight_sum > 0.0 && rnd < w / r.weight_sum) r.selected = s;
}
)glsl";

// ---- ReSTIR GI compute shader strings ---------------------------------------

/// Initial GI sample generation.
/// Each invocation traces one path (primary + one bounce) and stores
/// the secondary hit geometry + incoming radiance into the output reservoir.
constexpr std::string_view kRestirGiSampleCS = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(local_size_x = 8, local_size_y = 8) in;

struct GiSample   { vec3 point; float pad0; vec3 normal; float pad1; vec3 incoming; uint valid; };
struct GiReservoir{ GiSample selected; float weight_sum; uint M; uint age; };

layout(set = 0, binding = 0, std430) buffer GiReservoirOut {
    GiReservoir reservoirs[];
};
layout(set = 0, binding = 1) uniform accelerationStructureEXT u_TLAS;
layout(set = 0, binding = 2) uniform sampler2D u_GBufNormal;
layout(set = 0, binding = 3) uniform sampler2D u_GBufDepth;
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  frame_index;
    uint  candidates;   // typically 1 for GI (single-bounce per pixel)
    mat4  inv_proj_view;
    vec4  camera_pos;   // xyz = pos
} pc;

uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}
float rand(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

// Cosine-weighted hemisphere sample in local frame.
vec3 cosine_sample(vec3 N, inout uint seed) {
    float u1 = rand(seed), u2 = rand(seed);
    float r   = sqrt(u1);
    float phi = 6.28318530 * u2;
    vec3 T    = normalize(abs(N.x) > 0.9 ? cross(N, vec3(0,1,0)) : cross(N, vec3(1,0,0)));
    vec3 B    = cross(N, T);
    return r * cos(phi) * T + r * sin(phi) * B + sqrt(1.0 - u1) * N;
}

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint seed = pcg(px.x + px.y * pc.resolution.x + pc.frame_index * 3571u);
    vec3 N = texelFetch(u_GBufNormal, ivec2(px), 0).xyz * 2.0 - 1.0;

    GiReservoir R;
    R.selected.valid = 0u;
    R.weight_sum = 0.0;
    R.M = 0u;
    R.age = 0u;

    for (uint i = 0u; i < pc.candidates; ++i) {
        vec3 dir = cosine_sample(N, seed);
        // Placeholder: actual RT traversal would fill hit point + incoming.
        // In a real shader this issues a ray query against u_TLAS.
        GiSample s;
        s.point    = pc.camera_pos.xyz + dir * 1.0; // placeholder hit
        s.normal   = -dir;
        s.incoming = vec3(0.1);   // placeholder cached radiance
        s.valid    = 1u;
        float p_hat = max(0.0, dot(N, dir));
        float w = p_hat;
        R.weight_sum += w;
        R.M          += 1u;
        if (R.weight_sum > 0.0 && rand(seed) < w / R.weight_sum)
            R.selected = s;
    }

    uint idx = px.y * pc.resolution.x + px.x;
    reservoirs[idx] = R;
}
)glsl";

/// GI temporal reuse pass — reprojects previous-frame GI reservoir.
constexpr std::string_view kRestirGiTemporalReuseCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct GiSample   { vec3 point; float pad0; vec3 normal; float pad1; vec3 incoming; uint valid; };
struct GiReservoir{ GiSample selected; float weight_sum; uint M; uint age; };

float gi_fw(GiReservoir r, float p_hat) {
    if (r.M == 0u || p_hat <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * p_hat);
}

layout(set = 0, binding = 0, std430) readonly buffer GiReservoirCurrent  { GiReservoir cur[];  };
layout(set = 0, binding = 1, std430) readonly buffer GiReservoirPrevious { GiReservoir prev[]; };
layout(set = 0, binding = 2, std430) buffer        GiReservoirOut        { GiReservoir out[];  };
layout(set = 0, binding = 3) uniform sampler2D u_MotionVectors;
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  frame_index;
    uint  M_cap;
} pc;

uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}
float rand(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint idx = px.y * pc.resolution.x + px.x;
    GiReservoir R = cur[idx];

    vec2  mv   = texelFetch(u_MotionVectors, ivec2(px), 0).rg;
    ivec2 ppx  = ivec2(vec2(px) - mv * vec2(pc.resolution) + 0.5);
    bool  ok   = all(greaterThanEqual(ppx, ivec2(0))) &&
                 all(lessThan(ppx, ivec2(pc.resolution)));

    if (ok) {
        uint pidx = uint(ppx.y) * pc.resolution.x + uint(ppx.x);
        GiReservoir P = prev[pidx];
        if (P.age > 30u) { P.weight_sum = 0.0; P.M = 0u; }

        float p_hat = dot(P.selected.incoming, P.selected.incoming); // luminance proxy
        float w = p_hat * gi_fw(P, p_hat) * float(P.M);
        R.weight_sum += w;
        R.M          += P.M;
        uint seed = pcg(idx ^ pc.frame_index * 48271u);
        if (R.weight_sum > 0.0 && rand(seed) < w / R.weight_sum) {
            R.selected       = P.selected;
            R.selected.valid = 0u;  // re-test visibility
        }
        if (R.M > pc.M_cap) {
            R.weight_sum *= float(pc.M_cap) / float(R.M);
            R.M = pc.M_cap;
        }
    }

    R.age = cur[idx].age + 1u;
    out[idx] = R;
}
)glsl";

/// GI spatial reuse pass — 5-tap neighbourhood combine.
constexpr std::string_view kRestirGiSpatialReuseCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct GiSample   { vec3 point; float pad0; vec3 normal; float pad1; vec3 incoming; uint valid; };
struct GiReservoir{ GiSample selected; float weight_sum; uint M; uint age; };

float gi_fw(GiReservoir r, float p_hat) {
    if (r.M == 0u || p_hat <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * p_hat);
}

layout(set = 0, binding = 0, std430) readonly buffer GiIn  { GiReservoir in_res[];  };
layout(set = 0, binding = 1, std430) buffer           GiOut { GiReservoir out_res[]; };
layout(set = 0, binding = 2) uniform sampler2D u_GBufNormal;
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  frame_index;
    uint  spatial_radius;
    uint  spatial_taps;
} pc;

uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}
float rand(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

const float kPi = 3.14159265358979;

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint seed = pcg(px.x + px.y * pc.resolution.x + pc.frame_index * 69069u);
    uint idx  = px.y * pc.resolution.x + px.x;

    GiReservoir R = in_res[idx];
    vec3 N_r = texelFetch(u_GBufNormal, ivec2(px), 0).xyz * 2.0 - 1.0;

    float theta0 = rand(seed) * 2.0 * kPi;

    for (uint t = 0u; t < pc.spatial_taps; ++t) {
        float theta = theta0 + float(t) * (2.0 * kPi / float(pc.spatial_taps));
        float r     = float(pc.spatial_radius) * sqrt(rand(seed));
        ivec2 npx   = ivec2(vec2(px) + r * vec2(cos(theta), sin(theta)));
        if (any(lessThan(npx, ivec2(0))) || any(greaterThanEqual(npx, ivec2(pc.resolution))))
            continue;

        vec3 N_n = texelFetch(u_GBufNormal, npx, 0).xyz * 2.0 - 1.0;
        if (dot(N_r, N_n) < 0.906) continue;

        uint nidx = uint(npx.y) * pc.resolution.x + uint(npx.x);
        GiReservoir nei = in_res[nidx];
        if (nei.M == 0u) continue;

        float p_hat = dot(nei.selected.incoming, nei.selected.incoming)
                    * max(0.0, dot(N_r, N_n));
        float w = p_hat * gi_fw(nei, p_hat) * float(nei.M);
        R.weight_sum += w;
        R.M          += nei.M;
        if (R.weight_sum > 0.0 && rand(seed) < w / R.weight_sum) {
            R.selected       = nei.selected;
            R.selected.valid = 0u;
        }
    }

    out_res[idx] = R;
}
)glsl";

}  // namespace cd::restir_gi
