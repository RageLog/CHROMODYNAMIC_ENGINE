// =============================================================================
// CHROMODYNAMIC - cd/game/camera/VirtualCamera.hpp
// Phase 482 (G3.3) - Cinemachine-style virtual camera stack + brain.
//
// Design: Unity Cinemachine "virtual camera + brain" pattern.  A virtual camera
// (vcam) is a lightweight transform-source description with a priority; the
// brain owns a registry of vcams and each tick selects the highest-priority
// *enabled* one as the "live" vcam.  When the live vcam changes, the brain
// runs a time-based linear blend between the previous and next vcam so the
// hand-off is continuous (no pops in position / rotation / FOV).
//
// Damping (critically-damped spring):
//   Per axis we run the standard semi-implicit second-order critically-damped
//   spring (omega = 2 / damping_seconds):
//
//        accel = -2 * omega * vel - omega^2 * (current - target)
//        vel  += accel * dt
//        cur  += vel * dt
//
//   When `damping_seconds == 0` the spring degenerates to an instantaneous
//   snap (no overshoot, no lag) which is the right behaviour for "no smoothing
//   requested".  Critically damped (zeta = 1) chosen over under- or over-
//   damped because gameplay cameras must not overshoot and must not feel
//   sluggish - Cinemachine, Half-Life 2, Doom (2016) all converge on
//   critically-damped springs for the same reason.
//
// Live-vcam selection during a blend:
//   Once a blend starts the *target* vcam (the one with the new highest
//   priority) is treated as the live source until the blend completes; if a
//   third even-higher-priority vcam appears mid-blend, the brain interrupts
//   the current blend at its current output (no double-blending) and starts
//   a fresh blend FROM the current output TO the new winner.  This matches
//   Cinemachine's "BlendList interrupt" rule and keeps blend chains finite.
//
// Library boundary (CLAUDE.md S7):
//   Depends on cd::core (defines), cd::math (Vec3f), cd::camera (Camera),
//   cd::ecs (Entity for follow targets).  No render / scene / world includes.
//   The brain consumes target positions through a free function the caller
//   provides at tick time (`TargetPositionFn`) - that keeps the scene-graph
//   integration as an upward dependency at the call site instead of pulling
//   cd::scene into this library.
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace cd::game::camera
{

// -----------------------------------------------------------------------------
// VCamSettings - lens + framing + smoothing description.
//
// All fields are world-space POD; the brain reads them every tick so the
// gameplay layer is free to mutate them between ticks without notifying.
//
// `position_offset`: added to the target's world position to produce the
// vcam's desired eye position (Cinemachine "Body Offset").
//
// `rotation_offset`: extra eye-relative offset applied after the target
// pose is resolved.  This is an Euler-angle vector (pitch / yaw / roll in
// radians) folded into the look direction.  Kept as Vec3f rather than a
// quaternion because the brain only ever consumes the desired *look-at
// point* (= eye + forward), and small offsets are easier to author as
// Euler triples.
//
// `damping`: per-axis critically-damped spring half-life *in seconds*.
// Zero -> instantaneous snap; 0.3 -> typical "smooth follow"; 1.0 -> lazy
// pan.  Authoring guide: damping should be smaller than the blend
// duration, otherwise the smoothing eats the blend.
// -----------------------------------------------------------------------------
struct VCamSettings
{
    float fov_y { 1.0F };          ///< Vertical field of view, radians (~57 deg).
    float near_z { 0.1F };         ///< Near clip plane, world units.
    float far_z { 100.0F };        ///< Far clip plane, world units.
    cd::math::Vec3f position_offset { 0.0F, 0.0F, 0.0F };  ///< Eye offset from target.
    cd::math::Vec3f rotation_offset { 0.0F, 0.0F, 0.0F };  ///< Euler look offset (rad).
    cd::math::Vec3f damping { 0.0F, 0.0F, 0.0F };          ///< Per-axis spring half-life (s).
};

// -----------------------------------------------------------------------------
// VirtualCamera - one entry in the brain's priority stack.
//
// The vcam is a pure description object: it does NOT own a Camera and never
// mutates one directly.  The brain is the sole writer of the output camera.
// Vcams just say "if I am live, follow this entity with these settings".
//
// Lifecycle:
//   - set_target(entity)            : follow that entity's world position.
//   - set_priority(int)             : higher wins; ties broken by insertion order.
//   - blend_in(seconds)             : duration used when THIS vcam becomes live.
//   - blend_out(seconds)            : duration used when THIS vcam becomes non-live.
//                                     The brain takes the max(prev.blend_out,
//                                     next.blend_in) so authoring stays sensible
//                                     - "the more emphatic side wins".
//   - settings()                    : mutable lens/offsets/damping reference.
//   - set_enabled(bool)             : skip in selection without unregistering.
// -----------------------------------------------------------------------------
class VirtualCamera
{
public:
    VirtualCamera() = default;
    explicit VirtualCamera(std::int32_t priority) noexcept : priority_(priority) {}

    void set_target(cd::ecs::Entity target) noexcept { target_ = target; }
    CD_NODISCARD cd::ecs::Entity target() const noexcept { return target_; }

    void set_priority(std::int32_t p) noexcept { priority_ = p; }
    CD_NODISCARD std::int32_t priority() const noexcept { return priority_; }

    /// Set the duration (seconds) used when THIS vcam becomes the live one.
    /// Negative inputs are clamped to zero (snap on activation).
    void blend_in(float duration_s) noexcept
    {
        blend_in_s_ = (duration_s > 0.0F) ? duration_s : 0.0F;
    }
    CD_NODISCARD float blend_in_duration() const noexcept { return blend_in_s_; }

    /// Set the duration (seconds) used when THIS vcam stops being live.
    /// Negative inputs are clamped to zero (snap on deactivation).
    void blend_out(float duration_s) noexcept
    {
        blend_out_s_ = (duration_s > 0.0F) ? duration_s : 0.0F;
    }
    CD_NODISCARD float blend_out_duration() const noexcept { return blend_out_s_; }

    void set_enabled(bool e) noexcept { enabled_ = e; }
    CD_NODISCARD bool enabled() const noexcept { return enabled_; }

    /// Mutable settings access - the brain reads these each tick.
    CD_NODISCARD VCamSettings&       settings() noexcept       { return settings_; }
    CD_NODISCARD const VCamSettings& settings() const noexcept { return settings_; }

    /// Position offset shortcut - the brief calls this out as a first-class field
    /// of VCamSettings but the most common authoring path is to set it directly.
    void set_position_offset(const cd::math::Vec3f& off) noexcept
    {
        settings_.position_offset = off;
    }
    CD_NODISCARD const cd::math::Vec3f& position_offset() const noexcept
    {
        return settings_.position_offset;
    }

private:
    cd::ecs::Entity target_ {};
    std::int32_t    priority_ { 0 };
    float           blend_in_s_  { 0.0F };
    float           blend_out_s_ { 0.0F };
    bool            enabled_     { true };
    VCamSettings    settings_    {};
};

// -----------------------------------------------------------------------------
// CameraBrain - the dispatcher.
//
// add_vcam(vcam, priority): registers a copy of the vcam under an opaque
// integer id (returned).  The priority argument overrides whatever priority
// the vcam was constructed with - the brief's signature requires it as a
// separate parameter so we honour that.
//
// remove_vcam(id): drops the vcam.  If the removed vcam was the live one,
// the brain falls back to the next-highest-priority enabled vcam on the
// next tick (a `blend_in` from the fallback's duration, snap if there is
// no fallback).
//
// tick(dt, target_pos_fn): advances the brain's internal state and returns
// the resulting cd::camera::Camera.
//   - dt > 0  : advance damping + blend timers, return the smoothed camera.
//   - dt == 0 : sample without integration (preview / first frame).
//   - dt < 0  : REJECTED (returns last output unchanged; std::expected would
//               be ideal but the brief asks for the simpler tick() shape).
//               `last_tick_ok()` reports whether the previous tick was
//               accepted - the negative-dt unit test inspects this.
//
// target_pos_fn(entity) -> Vec3f: resolves the entity's world position.
// Returns Vec3f{0,0,0} for the default (invalid) entity so the vcam can be
// used "stationary" without a target.
// -----------------------------------------------------------------------------
class CameraBrain
{
public:
    using Id = std::uint32_t;
    using TargetPositionFn = std::function<cd::math::Vec3f(cd::ecs::Entity)>;

    CameraBrain() = default;

    /// Register a copy of `vcam` with the brain.  The provided `priority`
    /// overrides `vcam.priority()` (mirrors Cinemachine Authoring API).
    Id add_vcam(VirtualCamera vcam, std::int32_t priority);

    /// Remove the vcam previously returned by `add_vcam`.  Returns true if
    /// `id` was known.  If it was the live vcam, the brain transitions to
    /// the new winner on the next tick via its `blend_in`.
    bool remove_vcam(Id id);

    /// Direct mutable access to a registered vcam (priority changes, target
    /// re-bind, etc).  Returns nullptr for unknown ids.
    CD_NODISCARD VirtualCamera*       vcam(Id id) noexcept;
    CD_NODISCARD const VirtualCamera* vcam(Id id) const noexcept;

    /// Tick the brain.  `dt` is seconds elapsed since the last tick;
    /// negative values are rejected (see header note).  The returned
    /// Camera is the brain's current output.  If no vcam is registered the
    /// output is left unchanged (caller initialised it sensibly).
    cd::camera::Camera tick(float dt, const TargetPositionFn& target_pos_fn);

    /// True if the most recent call to tick() was accepted (dt >= 0).
    CD_NODISCARD bool last_tick_ok() const noexcept { return last_tick_ok_; }

    /// Currently live vcam id, or zero if none.
    CD_NODISCARD Id live_id() const noexcept { return live_id_; }

    /// True while a blend is in progress.
    CD_NODISCARD bool is_blending() const noexcept { return blend_total_s_ > 0.0F && blend_t_s_ < blend_total_s_; }

    /// Blend progress in [0, 1] - useful for debug HUD.
    CD_NODISCARD float blend_progress() const noexcept
    {
        if (blend_total_s_ <= 0.0F || blend_t_s_ >= blend_total_s_)
            return 1.0F;
        return blend_t_s_ / blend_total_s_;
    }

    /// Number of vcams currently registered.
    CD_NODISCARD std::size_t vcam_count() const noexcept { return vcams_.size(); }

    /// Reset blend + damping integrators (use after teleports).
    void warp() noexcept;

private:
    struct Entry
    {
        Id            id {0};
        VirtualCamera vcam {};
    };

    /// Compute the live (non-smoothed) sample for a given vcam given the
    /// target position function.  This is the "desired" eye / target / lens
    /// state before blending and damping are applied.
    static cd::camera::Camera sample_vcam(const VirtualCamera&    vcam,
                                          const TargetPositionFn& target_pos_fn);

    /// Pick the highest-priority enabled vcam.  Returns nullptr if none.
    CD_NODISCARD const Entry* select_live_entry() const noexcept;

    /// Linear interpolation between two cd::camera::Camera frames.  All
    /// scalar lens fields lerp; vectors lerp; up is renormalised.
    static cd::camera::Camera lerp_camera(const cd::camera::Camera& a,
                                          const cd::camera::Camera& b,
                                          float                     t) noexcept;

    /// Critically-damped spring step for a Vec3f axis triple.
    /// `damping[i]` is the half-life in seconds; zero -> snap.
    static cd::math::Vec3f damp_axis(const cd::math::Vec3f& current,
                                     const cd::math::Vec3f& target,
                                     cd::math::Vec3f&       velocity,
                                     const cd::math::Vec3f& damping_s,
                                     float                  dt) noexcept;

    std::vector<Entry> vcams_ {};
    Id                 next_id_     { 1 };
    Id                 live_id_     { 0 };
    Id                 prev_live_id_ { 0 };

    // Blend state.  `blend_total_s_ == 0` means "no blend in progress".
    float blend_total_s_ { 0.0F };
    float blend_t_s_     { 0.0F };

    // The blend's "from" camera - captured at the moment the live vcam changes
    // so the lerp source is the current output rather than the previous vcam's
    // raw description.  This lets a third vcam interrupt cleanly.
    cd::camera::Camera blend_from_ {};

    // Damping integrators (one velocity vector per smoothed quantity).
    cd::math::Vec3f damp_vel_eye_    { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f damp_vel_target_ { 0.0F, 0.0F, 0.0F };

    // Last output - returned on negative-dt and used as the blend "from" anchor.
    cd::camera::Camera last_output_ {};
    bool               have_last_output_ { false };
    bool               last_tick_ok_     { true };
};

}  // namespace cd::game::camera
