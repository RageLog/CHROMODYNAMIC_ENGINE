// =============================================================================
// CHROMODYNAMIC -- cd/ui/animation/Animation.cpp
//
// Phase 3.2 of ADR-20260530-ui-widget-library. Easing curves + Timeline
// implementation. See Animation.hpp for the public API contract and the
// scope notes.
//
// Easing math reference:
//   Robert Penner, "Robert Penner's Easing Functions" (public domain,
//   2001). The exact polynomial / oscillation constants used here match
//   the de-facto numbers shared between gsap.com, easings.net and the
//   original Penner reference. They are also the constants used by
//   CSS cubic-bezier shorthand `cubic-bezier(0.34, 1.56, 0.64, 1.0)` ~
//   kEaseOutBack with c1 ~= 1.70158.
// =============================================================================
#include <cd/ui/animation/Animation.hpp>

#include <algorithm>
#include <cmath>

namespace cd::ui::animation
{

namespace
{

[[nodiscard]] float clamp01(float t) noexcept
{
    return std::clamp(t, 0.0F, 1.0F);
}

// ----- Quadratic family -----------------------------------------------------

[[nodiscard]] float ease_in_quad(float t) noexcept
{
    return t * t;
}

[[nodiscard]] float ease_out_quad(float t) noexcept
{
    const float inv = 1.0F - t;
    return 1.0F - inv * inv;
}

[[nodiscard]] float ease_in_out_quad(float t) noexcept
{
    if (t < 0.5F)
    {
        return 2.0F * t * t;
    }
    const float inv = -2.0F * t + 2.0F;
    return 1.0F - (inv * inv) * 0.5F;
}

// ----- Cubic family ---------------------------------------------------------

[[nodiscard]] float ease_in_cubic(float t) noexcept
{
    return t * t * t;
}

[[nodiscard]] float ease_out_cubic(float t) noexcept
{
    const float inv = 1.0F - t;
    return 1.0F - inv * inv * inv;
}

[[nodiscard]] float ease_in_out_cubic(float t) noexcept
{
    if (t < 0.5F)
    {
        return 4.0F * t * t * t;
    }
    const float inv = -2.0F * t + 2.0F;
    return 1.0F - (inv * inv * inv) * 0.5F;
}

// ----- Back (Penner kEaseOutBack with c1 = 1.70158, c3 = c1 + 1) ------------

[[nodiscard]] float ease_out_back(float t) noexcept
{
    constexpr float kC1 = 1.70158F;
    constexpr float kC3 = kC1 + 1.0F;
    const float     u  = t - 1.0F;
    // 1 + c3 * u^3 + c1 * u^2 -- classic Penner outBack
    return 1.0F + kC3 * u * u * u + kC1 * u * u;
}

// ----- Elastic (damped sine; standard EaseOutElastic) -----------------------

[[nodiscard]] float ease_out_elastic(float t) noexcept
{
    if (t <= 0.0F)
    {
        return 0.0F;
    }
    if (t >= 1.0F)
    {
        return 1.0F;
    }
    // c4 = (2*pi) / 3 -- matches easings.net reference output.
    constexpr float kC4 = 2.0943951023931953F;  // (2 * pi) / 3
    const float     d  = std::pow(2.0F, -10.0F * t);
    const float     s  = std::sin((t * 10.0F - 0.75F) * kC4);
    return d * s + 1.0F;
}

// ----- Bounce (Penner outBounce; 4-segment piecewise quadratic) -------------

[[nodiscard]] float ease_out_bounce(float t) noexcept
{
    constexpr float kN1 = 7.5625F;
    constexpr float kD1 = 2.75F;

    if (t < 1.0F / kD1)
    {
        return kN1 * t * t;
    }
    if (t < 2.0F / kD1)
    {
        const float u = t - 1.5F / kD1;
        return kN1 * u * u + 0.75F;
    }
    if (t < 2.5F / kD1)
    {
        const float u = t - 2.25F / kD1;
        return kN1 * u * u + 0.9375F;
    }
    const float u = t - 2.625F / kD1;
    return kN1 * u * u + 0.984375F;
}

}  // namespace

// ---- Public dispatch -------------------------------------------------------

float ease(float t, Easing curve) noexcept
{
    // Clamp the input parameter; curves themselves may produce values
    // outside [0..1] (e.g. EaseOutBack overshoot, EaseOutElastic damped
    // oscillation), which is preserved as a feature.
    const float u = clamp01(t);
    switch (curve)
    {
        case Easing::kLinear:         return u;
        case Easing::kEaseInQuad:     return ease_in_quad(u);
        case Easing::kEaseOutQuad:    return ease_out_quad(u);
        case Easing::kEaseInOutQuad:  return ease_in_out_quad(u);
        case Easing::kEaseInCubic:    return ease_in_cubic(u);
        case Easing::kEaseOutCubic:   return ease_out_cubic(u);
        case Easing::kEaseInOutCubic: return ease_in_out_cubic(u);
        case Easing::kEaseOutBack:    return ease_out_back(u);
        case Easing::kEaseOutElastic: return ease_out_elastic(u);
        case Easing::kEaseOutBounce:  return ease_out_bounce(u);
    }
    return u;  // fallback for forward-compat enum values
}

// ---- Timeline implementation -----------------------------------------------

ChannelId Timeline::add(float at_time_s, const Animation<float>& anim)
{
    Entry e {};
    e.start_s = (at_time_s < 0.0F) ? 0.0F : at_time_s;
    e.anim    = anim;
    channels_.push_back(e);
    return ChannelId { static_cast<std::uint32_t>(channels_.size() - 1U) };
}

void Timeline::tick(float dt_s) noexcept
{
    if (dt_s <= 0.0F)
    {
        return;
    }
    now_s_ += dt_s;
}

void Timeline::reset() noexcept
{
    now_s_ = 0.0F;
}

float Timeline::value_for(ChannelId channel) const noexcept
{
    if (!channel.is_valid() || channel.value >= channels_.size())
    {
        return 0.0F;
    }
    const Entry& e = channels_[channel.value];
    if (now_s_ <= e.start_s)
    {
        return e.anim.from;
    }
    const float local = now_s_ - e.start_s;
    if (e.anim.duration_s <= 0.0F || local >= e.anim.duration_s)
    {
        return e.anim.to;
    }
    const float t       = local / e.anim.duration_s;
    const float t_eased = ease(t, e.anim.easing);
    return e.anim.from + (e.anim.to - e.anim.from) * t_eased;
}

bool Timeline::done() const noexcept
{
    return std::ranges::all_of(channels_, [&](const Entry& e)
    {
        return now_s_ >= e.start_s + e.anim.duration_s;
    });
}

}  // namespace cd::ui::animation
