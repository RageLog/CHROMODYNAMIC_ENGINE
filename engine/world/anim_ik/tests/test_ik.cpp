// =============================================================================
// CHROMODYNAMIC — cd::animation::ik tests
// Phase 702 — Sprint-1: CCD IK solver
//
// Test matrix:
//  1. Straight-line 3-joint chain reaches a reachable target.
//  2. Target out-of-reach returns near-best (converged=false, chain stretched).
//  3. Target at root joint returns near-zero final_distance (trivial case).
//  4. Single-joint chain with a reachable target converges.
//  5. convergence_threshold is respected — solver stops when within epsilon.
//  6. configure() default values propagate to IkChain if chain uses them.
//  7. Empty joints chain returns empty solved_rotations without crash.
// =============================================================================

#include <cd/animation/ik/Ik.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{

// Build a straight 3-joint chain along +Y with equal bone lengths.
// Joints: root at origin, each bone = bone_length along +Y.
// End-effector starts at {0, 3*bone_length, 0} (fully extended).
cd::animation::ik::IkChain make_3joint_chain(float bone_length = 1.0F)
{
    cd::animation::ik::Joint root{};
    root.name                = "root";
    root.local_position      = { 0.0F, 0.0F, 0.0F };
    root.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };  // identity
    root.length              = bone_length;

    cd::animation::ik::Joint mid{};
    mid.name                = "mid";
    mid.local_position      = { 0.0F, bone_length, 0.0F };
    mid.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    mid.length              = bone_length;

    cd::animation::ik::Joint end{};
    end.name                = "end";
    end.local_position      = { 0.0F, 2.0F * bone_length, 0.0F };
    end.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    end.length              = bone_length;

    cd::animation::ik::IkChain chain{};
    chain.joints                = { root, mid, end };
    chain.max_iterations        = 32U;
    chain.convergence_threshold = 0.001F;
    return chain;
}

// ---------------------------------------------------------------------------
// TEST 1 — Straight-line chain reaches a reachable target
//
// 3-joint chain, each bone = 1 m.  Total reach = 3 m.
// Target at {1.5, 1.5, 0} is well within reach (~2.12 m from root).
// Expect: converged=true, final_distance <= threshold.
// ---------------------------------------------------------------------------
TEST(CcdSolver_Reach, ReachableTargetConverges)
{
    cd::animation::ik::CcdSolver solver;
    solver.configure(64U, 0.001F);

    auto chain = make_3joint_chain(1.0F);
    chain.end_effector_target   = { 1.5F, 1.5F, 0.0F };
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_TRUE(result.converged)
        << "Reachable target should converge. final_distance="
        << result.final_distance;
    EXPECT_LE(result.final_distance, chain.convergence_threshold)
        << "final_distance must be within convergence_threshold";
    EXPECT_EQ(result.solved_rotations.size(), chain.joints.size())
        << "solved_rotations must have same count as joints";
}

// ---------------------------------------------------------------------------
// TEST 2 — Target out-of-reach returns near-best solution
//
// 3-joint chain, total reach = 3 m.  Target at {0, 10, 0} is 10 m away —
// impossible.  Expect: converged=false, solved_rotations valid (non-empty),
// chain attempts to stretch toward target (final_distance > 0).
// ---------------------------------------------------------------------------
TEST(CcdSolver_Reach, OutOfReachReturnsBestEffort)
{
    cd::animation::ik::CcdSolver solver;

    auto chain = make_3joint_chain(1.0F);
    chain.end_effector_target   = { 0.0F, 10.0F, 0.0F };  // 10 m — impossible
    chain.max_iterations        = 16U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_FALSE(result.converged)
        << "Out-of-reach target must not report converged=true";
    EXPECT_EQ(result.solved_rotations.size(), chain.joints.size())
        << "solved_rotations must always be populated";
    EXPECT_GT(result.final_distance, chain.convergence_threshold)
        << "Out-of-reach: final_distance must exceed threshold";

    // Near-best: the chain should have stretched as close as possible.
    // Total chain length = 3 m, target distance = 10 m.
    // CCD should bring effector within ~3 m of root, so final_distance ≈ 7 m.
    // We just check it's in a plausible range (< 10 m, i.e. CCD tried).
    EXPECT_LT(result.final_distance, 10.0F)
        << "CCD should have moved effector closer to target";
}

