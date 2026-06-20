// =============================================================================
// CHROMODYNAMIC — cd/gameplay/time/Time.cpp
// Phase 459 — TimeKeeper implementation.
// =============================================================================
#include <cd/gameplay/time/Time.hpp>

namespace cd::gameplay::time
{

bool TimeKeeper::tick(double real_dt_seconds) noexcept
{
    // Defensive: negative dt is a programmer error (clock skew, replay bug,
    // unsigned underflow, etc.). Reject and do nothing — including no frame
    // bump — so a misbehaving caller surfaces in tests rather than silently
    // mutating monotonic counters.
    if (real_dt_seconds < 0.0)
    {
        return false;
    }

    for (auto& ch : channels_)
    {
        // Real delta is always reported, even when paused. UI animations
        // should bind to `delta_seconds` so menus keep moving.
        ch.delta_seconds = real_dt_seconds;

        if (!ch.paused)
        {
            ch.elapsed_seconds += real_dt_seconds * ch.time_scale;
        }
        // While paused, `elapsed_seconds` is frozen but `delta_seconds`
        // and the global `frame_index_` still advance.
    }

    ++frame_index_;
    return true;
}

void TimeKeeper::set_paused(TimerCategory category, bool paused) noexcept
{
    channels_[index_of(category)].paused = paused;
}

bool TimeKeeper::is_paused(TimerCategory category) const noexcept
{
    return channels_[index_of(category)].paused;
}

void TimeKeeper::set_time_scale(TimerCategory category, double scale) noexcept
{
    // Clamp negative scale to 0 — running time backwards would desync every
    // age/animation cursor downstream. Tests rely on this clamp.
    const double safe_scale = scale < 0.0 ? 0.0 : scale;
    channels_[index_of(category)].time_scale = safe_scale;
}

double TimeKeeper::time_scale(TimerCategory category) const noexcept
{
    return channels_[index_of(category)].time_scale;
}

double TimeKeeper::scaled_delta(TimerCategory category) const noexcept
{
    const auto& ch = channels_[index_of(category)];
    if (ch.paused)
    {
        return 0.0;
    }
    return ch.delta_seconds * ch.time_scale;
}

GameTime TimeKeeper::get(TimerCategory category) const noexcept
{
    const auto& ch = channels_[index_of(category)];
    GameTime t;
    t.elapsed_seconds = ch.elapsed_seconds;
    t.delta_seconds   = ch.delta_seconds;
    t.frame_index     = frame_index_;
    t.time_scale      = ch.time_scale;
    return t;
}

void TimeKeeper::reset() noexcept
{
    for (auto& ch : channels_)
    {
        ch = Channel {};
    }
    frame_index_ = 0;
}

}  // namespace cd::gameplay::time
