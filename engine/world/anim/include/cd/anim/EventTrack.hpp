// =============================================================================
// CHROMODYNAMIC — cd/anim/EventTrack.hpp
// Phase 59.A / Wave 227 — timestamped event channel for animation playback.
//
// Animation clips often need to trigger gameplay logic mid-playback:
// "foot hits the ground" → footstep sound, "weapon swing peak" →
// damage check. EventTrack stores `(t, event_id)` pairs and lets the
// playback loop advance through them with `advance(t_prev, t_now, fn)`,
// invoking `fn(event_id)` for every event in (t_prev, t_now].
//
// Wraps if `t_now < t_prev` (loop boundary) — the timeline is assumed
// circular only on caller request. By default `advance` requires a
// monotonic time window.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cd::anim
{

struct AnimEvent
{
    float         t {};
    std::uint32_t id { 0 };
};

class EventTrack
{
public:
    void add(float t, std::uint32_t id)
    {
        // NOLINTNEXTLINE(modernize-use-ranges): heterogeneous comparator (AnimEvent vs float key) has no clean ranges-projection form.
        auto it = std::lower_bound(events_.begin(), events_.end(), t,
            [](const AnimEvent& a, float v) { return a.t < v; });
        events_.insert(it, AnimEvent { t, id });
    }

    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

    [[nodiscard]] const std::vector<AnimEvent>& events() const noexcept { return events_; }

    void clear() noexcept { events_.clear(); }

    /// Fires `fn(id)` for every event whose `t ∈ (t_prev, t_now]`.
    /// Caller guarantees `t_prev <= t_now`.
    template <class F>
    std::size_t advance(float t_prev, float t_now, F&& fn) const
    {
        std::size_t fired = 0;
        for (const auto& e : events_)
        {
            if (e.t > t_prev && e.t <= t_now)
            {
                fn(e.id);
                ++fired;
            }
        }
        return fired;
    }

private:
    std::vector<AnimEvent> events_;
};

}  // namespace cd::anim