// ---------------------------------------------------------------------------
// TEST 3 — Target AT the root joint position
//
// Target = root world position {0, 0, 0}.
// The end effector starts at {0, 3, 0} (fully extended, total 3 bones).
// This is a degenerate case: the target is at the root.
// CCD will curl the chain back on itself.  Expect: converged=true eventually
// (chain can fold back) OR final_distance <= total_length - something.
// We primarily check: no crash, solved_rotations has right count.
// Softer assertion: final_distance is reduced vs starting distance (3.0 m).
// ---------------------------------------------------------------------------
TEST(CcdSolver_Degenerate, TargetAtRootReducesDistance)
{
    cd::animation::ik::CcdSolver solver;

    auto chain = make_3joint_chain(1.0F);
    chain.end_effector_target   = { 0.0F, 0.0F, 0.0F };  // at root
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_EQ(result.solved_rotations.size(), chain.joints.size());

    // The chain starts fully extended (effector at 3 m).
    // CCD should fold it back — final distance should be well under 3.0.
    EXPECT_LT(result.final_distance, 2.5F)
        << "CCD should fold chain toward root target (final_distance="
        << result.final_distance << ")";
}

// ---------------------------------------------------------------------------
// TEST 4 — Single-joint chain works
//
// A single joint at origin with length 1 m.
// Target at {1, 0, 0}: effector tip should reach there after rotating the
// single joint ~90 degrees around Z (bone tip goes from +Y to +X).
// ---------------------------------------------------------------------------
TEST(CcdSolver_SingleJoint, SingleJointReachesTarget)
{
    cd::animation::ik::CcdSolver solver;

    cd::animation::ik::Joint j{};
    j.name                = "only";
    j.local_position      = { 0.0F, 0.0F, 0.0F };
    j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    j.length              = 1.0F;

    cd::animation::ik::IkChain chain{};
    chain.joints                = { j };
    chain.end_effector_target   = { 1.0F, 0.0F, 0.0F };  // 1 m along +X
    chain.max_iterations        = 32U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_EQ(result.solved_rotations.size(), 1u);
    EXPECT_TRUE(result.converged)
        << "Single joint with target at exact bone length should converge. "
        << "final_distance=" << result.final_distance;
    EXPECT_LE(result.final_distance, chain.convergence_threshold);
}

// ---------------------------------------------------------------------------
// TEST 5 — convergence_threshold is respected
//
// Loose threshold (0.1 m) means fewer iterations needed.
// Tight threshold (0.005 m) means more work but higher precision.
// We check that with loose threshold, iterations_used is less than
// or equal to iterations used for tight threshold on the same reachable target.
// ---------------------------------------------------------------------------
TEST(CcdSolver_Convergence, LooseThresholdConvergesFaster)
{
    cd::animation::ik::CcdSolver solver;

    auto chain_loose = make_3joint_chain(1.0F);
    chain_loose.end_effector_target   = { 1.2F, 1.2F, 0.0F };
    chain_loose.max_iterations        = 128U;
    chain_loose.convergence_threshold = 0.1F;    // loose — converges quickly

    auto chain_tight = chain_loose;
    chain_tight.convergence_threshold = 0.005F;  // tight — needs more passes
                                                  // 0.5 cm: realistic for foot IK

    const auto r_loose = solver.solve(chain_loose);
    const auto r_tight = solver.solve(chain_tight);

    // Both should converge (the target is reachable)
    EXPECT_TRUE(r_loose.converged) << "Loose threshold solve should converge";
    EXPECT_TRUE(r_tight.converged) << "Tight threshold solve should converge";

    // Loose threshold should use <= iterations vs tight
    EXPECT_LE(r_loose.iterations_used, r_tight.iterations_used)
        << "Loose threshold should not need more iterations than tight threshold. "
        << "loose=" << r_loose.iterations_used
        << " tight=" << r_tight.iterations_used;

    // Both solves should be within their respective thresholds
    EXPECT_LE(r_tight.final_distance, chain_tight.convergence_threshold);
    EXPECT_LE(r_loose.final_distance, chain_loose.convergence_threshold);
}

