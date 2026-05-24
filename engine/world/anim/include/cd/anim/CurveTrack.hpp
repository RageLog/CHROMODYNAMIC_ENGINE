// =============================================================================
// CHROMODYNAMIC — cd/anim/CurveTrack.hpp
// Phase 41.A / Wave 209 — keyframed scalar animation curve.
//
// A CurveTrack is a sorted list of (time, value) keyframes plus
// `sample(t)` returning a scalar interpolated value:
//
//   * t ≤ first.t  → first.value (clamp)
//   * t ≥ last.t   → last.value  (clamp)
//   * otherwise    → linear interpolation between bracketing keys.
//
// Linear is enough for fade-in/fade-out, audio volume envelopes, and
// material-parameter sweeps. Future variants (Bezier, stepped, etc.)
// will be sibling tracks; the rendering path treats them all as
// `sample(t) → float`.
//
// Keyframe insertion is O(log n) lookup + O(n) insert (sorted vector).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <vector>

namespace cd::anim
{

struct CurveKey
{
    float t {};
    float value {};
};

class CurveTrack
{
public:
    void add_key(float t, float value)
    {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), t,
            [](const CurveKey& a, float v) { return a.t < v; });
        keys_.insert(it, CurveKey { t, value });
    }

    [[nodiscard]] std::size_t size() const noexcept { return keys_.size(); }

    [[nodiscard]] const std::vector<CurveKey>& keys() const noexcept { return keys_; }

    void clear() noexcept { keys_.clear(); }

    [[nodiscard]] float sample(float t) const noexcept
    {
        if (keys_.empty()) return 0.0F;
        if (t <= keys_.front().t) return keys_.front().value;
        if (t >= keys_.back().t)  return keys_.back().value;
        const auto it = std::lower_bound(keys_.begin(), keys_.end(), t,
            [](const CurveKey& a, float v) { return a.t < v; });
        const auto& k1 = *it;
        const auto& k0 = *(it - 1);
        const float seg = (k1.t - k0.t);
        const float u = (seg > 0.0F) ? ((t - k0.t) / seg) : 0.0F;
        return k0.value + (k1.value - k0.value) * u;
    }

private:
    std::vector<CurveKey> keys_;
};

}  // namespace cd::anim
