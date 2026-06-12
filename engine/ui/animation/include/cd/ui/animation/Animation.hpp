// =============================================================================
// CHROMODYNAMIC -- cd/ui/animation/Animation.hpp
//
// Phase 3.2 of ADR-20260530-ui-widget-library. Property animation, easing
// curves and timeline composition for retained-mode UI widgets. Pure C++
// (no RHI / no font / no input dependencies); produces interpolated values
// that downstream layers (Widget, renderer, theme transitions) consume.
//
// Scope (Phase 3.2):
//   * Easing enum: linear + quad/cubic in-out family + back/elastic/bounce
//                   "out" variants (Robert Penner reference set).
//   * float ease(float t, Easing) -> mapped curve in [0..1] for typical t
//                   in [0..1]. Curves with overshoot (back/elastic) may
//                   range slightly outside the unit interval by design.
//   * Animation<T>: declarative {from, to, duration, easing}. Trivially
//                   copyable; reusable across multiple Tweeners.
//   * Tweener<T>:   stateful playhead. start(anim) resets, tick(dt) advances,
//                   value() returns the current interpolated T, done() flips
//                   true at duration.
//   * Timeline:     opaque channel container. add(at_time, anim) schedules
//                   an Animation<float> on a channel; tick(dt) advances the
//                   shared playhead; value_for(channel_id) returns the
//                   most-recent value for that channel.
//
// Out of Phase 3.2 (Phase 4+):
//   * Spring physics (critically damped, under-/over-damped).
//   * Path animation along bezier / catmull splines.
//   * Looping / reverse / ping-pong policies (kept manual via Tweener).
//   * Property binding (curve -> Widget property), see ADR Phase 3.3.
//
// Design notes:
//   * Header-only public types except for the easing math, which lives in
//     Animation.cpp to keep inline expansion bounded and to give the
//     unit tests a single translation unit to validate.
//   * Easing functions take t in [0..1]; values outside that range are
//     clamped by ease() at the boundary (except for back/elastic, where
//     the natural overshoot is preserved as a feature).
//   * Tweener<T> uses lerp() = from + t*(to - from); supplied for float
//     and cd::math::Vec2f / Vec3f / Vec4f via SFINAE-friendly template.
//   * Timeline is intentionally minimal at Phase 3.2: linear advance over
//     a master time, per-channel lookup. Animation composition (chain,
//     stagger) is layered on top by widgets without engine help.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::ui::animation
{

// ---- Easing curve enum -----------------------------------------------------

/// Curve identifier. Mapped to a function in Animation.cpp. Values match
/// Penner's classic naming (linear, quadratic in/out, cubic in/out, etc.).
enum class Easing : std::uint8_t
{
    kLinear         = 0,  ///< t        -- identity
    kEaseInQuad     = 1,  ///< t^2      -- slow start
    kEaseOutQuad    = 2,  ///< 1-(1-t)^2 -- slow end
    kEaseInOutQuad  = 3,  ///< piecewise quadratic in/out
    kEaseInCubic    = 4,  ///< t^3
    kEaseOutCubic   = 5,  ///< 1-(1-t)^3
    kEaseInOutCubic = 6,  ///< piecewise cubic in/out
    kEaseOutBack    = 7,  ///< overshoots target then settles back
    kEaseOutElastic = 8,  ///< damped sine oscillation
    kEaseOutBounce  = 9,  ///< 4-segment ground-bounce
};

/// Map an input normalised parameter `t` (typically in [0..1]) through the
/// selected easing curve. Returns a value in [0..1] for the monotonic
/// family (linear, quad, cubic) and may slightly exceed the unit interval
/// for back / elastic (overshoot) before settling. Bounce stays in [0..1].
///
/// Out-of-range t is clamped to [0..1] at the boundary except where
/// callers intentionally drive overshoot animations -- the curve itself
/// produces values >1 / <0 only in its "interior" segment.
[[nodiscard]] float ease(float t, Easing curve) noexcept;

// ---- Animation<T> -- declarative tween -------------------------------------

/// Declarative animation description. Trivially copyable; reusable across
/// many Tweeners. Caller picks T as float / cd::math::Vec2f / Vec3f / Vec4f.
template <class T>
struct Animation
{
    T      from       {};
    T      to         {};
    float  duration_s { 1.0F };           ///< seconds; must be > 0
    Easing easing     { Easing::kLinear };
};

// ---- Tweener<T> -- stateful playhead --------------------------------------

/// Stateful playhead bound to one Animation<T>. start() resets, tick(dt)
/// advances the elapsed time, value() returns the interpolated T and
/// done() flips true when elapsed >= duration. Designed to be cheap to
/// poll every frame; no heap allocation.
template <class T>
class Tweener
{
public:
    Tweener() noexcept = default;

    /// Bind a new animation and reset the playhead.
    void start(const Animation<T>& anim) noexcept
    {
        anim_      = anim;
        elapsed_s_ = 0.0F;
        active_    = true;
    }

    /// Advance the playhead. Negative dt is treated as zero.
    void tick(float dt_s) noexcept
    {
        if (!active_)
        {
            return;
        }
        dt_s = std::max(dt_s, 0.0F);
        elapsed_s_ += dt_s;
        if (elapsed_s_ >= anim_.duration_s)
        {
            elapsed_s_ = anim_.duration_s;
            active_    = false;
        }
    }

    /// Current interpolated value. Returns `to` once done() is true.
    [[nodiscard]] T value() const noexcept
    {
        if (anim_.duration_s <= 0.0F)
        {
            return anim_.to;
        }
        const float t       = elapsed_s_ / anim_.duration_s;
        const float t_eased = ease(t, anim_.easing);
        return lerp(anim_.from, anim_.to, t_eased);
    }

    /// True once the playhead has reached `duration_s`.
    [[nodiscard]] bool done() const noexcept { return !active_; }

    /// Elapsed time since start() (clamped to duration once done).
    [[nodiscard]] float elapsed_s() const noexcept { return elapsed_s_; }

    /// Underlying animation (e.g. for callers that want to inspect duration).
    [[nodiscard]] const Animation<T>& animation() const noexcept { return anim_; }

private:
    Animation<T> anim_      {};
    float        elapsed_s_ { 0.0F };
    bool         active_    { false };

    /// Generic lerp helper. Works for arithmetic T and any T that supports
    /// operator+, operator- and operator*(float). For cd::math::Vec*f the
    /// componentwise operators in Vector.hpp satisfy this.
    [[nodiscard]] static T lerp(const T& a, const T& b, float t) noexcept
    {
        return a + (b - a) * t;
    }
};

// ---- Timeline -- multi-channel scheduler ----------------------------------

/// Identifier for a Timeline channel. `value` is an index into the channel
/// store; stable across the timeline's lifetime.
struct ChannelId
{
    std::uint32_t value { 0U };
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0xFFFFFFFFu; }
    [[nodiscard]] constexpr bool operator==(ChannelId other) const noexcept { return value == other.value; }
};

