// =============================================================================
// CHROMODYNAMIC — apps/common/src/CameraMath3D.cpp
// =============================================================================
#include <cd/app_common/CameraMath3D.hpp>

#include <algorithm>
#include <cmath>

namespace cd::app_common
{

CameraBasis3D compute_camera_basis(
    cd::math::Vec3f eye,
    cd::math::Vec3f target,
    cd::math::Vec3f world_up) noexcept
{
    CameraBasis3D basis {};
    const cd::math::Vec3f delta = target - eye;
    const float dlen = cd::math::length(delta);
    basis.forward = (dlen > 1e-5F)
        ? (delta * (1.0F / dlen))
        : cd::math::Vec3f { 0.0F, 0.0F, -1.0F };

    cd::math::Vec3f right_raw = cd::math::cross(basis.forward, world_up);
    float rlen = cd::math::length(right_raw);
    if (rlen <= 1e-5F)
    {
        const cd::math::Vec3f fallback_up = (std::fabs(basis.forward.z) < 0.99F)
            ? cd::math::Vec3f { 0.0F, 0.0F, 1.0F }
            : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
        right_raw = cd::math::cross(basis.forward, fallback_up);
        rlen = cd::math::length(right_raw);
    }
    basis.right = (rlen > 1e-5F)
        ? (right_raw * (1.0F / rlen))
        : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };

    const cd::math::Vec3f up_raw = cd::math::cross(basis.right, basis.forward);
    const float ulen = cd::math::length(up_raw);
    basis.up = (ulen > 1e-5F)
        ? (up_raw * (1.0F / ulen))
        : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
    return basis;
}

CameraBasis3D compute_yaw_pitch_basis(float yaw_rad, float pitch_rad) noexcept
{
    const float cp = std::cos(pitch_rad);
    const float sp = std::sin(pitch_rad);
    const float cy = std::cos(yaw_rad);
    const float sy = std::sin(yaw_rad);

    const cd::math::Vec3f fwd { cp * sy, sp, -cp * cy };
    return compute_camera_basis({ 0.0F, 0.0F, 0.0F }, fwd, { 0.0F, 1.0F, 0.0F });
}

cd::math::Vec3f unproject_ndc_ray_dir(
    const CameraBasis3D& basis,
    float                fov_y_rad,
    float                aspect,
    float                ndc_x,
    float                ndc_y) noexcept
{
    const float safe_aspect = std::max(1e-4F, aspect);
    const float half_h = std::tan(fov_y_rad * 0.5F);
    const float half_w = half_h * safe_aspect;
    const cd::math::Vec3f dir =
        basis.forward + basis.right * (ndc_x * half_w) + basis.up * (ndc_y * half_h);
    const float len = cd::math::length(dir);
    return (len > 1e-6F) ? (dir * (1.0F / len)) : basis.forward;
}

cd::math::Vec3f screen_pixel_to_world_ray(
    const CameraBasis3D& basis,
    float                fov_y_rad,
    float                vp_x,
    float                vp_y,
    float                vp_w,
    float                vp_h,
    float                pixel_x,
    float                pixel_y) noexcept
{
    const float safe_w = std::max(1.0F, vp_w);
    const float safe_h = std::max(1.0F, vp_h);
    const float ndc_x  = 2.0F * ((pixel_x - vp_x) / safe_w) - 1.0F;
    const float ndc_y  = 1.0F - 2.0F * ((pixel_y - vp_y) / safe_h);
    return unproject_ndc_ray_dir(basis, fov_y_rad, safe_w / safe_h, ndc_x, ndc_y);
}

ScreenProjection2D project_world_to_screen_vk(
    const cd::math::Mat4f& vp,
    cd::math::Vec3f        world_pos,
    float                  vp_x,
    float                  vp_y,
    float                  vp_w,
    float                  vp_h) noexcept
{
    ScreenProjection2D out {};
    const cd::math::Vec4f clip =
        vp[0] * world_pos.x +
        vp[1] * world_pos.y +
        vp[2] * world_pos.z +
        vp[3];
    out.clip_w = clip.w;
    if (clip.w <= 1e-4F)
    {
        return out;
    }
    const float inv_w = 1.0F / clip.w;
    const float ndc_x = clip.x * inv_w;
    const float ndc_y = clip.y * inv_w;  // Vulkan clip +Y already points downward
    out.pixel = {
        vp_x + (ndc_x * 0.5F + 0.5F) * vp_w,
        vp_y + (ndc_y * 0.5F + 0.5F) * vp_h,
    };
    out.in_front = true;
    return out;
}

