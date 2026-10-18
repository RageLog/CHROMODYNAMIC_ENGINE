// =============================================================================
// CHROMODYNAMIC — cd/physics/rockstar/ActiveRagdollEuphoria3D.hpp
// Rockstar RAGE / NaturalMotion Euphoria-style Biomechanical Active Ragdoll.
//
// Combines proportional-derivative (PD) joint muscle drives, dynamic stagger
// (muscle stiffness reduction under ballistic impact), shock propagation across
// the kinematic hierarchy, Inverted Pendulum Capture Point (ZMP/ICP) balance
// recovery stepping, and protective arm-bracing reflexes during falls.
//
// References:
//   * Pratt et al. (2006), "Capture Point: A Step toward Humanoid Push Recovery"
//   * NaturalMotion Euphoria / Rockstar RAGE procedural biomechanical synthesis
//   * Yin et al. (2007), "SIMBICON: Simple Biped Locomotion Control"
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

enum class RagdollBoneId : std::uint8_t
{
    kPelvis = 0,
    kSpine,
    kChest,
    kHead,
    kUpperArmL,
    kForearmL,
    kUpperArmR,
    kForearmR,
    kThighL,
    kCalfL,
    kFootL,
    kThighR,
    kCalfR,
    kFootR,
    kCount
};

[[nodiscard]] constexpr std::size_t to_bone_index(RagdollBoneId id) noexcept
{
    return static_cast<std::size_t>(id);
}

struct MuscleDriveConfig
{
    float proportional_kp  { 180.0F };
    float derivative_kd    { 22.0F };
    float max_torque_nm    { 320.0F };
    float muscle_stiffness { 1.0F };
};

struct BalanceState3D
{
    cd::math::Vec3f center_of_mass         {};
    cd::math::Vec3f com_velocity           {};
    cd::math::Vec3f capture_point          {};
    cd::math::Vec3f support_polygon_center {};
    float           balance_error_m        { 0.0F };
    bool            needs_recovery_step    { false };
    bool            is_falling             { false };
    bool            brace_for_impact       { false };
    cd::math::Vec3f step_target_world      {};
    cd::math::Vec3f brace_target_world     {};
};

struct RagdollBoneState3D
{
    RagdollBoneId   id                      { RagdollBoneId::kPelvis };
    RagdollBoneId   parent                  { RagdollBoneId::kPelvis };
    float           mass_kg                 { 5.0F };
    cd::math::Vec3f local_offset            {};
    cd::math::Vec3f world_position          {};
    cd::math::Vec3f linear_velocity         {};
    cd::math::Quatf local_rotation          { cd::math::Quatf::identity() };
    cd::math::Quatf target_local_rotation   { cd::math::Quatf::identity() };
    cd::math::Vec3f angular_velocity        {};
    cd::math::Vec3f applied_muscle_torque   {};
    MuscleDriveConfig muscle                {};
    float           current_stiffness_scale { 1.0F };
    float           stagger_timer_s         { 0.0F };
};

class ActiveRagdollEuphoria3D
{
public:
    static constexpr std::size_t kBoneCount = static_cast<std::size_t>(RagdollBoneId::kCount);
    static constexpr float kGravityMagnitude = 9.81F;
    static constexpr float kSupportRadiusM = 0.18F;
    static constexpr float kFallingErrorThresholdM = 0.55F;

    constexpr ActiveRagdollEuphoria3D() noexcept
    {
        configure_humanoid_rig({ 0.0F, 1.0F, 0.0F }, 78.0F);
    }

