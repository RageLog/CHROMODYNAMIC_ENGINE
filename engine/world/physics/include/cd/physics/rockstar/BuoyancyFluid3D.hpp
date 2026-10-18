// =============================================================================
// CHROMODYNAMIC — cd/physics/rockstar/BuoyancyFluid3D.hpp
// Archimedes Multi-Probe Buoyancy & Gerstner Wave Hydrodynamics Solver.
//
// Evaluates a multi-component trochoidal Gerstner wave spectrum for sea/river
// surface height and surface normal, then integrates per-probe submerged volume
// fraction, Archimedes upward & wave-normal buoyancy forces, righting torque,
// and quadratic hydrodynamic linear/angular drag.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::physics::rockstar
{

struct GerstnerWaveComponent3D
{
    cd::math::Vec2f direction    { 1.0F, 0.0F };
    float           amplitude_m  { 0.35F };
    float           wavelength_m { 14.0F };
    float           steepness    { 0.45F };
    float           speed_m_s    { 3.8F };
};

struct BuoyancyBodyConfig3D
{
    float total_volume_m3    { 1.0F };
    float probe_radius_m     { 0.25F };
    float linear_drag_coeff  { 1.25F };
    float angular_drag_coeff { 0.85F };
};

struct BuoyancyForceResult3D
{
    cd::math::Vec3f net_force_n           {};
    cd::math::Vec3f net_torque_nm         {};
    cd::math::Vec3f buoyancy_force_n      {};
    cd::math::Vec3f linear_drag_force_n   {};
    cd::math::Vec3f angular_drag_torque_nm {};
    float           submerged_fraction    { 0.0F };
    std::uint32_t   submerged_probe_count { 0u };
};

class BuoyancyFluidField3D
{
public:
    static constexpr float kTwoPi = 6.28318530718F;
    static constexpr float kGravity = 9.81F;

    BuoyancyFluidField3D()
    {
        // Default 3-component ocean swell + cross-chop spectrum
        waves_.push_back(GerstnerWaveComponent3D { {  1.00F,  0.00F }, 0.40F, 16.0F, 0.45F, 4.2F });
        waves_.push_back(GerstnerWaveComponent3D { {  0.71F,  0.71F }, 0.22F,  9.5F, 0.35F, 3.1F });
        waves_.push_back(GerstnerWaveComponent3D { { -0.45F,  0.89F }, 0.12F,  5.2F, 0.25F, 2.4F });
    }

    constexpr void set_fluid_density(float density_kg_m3) noexcept
    {
        fluid_density_kg_m3_ = std::max(density_kg_m3, 1.0F);
    }

    [[nodiscard]] constexpr float fluid_density() const noexcept
    {
        return fluid_density_kg_m3_;
    }

    constexpr void set_base_water_level(float level_y) noexcept
    {
        base_water_level_y_ = level_y;
    }

    [[nodiscard]] constexpr float base_water_level() const noexcept
    {
        return base_water_level_y_;
    }

    void clear_waves() noexcept
    {
        waves_.clear();
    }

    void add_wave(const GerstnerWaveComponent3D& wave)
    {
        waves_.push_back(wave);
    }

    /// Evaluates the Gerstner wave surface height Y(x, z, t).
    [[nodiscard]] float sample_wave_height(float x, float z, float time_sec) const noexcept
    {
        float height = base_water_level_y_;
        for (const auto& w : waves_)
        {
            const float wavelength = std::max(w.wavelength_m, 0.25F);
            const float k = kTwoPi / wavelength;
            const cd::math::Vec2f dir = cd::math::normalize(w.direction);
            const float phase = k * (dir.x * x + dir.y * z - w.speed_m_s * time_sec);
            height += w.amplitude_m * std::sin(phase);
        }
        return height;
    }

    /// Evaluates the 3D Gerstner trochoidal displacement (dx, dy, dz) at (x, z, t).
    [[nodiscard]] cd::math::Vec3f sample_wave_displacement(float x, float z, float time_sec) const noexcept
    {
        cd::math::Vec3f disp { 0.0F, base_water_level_y_, 0.0F };
        for (const auto& w : waves_)
        {
            const float wavelength = std::max(w.wavelength_m, 0.25F);
            const float k = kTwoPi / wavelength;
            const cd::math::Vec2f dir = cd::math::normalize(w.direction);
            const float phase = k * (dir.x * x + dir.y * z - w.speed_m_s * time_sec);
            const float cos_p = std::cos(phase);
            const float sin_p = std::sin(phase);

            disp.x += dir.x * (w.steepness * w.amplitude_m) * cos_p;
            disp.y += w.amplitude_m * sin_p;
            disp.z += dir.y * (w.steepness * w.amplitude_m) * cos_p;
        }
        return disp;
    }

    /// Evaluates the analytical Gerstner wave surface normal at (x, z, t).
    [[nodiscard]] cd::math::Vec3f sample_wave_normal(float x, float z, float time_sec) const noexcept
    {
        float dh_dx = 0.0F;
        float dh_dz = 0.0F;
        for (const auto& w : waves_)
        {
            const float wavelength = std::max(w.wavelength_m, 0.25F);
            const float k = kTwoPi / wavelength;
            const cd::math::Vec2f dir = cd::math::normalize(w.direction);
            const float phase = k * (dir.x * x + dir.y * z - w.speed_m_s * time_sec);
            const float d_cos = k * w.amplitude_m * std::cos(phase);
            dh_dx += dir.x * d_cos;
            dh_dz += dir.y * d_cos;
        }
        return cd::math::normalize(cd::math::Vec3f { -dh_dx, 1.0F, -dh_dz });
    }

    /// Computes multi-probe Archimedes buoyancy force, righting torque, and
    /// quadratic hydrodynamic linear/angular drag.
    [[nodiscard]] BuoyancyForceResult3D compute_buoyancy_forces(
        cd::math::Vec3f body_pos,
        cd::math::Quatf body_rot,
        cd::math::Vec3f linear_vel,
        cd::math::Vec3f angular_vel,
        std::span<const cd::math::Vec3f> local_probes,
        const BuoyancyBodyConfig3D& config = {},
        float time_sec = 0.0F) const noexcept
    {
        BuoyancyForceResult3D result {};
        if (local_probes.empty())
        {
            return result;
        }

        const float probe_count_f = static_cast<float>(local_probes.size());
        const float volume_per_probe = std::max(config.total_volume_m3, 1e-4F) / probe_count_f;
        const float probe_diameter = std::max(2.0F * config.probe_radius_m, 0.05F);

        float total_submerged_ratio = 0.0F;

        for (const auto& local_probe : local_probes)
        {
            const cd::math::Vec3f r_world = cd::math::rotate(body_rot, local_probe);
            const cd::math::Vec3f world_probe = body_pos + r_world;

            const float water_y = sample_wave_height(world_probe.x, world_probe.z, time_sec);
            const float depth = water_y - (world_probe.y - config.probe_radius_m);
            if (depth <= 0.0F)
            {
                continue;
            }

            const float probe_submergence = std::clamp(depth / probe_diameter, 0.0F, 1.0F);
            total_submerged_ratio += probe_submergence;
            ++result.submerged_probe_count;

            // Archimedes buoyancy at this probe blended slightly with wave surface normal
            const cd::math::Vec3f wave_normal = sample_wave_normal(world_probe.x, world_probe.z, time_sec);
            const cd::math::Vec3f effective_up =
                cd::math::normalize(cd::math::Vec3f { wave_normal.x * 0.20F, 1.0F, wave_normal.z * 0.20F });

            const float displaced_volume = volume_per_probe * probe_submergence;
            const cd::math::Vec3f probe_buoyancy =
                effective_up * (fluid_density_kg_m3_ * kGravity * displaced_volume);

            // Local point velocity for hydrodynamic drag torque
            const cd::math::Vec3f probe_vel = linear_vel + cd::math::cross(angular_vel, r_world);
            const float probe_speed = cd::math::length(probe_vel);
            const cd::math::Vec3f probe_drag =
                -probe_vel * (0.5F * fluid_density_kg_m3_ * config.linear_drag_coeff *
                              (displaced_volume / probe_diameter) * (1.0F + probe_speed));

            result.buoyancy_force_n += probe_buoyancy;
            result.linear_drag_force_n += probe_drag;
            result.net_torque_nm += cd::math::cross(r_world, probe_buoyancy + probe_drag);
        }

        result.submerged_fraction = std::clamp(total_submerged_ratio / probe_count_f, 0.0F, 1.0F);

        if (result.submerged_fraction > 0.0F)
        {
            const float omega_mag = cd::math::length(angular_vel);
            result.angular_drag_torque_nm =
                -angular_vel * (result.submerged_fraction * fluid_density_kg_m3_ *
                                config.angular_drag_coeff * 0.15F * (1.0F + omega_mag));
            result.net_torque_nm += result.angular_drag_torque_nm;
        }

        result.net_force_n = result.buoyancy_force_n + result.linear_drag_force_n;
        return result;
    }

    /// Convenience overload with explicit scalar parameters (identity rotation default).
    [[nodiscard]] BuoyancyForceResult3D compute_buoyancy_forces(
        cd::math::Vec3f body_pos,
        cd::math::Vec3f linear_vel,
        cd::math::Vec3f angular_vel,
        std::span<const cd::math::Vec3f> local_probes,
        float total_volume_m3,
        float time_sec = 0.0F,
        float linear_drag = 1.25F,
        float angular_drag = 0.85F) const noexcept
    {
        BuoyancyBodyConfig3D cfg {};
        cfg.total_volume_m3 = total_volume_m3;
        cfg.linear_drag_coeff = linear_drag;
        cfg.angular_drag_coeff = angular_drag;
        return compute_buoyancy_forces(
            body_pos,
            cd::math::Quatf::identity(),
            linear_vel,
            angular_vel,
            local_probes,
            cfg,
            time_sec);
    }

private:
    float                               fluid_density_kg_m3_ { 1025.0F };
    float                               base_water_level_y_  { 0.0F };
    std::vector<GerstnerWaveComponent3D> waves_              {};
};

}  // namespace cd::physics::rockstar
