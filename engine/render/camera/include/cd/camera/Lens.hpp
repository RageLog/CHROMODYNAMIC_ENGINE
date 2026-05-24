// =============================================================================
// CHROMODYNAMIC — cd/camera/Lens.hpp
// Phase 52.B / Wave 220 — physical-lens parameters (focal-length, etc.).
//
// `Camera` carries vertical FOV directly because shaders only need
// the resulting projection matrix. Cinematographers / level artists
// however think in lens terms (35mm focal length, T-stop, focus
// distance). `Lens` is the artist-facing struct + helpers that
// convert into engine FOV.
//
// Reference: photographic full-frame sensor = 36×24mm; vertical FOV
// from focal length `f` (mm) and sensor height `h` (mm):
//
//   fov_y_rad = 2 * atan(h / (2 * f))
//
// `Lens::fov_y_for(sensor_height_mm)` evaluates that formula. We
// expose both: `apply_to(camera)` mutates the existing Camera's
// `fov_y` to match the lens.
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/core/Defines.hpp>

#include <cmath>

namespace cd::camera
{

struct Lens
{
    float focal_length_mm { 50.0F };
    float aperture        { 1.4F };  // T-stop, lower = wider
    float focus_distance_m { 5.0F };

    [[nodiscard]] float fov_y_for(float sensor_height_mm = 24.0F) const noexcept
    {
        if (focal_length_mm <= 0.0F) return 1.0F;
        return 2.0F * std::atan(sensor_height_mm / (2.0F * focal_length_mm));
    }

    void apply_to(Camera& cam, float sensor_height_mm = 24.0F) const noexcept
    {
        cam.fov_y = fov_y_for(sensor_height_mm);
    }
};

[[nodiscard]] inline Lens lens_wide() noexcept       { return Lens { 24.0F, 2.8F, 5.0F }; }
[[nodiscard]] inline Lens lens_standard() noexcept   { return Lens { 50.0F, 1.4F, 5.0F }; }
[[nodiscard]] inline Lens lens_portrait() noexcept   { return Lens { 85.0F, 1.8F, 3.0F }; }
[[nodiscard]] inline Lens lens_telephoto() noexcept  { return Lens { 200.0F, 2.8F, 10.0F }; }

}  // namespace cd::camera
