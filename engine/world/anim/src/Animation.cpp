// =============================================================================
// CHROMODYNAMIC — cd/anim/Animation.cpp
// =============================================================================
#include <cd/anim/Animation.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Transform.hpp>

#include <algorithm>
#include <cstddef>

namespace cd::anim
{

namespace
{

[[nodiscard]] cd::math::Vec3f vec_lerp(cd::math::Vec3f a, cd::math::Vec3f b, float t) noexcept
{
    return cd::math::Vec3f { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

[[nodiscard]] cd::math::Transformf
interpolate(const cd::math::Transformf& a, const cd::math::Transformf& b, float t) noexcept
{
    cd::math::Transformf out;
    out.position = vec_lerp(a.position, b.position, t);
    out.rotation = cd::math::slerp(a.rotation, b.rotation, t);
    out.scale = vec_lerp(a.scale, b.scale, t);
    return out;
}

}  // namespace

cd::math::Transformf AnimationClip::sample(float t) const noexcept
{
    if (frames_.empty())
        return cd::math::Transformf {};
    if (frames_.size() == 1)
        return frames_.front().value;
    if (t <= frames_.front().time)
        return frames_.front().value;
    if (t >= frames_.back().time)
        return frames_.back().value;

    // Binary search for the upper bound, then interpolate against the
    // preceding frame. std::ranges::upper_bound on a small sorted vector
    // keeps the sample call O(log N) without overhead from a custom binary
    // loop. The projection is identity over Keyframe::time so the lambda
    // sees `t` as the lhs and the projected `k.time` as the rhs.
    auto it = std::ranges::upper_bound(
        frames_, t, std::less<>{},
        &Keyframe::time);
    const auto& hi = *it;
    const auto& lo = *(it - 1);
    const float span = hi.time - lo.time;
    const float local_t = span > 0.0F ? (t - lo.time) / span : 0.0F;
    return interpolate(lo.value, hi.value, local_t);
}

float AnimationPlayer::wrap_(float t, float duration) const noexcept
{
    if (duration <= 0.0F)
        return 0.0F;
    switch (loop_)
    {
        case LoopMode::kClamp:
            return std::clamp(t, 0.0F, duration);
        case LoopMode::kLoop:
        {
            // Modulo on float — std::fmod has the right sign behavior.
            const float wrapped = std::fmod(t, duration);
            return wrapped < 0.0F ? wrapped + duration : wrapped;
        }
        case LoopMode::kPingPong:
        {
            // Reflect at the boundaries: a full cycle is 2 * duration long.
            const float full = duration * 2.0F;
            float wrapped = std::fmod(t, full);
            if (wrapped < 0.0F)
                wrapped += full;
            return wrapped > duration ? full - wrapped : wrapped;
        }
    }
    return t;
}

cd::math::Transformf AnimationPlayer::update(float dt) noexcept
{
    if (clip_ == nullptr || clip_->empty())
        return cd::math::Transformf {};
    if (playing_)
    {
        time_ += dt * speed_;
    }
    // Time relative to the first keyframe (clips don't need to start at 0).
    const float t0 = clip_->frames().front().time;
    const float relative = time_ - 0.0F;
    const float wrapped = wrap_(relative, clip_->duration()) + t0;
    return clip_->sample(wrapped);
}

}  // namespace cd::anim

// =============================================================================
// SkinnedClip — same lerp/slerp logic as AnimationClip but applied
// per-joint. Implemented out-of-line so the header stays light.
// =============================================================================

namespace cd::anim
{

namespace
{

cd::math::Vec3f vec_lerp_local(cd::math::Vec3f a, cd::math::Vec3f b, float t) noexcept
{
    return cd::math::Vec3f { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

cd::math::Transformf interpolate_local(
    const cd::math::Transformf& a, const cd::math::Transformf& b, float t) noexcept
{
    cd::math::Transformf out;
    out.position = vec_lerp_local(a.position, b.position, t);
    out.rotation = cd::math::slerp(a.rotation, b.rotation, t);
    out.scale = vec_lerp_local(a.scale, b.scale, t);
    return out;
}

}  // namespace

cd::math::Transformf SkinnedClip::sample_track_(const std::vector<Keyframe>& frames, float t) noexcept
{
    if (frames.empty())
        return cd::math::Transformf {};
    if (frames.size() == 1)
        return frames.front().value;
    if (t <= frames.front().time)
        return frames.front().value;
    if (t >= frames.back().time)
        return frames.back().value;

    auto it = std::ranges::upper_bound(
        frames, t, std::less<>{},
        &Keyframe::time);
    const auto& b = *it;
    const auto& a = *(it - 1);
    const float span = b.time - a.time;
    const float alpha = span > 0.0F ? (t - a.time) / span : 0.0F;
    return interpolate_local(a.value, b.value, alpha);
}

}  // namespace cd::anim
