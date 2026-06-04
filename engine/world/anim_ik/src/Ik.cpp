// =============================================================================
// CHROMODYNAMIC — cd/animation/ik/Ik.cpp
// Phase 722 — cd::animation::ik Sprint-2 implementation (joint limits).
//
// CCD algorithm (per-iteration, per-joint):
//
//   For joint i (iterating from N-1 down to 0):
//     1. Compute world positions of all joints + end effector.
//     2. For joint i, form two unit vectors:
//          to_effector = normalize(end_effector - joint_i_world_pos)
//          to_target   = normalize(target      - joint_i_world_pos)
//     3. Build rotation delta: quat_from_two_vectors(to_effector, to_target).
//     4. Apply delta_q to joint i's accumulated rotation (world-space):
//          rotations[i] = quat_mul(delta_q, rotations[i])
//     5. Recompute world positions with updated rotation.
//
//   After all joints in a pass: check convergence.
//
// Bone forward direction convention:
//   The bone extends along the local +Y axis.  The child joint is at:
//     parent_world_pos + rotate(parent_rotation, {0, length, 0})
//   The end effector is the tip of the LAST bone:
//     world_pos[last] + rotate(rotations[last], {0, length_last, 0})
//
// Quaternion convention: [x, y, z, w] — same as glTF / DirectX Math.
// =============================================================================

