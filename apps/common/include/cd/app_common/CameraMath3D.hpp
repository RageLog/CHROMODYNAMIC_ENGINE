// =============================================================================
// CHROMODYNAMIC — cd/app_common/CameraMath3D.hpp
//
// Single canonical 3D camera basis, NDC ray unprojection, world-to-screen
// projection, YXZ quaternion <-> Euler degree conversion, grid snapping, and
// ray-sphere / ray-plane intersection helpers shared across applications.
// =============================================================================
#pragma once

#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <optional>

namespace cd::app_common
{

struct CameraBasis3D
{
    cd::math::Vec3f forward { 0.0F, 0.0F, -1.0F };
    cd::math::Vec3f right   { 1.0F, 0.0F,  0.0F };
    cd::math::Vec3f up      { 0.0F, 1.0F,  0.0F };
};

struct ScreenProjection2D
{
    cd::math::Vec2f pixel { -10000.0F, -10000.0F };
    float           clip_w { 0.0F };
    bool            in_front { false };
};

/// Build a right-handed orthonormal camera basis from eye -> target and world_up.
[[nodiscard]] CameraBasis3D compute_camera_basis(
    cd::math::Vec3f eye,
    cd::math::Vec3f target,
    cd::math::Vec3f world_up = { 0.0F, 1.0F, 0.0F }) noexcept;

/// Build a right-handed orthonormal camera basis from yaw/pitch angles (radians).
[[nodiscard]] CameraBasis3D compute_yaw_pitch_basis(
    float yaw_rad,
    float pitch_rad) noexcept;

/// Unproject normalized device coordinates (ndc_x, ndc_y in [-1, 1], +Y up) into a world unit ray.
[[nodiscard]] cd::math::Vec3f unproject_ndc_ray_dir(
    const CameraBasis3D& basis,
    float                fov_y_rad,
    float                aspect,
    float                ndc_x,
    float                ndc_y) noexcept;

/// Unproject a viewport pixel (px, py, top-left origin) into a world unit ray.
[[nodiscard]] cd::math::Vec3f screen_pixel_to_world_ray(
    const CameraBasis3D& basis,
    float                fov_y_rad,
    float                vp_x,
    float                vp_y,
    float                vp_w,
    float                vp_h,
    float                pixel_x,
    float                pixel_y) noexcept;

/// Project a 3D world point through a Vulkan-NDC view-projection matrix (`+Y` clip down -> screen pixel).
[[nodiscard]] ScreenProjection2D project_world_to_screen_vk(
    const cd::math::Mat4f& vp,
    cd::math::Vec3f        world_pos,
    float                  vp_x,
    float                  vp_y,
    float                  vp_w,
    float                  vp_h) noexcept;

/// Convert Euler degrees (Pitch X, Yaw Y, Roll Z; intrinsic YXZ) to a unit quaternion.
[[nodiscard]] cd::math::Quatf euler_deg_to_quat(cd::math::Vec3f deg) noexcept;

/// Convert a unit quaternion back to Euler degrees (Pitch X, Yaw Y, Roll Z; intrinsic YXZ).
[[nodiscard]] cd::math::Vec3f quat_to_euler_deg(const cd::math::Quatf& q_in) noexcept;

/// Snap a scalar or 3D vector to a uniform grid step (`step <= 1e-4F` returns input unchanged).
[[nodiscard]] float snap_scalar_to_grid(float value, float step) noexcept;
[[nodiscard]] cd::math::Vec3f snap_vec3_to_grid(cd::math::Vec3f value, float step) noexcept;

/// Nearest positive ray-sphere intersection distance `t`, or `std::nullopt` on miss.
[[nodiscard]] std::optional<float> ray_sphere_hit_t(
    cd::math::Vec3f ray_origin,
    cd::math::Vec3f ray_dir,
    cd::math::Vec3f sphere_center,
    float           sphere_radius) noexcept;

/// Intersect ray with plane `(point - plane_origin) . plane_normal = 0`.
[[nodiscard]] std::optional<cd::math::Vec3f> ray_plane_hit_point(
    cd::math::Vec3f ray_origin,
    cd::math::Vec3f ray_dir,
    cd::math::Vec3f plane_origin,
    cd::math::Vec3f plane_normal) noexcept;

}  // namespace cd::app_common
