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
// with dynamic direct lighting" (SIGGRAPH 2020).
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
    /// Final per-sample contribution weight = (weight_sum / M / target_pdf).
    /// Computed once on demand (Bitterli Eq. 6).
    [[nodiscard]] float final_weight() const noexcept
    {
        if (M == 0 || selected.target_pdf <= 0.0F) return 0.0F;
        return weight_sum / (static_cast<float>(M) * selected.target_pdf);
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

}  // namespace cd::restir_di
