// =============================================================================
// CHROMODYNAMIC - cd/camera/FirstPersonController.hpp
//
// Mouse-look + WASD camera driver. Companion to OrbitController for samples
// that want fly-through controls instead of an orbit cradle.
//
// State:
//   * yaw   (radians, around world +Y)
//   * pitch (radians, around the camera's local right vector; clamped)
//   * dist  (eye-to-target distance; keeps a stable focal point so the
//            HUD's target overlay doesn't visually 'fly off' when the
//            user pans)
//
// Public methods:
//   sync_from_camera(c)  — derive yaw/pitch/dist from current eye/target;
//                          call once before the first update so the
//                          controller picks up from where the caller
//                          (e.g. an OrbitController) left off.
//   look(dx_px, dy_px)   — pixel-delta mouse look. dx > 0 yaws right.
//                          radians_per_pixel and pitch clamp are tunables.
//   move(dt, fwd, right, up) — translation per axis from a -1..+1 input
//                          vector. `fwd` is along the view direction
//                          (W=+, S=-); `right` is strafe (D=+, A=-);
//                          `up` is world-Y lift (E=+, Q=-).
//   apply(c)             — write eye + target back to the camera.
//
// Header-only and dependency-light (cd::camera + cd::math + std).
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::camera
{

class FirstPersonController
{
public:
    FirstPersonController() noexcept = default;

    /// Derive (yaw, pitch, dist) from the camera's current view ray so
    /// the controller picks up from whatever state set eye+target.
    void sync_from_camera(const Camera& c) noexcept
    {
        const float dx = c.target[0] - c.eye[0];
        const float dy = c.target[1] - c.eye[1];
        const float dz = c.target[2] - c.eye[2];
        const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > 1e-4F)
        {
            dist_  = d;
            pitch_ = std::asin(std::clamp(dy / d, -1.0F, 1.0F));
            yaw_   = std::atan2(dx, -dz);
        }
        else
        {
            dist_  = 1.0F;
            pitch_ = 0.0F;
            yaw_   = 0.0F;
        }
    }

    /// Mouse pixel delta (dx > 0 = move-right). Positive vertical pixel
    /// delta tilts the view down by default (set invert_pitch=true to
    /// match the FPS convention where pull-down = look-up).
    void look(float dx_px, float dy_px) noexcept
    {
        yaw_ += dx_px * radians_per_pixel;
        const float pitch_sign = invert_pitch ? -1.0F : 1.0F;
        pitch_ = std::clamp(pitch_ + pitch_sign * dy_px * radians_per_pixel,
                            -kPitchMax, kPitchMax);
    }

    /// Translate. `axis` components are -1..+1 (or arbitrary scalar). The
    /// controller folds in move_speed * dt internally; caller passes
    /// the raw input axis. `axis.x` strafes, `axis.y` lifts (world-Y),
    /// `axis.z` walks along view forward.
    void move(float dt, cd::math::Vec3f axis) noexcept
    {
        const float cp = std::cos(pitch_);
        const float sp = std::sin(pitch_);
        const float cy = std::cos(yaw_);
        const float sy = std::sin(yaw_);
        const cd::math::Vec3f fwd { cp * sy, sp, -cp * cy };
        const cd::math::Vec3f right { cy,    0.0F, sy };
        const float spd = move_speed * dt;
        // Translate the target (eye follows from yaw/pitch/dist).
        target_offset_.x += (fwd.x * axis.z + right.x * axis.x) * spd;
        target_offset_.y += (fwd.y * axis.z + axis.y) * spd;
        target_offset_.z += (fwd.z * axis.z + right.z * axis.x) * spd;
    }

    /// Multiplicative zoom on the focal distance. Positive `delta` zooms
    /// IN (dist shrinks). Mirrors OrbitController::zoom() so the two
    /// controllers map a scroll wheel identically.
    void zoom(float delta) noexcept
    {
        dist_ = std::clamp(dist_ * std::exp(-delta * zoom_step),
                           dist_min, dist_max);
    }

    /// Write yaw/pitch/dist into the camera's eye + target. Adds the
    /// accumulated `move()` target offset to the camera's current
    /// target so successive frames smoothly walk the scene.
    void apply(Camera& c) noexcept
    {
        c.target[0] += target_offset_.x;
        c.target[1] += target_offset_.y;
        c.target[2] += target_offset_.z;
        target_offset_ = { 0.0F, 0.0F, 0.0F };
        const float cp = std::cos(pitch_);
        const float sp = std::sin(pitch_);
        const float cy = std::cos(yaw_);
        const float sy = std::sin(yaw_);
        const cd::math::Vec3f fwd { cp * sy, sp, -cp * cy };
        c.eye[0] = c.target[0] - fwd.x * dist_;
        c.eye[1] = c.target[1] - fwd.y * dist_;
        c.eye[2] = c.target[2] - fwd.z * dist_;
    }

    [[nodiscard]] float yaw()   const noexcept { return yaw_; }
    [[nodiscard]] float pitch() const noexcept { return pitch_; }
    [[nodiscard]] float dist()  const noexcept { return dist_; }

    // ------ Tunables (public; treat as plain config) -----------------------
    float radians_per_pixel { 0.0035F };
    float move_speed { 3.5F };        ///< units / second when axis component = 1
    float zoom_step { 0.15F };
    float dist_min { 0.05F };
    float dist_max { 200.0F };
    bool  invert_pitch { false };

private:
    static constexpr float kPitchMax = 1.55334F;  ///< just under PI/2

    float yaw_   { 0.0F };
    float pitch_ { 0.0F };
    float dist_  { 5.0F };
    cd::math::Vec3f target_offset_ {};
};

}  // namespace cd::camera
