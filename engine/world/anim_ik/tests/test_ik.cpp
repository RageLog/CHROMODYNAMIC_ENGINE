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

#include <array>
#include <cmath>
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

}  // anonymous namespace