    constexpr void configure_humanoid_rig(
        cd::math::Vec3f root_pos = { 0.0F, 1.0F, 0.0F },
        float total_mass_kg = 78.0F) noexcept
    {
        const float safe_mass = total_mass_kg > 1.0F ? total_mass_kg : 78.0F;
        total_mass_kg_ = safe_mass;
        configured_ = true;

        // Biomechanical Winter (2009) anthropometry mass fractions & local offsets
        struct BoneTemplate
        {
            RagdollBoneId   id;
            RagdollBoneId   parent;
            float           mass_fraction;
            cd::math::Vec3f offset_from_parent;
            float           kp;
            float           kd;
            float           max_torque;
        };

        constexpr std::array<BoneTemplate, kBoneCount> kTemplates {{
            { RagdollBoneId::kPelvis,    RagdollBoneId::kPelvis,    0.142F, {  0.00F,  0.00F, 0.0F }, 240.0F, 28.0F, 450.0F },
            { RagdollBoneId::kSpine,     RagdollBoneId::kPelvis,    0.130F, {  0.00F,  0.22F, 0.0F }, 220.0F, 26.0F, 400.0F },
            { RagdollBoneId::kChest,     RagdollBoneId::kSpine,     0.160F, {  0.00F,  0.24F, 0.0F }, 200.0F, 24.0F, 360.0F },
            { RagdollBoneId::kHead,      RagdollBoneId::kChest,     0.081F, {  0.00F,  0.26F, 0.0F }, 120.0F, 16.0F, 180.0F },
            { RagdollBoneId::kUpperArmL, RagdollBoneId::kChest,     0.028F, { -0.22F,  0.14F, 0.0F }, 140.0F, 18.0F, 220.0F },
            { RagdollBoneId::kForearmL,  RagdollBoneId::kUpperArmL, 0.022F, { -0.26F,  0.00F, 0.0F }, 110.0F, 14.0F, 160.0F },
            { RagdollBoneId::kUpperArmR, RagdollBoneId::kChest,     0.028F, {  0.22F,  0.14F, 0.0F }, 140.0F, 18.0F, 220.0F },
            { RagdollBoneId::kForearmR,  RagdollBoneId::kUpperArmR, 0.022F, {  0.26F,  0.00F, 0.0F }, 110.0F, 14.0F, 160.0F },
            { RagdollBoneId::kThighL,    RagdollBoneId::kPelvis,    0.100F, { -0.11F, -0.08F, 0.0F }, 260.0F, 30.0F, 480.0F },
            { RagdollBoneId::kCalfL,     RagdollBoneId::kThighL,    0.0465F,{  0.00F, -0.42F, 0.0F }, 220.0F, 25.0F, 380.0F },
            { RagdollBoneId::kFootL,     RagdollBoneId::kCalfL,     0.014F, {  0.00F, -0.42F, 0.06F }, 150.0F, 18.0F, 240.0F },
            { RagdollBoneId::kThighR,    RagdollBoneId::kPelvis,    0.100F, {  0.11F, -0.08F, 0.0F }, 260.0F, 30.0F, 480.0F },
            { RagdollBoneId::kCalfR,     RagdollBoneId::kThighR,    0.0465F,{  0.00F, -0.42F, 0.0F }, 220.0F, 25.0F, 380.0F },
            { RagdollBoneId::kFootR,     RagdollBoneId::kCalfR,     0.080F, {  0.00F, -0.42F, 0.06F }, 150.0F, 18.0F, 240.0F },
        }};

        for (std::size_t i = 0; i < kBoneCount; ++i)
        {
            const auto& tmpl = kTemplates[i];
            auto& b = bones_[i];
            b.id = tmpl.id;
            b.parent = tmpl.parent;
            b.mass_kg = safe_mass * tmpl.mass_fraction;
            b.local_offset = tmpl.offset_from_parent;
            b.world_position = (i == 0)
                ? root_pos
                : (bones_[to_bone_index(tmpl.parent)].world_position + tmpl.offset_from_parent);
            b.linear_velocity = {};
            b.local_rotation = cd::math::Quatf::identity();
            b.target_local_rotation = cd::math::Quatf::identity();
            b.angular_velocity = {};
            b.applied_muscle_torque = {};
            b.muscle = MuscleDriveConfig { tmpl.kp, tmpl.kd, tmpl.max_torque, 1.0F };
            b.current_stiffness_scale = 1.0F;
            b.stagger_timer_s = 0.0F;
        }

        update_balance_telemetry(0.0F);
    }