#include <cd/animation/ik/Ik.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace cd::animation::ik
{

// =============================================================================
// JointLimitPresets — factory implementations (Sprint-2)
// =============================================================================

// Knee: sagittal hinge only.
//   X: 0 (no hyperextension) to ~140 deg flex (2.443 rad)
//   Y, Z: very small tolerance (±0.05 rad) to avoid hard lock, absorb numeric noise
JointLimits JointLimitPresets::knee() noexcept
{
    JointLimits lim;
    lim.enabled     = true;
    lim.min_euler   = {  0.0F,   -0.05F, -0.05F };
    lim.max_euler   = {  2.443F,  0.05F,  0.05F };
    return lim;
}

// Elbow: sagittal hinge only.
//   X: 0 (no hyperextension) to ~145 deg flex (2.530 rad)
//   Y, Z: small tolerance ±0.1 rad for forearm rotation
JointLimits JointLimitPresets::elbow() noexcept
{
    JointLimits lim;
    lim.enabled     = true;
    lim.min_euler   = {  0.0F,   -0.1F, -0.1F };
    lim.max_euler   = {  2.530F,  0.1F,  0.1F };
    return lim;
}

// Shoulder: ball-and-socket.
//   X (forward flex / extension): -0.52 (30 deg extension) to 2.97 (170 deg flex)
//   Y (abduction / adduction):    -0.52 (30 deg in) to 1.57 (90 deg out)
//   Z (internal / external rot):  -1.57 (90 deg internal) to 0.87 (50 deg external)
JointLimits JointLimitPresets::shoulder() noexcept
{
    JointLimits lim;
    lim.enabled     = true;
    lim.min_euler   = { -0.524F, -0.524F, -1.571F };
    lim.max_euler   = {  2.967F,  1.571F,  0.873F };
    return lim;
}

// Hip: ball-and-socket.
//   X (forward flex / extension): -0.35 (20 deg extension) to 2.09 (120 deg flex)
//   Y (abduction / adduction):    -0.35 (20 deg adduction) to 0.79 (45 deg abduction)
//   Z (internal / external rot):  -0.79 (45 deg internal) to 0.79 (45 deg external)
JointLimits JointLimitPresets::hip() noexcept
{
    JointLimits lim;
    lim.enabled     = true;
    lim.min_euler   = { -0.349F, -0.349F, -0.785F };
    lim.max_euler   = {  2.094F,  0.785F,  0.785F };
    return lim;
}

// =============================================================================
// CcdSolver — public interface
// =============================================================================

void CcdSolver::configure(uint32_t max_iters, float convergence_eps) noexcept
{
    default_max_iters_   = max_iters;
    default_convergence_ = convergence_eps;
}

IkResult CcdSolver::solve(const IkChain& chain) const
{
    const auto& joints = chain.joints;
    IkResult result{};

    if (joints.empty())
        return result;

    const auto n = static_cast<uint32_t>(joints.size());

    // Use per-chain settings
    const uint32_t max_iters = chain.max_iterations;
    const float    threshold = chain.convergence_threshold;

    // Working copy of joint rotations (start from chain's current rotations)
    std::vector<std::array<float, 4>> rotations(n);
    for (uint32_t i = 0U; i < n; ++i)
        rotations[i] = joints[i].local_rotation_quat;

    result.solved_rotations.resize(n);

    // -------------------------------------------------------------------------
    // CCD main loop
    // -------------------------------------------------------------------------
    for (uint32_t iter = 0U; iter < max_iters; ++iter)
    {
        // Iterate joints from end (closest to effector) back to root
        for (int32_t ji = static_cast<int32_t>(n) - 1; ji >= 0; --ji)
        {
            // Compute world positions + end effector with current rotations
            const auto world_pos = compute_world_positions(joints, rotations);
            const auto effector  = compute_end_effector(world_pos, joints, rotations);

            // Early exit if already converged
            const float dist_before = distance(effector, chain.end_effector_target);
            if (dist_before <= threshold)
            {
                result.converged       = true;
                result.iterations_used = iter;
                result.final_distance  = dist_before;
                result.solved_rotations = rotations;
                return result;
            }

            const std::size_t i = static_cast<std::size_t>(ji);
            const std::array<float, 3>& joint_pos = world_pos[i];

            // Direction from this joint to end effector (current)
            const std::array<float, 3> to_eff_raw = {
                effector[0]  - joint_pos[0],
                effector[1]  - joint_pos[1],
                effector[2]  - joint_pos[2]
            };

            // Direction from this joint to target
            const std::array<float, 3> to_tgt_raw = {
                chain.end_effector_target[0] - joint_pos[0],
                chain.end_effector_target[1] - joint_pos[1],
                chain.end_effector_target[2] - joint_pos[2]
            };

            // Skip if joint coincides with effector or target (degenerate)
            if (length3(to_eff_raw) < 1e-7F || length3(to_tgt_raw) < 1e-7F)
                continue;

            const std::array<float, 3> to_eff = normalize3(to_eff_raw);
            const std::array<float, 3> to_tgt = normalize3(to_tgt_raw);

            // Build rotation delta: rotates to_eff onto to_tgt
            const std::array<float, 4> delta_q = quat_from_two_vectors(to_eff, to_tgt);

            // Apply delta in world-space: new_rot = delta_q * old_rot
            rotations[i] = quat_mul(delta_q, rotations[i]);

            // Sprint-2: clamp to per-joint limits if enabled
            rotations[i] = apply_joint_limits(rotations[i], joints[i].limits);
        }

        // End-of-pass convergence check
        {
            const auto world_pos = compute_world_positions(joints, rotations);
            const auto effector  = compute_end_effector(world_pos, joints, rotations);
            const float dist     = distance(effector, chain.end_effector_target);

            if (dist <= threshold)
            {
                result.converged       = true;
                result.iterations_used = iter + 1U;
                result.final_distance  = dist;
                result.solved_rotations = rotations;
                return result;
            }
        }
    }

    // Exhausted iterations — return near-best
    {
        const auto world_pos = compute_world_positions(joints, rotations);
        const auto effector  = compute_end_effector(world_pos, joints, rotations);
        result.converged       = false;
        result.iterations_used = max_iters;
        result.final_distance  = distance(effector, chain.end_effector_target);
        result.solved_rotations = rotations;
    }
    return result;
}

// =============================================================================
// Static helpers
// =============================================================================

std::vector<std::array<float, 3>> CcdSolver::compute_world_positions(
    const std::vector<Joint>&                joints,
    const std::vector<std::array<float, 4>>& rotations) noexcept
{
    const std::size_t n = joints.size();
    std::vector<std::array<float, 3>> world_pos(n);

    // Root: local_position is the world-space position of joint 0
    world_pos[0] = joints[0].local_position;

    for (std::size_t i = 1U; i < n; ++i)
    {
        // Child position = parent_world_pos + rotate(parent_rot, {0, length, 0})
        const std::array<float, 3> bone_local { 0.0F, joints[i - 1U].length, 0.0F };
        const std::array<float, 3> bone_world = quat_rotate(rotations[i - 1U], bone_local);

        world_pos[i] = {
            world_pos[i - 1U][0] + bone_world[0],
            world_pos[i - 1U][1] + bone_world[1],
            world_pos[i - 1U][2] + bone_world[2]
        };
    }

    return world_pos;
}

std::array<float, 3> CcdSolver::compute_end_effector(
    const std::vector<std::array<float, 3>>& world_positions,
    const std::vector<Joint>&                joints,
    const std::vector<std::array<float, 4>>& rotations) noexcept
{
    if (world_positions.empty())
        return { 0.0F, 0.0F, 0.0F };

    const std::size_t last = world_positions.size() - 1U;

    // End effector = tip of the last bone
    // The last bone extends along local +Y by joints[last].length, rotated by rotations[last]
    const std::array<float, 3> bone_local { 0.0F, joints[last].length, 0.0F };
    const std::array<float, 3> bone_world = quat_rotate(rotations[last], bone_local);

    return {
        world_positions[last][0] + bone_world[0],
        world_positions[last][1] + bone_world[1],
        world_positions[last][2] + bone_world[2]
    };
}

float CcdSolver::distance(std::array<float, 3> a, std::array<float, 3> b) noexcept
{
    const float dx = b[0] - a[0];
    const float dy = b[1] - a[1];
    const float dz = b[2] - a[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float CcdSolver::dot3(std::array<float, 3> a, std::array<float, 3> b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> CcdSolver::cross3(
    std::array<float, 3> a,
    std::array<float, 3> b) noexcept
{
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    };
}

float CcdSolver::length3(std::array<float, 3> v) noexcept
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

std::array<float, 3> CcdSolver::normalize3(std::array<float, 3> v) noexcept
{
    const float len = length3(v);
    if (len < 1e-7F)
        return { 0.0F, 0.0F, 0.0F };
    const float inv = 1.0F / len;
    return { v[0] * inv, v[1] * inv, v[2] * inv };
}

std::array<float, 4> CcdSolver::quat_mul(
    std::array<float, 4> q_b,
    std::array<float, 4> q_a) noexcept
{
    // Hamilton product: q_b * q_a  (q_a applied first)
    // [x, y, z, w] convention
    const float ax = q_a[0], ay = q_a[1], az = q_a[2], aw = q_a[3];
    const float bx = q_b[0], by = q_b[1], bz = q_b[2], bw = q_b[3];

    return {
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz
    };
}

std::array<float, 3> CcdSolver::quat_rotate(
    std::array<float, 4> q,
    std::array<float, 3> v) noexcept
{
    // Rodrigues rotation: v' = v + 2w*(q_xyz x v) + 2*(q_xyz x (q_xyz x v))
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];

    const float tx = 2.0F * (qy * v[2] - qz * v[1]);
    const float ty = 2.0F * (qz * v[0] - qx * v[2]);
    const float tz = 2.0F * (qx * v[1] - qy * v[0]);

    return {
        v[0] + qw * tx + (qy * tz - qz * ty),
        v[1] + qw * ty + (qz * tx - qx * tz),
        v[2] + qw * tz + (qx * ty - qy * tx)
    };
}

std::array<float, 4> CcdSolver::quat_from_two_vectors(
    std::array<float, 3> from_unit,
    std::array<float, 3> to_unit) noexcept
{
    // cos(angle) = dot(from, to)
    const float cos_angle = std::clamp(dot3(from_unit, to_unit), -1.0F, 1.0F);

    // Anti-parallel case: rotation of pi around an arbitrary perpendicular axis
    if (cos_angle <= -1.0F + 1e-6F)
    {
        // Find a perpendicular axis
        std::array<float, 3> perp = cross3(from_unit, { 1.0F, 0.0F, 0.0F });
        if (length3(perp) < 1e-5F)
            perp = cross3(from_unit, { 0.0F, 1.0F, 0.0F });
        perp = normalize3(perp);
        // 180-degree rotation quaternion: [axis * sin(pi/2), cos(pi/2)] = [axis, 0]
        return { perp[0], perp[1], perp[2], 0.0F };
    }

    // General case: axis = cross(from, to), half-angle formula
    const std::array<float, 3> axis = cross3(from_unit, to_unit);
    const float axis_len = length3(axis);

    if (axis_len < 1e-7F)
    {
        // Parallel vectors — identity quaternion (no rotation needed)
        return { 0.0F, 0.0F, 0.0F, 1.0F };
    }

    // sin(angle) = axis_len (for unit input vectors)
    // Use half-angle:
    //   w = cos(angle/2) = sqrt((1 + cos_angle) / 2)
    //   |xyz| = sin(angle/2) = sqrt((1 - cos_angle) / 2)
    const float w   = std::sqrt(std::max(0.0F, (1.0F + cos_angle) * 0.5F));
    const float s   = std::sqrt(std::max(0.0F, (1.0F - cos_angle) * 0.5F));
    const float inv = s / axis_len;

    return {
        axis[0] * inv,
        axis[1] * inv,
        axis[2] * inv,
        w
    };
}

// =============================================================================
// Sprint-2 helpers: Euler <-> quaternion, joint-limit clamping
// =============================================================================

// Intrinsic XYZ Euler decomposition from unit quaternion [x, y, z, w].
// Returns [pitch_x, yaw_y, roll_z] in radians, range [-pi, pi].
// Uses standard rotation matrix extraction to avoid gimbal-lock singularity
// issues at poles (±90 deg pitch), where yaw/roll are degenerate; we keep
// yaw=0 in the degenerate case (safe default for IK clamping).
std::array<float, 3> CcdSolver::quat_to_euler_xyz(std::array<float, 4> q) noexcept
{
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];

    // Rotation matrix elements needed for XYZ Euler extraction
    // R = Rz * Ry * Rx  (intrinsic XYZ = extrinsic ZYX)
    // sin(pitch) = R[2][0] = 2*(qx*qz + qy*qw)  — note sign differs by convention
    // We use the column-major rotation matrix form.
    //
    // M[row][col]:
    //   M[0][0] = 1 - 2*(qy^2 + qz^2)
    //   M[1][0] = 2*(qx*qy + qz*qw)
    //   M[2][0] = 2*(qx*qz - qy*qw)
    //   M[2][1] = 2*(qy*qz + qx*qw)
    //   M[2][2] = 1 - 2*(qx^2 + qy^2)

    const float m20 = 2.0F * (qx * qz - qy * qw);
    const float m21 = 2.0F * (qy * qz + qx * qw);
    const float m22 = 1.0F - 2.0F * (qx * qx + qy * qy);
    const float m10 = 2.0F * (qx * qy + qz * qw);
    const float m00 = 1.0F - 2.0F * (qy * qy + qz * qz);

    // pitch_y (rotation about Y from intrinsic XYZ)
    // sin(ry) = m20 (clamped to [-1, 1])
    const float sin_ry = std::clamp(-m20, -1.0F, 1.0F);  // note negation: R[2][0] = -sin(ry)
    const float ry = std::asin(sin_ry);

    float rx = 0.0F;
    float rz = 0.0F;
    const float cos_ry = std::cos(ry);

    if (cos_ry > 1e-6F)
    {
        // General case
        rx = std::atan2(m21 / cos_ry, m22 / cos_ry);
        rz = std::atan2(m10 / cos_ry, m00 / cos_ry);
    }
    else
    {
        // Gimbal lock: set rz=0, solve rx
        // When ry = +pi/2: m21 = 2*(qy*qz + qx*qw), m10 similar
        rx = std::atan2(m21, m22);
        rz = 0.0F;
    }

    return { rx, ry, rz };
}

// Build unit quaternion from intrinsic XYZ Euler angles (radians).
// q = Rx * Ry * Rz  (applied X first, then Y, then Z about fixed axes)
// Equivalent to: qz * qy * qx  (Hamilton product, rightmost applied first).
std::array<float, 4> CcdSolver::euler_xyz_to_quat(std::array<float, 3> euler) noexcept
{
    const float hx = euler[0] * 0.5F;
    const float hy = euler[1] * 0.5F;
    const float hz = euler[2] * 0.5F;

    const float cx = std::cos(hx), sx = std::sin(hx);
    const float cy = std::cos(hy), sy = std::sin(hy);
    const float cz = std::cos(hz), sz = std::sin(hz);

    // q = qx * qy * qz  (intrinsic XYZ)
    return {
        sx * cy * cz + cx * sy * sz,
        cx * sy * cz - sx * cy * sz,
        cx * cy * sz + sx * sy * cz,
        cx * cy * cz - sx * sy * sz
    };
}

// Clamp quaternion to JointLimits by decomposing to intrinsic XYZ Euler,
// clamping each axis, then rebuilding.  Returns q unchanged if !limits.enabled.
std::array<float, 4> CcdSolver::apply_joint_limits(
    std::array<float, 4> q,
    const JointLimits&   limits) noexcept
{
    if (!limits.enabled)
        return q;

    std::array<float, 3> euler = quat_to_euler_xyz(q);

    euler[0] = std::clamp(euler[0], limits.min_euler[0], limits.max_euler[0]);
    euler[1] = std::clamp(euler[1], limits.min_euler[1], limits.max_euler[1]);
    euler[2] = std::clamp(euler[2], limits.min_euler[2], limits.max_euler[2]);

    return euler_xyz_to_quat(euler);
}

}  // namespace cd::animation::ik
