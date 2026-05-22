// =============================================================================
// CHROMODYNAMIC — cd/camera/OrbitController.hpp
//
// Orbits a Camera around `Camera::target` on a sphere with controllable
// azimuth (yaw around Y) and elevation (pitch around the camera's right
// vector). The controller writes back into the Camera each frame; nothing
// else needs to track the spherical-coordinate state.
//
// Two driving modes:
//
//   * AUTO-SPIN — `update_auto(dt)` advances azimuth at `auto_spin_rate`
//     rad/s. Convenient for demo viewers (hello_cube, hello_gltf) and for
//     "preview thumbnails" in an editor.
//
//   * INPUT-DRIVEN — `drag_azimuth(dx)` / `drag_elevation(dy)` / `zoom(dy)`
//     accept arbitrary cursor deltas. The controller clamps the elevation
//     to slightly less than ±π/2 so the up vector never flips, and clamps
//     the radius to a configurable [min, max] window.
//
// Header-only to keep cd::camera dependency-free of cd::core / cd::math
// implementation TU symbols. Inlining is fine — this is hot-path tiny code.
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::camera
{

class OrbitController
{
public:
    OrbitController() noexcept = default;

    /// Re-derive (radius, azimuth, elevation) from the camera's current
    /// (eye, target) so the controller picks up wherever an outside
    /// `Camera::eye` write left off. Call once after constructing the
    /// camera + before the first update().
    void sync_from_camera(const Camera& c) noexcept
    {
        const cd::math::Vec3f d { c.eye[0] - c.target[0],
                                  c.eye[1] - c.target[1],
                                  c.eye[2] - c.target[2] };
        radius_ = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (radius_ < 1e-4F)
            radius_ = 1e-4F;
        elevation_ = std::asin(std::clamp(d[1] / radius_, -1.0F, 1.0F));
        azimuth_ = std::atan2(d[2], d[0]);
    }

    /// Auto-rotate around the target. `dt` is seconds since the last call.
    void update_auto(Camera& c, float dt) noexcept
    {
        azimuth_ += auto_spin_rate * dt;
        write_back_(c);
    }

    /// Mouse-drag delta in pixels (or whatever the platform reports). The
    /// `radians_per_pixel` factor is what turns a finger flick into a
    /// camera rotation; default ~0.005 feels right at 1080p.
    void drag(float dx_px, float dy_px) noexcept
    {
        azimuth_ -= dx_px * radians_per_pixel;
        elevation_ = std::clamp(elevation_ + dy_px * radians_per_pixel, -kElevationMax, kElevationMax);
    }

    /// Scroll wheel / pinch zoom. Positive `delta` zooms IN (radius down).
    void zoom(float delta) noexcept
    {
        radius_ = std::clamp(radius_ * std::exp(-delta * zoom_step), radius_min, radius_max);
    }

    /// Flush state to the camera. Call after any drag/zoom batch and before
    /// the renderer reads `c.eye`.
    void apply(Camera& c) noexcept
    {
        write_back_(c);
    }

    // ------ Tunables (public; treat as plain config) ------------------------
    float auto_spin_rate { 0.6F };     ///< rad/s for `update_auto`
    float radians_per_pixel { 0.005F };
    float zoom_step { 0.1F };          ///< multiplicative; `e^(-delta*step)`
    float radius_min { 0.1F };
    float radius_max { 1000.0F };

private:
    static constexpr float kElevationMax = 1.55334F;  ///< just under π/2

    void write_back_(Camera& c) noexcept
    {
        const float ce = std::cos(elevation_);
        c.eye = { c.target[0] + radius_ * std::cos(azimuth_) * ce,
                  c.target[1] + radius_ * std::sin(elevation_),
                  c.target[2] + radius_ * std::sin(azimuth_) * ce };
    }

    float azimuth_ { 0.0F };
    float elevation_ { 0.45F };  ///< modest looking-down angle
    float radius_ { 3.0F };
};

}  // namespace cd::camera
