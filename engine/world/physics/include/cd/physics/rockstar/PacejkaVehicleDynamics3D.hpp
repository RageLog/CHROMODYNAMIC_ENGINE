// =============================================================================
// CHROMODYNAMIC — cd/physics/rockstar/PacejkaVehicleDynamics3D.hpp
// Rockstar GTA V / RAGE-style 3D Vehicle Dynamics & Chassis Deformation Grid.
//
// Features:
//   * 4-wheel independent spring-damper suspension + Front/Rear Anti-Roll Sway Bars.
//   * Pacejka "Magic Formula" (F = D * sin(C * atan(B*x - E*(B*x - atan(B*x)))))
//     coupled with Friction Ellipse combined slip for powerslides, handbrake
//     drifts, and burnouts.
//   * 6-speed Automatic/Manual transmission + Limited Slip Differential (LSD)
//     + Aerodynamic Downforce & Quadratic Drag.
//   * 4x3x2 (24-cell) Chassis Plastic Deformation Grid for localized collision
//     crumple zones.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cd::physics::rockstar
{

struct PacejkaTireParams
{
    float stiffness_b { 10.0F };
    float shape_c     { 1.9F };
    float peak_mu_d   { 1.15F };
    float curvature_e { 0.97F };
};

struct WheelRuntimeState
{
    float compression_m          { 0.0F };
    float suspension_force_n     { 0.0F };
    float slip_ratio             { 0.0F };
    float slip_angle_rad         { 0.0F };
    float longitudinal_force_n   { 0.0F };
    float lateral_force_n        { 0.0F };
    float angular_velocity_rad_s { 0.0F };
    bool  grounded               { true };
    bool  skidding               { false };
};

enum class WheelIndex : std::uint8_t
{
    kFrontLeft  = 0,
    kFrontRight = 1,
    kRearLeft   = 2,
    kRearRight  = 3,
    kCount      = 4
};

struct VehicleConfig3D
{
    float                 mass_kg                  { 1500.0F };
    cd::math::Vec3f       chassis_half_extents     { 0.95F, 0.65F, 2.20F };
    float                 suspension_rest_length_m { 0.35F };
    float                 suspension_stiffness_n_m { 35000.0F };
    float                 suspension_damping_n_s_m { 4200.0F };
    float                 anti_roll_stiffness_front { 12000.0F };
    float                 anti_roll_stiffness_rear  { 10000.0F };
    float                 wheel_radius_m           { 0.34F };
    float                 wheel_inertia_kg_m2      { 1.2F };
    float                 max_engine_torque_nm     { 520.0F };
    float                 max_brake_torque_nm      { 3600.0F };
    float                 handbrake_torque_nm      { 5200.0F };
    float                 lsd_locking_coeff        { 0.45F };
    float                 aero_drag_coeff          { 0.36F };
    float                 aero_downforce_coeff     { 1.35F };
    std::array<float, 6>  gear_ratios              { 3.42F, 2.14F, 1.45F, 1.10F, 0.88F, 0.72F };
    float                 final_drive_ratio        { 3.73F };
    PacejkaTireParams     tire_params              {};
};

struct VehicleInputState
{
    float         throttle            { 0.0F };
    float         brake               { 0.0F };
    float         steer_angle_rad     { 0.0F };
    float         handbrake           { 0.0F };
    bool          manual_transmission { false };
    std::uint32_t requested_gear      { 1u };
};

class PacejkaVehicleDynamics3D
{
public:
    static constexpr std::size_t kWheelCount = 4;
    static constexpr std::size_t kDeformGridX = 4;
    static constexpr std::size_t kDeformGridY = 3;
    static constexpr std::size_t kDeformGridZ = 2;
    static constexpr std::size_t kDeformCellCount = kDeformGridX * kDeformGridY * kDeformGridZ;

    constexpr PacejkaVehicleDynamics3D() noexcept = default;

    constexpr explicit PacejkaVehicleDynamics3D(const VehicleConfig3D& config) noexcept
        : config_ { config }
    {
        initialize_default_suspension();
    }

    constexpr void configure(const VehicleConfig3D& config) noexcept
    {
        config_ = config;
        initialize_default_suspension();
    }

    /// Evaluates the normalized Pacejka Magic Formula:
    /// Y(x) = D * sin(C * atan(B*x - E * (B*x - atan(B*x))))
    [[nodiscard]] static float evaluate_magic_formula(float slip, const PacejkaTireParams& p) noexcept
    {
        const float bx = p.stiffness_b * slip;
        const float inner = bx - p.curvature_e * (bx - std::atan(bx));
        return p.peak_mu_d * std::sin(p.shape_c * std::atan(inner));
    }

    /// Applies custom wheel compression values and computes suspension + anti-roll sway bar forces.
    void update_suspension_from_compressions(
        const std::array<float, kWheelCount>& compressions_m,
        float dt = 1.0F / 60.0F) noexcept
    {
        const float safe_dt = std::max(dt, 1e-4F);
        for (std::size_t i = 0; i < kWheelCount; ++i)
        {
            const float prev_comp = wheels_[i].compression_m;
            const float clamped_comp = std::clamp(compressions_m[i], 0.0F, config_.suspension_rest_length_m);
            const float comp_vel = (clamped_comp - prev_comp) / safe_dt;
            wheels_[i].compression_m = clamped_comp;
            wheels_[i].grounded = clamped_comp > 1e-5F;
            wheels_[i].suspension_force_n = std::max(
                0.0F,
                clamped_comp * config_.suspension_stiffness_n_m + comp_vel * config_.suspension_damping_n_s_m);
        }

        // Front & Rear Anti-Roll Sway Bar force transfer
        const float front_diff = wheels_[0].compression_m - wheels_[1].compression_m;
        anti_roll_force_front_n_ = front_diff * config_.anti_roll_stiffness_front;
        wheels_[0].suspension_force_n = std::max(0.0F, wheels_[0].suspension_force_n + anti_roll_force_front_n_);
        wheels_[1].suspension_force_n = std::max(0.0F, wheels_[1].suspension_force_n - anti_roll_force_front_n_);

        const float rear_diff = wheels_[2].compression_m - wheels_[3].compression_m;
        anti_roll_force_rear_n_ = rear_diff * config_.anti_roll_stiffness_rear;
        wheels_[2].suspension_force_n = std::max(0.0F, wheels_[2].suspension_force_n + anti_roll_force_rear_n_);
        wheels_[3].suspension_force_n = std::max(0.0F, wheels_[3].suspension_force_n - anti_roll_force_rear_n_);
    }

    void step(
        float throttle,
        float brake,
        float steer_angle_rad,
        float handbrake,
        float dt) noexcept
    {
        VehicleInputState input {};
        input.throttle = throttle;
        input.brake = brake;
        input.steer_angle_rad = steer_angle_rad;
        input.handbrake = handbrake;
        step(input, dt);
    }

    void step(const VehicleInputState& input, float dt) noexcept
    {
        if (dt <= 0.0F)
        {
            return;
        }

        const float throttle = std::clamp(input.throttle, 0.0F, 1.0F);
        const float brake = std::clamp(input.brake, 0.0F, 1.0F);
        const float handbrake = std::clamp(input.handbrake, 0.0F, 1.0F);
        const float steer = std::clamp(input.steer_angle_rad, -0.75F, 0.75F);

        const float forward_speed = linear_velocity_.z;
        const float lateral_speed = linear_velocity_.x;
        const float speed_sq = forward_speed * forward_speed + lateral_speed * lateral_speed;
        const float speed = std::sqrt(speed_sq);

        // 1. Aerodynamic quadratic drag & downforce
        aero_drag_force_n_ = 0.5F * 1.225F * config_.aero_drag_coeff * 2.2F * speed_sq;
        aero_downforce_n_ = 0.5F * 1.225F * config_.aero_downforce_coeff * 2.2F * speed_sq;

        // 2. Suspension + Dynamic Weight Transfer (pitch & roll) + Anti-Roll Sway Bars
        const float static_wheel_load = 0.25F * (config_.mass_kg * 9.81F + aero_downforce_n_);
        const float base_comp = std::clamp(
            static_wheel_load / std::max(config_.suspension_stiffness_n_m, 1.0F),
            0.02F,
            config_.suspension_rest_length_m * 0.85F);

        const float roll_comp_delta = std::clamp(lateral_accel_m_s2_ * 0.012F, -0.12F, 0.12F);
        const float pitch_comp_delta = std::clamp(longitudinal_accel_m_s2_ * 0.008F, -0.08F, 0.08F);

        const std::array<float, kWheelCount> target_compressions {{
            std::clamp(base_comp - pitch_comp_delta + roll_comp_delta, 0.0F, config_.suspension_rest_length_m),
            std::clamp(base_comp - pitch_comp_delta - roll_comp_delta, 0.0F, config_.suspension_rest_length_m),
            std::clamp(base_comp + pitch_comp_delta + roll_comp_delta, 0.0F, config_.suspension_rest_length_m),
            std::clamp(base_comp + pitch_comp_delta - roll_comp_delta, 0.0F, config_.suspension_rest_length_m),
        }};
        update_suspension_from_compressions(target_compressions, dt);

        // 3. Automatic / Manual 6-speed transmission & Engine RPM
        const float rear_wheel_omega = 0.5F * (wheels_[2].angular_velocity_rad_s + wheels_[3].angular_velocity_rad_s);
        const float current_ratio = config_.gear_ratios[std::clamp<std::size_t>(current_gear_ - 1u, 0u, 5u)] *
                                    config_.final_drive_ratio;
        engine_rpm_ = std::clamp(
            std::abs(rear_wheel_omega) * current_ratio * (60.0F / (2.0F * 3.14159265F)),
            900.0F,
            7500.0F);

        if (input.manual_transmission)
        {
            current_gear_ = std::clamp(input.requested_gear, 1u, 6u);
        }
        else
        {
            if (engine_rpm_ > 6400.0F && current_gear_ < 6u)
            {
                ++current_gear_;
            }
            else if (engine_rpm_ < 2100.0F && current_gear_ > 1u && speed < 40.0F)
            {
                --current_gear_;
            }
        }

        const float active_gear_ratio =
            config_.gear_ratios[std::clamp<std::size_t>(current_gear_ - 1u, 0u, 5u)] *
            config_.final_drive_ratio;

        // Torque curve peaking around 4500 RPM
        const float rpm_norm = (engine_rpm_ - 900.0F) / (7500.0F - 900.0F);
        const float torque_curve = 0.72F + 0.28F * std::sin(rpm_norm * 3.14159265F);
        const float total_drive_torque = throttle * config_.max_engine_torque_nm * torque_curve * active_gear_ratio;

        // 4. Limited Slip Differential (LSD) rear torque split
        const float rear_omega_diff = wheels_[2].angular_velocity_rad_s - wheels_[3].angular_velocity_rad_s;
        const float lsd_transfer = std::clamp(
            rear_omega_diff * config_.lsd_locking_coeff * 45.0F,
            -0.40F * total_drive_torque,
            0.40F * total_drive_torque);
        const float drive_torque_rl = 0.5F * total_drive_torque - lsd_transfer;
        const float drive_torque_rr = 0.5F * total_drive_torque + lsd_transfer;

        // 5. Per-wheel Pacejka Magic Formula + Friction Ellipse combined slip
        float net_fx = 0.0F;
        float net_fz = -aero_drag_force_n_ * (forward_speed >= 0.0F ? 1.0F : -1.0F);
        float net_yaw_torque = 0.0F;

        const float half_track = config_.chassis_half_extents.x;
        const float half_wheelbase = config_.chassis_half_extents.z * 0.75F;

        for (std::size_t i = 0; i < kWheelCount; ++i)
        {
            auto& w = wheels_[i];
            const bool is_front = (i < 2u);
            const float wheel_steer = is_front ? steer : 0.0F;

            // Integrate wheel angular velocity from drive & brake torques
            const float wheel_drive_torque = is_front ? 0.0F : ((i == 2u) ? drive_torque_rl : drive_torque_rr);
            const float wheel_brake_torque =
                brake * config_.max_brake_torque_nm + (is_front ? 0.0F : handbrake * config_.handbrake_torque_nm);

            const float free_rolling_omega = forward_speed / std::max(config_.wheel_radius_m, 0.1F);
            w.angular_velocity_rad_s += (wheel_drive_torque / std::max(config_.wheel_inertia_kg_m2, 0.1F)) * dt;

            if (wheel_brake_torque > 0.0F)
            {
                const float brake_delta = (wheel_brake_torque / std::max(config_.wheel_inertia_kg_m2, 0.1F)) * dt;
                if (std::abs(w.angular_velocity_rad_s) <= brake_delta)
                {
                    w.angular_velocity_rad_s = 0.0F;
                }
                else
                {
                    w.angular_velocity_rad_s -= std::copysign(brake_delta, w.angular_velocity_rad_s);
                }
            }
            else
            {
                // Relax toward ground rolling speed when not locked
                w.angular_velocity_rad_s = cd::math::lerp(w.angular_velocity_rad_s, free_rolling_omega, 0.35F);
            }

            const float wheel_ground_vel = w.angular_velocity_rad_s * config_.wheel_radius_m;
            const float denom_speed = std::max({ std::abs(forward_speed), std::abs(wheel_ground_vel), 1.0F });
            w.slip_ratio = std::clamp((wheel_ground_vel - forward_speed) / denom_speed, -1.0F, 1.0F);

            const float yaw_vel_x = angular_velocity_.y * (is_front ? half_wheelbase : -half_wheelbase);
            const float local_vx = lateral_speed + yaw_vel_x;
            const float local_vz = std::max(std::abs(forward_speed), 0.5F);
            w.slip_angle_rad = wheel_steer - std::atan2(local_vx, local_vz);

            if (!w.grounded)
            {
                w.longitudinal_force_n = 0.0F;
                w.lateral_force_n = 0.0F;
                w.skidding = false;
                continue;
            }

            const float normal_load = w.suspension_force_n;
            const float mu_long = evaluate_magic_formula(w.slip_ratio, config_.tire_params);
            const float mu_lat = evaluate_magic_formula(w.slip_angle_rad, config_.tire_params);

            float fx_tire = normal_load * mu_long;
            float fy_tire = normal_load * mu_lat;

            // Friction Ellipse: combined longitudinal + lateral force cannot exceed peak friction circle
            const float max_friction_n = normal_load * config_.tire_params.peak_mu_d;
            const float combined_force = std::sqrt(fx_tire * fx_tire + fy_tire * fy_tire);
            if (combined_force > max_friction_n && combined_force > 1e-5F)
            {
                const float scale = max_friction_n / combined_force;
                fx_tire *= scale;
                fy_tire *= scale;
            }

            // Handbrake drift induces rear lateral saturation
            if (!is_front && handbrake > 0.2F)
            {
                fy_tire *= (1.0F - 0.55F * handbrake);
            }

            w.longitudinal_force_n = fx_tire;
            w.lateral_force_n = fy_tire;
            w.skidding = (std::abs(w.slip_ratio) > 0.22F) ||
                         (std::abs(w.slip_angle_rad) > 0.18F) ||
                         (!is_front && handbrake > 0.5F && speed > 2.0F);

            // Project wheel forces into chassis frame
            const float cos_s = std::cos(wheel_steer);
            const float sin_s = std::sin(wheel_steer);
            const float chassis_fx = fy_tire * cos_s + fx_tire * sin_s;
            const float chassis_fz = fx_tire * cos_s - fy_tire * sin_s;

            net_fx += chassis_fx;
            net_fz += chassis_fz;

            const float lever_z = is_front ? half_wheelbase : -half_wheelbase;
            const float lever_x = (i % 2u == 0u) ? -half_track : half_track;
            net_yaw_torque += lever_z * chassis_fx - lever_x * chassis_fz;
        }

        // 6. Integrate chassis linear & angular state
        const float inv_mass = 1.0F / std::max(config_.mass_kg, 1.0F);
        lateral_accel_m_s2_ = net_fx * inv_mass;
        longitudinal_accel_m_s2_ = net_fz * inv_mass;

        linear_velocity_.x += lateral_accel_m_s2_ * dt;
        linear_velocity_.z += longitudinal_accel_m_s2_ * dt;
        linear_velocity_.x *= std::max(0.0F, 1.0F - 1.2F * dt);

        const float yaw_inertia = std::max(200.0F, config_.mass_kg * 1.6F);
        angular_velocity_.y += (net_yaw_torque / yaw_inertia) * dt;
        angular_velocity_.y *= std::max(0.0F, 1.0F - 2.0F * dt);

        position_ += linear_velocity_ * dt;
    }

    /// Applies plastic collision deformation to the 4x3x2 (24-cell) chassis grid
    /// and returns the resulting inward deformation offset vector at the impact region.
    cd::math::Vec3f apply_collision_deformation(
        cd::math::Vec3f local_hit_point,
        cd::math::Vec3f world_impulse) noexcept
    {
        const float impulse_mag = cd::math::length(world_impulse);
        if (impulse_mag <= 1e-5F)
        {
            return {};
        }

        const cd::math::Vec3f impulse_dir = world_impulse / impulse_mag;
        // Plastic yield threshold (~1500 N*s) and maximum crush limit (0.45 m)
        const float crush_magnitude = std::clamp(impulse_mag / 18000.0F, 0.01F, 0.45F);
        const cd::math::Vec3f primary_offset = impulse_dir * crush_magnitude;

        for (std::size_t ix = 0; ix < kDeformGridX; ++ix)
        {
            for (std::size_t iy = 0; iy < kDeformGridY; ++iy)
            {
                for (std::size_t iz = 0; iz < kDeformGridZ; ++iz)
                {
                    const cd::math::Vec3f cell_pos = cell_local_position(ix, iy, iz);
                    const float dist = cd::math::length(cell_pos - local_hit_point);
                    const float falloff = std::max(0.0F, 1.0F - dist / 1.85F);
                    if (falloff > 0.0F)
                    {
                        auto& cell_def = deformation_grid_[cell_index(ix, iy, iz)];
                        cell_def += primary_offset * falloff;
                        const float def_len = cd::math::length(cell_def);
                        if (def_len > 0.55F)
                        {
                            cell_def *= (0.55F / def_len);
                        }
                        max_deformation_m_ = std::max(max_deformation_m_, cd::math::length(cell_def));
                    }
                }
            }
        }

        return primary_offset;
    }

    [[nodiscard]] cd::math::Vec3f sample_deformation_at(cd::math::Vec3f local_point) const noexcept
    {
        std::size_t best_idx = 0;
        float best_dist_sq = 1e30F;
        for (std::size_t ix = 0; ix < kDeformGridX; ++ix)
        {
            for (std::size_t iy = 0; iy < kDeformGridY; ++iy)
            {
                for (std::size_t iz = 0; iz < kDeformGridZ; ++iz)
                {
                    const float d2 = cd::math::length_squared(cell_local_position(ix, iy, iz) - local_point);
                    if (d2 < best_dist_sq)
                    {
                        best_dist_sq = d2;
                        best_idx = cell_index(ix, iy, iz);
                    }
                }
            }
        }
        return deformation_grid_[best_idx];
    }

    constexpr void reset_deformation() noexcept
    {
        deformation_grid_.fill({});
        max_deformation_m_ = 0.0F;
    }

    [[nodiscard]] constexpr const WheelRuntimeState& wheel_state(std::size_t idx) const noexcept
    {
        return wheels_[idx < kWheelCount ? idx : 0u];
    }

    [[nodiscard]] constexpr const WheelRuntimeState& wheel_state(WheelIndex idx) const noexcept
    {
        return wheel_state(static_cast<std::size_t>(idx));
    }

    [[nodiscard]] constexpr std::span<const WheelRuntimeState> wheels() const noexcept
    {
        return wheels_;
    }

    [[nodiscard]] constexpr std::span<const cd::math::Vec3f> deformation_grid() const noexcept
    {
        return deformation_grid_;
    }

    [[nodiscard]] constexpr float max_deformation_m() const noexcept
    {
        return max_deformation_m_;
    }

    [[nodiscard]] constexpr cd::math::Vec3f position() const noexcept
    {
        return position_;
    }

    [[nodiscard]] constexpr cd::math::Vec3f linear_velocity() const noexcept
    {
        return linear_velocity_;
    }

    [[nodiscard]] constexpr cd::math::Vec3f angular_velocity() const noexcept
    {
        return angular_velocity_;
    }

    [[nodiscard]] constexpr float forward_speed_m_s() const noexcept
    {
        return linear_velocity_.z;
    }

    [[nodiscard]] constexpr std::uint32_t current_gear() const noexcept
    {
        return current_gear_;
    }

    [[nodiscard]] constexpr float engine_rpm() const noexcept
    {
        return engine_rpm_;
    }

    [[nodiscard]] constexpr float aero_downforce_n() const noexcept
    {
        return aero_downforce_n_;
    }

    [[nodiscard]] constexpr float aero_drag_force_n() const noexcept
    {
        return aero_drag_force_n_;
    }

    [[nodiscard]] constexpr float anti_roll_force_front_n() const noexcept
    {
        return anti_roll_force_front_n_;
    }

    [[nodiscard]] constexpr float anti_roll_force_rear_n() const noexcept
    {
        return anti_roll_force_rear_n_;
    }

    [[nodiscard]] constexpr const VehicleConfig3D& config() const noexcept
    {
        return config_;
    }

private:
    [[nodiscard]] static constexpr std::size_t cell_index(
        std::size_t ix,
        std::size_t iy,
        std::size_t iz) noexcept
    {
        return (iz * kDeformGridY + iy) * kDeformGridX + ix;
    }

    [[nodiscard]] constexpr cd::math::Vec3f cell_local_position(
        std::size_t ix,
        std::size_t iy,
        std::size_t iz) const noexcept
    {
        const float tx = (static_cast<float>(ix) / static_cast<float>(kDeformGridX - 1)) * 2.0F - 1.0F;
        const float ty = (static_cast<float>(iy) / static_cast<float>(kDeformGridY - 1)) * 2.0F - 1.0F;
        const float tz = (static_cast<float>(iz) / static_cast<float>(kDeformGridZ - 1)) * 2.0F - 1.0F;
        return {
            tx * config_.chassis_half_extents.x,
            ty * config_.chassis_half_extents.y,
            tz * config_.chassis_half_extents.z
        };
    }

    constexpr void initialize_default_suspension() noexcept
    {
        const float static_load = 0.25F * config_.mass_kg * 9.81F;
        const float static_comp = std::clamp(
            static_load / std::max(config_.suspension_stiffness_n_m, 1.0F),
            0.0F,
            config_.suspension_rest_length_m);
        for (auto& w : wheels_)
        {
            w.compression_m = static_comp;
            w.suspension_force_n = static_load;
            w.grounded = true;
        }
    }

    VehicleConfig3D                               config_ {};
    std::array<WheelRuntimeState, kWheelCount>    wheels_ {};
    std::array<cd::math::Vec3f, kDeformCellCount> deformation_grid_ {};
    cd::math::Vec3f                               position_ {};
    cd::math::Vec3f                               linear_velocity_ {};
    cd::math::Vec3f                               angular_velocity_ {};
    float                                         lateral_accel_m_s2_ { 0.0F };
    float                                         longitudinal_accel_m_s2_ { 0.0F };
    float                                         engine_rpm_ { 900.0F };
    std::uint32_t                                 current_gear_ { 1u };
    float                                         aero_downforce_n_ { 0.0F };
    float                                         aero_drag_force_n_ { 0.0F };
    float                                         anti_roll_force_front_n_ { 0.0F };
    float                                         anti_roll_force_rear_n_ { 0.0F };
    float                                         max_deformation_m_ { 0.0F };
};

}  // namespace cd::physics::rockstar