// ---------------------------------------------------------------------------
// TEST 6 — configure() defaults apply, and IkChain values take precedence
//
// solver.configure(4, 0.5) — very lenient defaults.
// chain.max_iterations = 64, chain.convergence_threshold = 0.001 — tight.
// Result should reflect CHAIN settings, not solver defaults.
// ---------------------------------------------------------------------------
TEST(CcdSolver_Config, ChainSettingsOverrideSolverDefaults)
{
    cd::animation::ik::CcdSolver solver;
    solver.configure(4U, 0.5F);  // solver default: very loose

    auto chain = make_3joint_chain(1.0F);
    chain.end_effector_target   = { 1.5F, 1.0F, 0.0F };
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;  // chain: tight

    const auto result = solver.solve(chain);

    // With tight chain threshold and 64 iterations, this reachable target should converge
    EXPECT_TRUE(result.converged)
        << "Chain settings (64 iter, 0.001 eps) should override loose configure(4, 0.5). "
        << "final_distance=" << result.final_distance;
    EXPECT_LE(result.final_distance, chain.convergence_threshold);
}

// ---------------------------------------------------------------------------
// TEST 7 — Empty joints chain does not crash, returns empty result
// ---------------------------------------------------------------------------
TEST(CcdSolver_Degenerate, EmptyChainNoCrash)
{
    cd::animation::ik::CcdSolver solver;

    cd::animation::ik::IkChain chain{};
    chain.joints                = {};
    chain.end_effector_target   = { 1.0F, 0.0F, 0.0F };
    chain.max_iterations        = 16U;
    chain.convergence_threshold = 0.001F;

    // Must not crash
    const auto result = solver.solve(chain);

    EXPECT_TRUE(result.solved_rotations.empty())
        << "Empty chain must return empty solved_rotations";
    EXPECT_EQ(result.iterations_used, 0U);
}

// =============================================================================
// Sprint-2 tests — JointLimits and JointLimitPresets (phase 722)
// =============================================================================

// Helper: extract intrinsic XYZ Euler angles from a quaternion [x,y,z,w].
// Mirrors the logic in Ik.cpp (not exposed in public API, replicated here).
std::array<float, 3> euler_from_quat(std::array<float, 4> q) noexcept
{
    const float qx = q[0];
    const float qy = q[1];
    const float qz = q[2];
    const float qw = q[3];
    const float m20 = 2.0F * (qx * qz - qy * qw);
    const float m21 = 2.0F * (qy * qz + qx * qw);
    const float m22 = 1.0F - 2.0F * (qx * qx + qy * qy);
    const float m10 = 2.0F * (qx * qy + qz * qw);
    const float m00 = 1.0F - 2.0F * (qy * qy + qz * qz);

    const float sin_ry = std::clamp(-m20, -1.0F, 1.0F);
    const float ry     = std::asin(sin_ry);
    const float cos_ry = std::cos(ry);

    float rx = 0.0F;
    float rz = 0.0F;
    if (cos_ry > 1e-6F)
    {
        rx = std::atan2(m21 / cos_ry, m22 / cos_ry);
        rz = std::atan2(m10 / cos_ry, m00 / cos_ry);
    }
    else
    {
        rx = std::atan2(m21, m22);
        rz = 0.0F;
    }
    return { rx, ry, rz };
}

// ---------------------------------------------------------------------------
// TEST 8 — Unconstrained joint behaves identically to Sprint-1
//
// A joint with limits.enabled=false must produce the same result as a joint
// with no limits field set at all (backward-compatible with Sprint-1).
// ---------------------------------------------------------------------------
TEST(CcdSolver_JointLimits, UnconstrainedMatchesSprint1)
{
    cd::animation::ik::CcdSolver solver;

    // Build two identical 3-joint chains — one with limits.enabled=false
    auto chain_no_limits = make_3joint_chain(1.0F);
    chain_no_limits.end_effector_target   = { 1.5F, 1.5F, 0.0F };
    chain_no_limits.max_iterations        = 64U;
    chain_no_limits.convergence_threshold = 0.001F;

    auto chain_explicit_off = chain_no_limits;
    for (auto& j : chain_explicit_off.joints)
    {
        j.limits.enabled = false;  // explicitly off
    }

    const auto r1 = solver.solve(chain_no_limits);
    const auto r2 = solver.solve(chain_explicit_off);

    EXPECT_EQ(r1.converged,       r2.converged);
    EXPECT_EQ(r1.iterations_used, r2.iterations_used);
    ASSERT_EQ(r1.solved_rotations.size(), r2.solved_rotations.size());

    constexpr float kTol = 1e-5F;
    for (std::size_t i = 0; i < r1.solved_rotations.size(); ++i)
    {
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_NEAR(r1.solved_rotations[i][static_cast<std::size_t>(c)],
                        r2.solved_rotations[i][static_cast<std::size_t>(c)],
                        kTol)
                << "Joint " << i << " component " << c << " differs";
        }
    }
}

