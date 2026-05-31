// =============================================================================
// CHROMODYNAMIC — cd/restir_di/Reservoir.hpp
// Day 6/K — ReSTIR DI (Bitterli 2020).
//
// Per-pixel reservoir over candidate direct-illumination samples,
// with Weighted Reservoir Sampling (WRS) + temporal reuse +
// spatial reuse. Closes the "noisy 1-spp direct light" gap in the
// path tracer without raising sample count.
//
// API:
//   * Sample: candidate light index + radiance + target PDF.
//   * Reservoir: streaming pool of M samples reduced to 1.
//   * update / combine / temporal_reuse / spatial_reuse helpers.
//
// Reference: Bitterli, Wyman, Pharr, Shirley, Lefohn, Jarosz —
// "Spatiotemporal reservoir resampling for real-time ray tracing
// with dynamic direct lighting" (SIGGRAPH 2020 / TOG 39:4).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::restir_di
{

/// A single direct-illumination candidate sample. `light_index` is
/// the host-side light id; `radiance` is the unshadowed contribution
/// at the shading point; `target_pdf` is the unnormalised target PDF
/// used to drive the WRS weight.
struct Sample
{
    std::uint32_t   light_index { 0 };
    cd::math::Vec3f radiance    { 0, 0, 0 };
    float           target_pdf  { 0.0F };  ///< usually max-channel of radiance
};

/// Bitterli 2020 reservoir. `weight_sum` (W) tracks the running total
/// of resampling weights; `selected` holds the survivor; `M` counts
/// how many candidates streamed through (capped to avoid temporal
/// stale-history bias).
struct Reservoir
{
    Sample         selected   {};
    float          weight_sum { 0.0F };
    std::uint32_t  M          { 0 };
    /// Age in frames — incremented each frame the reservoir is carried
    /// forward without a new candidate. Used by TemporalBuffer to
    /// invalidate stale reservoirs.
    std::uint32_t  age        { 0 };

    /// Final per-sample contribution weight = (weight_sum / M / target_pdf).
    /// Computed once on demand (Bitterli Eq. 6).
    [[nodiscard]] float final_weight() const noexcept
    {
        if (M == 0 || selected.target_pdf <= 0.0F) return 0.0F;
        return weight_sum / (static_cast<float>(M) * selected.target_pdf);
    }

    /// Reset to zero-state (invalidated reservoir).
    void invalidate() noexcept
    {
        *this = Reservoir{};
    }
};

/// Stream a new candidate into the reservoir. `rand_01` is a uniform
/// random in [0, 1] used to decide whether the new sample replaces
/// the current survivor. `weight` is the resampling weight
/// (target_pdf / proposal_pdf).
inline void
update(Reservoir& r, const Sample& s, float weight, float rand_01) noexcept
{
    r.weight_sum += weight;
    r.M += 1;
    if (r.weight_sum <= 0.0F) return;
    if (rand_01 < weight / r.weight_sum) r.selected = s;
}

/// Combine two reservoirs (spatial / temporal reuse). Recomputes the
/// target PDF of the donor's sample in the receiver's domain via the
/// caller-provided functor `eval_pdf`, which returns the donor sample's
/// target_pdf evaluated at the receiver's shading point. Bitterli's
/// generalised RIS estimator.
template <typename F>
inline void combine(Reservoir& dst,
                    const Reservoir& other,
                    float rand_01,
                    F&& eval_pdf) noexcept
{
    if (other.M == 0) return;
    const float p_hat = std::forward<F>(eval_pdf)(other.selected);
    const float w = p_hat * other.final_weight() * static_cast<float>(other.M);
    dst.weight_sum += w;
    dst.M += other.M;
    if (dst.weight_sum > 0.0F && rand_01 < w / dst.weight_sum)
        dst.selected = other.selected;
}

/// Cap the reservoir's effective history. Bitterli recommends 20×
/// the initial M so disocclusion artefacts don't linger past a
/// reasonable number of frames.
inline void clamp_history(Reservoir& r, std::uint32_t cap) noexcept
{
    if (r.M > cap) { r.weight_sum *= static_cast<float>(cap) / static_cast<float>(r.M); r.M = cap; }
}

/// Temporal blend of two reservoirs with blending factor alpha in [0,1].
/// alpha = 0 → pure current, alpha = 1 → pure previous.  Intended for
/// smooth frame-to-frame interpolation of the weight_sum (not a strict
/// WRS operation — used as a smoothing heuristic when the estimator is
/// biased by design).
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

constexpr std::string_view kReservoirGlsl = R"glsl(
struct Sample {
  uint  light_index;
  float target_pdf;
  vec3  radiance;
};
struct Reservoir {
  Sample selected;
  float  weight_sum;
  uint   M;
};
float reservoir_final_weight(Reservoir r) {
  if (r.M == 0 || r.selected.target_pdf <= 0.0) return 0.0;
  return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}
void reservoir_update(inout Reservoir r, Sample s, float w, float rnd) {
  r.weight_sum += w;
  r.M          += 1u;
  if (r.weight_sum > 0.0 && rnd < w / r.weight_sum) r.selected = s;
}
)glsl";

