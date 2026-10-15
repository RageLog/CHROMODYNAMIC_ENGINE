// =============================================================================
// CHROMODYNAMIC — cd/elements_domain/DomainElements.cpp
// FAZ 5 Wave 5.2 — Domain Element Families Implementation.
// =============================================================================
#include <cd/elements_domain/DomainElements.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace cd::elements_visual
{

void SurfaceMesh3D::reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const
{
    Spatial3D::reflect_properties(out);
    out.push_back(cd::element_core::PropertyDesc {
        "mesh_asset_path", "Visual", cd::element_core::PropertyKind::kString, 0.0F, 0.0F, mesh_asset_path
    });
    out.push_back(cd::element_core::PropertyDesc {
        "cast_shadows", "Visual", cd::element_core::PropertyKind::kBool, 0.0F, 1.0F, cast_shadows ? "true" : "false"
    });
}

void CameraLens3D::reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const
{
    Spatial3D::reflect_properties(out);
    out.push_back(cd::element_core::PropertyDesc {
        "vertical_fov_deg", "Camera", cd::element_core::PropertyKind::kFloat, 10.0F, 150.0F, std::to_string(vertical_fov_deg)
    });
    out.push_back(cd::element_core::PropertyDesc {
        "aperture_fstop", "Camera", cd::element_core::PropertyKind::kFloat, 1.0F, 22.0F, std::to_string(aperture_fstop)
    });
}

float SpringArm3D::compute_clipped_arm_length(float hit_distance) const noexcept
{
    if (hit_distance < 0.0F || hit_distance >= target_arm_length)
        return target_arm_length;
    return std::max(hit_distance - probe_radius, 0.0F);
}

}  // namespace cd::elements_visual

namespace cd::elements_radiance
{

void SolarLight3D::reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const
{
    Spatial3D::reflect_properties(out);
    out.push_back(cd::element_core::PropertyDesc {
        "illuminance_lux", "Radiance", cd::element_core::PropertyKind::kFloat, 0.0F, 200000.0F, std::to_string(illuminance_lux)
    });
    out.push_back(cd::element_core::PropertyDesc {
        "temperature_kelvin", "Radiance", cd::element_core::PropertyKind::kFloat, 1000.0F, 20000.0F, std::to_string(temperature_kelvin)
    });
}

}  // namespace cd::elements_radiance

namespace cd::elements_aether
{

void CelestialDome3D::reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const
{
    Spatial3D::reflect_properties(out);
    out.push_back(cd::element_core::PropertyDesc {
        "time_of_day_hours", "Aether", cd::element_core::PropertyKind::kFloat, 0.0F, 24.0F, std::to_string(time_of_day_hours)
    });
}

void AetherFog3D::reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const
{
    Spatial3D::reflect_properties(out);
    out.push_back(cd::element_core::PropertyDesc {
        "base_density", "AetherFog", cd::element_core::PropertyKind::kFloat, 0.0F, 1.0F, std::to_string(base_density)
    });
    out.push_back(cd::element_core::PropertyDesc {
        "height_falloff", "AetherFog", cd::element_core::PropertyKind::kFloat, 0.0F, 5.0F, std::to_string(height_falloff)
    });
}

}  // namespace cd::elements_aether

namespace cd::elements_optics
{

void OpticsVolume3D::reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const
{
    Spatial3D::reflect_properties(out);
    out.push_back(cd::element_core::PropertyDesc {
        "tonemap_operator", "Optics", cd::element_core::PropertyKind::kString, 0.0F, 0.0F, tonemap_operator
    });
    out.push_back(cd::element_core::PropertyDesc {
        "bloom_intensity", "Optics", cd::element_core::PropertyKind::kFloat, 0.0F, 5.0F, std::to_string(bloom_intensity)
    });
}

}  // namespace cd::elements_optics

namespace cd::elements_dynamics
{

void KineticBody3D::on_fixed_tick(float fixed_dt)
{
    const auto p = local_position();
    set_local_position(
        p[0] + linear_velocity[0] * fixed_dt,
        p[1] + linear_velocity[1] * fixed_dt,
        p[2] + linear_velocity[2] * fixed_dt
    );
}

void ShatterMesh3D::apply_impact_impulse(float impulse_magnitude)
{
    if (!is_fractured && impulse_magnitude >= fracture_impulse_threshold)
    {
        is_fractured = true;
        on_shattered_pulse.emit(this);
    }
}

bool SensorVolume3D::contains_point(std::array<float, 3> world_pt) const noexcept
{
    const auto c = world_position();
    return std::abs(world_pt[0] - c[0]) <= half_extents[0] &&
           std::abs(world_pt[1] - c[1]) <= half_extents[1] &&
           std::abs(world_pt[2] - c[2]) <= half_extents[2];
}

}  // namespace cd::elements_dynamics

