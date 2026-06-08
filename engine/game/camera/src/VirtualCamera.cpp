// =============================================================================
// CHROMODYNAMIC - cd/game/camera/VirtualCamera.cpp
// Phase 482 (G3.3) - Cinemachine-style brain implementation.
// =============================================================================
#include <cd/game/camera/VirtualCamera.hpp>

#include <algorithm>
#include <cmath>

namespace cd::game::camera
{

// -----------------------------------------------------------------------------
// VirtualCamera (no out-of-line state) - all helpers inline in the header.
// CameraBrain implementation follows.
// -----------------------------------------------------------------------------

CameraBrain::Id CameraBrain::add_vcam(VirtualCamera vcam, std::int32_t priority)
{
    vcam.set_priority(priority);
    const Id id = next_id_++;
    // VirtualCamera is trivially-copyable; std::move() would be a no-op.
    vcams_.push_back(Entry {id, vcam});
    return id;
}

bool CameraBrain::remove_vcam(Id id)
{
    const auto it = std::ranges::find_if(vcams_,
                                         [id](const Entry& e) { return e.id == id; });
    if (it == vcams_.end())
    {
        return false;
    }
    vcams_.erase(it);

    // If the removed vcam was the live one, drop the live id so the next
    // tick re-selects.  The "fallback blend" gets its duration from the new
    // winner's blend_in (resolved on next tick); the from-camera anchor is
    // already last_output_ which is what we want.
    if (live_id_ == id)
    {
        live_id_ = 0;
    }
    if (prev_live_id_ == id)
    {
        prev_live_id_ = 0;
    }
    return true;
}

VirtualCamera* CameraBrain::vcam(Id id) noexcept
{
    const auto it = std::ranges::find_if(vcams_,
                                         [id](const Entry& e) { return e.id == id; });
    return it == vcams_.end() ? nullptr : &it->vcam;
}

const VirtualCamera* CameraBrain::vcam(Id id) const noexcept
{
    const auto it = std::ranges::find_if(vcams_,
                                         [id](const Entry& e) { return e.id == id; });
    return it == vcams_.cend() ? nullptr : &it->vcam;
}

void CameraBrain::warp() noexcept
{
    blend_total_s_   = 0.0F;
    blend_t_s_       = 0.0F;
    damp_vel_eye_    = cd::math::Vec3f {0.0F, 0.0F, 0.0F};
    damp_vel_target_ = cd::math::Vec3f {0.0F, 0.0F, 0.0F};
}

// -----------------------------------------------------------------------------
// Per-vcam sampler: turn a VirtualCamera + target lookup into an instantaneous
// Camera frame (no smoothing, no blending - just "what does this vcam want to
// see this frame").  Brain consumes this every tick for the live vcam.
// -----------------------------------------------------------------------------
cd::camera::Camera CameraBrain::sample_vcam(const VirtualCamera&    vcam,
                                            const TargetPositionFn& target_pos_fn)
{
    const cd::math::Vec3f tgt_pos = vcam.target().is_valid() && target_pos_fn
                                        ? target_pos_fn(vcam.target())
                                        : cd::math::Vec3f {0.0F, 0.0F, 0.0F};

    cd::camera::Camera out {};
    out.target = tgt_pos;
    out.eye    = tgt_pos + vcam.settings().position_offset;
    out.up     = cd::math::Vec3f {0.0F, 1.0F, 0.0F};
    out.fov_y  = vcam.settings().fov_y;
    out.near_z = vcam.settings().near_z;
    out.far_z  = vcam.settings().far_z;

    // rotation_offset rotates the look-at point around the eye.  We treat it
    // as a small Euler offset (pitch/yaw/roll) of the forward vector.  For
    // the typical authoring range (a few degrees) this is well-behaved; for
    // larger offsets a proper quaternion compose would be preferred, but the
    // Cinemachine API surface keeps this side as Vec3f.
    const cd::math::Vec3f& rot = vcam.settings().rotation_offset;
    const bool             has_rot = std::abs(rot.x) + std::abs(rot.y) + std::abs(rot.z) > 1e-6F;
    if (has_rot)
    {
        // forward = target - eye
        cd::math::Vec3f f = out.target - out.eye;
        const float len = cd::math::length(f);
        if (len > 1e-6F)
        {
            f = f / len;
            // Apply yaw (around Y) first, then pitch (around right axis).
            // Roll affects up vector only.
            const float cy = std::cos(rot.y);
            const float sy = std::sin(rot.y);
            cd::math::Vec3f f_yaw {
                f.x * cy + f.z * sy,
                f.y,
                -f.x * sy + f.z * cy
            };
            // Right = cross(forward, up); used for pitch.
            const cd::math::Vec3f right = cd::math::normalize(
                cd::math::cross(f_yaw, cd::math::Vec3f {0.0F, 1.0F, 0.0F}));
            const float cp = std::cos(rot.x);
            const float sp = std::sin(rot.x);
            cd::math::Vec3f f_final {
                f_yaw.x * cp + (right.y * f_yaw.z - right.z * f_yaw.y) * sp,
                f_yaw.y * cp + (right.z * f_yaw.x - right.x * f_yaw.z) * sp,
                f_yaw.z * cp + (right.x * f_yaw.y - right.y * f_yaw.x) * sp
            };
            out.target = out.eye + f_final * len;
            // Roll: rotate up around forward axis.
            if (std::abs(rot.z) > 1e-6F)
            {
                const float cr = std::cos(rot.z);
                const float sr = std::sin(rot.z);
                const cd::math::Vec3f right2 = cd::math::normalize(
                    cd::math::cross(f_final, cd::math::Vec3f {0.0F, 1.0F, 0.0F}));
                out.up = cd::math::normalize(
                    cd::math::Vec3f {0.0F, 1.0F, 0.0F} * cr + right2 * sr);
            }
        }
    }
    return out;
}

const CameraBrain::Entry* CameraBrain::select_live_entry() const noexcept
{
    const Entry* best = nullptr;
    for (const Entry& e : vcams_)
    {
        if (!e.vcam.enabled())
        {
            continue;
        }
        if (best == nullptr || e.vcam.priority() > best->vcam.priority())
        {
            best = &e;
        }
    }
    return best;
}

cd::camera::Camera CameraBrain::lerp_camera(const cd::camera::Camera& a,
                                            const cd::camera::Camera& b,
                                            float                     t) noexcept
{
    const float u = std::clamp(t, 0.0F, 1.0F);
    cd::camera::Camera out {};
    out.eye    = cd::math::lerp(a.eye,    b.eye,    u);
    out.target = cd::math::lerp(a.target, b.target, u);
    // Up should be renormalised - simple lerp is fine for short blends and
    // matches Cinemachine "linear" blend mode.
    const cd::math::Vec3f up = cd::math::lerp(a.up, b.up, u);
    const float up_len = cd::math::length(up);
    out.up = up_len > 1e-6F ? up / up_len : cd::math::Vec3f {0.0F, 1.0F, 0.0F};
    out.fov_y  = a.fov_y  + (b.fov_y  - a.fov_y)  * u;
    out.near_z = a.near_z + (b.near_z - a.near_z) * u;
    out.far_z  = a.far_z  + (b.far_z  - a.far_z)  * u;
    return out;
}

// -----------------------------------------------------------------------------
// Critically-damped spring (semi-implicit Euler).  Per axis:
//   omega = 2 / damping_s         (damping_s == 0 -> snap, returns target)
//   acc   = -2*omega*vel - omega^2 * (cur - target)
//   vel  += acc * dt
//   cur  += vel * dt
//
// This is the standard game-feel "smooth damp" formulation - Game Programming
// Gems 4 ch. 1.10 (Bourg, Bystedt) "A Critically Damped, Tunable Spring".  We
// run it independently on each axis so per-axis damping budgets (the Vec3f
// damping field) make sense.  An axis with damping_s == 0 collapses to "set
// current = target, velocity = 0" which is exactly the no-smoothing case.
// -----------------------------------------------------------------------------
cd::math::Vec3f CameraBrain::damp_axis(const cd::math::Vec3f& current,
                                       const cd::math::Vec3f& target,
                                       cd::math::Vec3f&       velocity,
                                       const cd::math::Vec3f& damping_s,
                                       float                  dt) noexcept
{
    cd::math::Vec3f out = current;
    for (std::size_t i = 0; i < 3; ++i)
    {
        const float d = damping_s[i];
        if (d <= 0.0F || dt <= 0.0F)
        {
            out[i]      = target[i];
            velocity[i] = 0.0F;
            continue;
        }
        const float omega = 2.0F / d;
        const float diff  = out[i] - target[i];
        const float acc   = -2.0F * omega * velocity[i] - omega * omega * diff;
        velocity[i] += acc * dt;
        out[i]      += velocity[i] * dt;
    }
    return out;
}

// -----------------------------------------------------------------------------
// Tick: the dispatch.
// -----------------------------------------------------------------------------
cd::camera::Camera CameraBrain::tick(float dt, const TargetPositionFn& target_pos_fn)
{
    // Reject negative dt.  Return last output unchanged so the renderer keeps
    // showing what it had - silently advancing a malformed dt would surface
    // as a teleport on the next valid tick.
    if (dt < 0.0F)
    {
        last_tick_ok_ = false;
        return last_output_;
    }
    last_tick_ok_ = true;

    // 1. Select the highest-priority enabled vcam.
    const Entry* live_entry = select_live_entry();
    const Id     new_live   = live_entry != nullptr ? live_entry->id : 0;

    // 2. Detect live-vcam change and start / restart a blend if needed.
    if (new_live != live_id_)
    {
        // Pull blend duration: prefer the larger of (prev.blend_out, next.blend_in).
        // This gives the more emphatic-side authoring win.
        float prev_out = 0.0F;
        if (const VirtualCamera* prev_vcam = vcam(live_id_); prev_vcam != nullptr)
        {
            prev_out = prev_vcam->blend_out_duration();
        }
        const float next_in   = live_entry != nullptr ? live_entry->vcam.blend_in_duration() : 0.0F;
        const float blend_dur = std::max(prev_out, next_in);

        // The blend "from" anchor is the current output - if we have one - so
        // mid-blend interrupts cross-fade smoothly instead of snapping to the
        // previous vcam's instantaneous sample.
        if (have_last_output_)
        {
            blend_from_ = last_output_;
        }
        else if (live_entry != nullptr)
        {
            blend_from_ = sample_vcam(live_entry->vcam, target_pos_fn);
        }

        blend_total_s_ = blend_dur;
        blend_t_s_     = 0.0F;

        prev_live_id_ = live_id_;
        live_id_      = new_live;
    }

    // 3. Compute the live target sample (no smoothing yet).
    cd::camera::Camera out {};
    if (live_entry != nullptr)
    {
        out = sample_vcam(live_entry->vcam, target_pos_fn);
    }
    else if (have_last_output_)
    {
        out = last_output_;
    }

    // 4. Advance the blend timer.  If we're still mid-blend, lerp from the
    //    captured from-frame to the live sample.
    if (blend_total_s_ > 0.0F && blend_t_s_ < blend_total_s_)
    {
        blend_t_s_ += dt;
        const float t = std::clamp(blend_t_s_ / blend_total_s_, 0.0F, 1.0F);
        out = lerp_camera(blend_from_, out, t);
        if (blend_t_s_ >= blend_total_s_)
        {
            blend_total_s_ = 0.0F;
            blend_t_s_     = 0.0F;
        }
    }

    // 5. Apply damping (critically-damped spring per axis) to eye + target so
    //    target-jumps are smoothed.  Damping is taken from the live vcam; a
    //    zero damping field means "no smoothing on that axis" and the spring
    //    short-circuits to snap.  We skip damping during an active blend
    //    because the blend itself is already a temporal interpolant - layering
    //    damping on top would slow the blend's tail to a crawl, which is the
    //    usual Cinemachine misconfiguration.
    if (live_entry != nullptr && have_last_output_ && blend_total_s_ == 0.0F && dt > 0.0F)
    {
        const cd::math::Vec3f damping = live_entry->vcam.settings().damping;
        out.eye    = damp_axis(last_output_.eye,    out.eye,    damp_vel_eye_,    damping, dt);
        out.target = damp_axis(last_output_.target, out.target, damp_vel_target_, damping, dt);
    }

    // 6. Record + return.
    last_output_      = out;
    have_last_output_ = true;
    return out;
}

}  // namespace cd::game::camera