    constexpr void set_target_pose(RagdollBoneId bone, cd::math::Quatf target_local_rot) noexcept
    {
        const std::size_t idx = to_bone_index(bone);
        if (idx < kBoneCount)
        {
            bones_[idx].target_local_rotation = cd::math::normalize(target_local_rot);
        }
    }

    constexpr void set_muscle_config(RagdollBoneId bone, const MuscleDriveConfig& config) noexcept
    {
        const std::size_t idx = to_bone_index(bone);
        if (idx < kBoneCount)
        {
            bones_[idx].muscle = config;
        }
    }

    void apply_ballistic_hit_impulse(
        RagdollBoneId hit_bone,
        cd::math::Vec3f world_impulse,
        cd::math::Vec3f hit_point = {}) noexcept
    {
        const std::size_t idx = to_bone_index(hit_bone);
        if (idx >= kBoneCount)
        {
            return;
        }

        const float impulse_mag = cd::math::length(world_impulse);
        if (impulse_mag <= 1e-6F)
        {
            return;
        }

        // 1. Direct ballistic shock to hit bone: drop muscle stiffness & start stagger timer
        auto& target = bones_[idx];
        const float safe_bone_mass = std::max(target.mass_kg, 0.5F);
        target.linear_velocity += (world_impulse * 0.55F) / safe_bone_mass;

        const cd::math::Vec3f lever = (cd::math::length_squared(hit_point) > 1e-6F)
            ? (hit_point - target.world_position)
            : cd::math::Vec3f { 0.0F, 0.08F, 0.0F };
        const cd::math::Vec3f angular_impulse = cd::math::cross(lever, world_impulse);
        target.angular_velocity += angular_impulse / (safe_bone_mass * 0.12F);

        const float shock_severity = std::clamp(impulse_mag / 120.0F, 0.15F, 0.90F);
        target.current_stiffness_scale = std::clamp(1.0F - shock_severity, 0.10F, 1.0F);
        target.stagger_timer_s = std::max(target.stagger_timer_s, 0.25F + 0.65F * shock_severity);

        // 2. Propagate shockwave to parent, pelvis, and whole-body momentum
        const std::size_t parent_idx = to_bone_index(target.parent);
        if (parent_idx < kBoneCount && parent_idx != idx)
        {
            auto& parent_bone = bones_[parent_idx];
            parent_bone.linear_velocity += (world_impulse * 0.30F) / std::max(parent_bone.mass_kg, 1.0F);
            parent_bone.current_stiffness_scale = std::min(
                parent_bone.current_stiffness_scale,
                std::clamp(1.0F - shock_severity * 0.6F, 0.25F, 1.0F));
            parent_bone.stagger_timer_s = std::max(parent_bone.stagger_timer_s, 0.18F + 0.35F * shock_severity);
        }

        // Distribute residual impulse across all segments so CoM velocity reflects whole-body push
        const cd::math::Vec3f whole_body_dv = world_impulse / std::max(total_mass_kg_, 1.0F);
        for (auto& bone : bones_)
        {
            bone.linear_velocity += whole_body_dv * 0.65F;
        }

        update_balance_telemetry(0.0F);
    }