cd::math::Quatf euler_deg_to_quat(cd::math::Vec3f deg) noexcept
{
    constexpr float kDegToRad = 3.1415926535F / 180.0F;
    const cd::math::Quatf qx = cd::math::Quatf::from_axis_angle({ 1.0F, 0.0F, 0.0F }, deg.x * kDegToRad);
    const cd::math::Quatf qy = cd::math::Quatf::from_axis_angle({ 0.0F, 1.0F, 0.0F }, deg.y * kDegToRad);
    const cd::math::Quatf qz = cd::math::Quatf::from_axis_angle({ 0.0F, 0.0F, 1.0F }, deg.z * kDegToRad);
    return cd::math::normalize(qy * qx * qz);
}

cd::math::Vec3f quat_to_euler_deg(const cd::math::Quatf& q_in) noexcept
{
    const cd::math::Quatf q = cd::math::normalize(q_in);
    constexpr float kRadToDeg = 180.0F / 3.1415926535F;

    const float sinp = 2.0F * (q.w * q.x - q.y * q.z);
    const float pitch = (std::fabs(sinp) >= 1.0F)
        ? std::copysign(90.0F, sinp)
        : (std::asin(sinp) * kRadToDeg);

    const float siny_cosp = 2.0F * (q.w * q.y + q.z * q.x);
    const float cosy_cosp = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
    const float yaw = std::atan2(siny_cosp, cosy_cosp) * kRadToDeg;

    const float sinr_cosp = 2.0F * (q.w * q.z + q.x * q.y);
    const float cosr_cosp = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
    const float roll = std::atan2(sinr_cosp, cosr_cosp) * kRadToDeg;

    return { pitch, yaw, roll };
}

float snap_scalar_to_grid(float value, float step) noexcept
{
    if (step <= 1e-4F)
    {
        return value;
    }
    return std::round(value / step) * step;
}

cd::math::Vec3f snap_vec3_to_grid(cd::math::Vec3f value, float step) noexcept
{
    if (step <= 1e-4F)
    {
        return value;
    }
    return {
        std::round(value.x / step) * step,
        std::round(value.y / step) * step,
        std::round(value.z / step) * step,
    };
}

std::optional<float> ray_sphere_hit_t(
    cd::math::Vec3f ray_origin,
    cd::math::Vec3f ray_dir,
    cd::math::Vec3f sphere_center,
    float           sphere_radius) noexcept
{
    if (sphere_radius <= 0.0F)
    {
        return std::nullopt;
    }
    const cd::math::Vec3f oc = ray_origin - sphere_center;
    const float a = cd::math::dot(ray_dir, ray_dir);
    if (a <= 1e-8F)
    {
        return std::nullopt;
    }
    const float half_b = cd::math::dot(oc, ray_dir);
    const float c      = cd::math::dot(oc, oc) - sphere_radius * sphere_radius;
    const float disc   = half_b * half_b - a * c;
    if (disc < 0.0F)
    {
        return std::nullopt;
    }
    const float sq = std::sqrt(disc);
    float t = (-half_b - sq) / a;
    if (t < 0.0F)
    {
        t = (-half_b + sq) / a;
    }
    if (t < 0.0F)
    {
        return std::nullopt;
    }
    return t;
}

std::optional<cd::math::Vec3f> ray_plane_hit_point(
    cd::math::Vec3f ray_origin,
    cd::math::Vec3f ray_dir,
    cd::math::Vec3f plane_origin,
    cd::math::Vec3f plane_normal) noexcept
{
    const float denom = cd::math::dot(ray_dir, plane_normal);
    if (std::fabs(denom) <= 1e-6F)
    {
        return std::nullopt;
    }
    const float t = cd::math::dot(plane_origin - ray_origin, plane_normal) / denom;
    if (t < 0.0F)
    {
        return std::nullopt;
    }
    return ray_origin + ray_dir * t;
}

}  // namespace cd::app_common