// ---- ReSTIR DI compute shader strings ---------------------------------------

/// Initial candidate generation pass.
/// Each invocation handles one pixel. It picks kCandidates light indices
/// from a uniform distribution, evaluates the unshadowed target PDF
/// (luminance of radiance * G-term), runs WRS, and writes the surviving
/// reservoir into the output storage buffer.
constexpr std::string_view kRestirDiSampleCS = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(local_size_x = 8, local_size_y = 8) in;

// ---- Reservoir struct (shared with kReservoirGlsl) ----
struct DiSample { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

// ---- Bindings ----
layout(set = 0, binding = 0, std430) buffer ReservoirOut {
    DiReservoir reservoirs[];
};
layout(set = 0, binding = 1) uniform sampler2D u_GBufNormal;   // world-space normal
layout(set = 0, binding = 2) uniform sampler2D u_GBufDepth;    // linear depth
layout(set = 0, binding = 3, std430) readonly buffer LightBuf {
    vec4 light_positions_intensity[];  // xyz=pos, w=intensity
};
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  light_count;
    uint  frame_index;
    uint  candidates;   // M_initial (e.g. 32)
} pc;

// ---- Hash / PCG ----
uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}
float rand(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

// ---- Main ----
void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint seed = pcg(px.x + px.y * pc.resolution.x + pc.frame_index * 1973u);

    vec3  N  = texelFetch(u_GBufNormal, ivec2(px), 0).xyz * 2.0 - 1.0;
    float Z  = texelFetch(u_GBufDepth,  ivec2(px), 0).r;

    DiReservoir R;
    R.selected  = DiSample(0u, 0.0, vec3(0));
    R.weight_sum = 0.0;
    R.M          = 0u;
    R.age        = 0u;

    for (uint i = 0u; i < pc.candidates; ++i) {
        uint li = uint(rand(seed) * float(pc.light_count));
        vec4 lp = light_positions_intensity[li];
        // Simple unshadowed radiance estimate (Lambertian, no attenuation).
        float p_hat = max(0.0, dot(N, normalize(lp.xyz))) * lp.w;
        float w = p_hat;  // proposal is uniform -> q = 1/light_count (cancels in ratio)
        R.weight_sum += w;
        R.M += 1u;
        if (R.weight_sum > 0.0 && rand(seed) < w / R.weight_sum)
            R.selected = DiSample(li, p_hat, vec3(lp.w));
    }

    uint idx = px.y * pc.resolution.x + px.x;
    reservoirs[idx] = R;
}
)glsl";

