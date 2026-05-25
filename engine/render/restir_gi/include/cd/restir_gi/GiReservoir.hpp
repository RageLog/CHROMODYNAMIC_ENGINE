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
// "ReSTIR GI: Path Resampling for Real-Time Path Tracing" (HPG
// 2021).
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

    [[nodiscard]] float final_weight(float target_pdf) const noexcept
    {
        if (M == 0 || target_pdf <= 0.0F) return 0.0F;
        return weight_sum / (static_cast<float>(M) * target_pdf);
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

}  // namespace cd::restir_gi