inline constexpr ChannelId kInvalidChannel { 0xFFFFFFFFu };

/// Multi-channel animation timeline. Each call to add(at_time, anim) creates
/// a new channel that will play `anim` starting at `at_time` (relative to
/// the timeline origin). tick(dt) advances the master playhead; value_for
/// returns the interpolated float for the channel at the current time.
///
/// Phase 3.2 supports float channels only; vector channels can be layered
/// by callers as multiple parallel float channels.
class Timeline
{
public:
    Timeline() = default;
    ~Timeline() = default;

    Timeline(const Timeline&)            = delete;
    Timeline& operator=(const Timeline&) = delete;
    Timeline(Timeline&&) noexcept            = default;
    Timeline& operator=(Timeline&&) noexcept = default;

    /// Schedule `anim` to start at `at_time_s` on a new channel. Returns
    /// the channel id. Negative `at_time_s` is clamped to 0.
    [[nodiscard]] ChannelId add(float at_time_s, const Animation<float>& anim);

    /// Advance the master playhead by `dt_s`. Idempotent for dt_s <= 0.
    void tick(float dt_s) noexcept;

    /// Reset the master playhead to zero. Channel schedule is preserved.
    void reset() noexcept;

    /// Value for `channel` at the current master time. Returns `from`
    /// before the channel's start time and `to` after its end time.
    [[nodiscard]] float value_for(ChannelId channel) const noexcept;

    /// Master elapsed time since construction / last reset().
    [[nodiscard]] float now_s() const noexcept { return now_s_; }

    /// True when every scheduled channel has reached or passed its end.
    [[nodiscard]] bool done() const noexcept;

    /// Number of scheduled channels.
    [[nodiscard]] std::size_t channel_count() const noexcept { return channels_.size(); }

private:
    struct Entry
    {
        float              start_s { 0.0F };
        Animation<float>   anim    {};
    };
    std::vector<Entry> channels_;
    float              now_s_ { 0.0F };
};

}  // namespace cd::ui::animation