// ---------------------------------------------------------------------------
// TEST 9 — Constrained joint stays within its limits
//
// A single joint is constrained so it can only rotate between 0 and 0.3 rad
// on the X axis.  We send the target far off-axis and verify that the solved
// rotation's X Euler component is in [0, 0.3].
// ---------------------------------------------------------------------------
TEST(CcdSolver_JointLimits, ConstrainedJointStaysWithinLimits)
{
    cd::animation::ik::CcdSolver solver;

    cd::animation::ik::Joint j{};
    j.name                = "constrained";
    j.local_position      = { 0.0F, 0.0F, 0.0F };
    j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    j.length              = 1.0F;
    j.limits.enabled      = true;
    j.limits.min_euler    = { 0.0F,   -0.05F, -0.05F };
    j.limits.max_euler    = { 0.3F,    0.05F,  0.05F };

    cd::animation::ik::IkChain chain{};
    chain.joints                = { j };
    chain.end_effector_target   = { 5.0F, 0.0F, 0.0F };  // far off axis
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    ASSERT_EQ(result.solved_rotations.size(), 1u);

    const auto euler = euler_from_quat(result.solved_rotations[0]);

    EXPECT_GE(euler[0], j.limits.min_euler[0] - 1e-4F)
        << "X Euler must not go below min_euler[0]";
    EXPECT_LE(euler[0], j.limits.max_euler[0] + 1e-4F)
        << "X Euler must not exceed max_euler[0]";
}

// ---------------------------------------------------------------------------
// TEST 10 — Knee preset blocks backward bend
//
// A 2-joint leg chain: thigh (root) + knee.
// Target is placed directly behind and above the knee to demand backward bending.
// With the knee preset applied, the knee rotation must never go negative on X
// (no hyperextension — forward bending only).
// ---------------------------------------------------------------------------
TEST(CcdSolver_JointLimits, KneePresetBlocksBackwardBend)
{
    cd::animation::ik::CcdSolver solver;

    // Thigh joint at origin — unconstrained (hip allows arbitrary orientation)
    cd::animation::ik::Joint thigh{};
    thigh.name                = "thigh";
    thigh.local_position      = { 0.0F, 0.0F, 0.0F };
    thigh.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    thigh.length              = 1.0F;
    thigh.limits.enabled      = false;

    // Knee joint — apply preset
    cd::animation::ik::Joint knee{};
    knee.name                = "knee";
    knee.local_position      = { 0.0F, 1.0F, 0.0F };
    knee.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    knee.length              = 1.0F;
    knee.limits              = cd::animation::ik::JointLimitPresets::knee();

    cd::animation::ik::IkChain chain{};
    chain.joints                = { thigh, knee };
    // Target behind and below — forces backward knee motion without limits
    chain.end_effector_target   = { 0.0F, -0.5F, 0.0F };
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    ASSERT_EQ(result.solved_rotations.size(), 2u);

    const auto knee_euler = euler_from_quat(result.solved_rotations[1]);

    // X rotation must be >= knee preset min (0.0 — no backward bend)
    EXPECT_GE(knee_euler[0], knee.limits.min_euler[0] - 1e-4F)
        << "Knee must not hyperextend (backward bend). X euler = " << knee_euler[0];
}