    void step(float dt, float floor_y = 0.0F) noexcept
    {
        if (dt <= 0.0F)
        {
            return;
        }

        // 1. Recover stagger timers and compute PD joint muscle torques
        for (std::size_t i = 0; i < kBoneCount; ++i)
        {
            auto& b = bones_[i];
            if (b.stagger_timer_s > 0.0F)
            {
                b.stagger_timer_s = std::max(0.0F, b.stagger_timer_s - dt);
                const float recovery_rate = 1.8F * dt;
                b.current_stiffness_scale = std::min(1.0F, b.current_stiffness_scale + recovery_rate);
            }
            else
            {
                b.current_stiffness_scale = std::min(1.0F, b.current_stiffness_scale + 3.0F * dt);
            }

            // Quaternion error q_err = q_target * conjugate(q_current)
            cd::math::Quatf q_err = b.target_local_rotation * cd::math::conjugate(b.local_rotation);
            if (q_err.w < 0.0F)
            {
                q_err.x = -q_err.x;
                q_err.y = -q_err.y;
                q_err.z = -q_err.z;
                q_err.w = -q_err.w;
            }

            // Small-angle / axis-angle rotation error vector in radians (2 * imag)
            const cd::math::Vec3f rot_error { 2.0F * q_err.x, 2.0F * q_err.y, 2.0F * q_err.z };
            const float effective_stiffness = b.muscle.muscle_stiffness * b.current_stiffness_scale;

            cd::math::Vec3f torque =
                (rot_error * b.muscle.proportional_kp - b.angular_velocity * b.muscle.derivative_kd) *
                effective_stiffness;

            const float torque_mag = cd::math::length(torque);
            const float max_torque = std::max(0.0F, b.muscle.max_torque_nm * effective_stiffness);
            if (torque_mag > max_torque && torque_mag > 1e-6F)
            {
                torque *= (max_torque / torque_mag);
            }
            b.applied_muscle_torque = torque;

            // Integrate angular velocity & local orientation
            const float approx_inertia = std::max(0.08F, b.mass_kg * 0.06F);
            b.angular_velocity += (torque / approx_inertia) * dt;
            b.angular_velocity *= std::max(0.0F, 1.0F - 4.0F * dt);

            const float omega_mag = cd::math::length(b.angular_velocity);
            if (omega_mag > 1e-6F)
            {
                const cd::math::Quatf dq =
                    cd::math::Quatf::from_axis_angle(b.angular_velocity / omega_mag, omega_mag * dt);
                b.local_rotation = cd::math::normalize(dq * b.local_rotation);
            }
        }

        // 2. Integrate linear kinematics and maintain hierarchical attachment + floor contact
        auto& pelvis = bones_[to_bone_index(RagdollBoneId::kPelvis)];
        // Upright stabilization force when not falling, or gravity pull when falling
        const float upright_support = balance_.is_falling ? 0.25F : 0.92F;
        pelvis.linear_velocity.y -= kGravityMagnitude * (1.0F - upright_support) * dt;
        pelvis.linear_velocity *= std::max(0.0F, 1.0F - 1.5F * dt);
        pelvis.world_position += pelvis.linear_velocity * dt;
        if (pelvis.world_position.y < floor_y + 0.15F)
        {
            pelvis.world_position.y = floor_y + 0.15F;
            if (pelvis.linear_velocity.y < 0.0F)
            {
                pelvis.linear_velocity.y = 0.0F;
            }
        }

        for (std::size_t i = 1; i < kBoneCount; ++i)
        {
            auto& b = bones_[i];
            const auto& parent_bone = bones_[to_bone_index(b.parent)];
            const cd::math::Vec3f rotated_offset = cd::math::rotate(parent_bone.local_rotation, b.local_offset);
            const cd::math::Vec3f target_world_pos = parent_bone.world_position + rotated_offset;

            b.linear_velocity *= std::max(0.0F, 1.0F - 2.2F * dt);
            b.world_position += b.linear_velocity * dt;
            // Enforce articulated joint constraint toward parent attachment
            const cd::math::Vec3f correction = (target_world_pos - b.world_position) * 0.85F;
            b.world_position += correction;
            b.linear_velocity += correction / std::max(dt, 1e-4F) * 0.15F;

            if (b.world_position.y < floor_y)
            {
                b.world_position.y = floor_y;
                if (b.linear_velocity.y < 0.0F)
                {
                    b.linear_velocity.y = 0.0F;
                }
            }
        }

        // 3. Recompute Capture Point, protective stepping, and arm-bracing reflexes
        update_balance_telemetry(floor_y);

        // 4. Procedural Euphoria reflexes: drive legs toward step_target and arms toward brace_target
        if (balance_.needs_recovery_step)
        {
            const cd::math::Vec3f step_dir = balance_.step_target_world - balance_.support_polygon_center;
            const float pitch_angle = std::clamp(step_dir.z * 0.65F, -0.75F, 0.75F);
            const float roll_angle = std::clamp(-step_dir.x * 0.55F, -0.55F, 0.55F);
            const cd::math::Quatf step_rot =
                cd::math::Quatf::from_axis_angle({ 1.0F, 0.0F, 0.0F }, pitch_angle) *
                cd::math::Quatf::from_axis_angle({ 0.0F, 0.0F, 1.0F }, roll_angle);
            set_target_pose(RagdollBoneId::kThighR, step_rot);
        }

        if (balance_.brace_for_impact)
        {
            const cd::math::Quatf arm_reach =
                cd::math::Quatf::from_axis_angle({ 1.0F, 0.0F, 0.0F }, -1.05F);
            set_target_pose(RagdollBoneId::kUpperArmL, arm_reach);
            set_target_pose(RagdollBoneId::kUpperArmR, arm_reach);
        }
    }

