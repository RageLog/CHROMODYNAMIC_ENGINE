// =============================================================================
// CHROMODYNAMIC — cd/anim/Animation.hpp
// Phase 4 / Sprint S4.4 — keyframe animation primitives.
//
// `AnimationClip` is an immutable timeline of `cd::math::Transformf`
// keyframes. `AnimationPlayer` walks a clip's time axis, samples the
// current pose via SLERP for rotation and linear interpolation for
// translation/scale, and exposes the resulting Transform to game code.
//
// SOTA references: glTF skinning spec, production engine's `AnimationCurve`,
// production `UAnimSequence`. The MVP here covers the single-target
// (root-transform) case that virtually every introductory animation
// runtime needs; per-bone tracks layer on top in a follow-up sprint.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cd::anim
{

enum class LoopMode : std::uint8_t
{
    kClamp,
    kLoop,
    kPingPong
};

/// One keyframe: a `time` (seconds, monotonic) and a `Transform` value.
struct Keyframe
{
    float time { 0.0F };
    cd::math::Transformf value {};
};

class AnimationClip
{
public:
    AnimationClip() noexcept = default;

    explicit AnimationClip(std::vector<Keyframe> frames) noexcept
        : frames_ { std::move(frames) }
    {
    }

    [[nodiscard]] std::size_t keyframe_count() const noexcept
    {
        return frames_.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return frames_.empty();
    }

    /// Duration = time of the last keyframe (0 for empty / single-frame clips).
    [[nodiscard]] float duration() const noexcept
    {
        return frames_.size() < 2 ? 0.0F : frames_.back().time - frames_.front().time;
    }

    /// Sample the clip at absolute `t` seconds (no looping applied — the
    /// player wraps the time before calling). Returns the front frame for
    /// empty clips and the boundary frame outside [t0, tN].
    [[nodiscard]] cd::math::Transformf sample(float t) const noexcept;

    [[nodiscard]] const std::vector<Keyframe>& frames() const noexcept
    {
        return frames_;
    }

private:
    /// Frames are assumed sorted by `time`. Constructors that accept user
    /// input should sort externally; the clip itself does not.
    std::vector<Keyframe> frames_ {};
};

/// Stateful player that advances clip time per tick and exposes the
/// interpolated transform. Cheap to copy; one player per animated entity.
class AnimationPlayer
{
public:
    AnimationPlayer() noexcept = default;

    explicit AnimationPlayer(const AnimationClip* clip) noexcept
        : clip_ { clip }
    {
    }

    void set_clip(const AnimationClip* clip) noexcept
    {
        clip_ = clip;
        time_ = 0.0F;
    }

    [[nodiscard]] const AnimationClip* clip() const noexcept
    {
        return clip_;
    }

    void set_loop_mode(LoopMode m) noexcept
    {
        loop_ = m;
    }

    [[nodiscard]] LoopMode loop_mode() const noexcept
    {
        return loop_;
    }

    void set_speed(float s) noexcept
    {
        speed_ = s;
    }

    [[nodiscard]] float speed() const noexcept
    {
        return speed_;
    }

    void set_time(float t) noexcept
    {
        time_ = t;
    }

    [[nodiscard]] float time() const noexcept
    {
        return time_;
    }

    void play() noexcept
    {
        playing_ = true;
    }

    void pause() noexcept
    {
        playing_ = false;
    }

    void stop() noexcept
    {
        playing_ = false;
        time_ = 0.0F;
    }

    [[nodiscard]] bool is_playing() const noexcept
    {
        return playing_;
    }

    /// Advance by `dt` seconds, honoring speed and loop mode. The result is
    /// the sampled transform; callers typically pipe it into a `LocalTransform`
    /// component on the animated entity.
    cd::math::Transformf update(float dt) noexcept;

private:
    [[nodiscard]] float wrap(float t, float duration) const noexcept;

    const AnimationClip* clip_ { nullptr };
    float time_ { 0.0F };
    float speed_ { 1.0F };
    LoopMode loop_ { LoopMode::kLoop };
    bool playing_ { true };
};

}  // namespace cd::anim