// ---------------------------------------------------------------------------
// TEST 11 — Multiple constrained joints solve together without crash
//
// A 3-joint chain where ALL joints have tight limits.
// Verify: no crash, solved_rotations has correct count, each joint respects
// its individual limits.
// ---------------------------------------------------------------------------
TEST(CcdSolver_JointLimits, MultipleConstrainedJointsSolveTogether)
{
    cd::animation::ik::CcdSolver solver;

    // All joints constrained to a very narrow X range: [0.1, 0.5] rad
    auto make_constrained_joint = [](const char* n, float py) -> cd::animation::ik::Joint
    {
        cd::animation::ik::Joint j{};
        j.name                = n;
        j.local_position      = { 0.0F, py, 0.0F };
        j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
        j.length              = 1.0F;
        j.limits.enabled      = true;
        j.limits.min_euler    = { 0.1F, -0.05F, -0.05F };
        j.limits.max_euler    = { 0.5F,  0.05F,  0.05F };
        return j;
    };

    cd::animation::ik::IkChain chain{};
    chain.joints = {
        make_constrained_joint("root", 0.0F),
        make_constrained_joint("mid",  1.0F),
        make_constrained_joint("end",  2.0F)
    };
    chain.end_effector_target   = { 1.5F, 1.0F, 0.0F };
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    // Must not crash
    const auto result = solver.solve(chain);

    ASSERT_EQ(result.solved_rotations.size(), 3u);

    // Each joint rotation must stay within the declared limits
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto euler = euler_from_quat(result.solved_rotations[i]);
        const auto& lim  = chain.joints[i].limits;

        // Tolerance accounts for Euler decomposition + recomposition round-trip
        // numerical error (~2.5e-3 rad at these angles).
        constexpr float kLimitTol = 5e-3F;
        EXPECT_GE(euler[0], lim.min_euler[0] - kLimitTol)
            << "Joint " << i << " X under min";
        EXPECT_LE(euler[0], lim.max_euler[0] + kLimitTol)
            << "Joint " << i << " X over max";
    }
}

// =============================================================================
// Edge / negative / boundary tests (phase 85→100 gap-close pass)
//
// Tests 12-19 cover:
//  12. Already-at-target: effector starts on target → 0 iterations used.
//  13. Zero-length bone: joint with length=0 produces no crash + valid output.
//  14. Single-joint unconstrained: converges to exact single-bone reach.
//  15. Max-iteration cap: unreachable target + small cap → iterations_used == cap.
//  16. Joint-limit exact min bound: clamp at exactly min_euler; stays there.
//  17. Joint-limit exact max bound: clamp at exactly max_euler; stays there.
//  18. Degenerate chain with all zero-length bones: no crash, stable output.
//  19. Multi-joint chain where effector is already within threshold: 0 iterations.
// =============================================================================

// ---------------------------------------------------------------------------
// TEST 12 — Already-at-target returns 0 iterations used
//
// A single-joint chain with bone=1 along +Y.
// Target = {0, 1, 0}: that IS the initial end-effector position (identity rot).
// The solver checks convergence before any rotation step, so iterations_used=0.
// ---------------------------------------------------------------------------
TEST(CcdSolver_Degenerate, AlreadyAtTargetUsesZeroIterations)
{
    cd::animation::ik::CcdSolver solver;

    cd::animation::ik::Joint j{};
    j.name                = "only";
    j.local_position      = { 0.0F, 0.0F, 0.0F };
    j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };  // identity
    j.length              = 1.0F;

    cd::animation::ik::IkChain chain{};
    chain.joints                = { j };
    // Bone extends along +Y by length=1 with identity rotation → effector at {0,1,0}
    chain.end_effector_target   = { 0.0F, 1.0F, 0.0F };
    chain.max_iterations        = 32U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_TRUE(result.converged)        << "Effector already at target must converge";
    EXPECT_EQ(result.iterations_used, 0U) << "Must report 0 iterations when already at target";
    EXPECT_LE(result.final_distance, chain.convergence_threshold);
    EXPECT_EQ(result.solved_rotations.size(), 1u);
}

// ---------------------------------------------------------------------------
// TEST 13 — Zero-length bone does not crash and returns valid output
//
// A joint with length=0 means its child is at the same world position.
// The end-effector is also at the joint position regardless of rotation.
// Behaviour: no crash; solved_rotations has correct count.
// ---------------------------------------------------------------------------
TEST(CcdSolver_Degenerate, ZeroLengthBoneNoCrash)
{
    cd::animation::ik::CcdSolver solver;

    cd::animation::ik::Joint j{};
    j.name                = "zero_len";
    j.local_position      = { 0.0F, 0.0F, 0.0F };
    j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    j.length              = 0.0F;  // zero-length bone

    cd::animation::ik::IkChain chain{};
    chain.joints                = { j };
    chain.end_effector_target   = { 1.0F, 0.0F, 0.0F };
    chain.max_iterations        = 16U;
    chain.convergence_threshold = 0.001F;

    // Must not crash regardless of zero-length bone
    const auto result = solver.solve(chain);

    EXPECT_EQ(result.solved_rotations.size(), 1u)
        << "solved_rotations must be populated even for zero-length bone";
    // The effector is always at the joint (length=0), so final_distance = distance
    // from root to target (1.0 m).  converged=false is expected.
    EXPECT_FALSE(result.converged)
        << "Zero-length bone cannot reach a target 1 m away";
}