    [[nodiscard]] constexpr const BalanceState3D& balance_state() const noexcept
    {
        return balance_;
    }

    [[nodiscard]] constexpr const RagdollBoneState3D& bone_state(RagdollBoneId bone) const noexcept
    {
        const std::size_t idx = to_bone_index(bone);
        return bones_[idx < kBoneCount ? idx : 0u];
    }

    [[nodiscard]] constexpr std::span<const RagdollBoneState3D> bones() const noexcept
    {
        return bones_;
    }

    [[nodiscard]] constexpr float total_mass_kg() const noexcept
    {
        return total_mass_kg_;
    }

    [[nodiscard]] constexpr bool is_configured() const noexcept
    {
        return configured_;
    }

private:
    constexpr void update_balance_telemetry(float floor_y) noexcept
    {
        cd::math::Vec3f weighted_pos {};
        cd::math::Vec3f weighted_vel {};
        float mass_sum = 0.0F;

        for (const auto& b : bones_)
        {
            weighted_pos += b.world_position * b.mass_kg;
            weighted_vel += b.linear_velocity * b.mass_kg;
            mass_sum += b.mass_kg;
        }

        const float inv_mass = mass_sum > 1e-6F ? (1.0F / mass_sum) : 1.0F;
        balance_.center_of_mass = weighted_pos * inv_mass;
        balance_.com_velocity = weighted_vel * inv_mass;

        const auto& foot_l = bones_[to_bone_index(RagdollBoneId::kFootL)];
        const auto& foot_r = bones_[to_bone_index(RagdollBoneId::kFootR)];
        balance_.support_polygon_center = {
            0.5F * (foot_l.world_position.x + foot_r.world_position.x),
            floor_y,
            0.5F * (foot_l.world_position.z + foot_r.world_position.z)
        };

        // Inverted Pendulum Capture Point: x_cp = x_com + v_com * sqrt(h / g)
        const float com_height = std::max(0.15F, balance_.center_of_mass.y - floor_y);
        const float omega_inv = std::sqrt(com_height / kGravityMagnitude);

        balance_.capture_point = {
            balance_.center_of_mass.x + balance_.com_velocity.x * omega_inv,
            floor_y,
            balance_.center_of_mass.z + balance_.com_velocity.z * omega_inv
        };

        const float dx = balance_.capture_point.x - balance_.support_polygon_center.x;
        const float dz = balance_.capture_point.z - balance_.support_polygon_center.z;
        balance_.balance_error_m = std::sqrt(dx * dx + dz * dz);

        balance_.needs_recovery_step = balance_.balance_error_m > kSupportRadiusM;
        balance_.step_target_world = balance_.capture_point;

        const bool pelvis_low =
            (bones_[to_bone_index(RagdollBoneId::kPelvis)].world_position.y - floor_y) < 0.55F;
        balance_.is_falling =
            (balance_.balance_error_m > kFallingErrorThresholdM) ||
            (balance_.com_velocity.y < -1.8F) ||
            pelvis_low;

        balance_.brace_for_impact =
            balance_.is_falling || (balance_.balance_error_m > 0.35F);
        balance_.brace_target_world = {
            balance_.capture_point.x,
            floor_y,
            balance_.capture_point.z
        };
    }

    std::array<RagdollBoneState3D, kBoneCount> bones_ {};
    BalanceState3D                             balance_ {};
    float                                      total_mass_kg_ { 78.0F };
    bool                                       configured_ { false };
};

}  // namespace cd::physics::rockstar