namespace cd::elements_acoustics
{

BinauralFrame SoundSource3D::evaluate_hrtf_and_doppler(
    std::array<float, 3> ear_pos,
    std::array<float, 3> ear_right_dir,
    std::array<float, 3> ear_vel_mps
) const noexcept
{
    BinauralFrame f {};
    const auto src_pos = world_position();
    const float dx = src_pos[0] - ear_pos[0];
    const float dy = src_pos[1] - ear_pos[1];
    const float dz = src_pos[2] - ear_pos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float safe_dist = std::max(dist, std::max(min_distance, 0.01F));

    const float atten = std::clamp(min_distance / safe_dist, 0.0F, 1.0F) * volume_gain;
    float azimuth_sin = 0.0F;
    if (dist > 1e-4F)
    {
        azimuth_sin = std::clamp(
            (dx * ear_right_dir[0] + dy * ear_right_dir[1] + dz * ear_right_dir[2]) / dist,
            -1.0F,
            1.0F
        );
    }

    if (enable_hrtf)
    {
        // Woodworth spherical-head ITD model (head radius a = 0.0875m, c = 343 m/s)
        constexpr float kHeadRadius = 0.0875F;
        constexpr float kSpeedOfSound = 343.0F;
        const float theta = std::asin(azimuth_sin);
        f.itd_delay_seconds = (kHeadRadius / kSpeedOfSound) * (theta + std::sin(theta));
        const float pan = 0.5F * (azimuth_sin + 1.0F);
        f.left_gain  = atten * std::cos(pan * 1.57079632F);
        f.right_gain = atten * std::sin(pan * 1.57079632F);
    }
    else
    {
        f.left_gain = atten;
        f.right_gain = atten;
    }

    if (enable_doppler && dist > 1e-4F)
    {
        constexpr float kSpeedOfSound = 343.0F;
        const float inv_d = 1.0F / dist;
        const float dir_x = dx * inv_d;
        const float dir_y = dy * inv_d;
        const float dir_z = dz * inv_d;
        const float v_listener = ear_vel_mps[0] * dir_x + ear_vel_mps[1] * dir_y + ear_vel_mps[2] * dir_z;
        const float v_source   = velocity_mps[0] * dir_x + velocity_mps[1] * dir_y + velocity_mps[2] * dir_z;
        const float num = std::clamp(kSpeedOfSound + v_listener, 10.0F, 600.0F);
        const float den = std::clamp(kSpeedOfSound + v_source, 10.0F, 600.0F);
        f.doppler_pitch_ratio = num / den;
    }

    return f;
}

}  // namespace cd::elements_acoustics

namespace cd::elements_wayfinding
{

void WayfinderAgent3D::on_tick(float dt)
{
    if (dt <= 0.0F || move_speed_mps <= 0.0F)
        return;

    const auto pos = local_position();
    const float dx = target_destination[0] - pos[0];
    const float dy = target_destination[1] - pos[1];
    const float dz = target_destination[2] - pos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist <= 1e-4F)
        return;

    const float step = std::min(move_speed_mps * dt, dist);
    const float s = step / dist;
    set_local_position(pos[0] + dx * s, pos[1] + dy * s, pos[2] + dz * s);
}

std::array<float, 3> SplineTrack3D::sample_catmull_rom(float u_normalized) const noexcept
{
    if (control_points.empty())
        return { 0.0F, 0.0F, 0.0F };
    if (control_points.size() == 1U)
        return control_points[0];

    const float u = std::clamp(u_normalized, 0.0F, 1.0F);
    const float scaled = u * static_cast<float>(control_points.size() - 1U);
    const auto i1 = static_cast<std::size_t>(scaled);
    const std::size_t i2 = std::min(i1 + 1U, control_points.size() - 1U);
    const float t = scaled - static_cast<float>(i1);

    return {
        control_points[i1][0] * (1.0F - t) + control_points[i2][0] * t,
        control_points[i1][1] * (1.0F - t) + control_points[i2][1] * t,
        control_points[i1][2] * (1.0F - t) + control_points[i2][2] * t,
    };
}

}  // namespace cd::elements_wayfinding

namespace cd::elements_planar
{

void Flipbook2D::on_tick(float dt)
{
    if (frame_count == 0U || frames_per_second <= 0.0F || dt <= 0.0F)
        return;
    accum_time_ += dt * frames_per_second;
    const auto advance = static_cast<std::uint32_t>(accum_time_);
    if (advance > 0U)
    {
        accum_time_ -= static_cast<float>(advance);
        current_frame = (current_frame + advance) % frame_count;
    }
}

void KineticBody2D::on_fixed_tick(float fixed_dt)
{
    const auto p = local_position();
    set_local_position(p[0] + velocity[0] * fixed_dt, p[1] + velocity[1] * fixed_dt);
}

}  // namespace cd::elements_planar