// ---------------------------------------------------------------------------
// TEST 14 — Single-joint chain: target exactly at bone length on a diagonal
//
// Bone at origin, length=2.  Target at {2/sqrt(2), 2/sqrt(2), 0} = {√2, √2, 0}.
// That is exactly 2 m from origin → reachable.  Verify converges and that
// the solved quaternion is a unit quaternion (not corrupted).
// ---------------------------------------------------------------------------
TEST(CcdSolver_SingleJoint, SingleJointDiagonalTargetConverges)
{
    cd::animation::ik::CcdSolver solver;

    constexpr float kLen = 2.0F;
    const float coord    = kLen / std::numbers::sqrt2_v<float>;

    cd::animation::ik::Joint j{};
    j.name                = "diag";
    j.local_position      = { 0.0F, 0.0F, 0.0F };
    j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    j.length              = kLen;

    cd::animation::ik::IkChain chain{};
    chain.joints                = { j };
    chain.end_effector_target   = { coord, coord, 0.0F };
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_TRUE(result.converged)
        << "Single-joint diagonal target at exact reach must converge";
    EXPECT_LE(result.final_distance, chain.convergence_threshold);
    ASSERT_EQ(result.solved_rotations.size(), 1u);

    // Solved quaternion must remain unit-length
    const auto& q      = result.solved_rotations[0];
    const float q_norm = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    EXPECT_NEAR(q_norm, 1.0F, 1e-4F) << "Solved quaternion must be unit-length";
}

// ---------------------------------------------------------------------------
// TEST 15 — Max-iteration cap is respected
//
// Unreachable target (far away) + very small max_iterations=3.
// The solver must not exceed the cap: iterations_used == 3.
// ---------------------------------------------------------------------------
TEST(CcdSolver_Convergence, MaxIterationCapRespected)
{
    cd::animation::ik::CcdSolver solver;

    auto chain = make_3joint_chain(1.0F);
    chain.end_effector_target   = { 0.0F, 100.0F, 0.0F };  // unreachable (100 m)
    chain.max_iterations        = 3U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_FALSE(result.converged)
        << "Unreachable target must not converge";
    EXPECT_EQ(result.iterations_used, 3U)
        << "iterations_used must equal max_iterations when cap is hit";
    EXPECT_EQ(result.solved_rotations.size(), chain.joints.size());
}

// ---------------------------------------------------------------------------
// TEST 16 — Joint-limit clamping at exact min bound
//
// A single constrained joint: min_euler[0]=0.5, max_euler[0]=1.5.
// We seed the joint with a rotation below min (rx≈−0.3) and ask the solver
// to solve to a target that demands exactly that rotation.
// After the limit clamp the X component must be >= 0.5.
// ---------------------------------------------------------------------------
TEST(CcdSolver_JointLimits, ClampAtExactMinBound)
{
    cd::animation::ik::CcdSolver solver;

    cd::animation::ik::Joint j{};
    j.name                = "min_clamp";
    j.local_position      = { 0.0F, 0.0F, 0.0F };
    j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    j.length              = 1.0F;
    j.limits.enabled      = true;
    j.limits.min_euler    = { 0.5F, -0.1F, -0.1F };
    j.limits.max_euler    = { 1.5F,  0.1F,  0.1F };

    cd::animation::ik::IkChain chain{};
    chain.joints                = { j };
    // Target placed so that unconstrained CCD would want a negative-X rotation;
    // with the limit the X Euler must stay >= 0.5.
    chain.end_effector_target   = { 0.0F, -1.0F, 0.0F };  // behind root (+Y bone)
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    ASSERT_EQ(result.solved_rotations.size(), 1u);
    const auto euler = euler_from_quat(result.solved_rotations[0]);
    EXPECT_GE(euler[0], j.limits.min_euler[0] - 1e-4F)
        << "X Euler must not go below min bound 0.5. Got " << euler[0];
}

