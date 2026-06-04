// =============================================================================
// CHROMODYNAMIC — cd/animation/ik/Ik.hpp
// Phase 702 — cd::animation::ik Sprint-1 public API.
//
// CCD (Cyclic Coordinate Descent) Inverse Kinematics solver.
// Sidecar relationship:
//   cd::anim       — FK clip playback: skeleton, animation, pose, LBS skinning.
//   cd::animation::ik — WHERE joints point: IK solver (CCD algorithm).
//
// CCD algorithm:
//   Starting from the joint closest to the end effector and working toward
//   the root, each joint is rotated to minimise the distance between the
//   current end-effector position and the target position.  This is repeated
//   for up to max_iterations passes until convergence_threshold is met.
//
// Sprint-1 scope:
//   - Pure CCD, no joint angle limits (limits are Sprint-2).
//   - Solve one chain at a time, single-threaded.
//   - Positions expressed in world-space or a consistent local frame.
//   - Quaternion output (compact, no gimbal lock).
//
// Sprint-2 queue:
//   - Per-joint angle clamp constraints (twist / swing limits).
//   - FABRIK alternative solver (faster convergence on long chains).
//   - Multi-chain solve with shared parent locks.
//
// Thread-safety: none — caller must serialise.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cd::animation::ik
{

// =============================================================================
// Joint — a single bone in the IK chain
//
// name                 : debug identifier (joint name from skeleton, optional).
// local_position       : position of this joint relative to its parent joint,
//                        expressed in the frame of the parent joint.
//                        For the root joint, this is the world-space position.
// local_rotation_quat  : current rotation of this joint [x, y, z, w].
//                        Quaternion is expected to be unit-length.
//                        The identity quaternion is {0, 0, 0, 1}.
// length               : bone length in metres — the distance from this joint
//                        to its child joint (or to the end effector for the
//                        last joint in the chain).  Must be > 0.
// =============================================================================
struct Joint
{
    std::string            name                {};
    std::array<float, 3>   local_position      { 0.0F, 0.0F, 0.0F };
    std::array<float, 4>   local_rotation_quat { 0.0F, 0.0F, 0.0F, 1.0F };
    float                  length              { 1.0F };
};

// =============================================================================
// IkChain — the IK problem definition
//
// joints                : ordered chain, index 0 = root (closest to body),
//                         last index = the joint that drives the end effector.
//                         Minimum 1 joint.
// end_effector_target   : world-space (or consistent local-frame) position that
//                         the end of the chain should reach.
// max_iterations        : hard cap on CCD passes (default: 16).
// convergence_threshold : distance (metres) below which the solve is considered
//                         converged.  Larger = faster but less precise.
// =============================================================================
struct IkChain
{
    std::vector<Joint>     joints                {};
    std::array<float, 3>   end_effector_target   { 0.0F, 0.0F, 0.0F };
    uint32_t               max_iterations        { 16 };
    float                  convergence_threshold { 0.001F };
};

// =============================================================================
// IkResult — output from CcdSolver::solve
//
// converged            : true if final_distance <= convergence_threshold.
// iterations_used      : number of CCD passes actually executed.
// final_distance       : distance from end effector to target at termination.
// solved_rotations     : one quaternion [x, y, z, w] per joint in chain order.
//                        Same length as IkChain::joints.  The caller applies
//                        these to the matching joints in cd::anim::Pose.
// =============================================================================
struct IkResult
{
    bool                                  converged        { false };
    uint32_t                              iterations_used  { 0 };
    float                                 final_distance   { 0.0F };
    std::vector<std::array<float, 4>>     solved_rotations {};
};

// =============================================================================
// CcdSolver — stateless Cyclic Coordinate Descent IK solver
//
// Usage:
//   CcdSolver solver;
//   solver.configure(16, 0.001F);   // optional — these are the defaults
//
//   IkChain chain;
//   chain.joints = { root_joint, mid_joint, end_joint };
//   chain.end_effector_target = { 1.5F, 0.0F, 0.0F };
//
//   IkResult result = solver.solve(chain);
//   if (result.converged)
//       // apply result.solved_rotations to skeleton pose
//
// The solver is stateless: configure() stores solver-level defaults that can be
// overridden per-chain via IkChain::max_iterations / convergence_threshold.
// =============================================================================
class CcdSolver
{
public:
    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// Set solver defaults.  These are lower-priority overrides — each IkChain
    /// carries its own max_iterations + convergence_threshold that take
    /// precedence.  Useful for tuning a solver instance globally.
    ///
    /// max_iters         : default maximum CCD passes per solve.
    /// convergence_eps   : default convergence distance threshold (metres).
    void configure(uint32_t max_iters = 16U, float convergence_eps = 0.001F) noexcept;

    // -------------------------------------------------------------------------
    // Solve
    // -------------------------------------------------------------------------

    /// Run the CCD IK solver on the given chain.
    ///
    /// The chain is solved in-place conceptually:
    ///   - joint world positions are computed forward from chain.joints[0].
    ///   - Starting from the joint closest to the end effector, each joint is
    ///     rotated to align its child chain toward the target.
    ///   - The process repeats for up to max_iterations full passes.
    ///
    /// Returns IkResult with solved_rotations of same length as chain.joints.
    /// Returns near-best solution even if not converged (out-of-reach target).
    [[nodiscard]] IkResult solve(const IkChain& chain) const;

private:
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Compute world-space joint positions from local_position chain offsets.
    /// positions[0] = chain.joints[0].local_position (root world pos).
    /// positions[i] = positions[i-1] + rotate(rotations[i-1], {0, length[i-1], 0})
    /// (bone extends along the local +Y axis convention; adjust if needed).
    [[nodiscard]] static std::vector<std::array<float, 3>> compute_world_positions(
        const std::vector<Joint>&             joints,
        const std::vector<std::array<float, 4>>& rotations) noexcept;

    /// Compute end-effector world position: tip of the last bone, using the
    /// last joint's rotation and bone length.
    [[nodiscard]] static std::array<float, 3> compute_end_effector(
        const std::vector<std::array<float, 3>>& world_positions,
        const std::vector<Joint>&                joints,
        const std::vector<std::array<float, 4>>& rotations) noexcept;

    /// Euclidean distance between two 3D points.
    [[nodiscard]] static float distance(
        std::array<float, 3> a,
        std::array<float, 3> b) noexcept;

    /// Dot product of two 3-vectors.
    [[nodiscard]] static float dot3(
        std::array<float, 3> a,
        std::array<float, 3> b) noexcept;

    /// Cross product of two 3-vectors.
    [[nodiscard]] static std::array<float, 3> cross3(
        std::array<float, 3> a,
        std::array<float, 3> b) noexcept;

    /// L2 norm of a 3-vector.
    [[nodiscard]] static float length3(std::array<float, 3> v) noexcept;

    /// Normalize a 3-vector.  Returns {0,0,0} if length < eps.
    [[nodiscard]] static std::array<float, 3> normalize3(
        std::array<float, 3> v) noexcept;

    /// Compose two unit quaternions: q_out = q_b * q_a  (apply q_a first).
    [[nodiscard]] static std::array<float, 4> quat_mul(
        std::array<float, 4> q_b,
        std::array<float, 4> q_a) noexcept;

    /// Rotate vector v by unit quaternion q using Rodrigues formula.
    [[nodiscard]] static std::array<float, 3> quat_rotate(
        std::array<float, 4> q,
        std::array<float, 3> v) noexcept;

    /// Build a quaternion that rotates 'from' onto 'to' (both unit vectors).
    /// Returns identity if vectors are anti-parallel.
    [[nodiscard]] static std::array<float, 4> quat_from_two_vectors(
        std::array<float, 3> from_unit,
        std::array<float, 3> to_unit) noexcept;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    uint32_t   default_max_iters_   { 16U };
    float      default_convergence_ { 0.001F };
};

}  // namespace cd::animation::ik