/// Temporal reuse pass.
/// Reprojects the previous-frame reservoir into the current pixel via
/// motion vectors, combines with current frame reservoir, applies M cap.
constexpr std::string_view kRestirDiTemporalReuseCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct DiSample { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

float reservoir_final_weight(DiReservoir r) {
    if (r.M == 0u || r.selected.target_pdf <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}

layout(set = 0, binding = 0, std430) readonly buffer ReservoirCurrent {
    DiReservoir cur_res[];
};
layout(set = 0, binding = 1, std430) readonly buffer ReservoirPrevious {
    DiReservoir prev_res[];
};
layout(set = 0, binding = 2, std430) buffer ReservoirOut {
    DiReservoir out_res[];
};
layout(set = 0, binding = 3) uniform sampler2D u_MotionVectors; // rg = screen uv delta
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  frame_index;
    uint  M_cap;   // history cap, Bitterli recommends 20 * M_initial
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
    DiReservoir cur = cur_res[idx];

    // Reproject.
    vec2 mv     = texelFetch(u_MotionVectors, ivec2(px), 0).rg;
    ivec2 ppx   = ivec2(vec2(px) - mv * vec2(pc.resolution) + 0.5);
    bool  valid = all(greaterThanEqual(ppx, ivec2(0))) &&
                  all(lessThan(ppx, ivec2(pc.resolution)));

    DiReservoir merged = cur;

    if (valid) {
        uint pidx = uint(ppx.y) * pc.resolution.x + uint(ppx.x);
        DiReservoir prev = prev_res[pidx];

        // Discard stale history.
        if (prev.age > 30u) {
            prev.weight_sum = 0.0;
            prev.M = 0u;
        }

        float p_hat = prev.selected.target_pdf;  // reuse same domain (biased mode)
        float w     = p_hat * reservoir_final_weight(prev) * float(prev.M);
        merged.weight_sum += w;
        merged.M          += prev.M;
        uint seed = pcg(idx ^ (pc.frame_index * 16807u));
        if (merged.weight_sum > 0.0 && rand(seed) < w / merged.weight_sum)
            merged.selected = prev.selected;

        // M cap.
        if (merged.M > pc.M_cap) {
            merged.weight_sum *= float(pc.M_cap) / float(merged.M);
            merged.M = pc.M_cap;
        }
    }

    merged.age = cur.age + 1u;
    out_res[idx] = merged;
}
)glsl";

/// Spatial reuse pass.
/// 5-tap randomised disc neighbourhood; re-evaluates p_hat in receiver
/// domain; outputs final lit reservoir ready for shadow-ray evaluation.
constexpr std::string_view kRestirDiSpatialReuseCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct DiSample { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

float reservoir_final_weight(DiReservoir r) {
    if (r.M == 0u || r.selected.target_pdf <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}

layout(set = 0, binding = 0, std430) readonly buffer ReservoirIn {
    DiReservoir in_res[];
};
layout(set = 0, binding = 1, std430) buffer ReservoirOut {
    DiReservoir out_res[];
};
layout(set = 0, binding = 2) uniform sampler2D u_GBufNormal;
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  frame_index;
    uint  spatial_radius;  // pixels (e.g. 30)
    uint  spatial_taps;    // (e.g. 5)
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

    uint  idx  = px.y * pc.resolution.x + px.x;
    uint  seed = pcg(idx ^ (pc.frame_index * 22695477u));

    DiReservoir R = in_res[idx];
    vec3 N_r = texelFetch(u_GBufNormal, ivec2(px), 0).xyz * 2.0 - 1.0;

    // Randomised rotation angle for this frame (breaks pattern over time).
    float theta0 = rand(seed) * 2.0 * kPi;

    for (uint t = 0u; t < pc.spatial_taps; ++t) {
        float theta = theta0 + float(t) * (2.0 * kPi / float(pc.spatial_taps));
        float r     = float(pc.spatial_radius) * sqrt(rand(seed));
        ivec2 npx   = ivec2(vec2(px) + r * vec2(cos(theta), sin(theta)));
        if (any(lessThan(npx, ivec2(0))) || any(greaterThanEqual(npx, ivec2(pc.resolution))))
            continue;

        vec3 N_n = texelFetch(u_GBufNormal, npx, 0).xyz * 2.0 - 1.0;
        // Skip geometrically dissimilar neighbours.
        if (dot(N_r, N_n) < 0.906) continue;  // > ~25 deg

        uint nidx = uint(npx.y) * pc.resolution.x + uint(npx.x);
        DiReservoir nei = in_res[nidx];
        if (nei.M == 0u) continue;

        // Re-evaluate p_hat in receiver domain (biased: skip visibility).
        float p_hat = nei.selected.target_pdf * max(0.0, dot(N_r, N_n));
        float w = p_hat * reservoir_final_weight(nei) * float(nei.M);
        R.weight_sum += w;
        R.M          += nei.M;
        if (R.weight_sum > 0.0 && rand(seed) < w / R.weight_sum)
            R.selected = nei.selected;
    }

    out_res[idx] = R;
}
)glsl";

}  // namespace cd::restir_di