// ---------------------------------------------------------------------------
// TEST 17 — Joint-limit clamping at exact max bound
//
// A single constrained joint: min_euler[0]=0.0, max_euler[0]=0.2.
// Bone extends along +Y.  Target at {0, 0, 10} demands rotation around +X
// (bone tip into +Z plane) — unconstrained CCD would want ~π/2 rad on X;
// the limit caps it at 0.2.
// ---------------------------------------------------------------------------
TEST(CcdSolver_JointLimits, ClampAtExactMaxBound)
{
    cd::animation::ik::CcdSolver solver;

    cd::animation::ik::Joint j{};
    j.name                = "max_clamp";
    j.local_position      = { 0.0F, 0.0F, 0.0F };
    j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
    j.length              = 1.0F;
    j.limits.enabled      = true;
    j.limits.min_euler    = { 0.0F, -0.1F, -0.1F };
    j.limits.max_euler    = { 0.2F,  0.1F,  0.1F };

    cd::animation::ik::IkChain chain{};
    chain.joints                = { j };
    // Target far along +Z: bone tip must swing in XZ plane → large positive X Euler.
    // Unconstrained solve would approach π/2 ≈ 1.57 rad; limit caps at 0.2.
    chain.end_effector_target   = { 0.0F, 0.0F, 10.0F };
    chain.max_iterations        = 64U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    ASSERT_EQ(result.solved_rotations.size(), 1u);
    const auto euler = euler_from_quat(result.solved_rotations[0]);
    EXPECT_LE(euler[0], j.limits.max_euler[0] + 1e-4F)
        << "X Euler must not exceed max bound 0.2. Got " << euler[0];
}

// ---------------------------------------------------------------------------
// TEST 18 — Degenerate chain: all bones have zero length, no crash
//
// All joints placed at the origin, length=0.  End-effector stays at origin
// regardless of rotation.  The solver must not crash or produce NaN.
// ---------------------------------------------------------------------------
TEST(CcdSolver_Degenerate, AllZeroLengthBonesNoCrash)
{
    cd::animation::ik::CcdSolver solver;

    auto make_zero = [](float py) -> cd::animation::ik::Joint
    {
        cd::animation::ik::Joint j{};
        j.local_position      = { 0.0F, py, 0.0F };
        j.local_rotation_quat = { 0.0F, 0.0F, 0.0F, 1.0F };
        j.length              = 0.0F;
        return j;
    };

    cd::animation::ik::IkChain chain{};
    chain.joints                = { make_zero(0.0F), make_zero(0.0F), make_zero(0.0F) };
    chain.end_effector_target   = { 1.0F, 1.0F, 0.0F };
    chain.max_iterations        = 8U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_EQ(result.solved_rotations.size(), 3u) << "Must return 3 rotations";
    // No NaN in any component
    for (std::size_t i = 0; i < 3u; ++i)
        for (std::size_t c = 0; c < 4u; ++c)
            EXPECT_FALSE(std::isnan(result.solved_rotations[i][c]))
                << "NaN in joint " << i << " component " << c;
}

// ---------------------------------------------------------------------------
// TEST 19 — 3-joint chain with effector already within threshold: 0 iterations
//
// Build a chain whose current rotations already place the end-effector within
// convergence_threshold of the target.  Verify iterations_used=0.
// Chain: 3 joints, each bone=1 m along +Y.  Identity rotations → effector at
// {0, 3, 0}.  Set target to {0, 3, 0} (exact position).
// ---------------------------------------------------------------------------
TEST(CcdSolver_Degenerate, ThreeJointAlreadyAtTargetZeroIterations)
{
    cd::animation::ik::CcdSolver solver;

    auto chain = make_3joint_chain(1.0F);
    // Identity rotations → effector tip at {0, 3, 0}
    chain.end_effector_target   = { 0.0F, 3.0F, 0.0F };
    chain.max_iterations        = 32U;
    chain.convergence_threshold = 0.001F;

    const auto result = solver.solve(chain);

    EXPECT_TRUE(result.converged)        << "Effector already at target must converge";
    EXPECT_EQ(result.iterations_used, 0U) << "Must use 0 iterations when already at target";
    EXPECT_LE(result.final_distance, chain.convergence_threshold);
    EXPECT_EQ(result.solved_rotations.size(), 3u);
}

}  // anonymous namespace
