// =============================================================================
// CHROMODYNAMIC — cd::anim edge / negative tests
// ≥80→100 marathon (docs/ROADMAP_80_TO_100.md) — ADD-ONLY depth coverage.
//
// These tests exercise boundary + degenerate paths that the original
// happy-path suites (test_anim.cpp / test_dual_quat.cpp) did not assert:
//   * DualQuat — normalize/identity/all-zero-weight/antipodal/endpoint/empty
//   * StateMachine — guard-false, no-transition, self-loop, multi-state order
//   * BlendTree2 — clamp out-of-bounds, center, antipodal NLERP shorter-arc
//   * PoseBlend — weight clamp, pose-count mismatch (prefix-only), filter
//   * Skeleton — bind-pose, hierarchy chain, empty, find-missing, single-root
//   * GpuSkinning — host data-prep matrix-palette correctness + cap behaviour
//
// NONE of these change pose/skinning math; they only assert existing
// behaviour, so golden output stays byte-identical.
// =============================================================================
#include <cd/anim/AdditiveBlend.hpp>
#include <cd/anim/BlendTree2.hpp>
#include <cd/anim/DualQuat.hpp>
#include <cd/anim/GpuSkinning.hpp>
#include <cd/anim/PoseBlend.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/anim/StateMachine.hpp>

#include <cd/math/Constants.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

constexpr float kEps = 1e-4F;

// ===========================================================================
// DualQuat — normalize / identity / degenerate blends
// ===========================================================================

TEST(DualQuatEdge, IdentityIsRigidIdentityMatrix)
{
    const cd::anim::DualQuatf id = cd::anim::DualQuatf::identity();
    const cd::math::Mat4f m = cd::anim::to_mat4(id);
    const cd::math::Mat4f ref = cd::math::Mat4f::identity();
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(m[c][r], ref[c][r], kEps) << "col=" << c << " row=" << r;
}

TEST(DualQuatEdge, IdentityRealPartIsUnit)
{
    const cd::anim::DualQuatf id = cd::anim::DualQuatf::identity();
    EXPECT_FLOAT_EQ(id.real.w, 1.0F);
    EXPECT_FLOAT_EQ(id.real.x, 0.0F);
    EXPECT_FLOAT_EQ(id.dual.w, 0.0F);
}

TEST(DualQuatEdge, BlendAllZeroWeightsReturnsIdentity)
{
    const cd::math::Quatf rot = cd::math::Quatf::from_axis_angle(
        { 0.0F, 1.0F, 0.0F }, cd::math::pi_v<float> * 0.4F);
    const cd::anim::DualQuatf dq = cd::anim::from_rigid(rot, { 9.0F, 0.0F, 0.0F });

    const std::array<float, 2>               w { 0.0F, 0.0F };
    const std::array<cd::anim::DualQuatf, 2> dqs { dq, dq };

    const cd::anim::DualQuatf b = cd::anim::blend(
        std::span<const float> { w.data(), 2 },
        std::span<const cd::anim::DualQuatf> { dqs.data(), 2 });

    const cd::anim::DualQuatf id = cd::anim::DualQuatf::identity();
    EXPECT_FLOAT_EQ(b.real.w, id.real.w);
    EXPECT_FLOAT_EQ(b.real.x, id.real.x);
    EXPECT_FLOAT_EQ(b.real.y, id.real.y);
    EXPECT_FLOAT_EQ(b.real.z, id.real.z);
}

TEST(DualQuatEdge, BlendEmptySpansReturnsIdentity)
{
    const cd::anim::DualQuatf b = cd::anim::blend(
        std::span<const float> {},
        std::span<const cd::anim::DualQuatf> {});
    EXPECT_FLOAT_EQ(b.real.w, 1.0F);
    EXPECT_FLOAT_EQ(b.real.x, 0.0F);
}

TEST(DualQuatEdge, BlendResultRealPartIsUnitLength)
{
    // After blend the real part is renormalised — must be unit.
    const cd::math::Quatf r0 = cd::math::Quatf::from_axis_angle(
        { 0.0F, 1.0F, 0.0F }, cd::math::pi_v<float> * 0.2F);
    const cd::math::Quatf r1 = cd::math::Quatf::from_axis_angle(
        { 0.0F, 1.0F, 0.0F }, cd::math::pi_v<float> * 0.6F);
    const cd::anim::DualQuatf dq0 = cd::anim::from_rigid(r0, { 1.0F, 0.0F, 0.0F });
    const cd::anim::DualQuatf dq1 = cd::anim::from_rigid(r1, { 0.0F, 1.0F, 0.0F });

    const std::array<float, 2>               w { 0.3F, 0.7F };
    const std::array<cd::anim::DualQuatf, 2> dqs { dq0, dq1 };
    const cd::anim::DualQuatf b = cd::anim::blend(
        std::span<const float> { w.data(), 2 },
        std::span<const cd::anim::DualQuatf> { dqs.data(), 2 });

    const float len = std::sqrt(b.real.x * b.real.x + b.real.y * b.real.y
                                + b.real.z * b.real.z + b.real.w * b.real.w);
    EXPECT_NEAR(len, 1.0F, kEps);
}

TEST(DualQuatEdge, BlendSingleNonZeroWeightAmongZerosPicksThatDQ)
{
    // Only the middle entry carries weight; result must reproduce dq1.
    const cd::math::Quatf r1 = cd::math::Quatf::from_axis_angle(
        { 1.0F, 0.0F, 0.0F }, cd::math::pi_v<float> * 0.5F);
    const cd::anim::DualQuatf dq1 = cd::anim::from_rigid(r1, { 2.0F, 3.0F, 4.0F });
    const cd::anim::DualQuatf id = cd::anim::DualQuatf::identity();

    const std::array<float, 3>               w { 0.0F, 1.0F, 0.0F };
    const std::array<cd::anim::DualQuatf, 3> dqs { id, dq1, id };
    const cd::anim::DualQuatf b = cd::anim::blend(
        std::span<const float> { w.data(), 3 },
        std::span<const cd::anim::DualQuatf> { dqs.data(), 3 });

    const cd::math::Mat4f got = cd::anim::to_mat4(b);
    const cd::math::Mat4f exp = cd::anim::to_mat4(dq1);
    EXPECT_NEAR(got[3][0], exp[3][0], kEps);
    EXPECT_NEAR(got[3][1], exp[3][1], kEps);
    EXPECT_NEAR(got[3][2], exp[3][2], kEps);
}

TEST(DualQuatEdge, FromRigidPureTranslationKeepsIdentityRotation)
{
    const cd::anim::DualQuatf dq = cd::anim::from_rigid(
        cd::math::Quatf::identity(), { 4.0F, -5.0F, 6.0F });
    const cd::math::Mat4f m = cd::anim::to_mat4(dq);
    // Rotation block stays identity, translation column carries the offset.
    EXPECT_NEAR(m[0][0], 1.0F, kEps);
    EXPECT_NEAR(m[1][1], 1.0F, kEps);
    EXPECT_NEAR(m[2][2], 1.0F, kEps);
    EXPECT_NEAR(m[3][0], 4.0F, kEps);
    EXPECT_NEAR(m[3][1], -5.0F, kEps);
    EXPECT_NEAR(m[3][2], 6.0F, kEps);
}

TEST(DualQuatEdge, BlendIdenticalDQsAtAnyWeightSplitIsThatDQ)
{
    const cd::math::Quatf rot = cd::math::Quatf::from_axis_angle(
        { 0.0F, 0.0F, 1.0F }, cd::math::pi_v<float> * 0.33F);
    const cd::anim::DualQuatf dq = cd::anim::from_rigid(rot, { 1.0F, 2.0F, 0.0F });

    const std::array<float, 2>               w { 0.25F, 0.75F };
    const std::array<cd::anim::DualQuatf, 2> dqs { dq, dq };
    const cd::anim::DualQuatf b = cd::anim::blend(
        std::span<const float> { w.data(), 2 },
        std::span<const cd::anim::DualQuatf> { dqs.data(), 2 });

    const cd::math::Mat4f got = cd::anim::to_mat4(b);
    const cd::math::Mat4f exp = cd::anim::to_mat4(dq);
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(got[c][r], exp[c][r], kEps) << "col=" << c << " row=" << r;
}

// ===========================================================================
// StateMachine — guards / self-loop / no-transition / ordering
// ===========================================================================

TEST(StateMachineEdge, GuardFalseKeepsCurrentState)
{
    cd::anim::AnimStateMachine sm;
    sm.add_state({ "idle", nullptr, true, 1.0F, 0.0F });
    sm.add_state({ "walk", nullptr, true, 1.0F, 0.0F });
    sm.add_transition({ "idle", "walk",
        [](const std::unordered_map<std::string, float>& bb, float) {
            const auto it = bb.find("speed");
            return it != bb.end() && it->second > 1.0F;
        },
        0.1F });
    sm.set_initial_state("idle");
    cd::anim::Skeleton skel;

    sm.set("speed", 0.5F);  // below threshold — guard must stay false
    sm.tick(0.016F, skel);
    EXPECT_EQ(sm.current_state(), "idle");
    EXPECT_FALSE(sm.is_blending());
}

TEST(StateMachineEdge, NoTransitionRegisteredNeverLeavesState)
{
    cd::anim::AnimStateMachine sm;
    sm.add_state({ "only", nullptr, true, 1.0F, 0.0F });
    sm.set_initial_state("only");
    cd::anim::Skeleton skel;
    for (int i = 0; i < 10; ++i) sm.tick(0.1F, skel);
    EXPECT_EQ(sm.current_state(), "only");
    EXPECT_FALSE(sm.is_blending());
}

TEST(StateMachineEdge, SelfLoopTransitionBlendsBackToSameState)
{
    cd::anim::AnimStateMachine sm;
    sm.add_state({ "loop", nullptr, true, 1.0F, 0.0F });
    sm.add_transition({ "loop", "loop",
        [](const std::unordered_map<std::string, float>& bb, float) {
            const auto it = bb.find("retrigger");
            return it != bb.end() && it->second > 0.5F;
        },
        0.05F });
    sm.set_initial_state("loop");
    cd::anim::Skeleton skel;

    sm.set("retrigger", 1.0F);
    sm.tick(0.016F, skel);
    EXPECT_EQ(sm.current_state(), "loop");
    EXPECT_TRUE(sm.is_blending());
}

TEST(StateMachineEdge, AddDuplicateStateRejected)
{
    cd::anim::AnimStateMachine sm;
    EXPECT_TRUE(sm.add_state({ "a", nullptr, true, 1.0F, 0.0F }));
    EXPECT_FALSE(sm.add_state({ "a", nullptr, true, 1.0F, 0.0F }));
}

TEST(StateMachineEdge, AddTransitionToUnknownStateRejected)
{
    cd::anim::AnimStateMachine sm;
    sm.add_state({ "a", nullptr, true, 1.0F, 0.0F });
    // 'b' never registered.
    EXPECT_FALSE(sm.add_transition({ "a", "b",
        [](const std::unordered_map<std::string, float>&, float) { return true; },
        0.1F }));
}

TEST(StateMachineEdge, SetInitialUnknownStateRejected)
{
    cd::anim::AnimStateMachine sm;
    sm.add_state({ "a", nullptr, true, 1.0F, 0.0F });
    EXPECT_FALSE(sm.set_initial_state("ghost"));
}

TEST(StateMachineEdge, FirstMatchingTransitionWins)
{
    cd::anim::AnimStateMachine sm;
    sm.add_state({ "src", nullptr, true, 1.0F, 0.0F });
    sm.add_state({ "first", nullptr, true, 1.0F, 0.0F });
    sm.add_state({ "second", nullptr, true, 1.0F, 0.0F });
    // Both predicates are true; the earlier-registered one must fire.
    sm.add_transition({ "src", "first",
        [](const std::unordered_map<std::string, float>&, float) { return true; },
        0.1F });
    sm.add_transition({ "src", "second",
        [](const std::unordered_map<std::string, float>&, float) { return true; },
        0.1F });
    sm.set_initial_state("src");
    cd::anim::Skeleton skel;
    sm.tick(0.016F, skel);
    EXPECT_EQ(sm.current_state(), "first");
}

TEST(StateMachineEdge, NullConditionTransitionNeverFires)
{
    cd::anim::AnimStateMachine sm;
    sm.add_state({ "x", nullptr, true, 1.0F, 0.0F });
    sm.add_state({ "y", nullptr, true, 1.0F, 0.0F });
    // condition default-constructed (empty std::function) — must be skipped.
    sm.add_transition({ "x", "y", {}, 0.1F });
    sm.set_initial_state("x");
    cd::anim::Skeleton skel;
    sm.tick(0.016F, skel);
    EXPECT_EQ(sm.current_state(), "x");
}

// ===========================================================================
// BlendTree2 — clamp / center / antipodal NLERP / mismatch
// ===========================================================================

TEST(BlendTree2Edge, WeightAboveOneClampsToB)
{
    std::array<cd::math::Transformf, 1> a {};
    std::array<cd::math::Transformf, 1> b {};
    std::array<cd::math::Transformf, 1> out {};
    a[0].position = { 0.0F, 0.0F, 0.0F };
    b[0].position = { 10.0F, 0.0F, 0.0F };
    ASSERT_TRUE(cd::anim::blend2(a, b, 5.0F, out));  // > 1 clamps to 1
    EXPECT_NEAR(out[0].position.x, 10.0F, kEps);
}

TEST(BlendTree2Edge, WeightBelowZeroClampsToA)
{
    std::array<cd::math::Transformf, 1> a {};
    std::array<cd::math::Transformf, 1> b {};
    std::array<cd::math::Transformf, 1> out {};
    a[0].position = { 3.0F, 0.0F, 0.0F };
    b[0].position = { 10.0F, 0.0F, 0.0F };
    ASSERT_TRUE(cd::anim::blend2(a, b, -2.0F, out));  // < 0 clamps to 0
    EXPECT_NEAR(out[0].position.x, 3.0F, kEps);
}

TEST(BlendTree2Edge, OutSpanSizeMismatchFails)
{
    std::array<cd::math::Transformf, 2> a {};
    std::array<cd::math::Transformf, 2> b {};
    std::array<cd::math::Transformf, 1> out {};  // wrong size
    EXPECT_FALSE(cd::anim::blend2(a, b, 0.5F, out));
}

TEST(BlendTree2Edge, NlerpShorterArcOnAntipodalQuaternions)
{
    // a = identity, b = -identity (same rotation, opposite hemisphere).
    // NLERP must pick the shorter arc so the half-blend stays unit and
    // close to identity, not collapse to zero.
    const cd::math::Quatf a { 0.0F, 0.0F, 0.0F, 1.0F };
    const cd::math::Quatf b { 0.0F, 0.0F, 0.0F, -1.0F };
    const cd::math::Quatf m = cd::anim::nlerp_quat(a, b, 0.5F);
    const float len = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z + m.w * m.w);
    EXPECT_NEAR(len, 1.0F, kEps);
    EXPECT_NEAR(std::fabs(m.w), 1.0F, kEps);
}

TEST(BlendTree2Edge, NlerpEndpointsAreExact)
{
    const cd::math::Quatf a = cd::math::Quatf::from_axis_angle(
        { 0.0F, 1.0F, 0.0F }, cd::math::pi_v<float> * 0.25F);
    const cd::math::Quatf b = cd::math::Quatf::from_axis_angle(
        { 1.0F, 0.0F, 0.0F }, cd::math::pi_v<float> * 0.25F);
    const cd::math::Quatf at0 = cd::anim::nlerp_quat(a, b, 0.0F);
    EXPECT_NEAR(at0.x, a.x, kEps);
    EXPECT_NEAR(at0.w, a.w, kEps);
}

TEST(BlendTree2Edge, CenterBlendsScaleAndPosition)
{
    std::array<cd::math::Transformf, 1> a {};
    std::array<cd::math::Transformf, 1> b {};
    std::array<cd::math::Transformf, 1> out {};
    a[0].position = { 0.0F, 0.0F, 0.0F };
    a[0].scale = { 2.0F, 2.0F, 2.0F };
    b[0].position = { 4.0F, 0.0F, 0.0F };
    b[0].scale = { 4.0F, 4.0F, 4.0F };
    ASSERT_TRUE(cd::anim::blend2(a, b, 0.5F, out));
    EXPECT_NEAR(out[0].position.x, 2.0F, kEps);
    EXPECT_NEAR(out[0].scale.x, 3.0F, kEps);
}

// ===========================================================================
// PoseBlend — weight clamp / pose-count mismatch / additive prefix
// ===========================================================================

TEST(PoseBlendEdge, WeightAboveOneClampsToB)
{
    cd::anim::Pose a;
    cd::anim::Pose b;
    a.joint_locals.resize(1);
    b.joint_locals.resize(1);
    a.joint_locals[0].position = { 1.0F, 0.0F, 0.0F };
    b.joint_locals[0].position = { 9.0F, 0.0F, 0.0F };
    cd::anim::Pose out;
    cd::anim::blend_pose(a, b, 3.0F, out);  // clamps to 1 → b
    EXPECT_FLOAT_EQ(out.joint_locals[0].position.x, 9.0F);
}

TEST(PoseBlendEdge, WeightBelowZeroClampsToA)
{
    cd::anim::Pose a;
    cd::anim::Pose b;
    a.joint_locals.resize(1);
    b.joint_locals.resize(1);
    a.joint_locals[0].position = { 1.0F, 0.0F, 0.0F };
    b.joint_locals[0].position = { 9.0F, 0.0F, 0.0F };
    cd::anim::Pose out;
    cd::anim::blend_pose(a, b, -3.0F, out);  // clamps to 0 → a
    EXPECT_FLOAT_EQ(out.joint_locals[0].position.x, 1.0F);
}

TEST(PoseBlendEdge, MismatchedPoseSizesBlendsCommonPrefixOnly)
{
    cd::anim::Pose a;  // 3 joints
    cd::anim::Pose b;  // 2 joints
    a.joint_locals.resize(3);
    b.joint_locals.resize(2);
    a.joint_locals[0].position = { 0.0F, 0.0F, 0.0F };
    a.joint_locals[1].position = { 0.0F, 0.0F, 0.0F };
    a.joint_locals[2].position = { 7.0F, 0.0F, 0.0F };
    b.joint_locals[0].position = { 10.0F, 0.0F, 0.0F };
    b.joint_locals[1].position = { 10.0F, 0.0F, 0.0F };
    cd::anim::Pose out;
    cd::anim::blend_pose(a, b, 1.0F, out);
    // out is sized to min(3,2)=2; the 3rd joint is NOT present in output.
    ASSERT_EQ(out.joint_locals.size(), 2u);
    EXPECT_FLOAT_EQ(out.joint_locals[0].position.x, 10.0F);
    EXPECT_FLOAT_EQ(out.joint_locals[1].position.x, 10.0F);
}

TEST(PoseBlendEdge, BlendIntoMismatchLeavesExtraTargetJointsUntouched)
{
    cd::anim::Pose target;  // 3 joints
    cd::anim::Pose b;       // 1 joint
    target.joint_locals.resize(3);
    b.joint_locals.resize(1);
    target.joint_locals[0].position = { 1.0F, 0.0F, 0.0F };
    target.joint_locals[2].position = { 5.0F, 0.0F, 0.0F };
    b.joint_locals[0].position = { 0.0F, 0.0F, 0.0F };
    cd::anim::blend_pose_into(target, b, 1.0F);
    EXPECT_FLOAT_EQ(target.joint_locals[0].position.x, 0.0F);  // blended
    EXPECT_FLOAT_EQ(target.joint_locals[2].position.x, 5.0F);  // untouched
}

TEST(PoseBlendEdge, AdditiveApplyMismatchSizesToMinPrefix)
{
    cd::anim::Pose base;      // 2 joints
    cd::anim::Pose additive;  // 1 joint
    base.joint_locals.resize(2);
    additive.joint_locals.resize(1);
    base.joint_locals[0].position = { 1.0F, 0.0F, 0.0F };
    additive.joint_locals[0].position = { 2.0F, 0.0F, 0.0F };
    cd::anim::Pose out;
    cd::anim::additive_apply(base, additive, 1.0F, out);
    ASSERT_EQ(out.joint_locals.size(), 1u);
    EXPECT_FLOAT_EQ(out.joint_locals[0].position.x, 3.0F);
}

TEST(PoseBlendEdge, AdditiveApplyWeightClampsAboveOne)
{
    cd::anim::Pose base;
    cd::anim::Pose additive;
    base.joint_locals.resize(1);
    additive.joint_locals.resize(1);
    base.joint_locals[0].position = { 1.0F, 0.0F, 0.0F };
    additive.joint_locals[0].position = { 2.0F, 0.0F, 0.0F };
    cd::anim::Pose out;
    cd::anim::additive_apply(base, additive, 4.0F, out);  // clamps to 1
    EXPECT_FLOAT_EQ(out.joint_locals[0].position.x, 3.0F);
}

// ===========================================================================
// Skeleton — bind / hierarchy / empty / single-root
// ===========================================================================

TEST(SkeletonEdge, EmptySkeletonHasZeroJoints)
{
    cd::anim::Skeleton s;
    EXPECT_EQ(s.joint_count(), 0u);
    EXPECT_EQ(s.find("anything"), -1);
    EXPECT_TRUE(s.topological_order().empty());
}

TEST(SkeletonEdge, BindPoseOfEmptySkeletonIsEmpty)
{
    cd::anim::Skeleton s;
    const auto p = cd::anim::Pose::bind_pose(s);
    EXPECT_TRUE(p.joint_locals.empty());
}

TEST(SkeletonEdge, SingleRootInverseBindIsIdentity)
{
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;  // identity local_bind
    cd::anim::Skeleton s { { root } };
    EXPECT_EQ(s.joint_count(), 1u);
    const auto& ib = s.joint(0).inverse_bind_matrix;
    const auto id = cd::math::Mat4f::identity();
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(ib[c][r], id[c][r], 1e-5F);
}

TEST(SkeletonEdge, BindPoseMatchesLocalBindTransforms)
{
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    cd::anim::Joint child;
    child.name = "child";
    child.parent = 0;
    child.local_bind.position = { 2.0F, 0.0F, 0.0F };
    cd::anim::Skeleton s { { root, child } };
    const auto p = cd::anim::Pose::bind_pose(s);
    ASSERT_EQ(p.joint_locals.size(), 2u);
    EXPECT_FLOAT_EQ(p.joint_locals[1].position.x, 2.0F);
}

TEST(SkeletonEdge, ThreeBoneChainInverseBindWalksHierarchy)
{
    // root@0 -> mid@(+1) -> tip@(+1) ; tip world bind = (2,0,0).
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    cd::anim::Joint mid;
    mid.name = "mid";
    mid.parent = 0;
    mid.local_bind.position = { 1.0F, 0.0F, 0.0F };
    cd::anim::Joint tip;
    tip.name = "tip";
    tip.parent = 1;
    tip.local_bind.position = { 1.0F, 0.0F, 0.0F };
    cd::anim::Skeleton s { { root, mid, tip } };

    const auto& tip_ib = s.joint(2).inverse_bind_matrix;
    const cd::math::Vec4f probe { 2.0F, 0.0F, 0.0F, 1.0F };  // tip world bind
    const cd::math::Vec4f r = tip_ib * probe;
    EXPECT_NEAR(r.x, 0.0F, 1e-5F);
    EXPECT_NEAR(r.y, 0.0F, 1e-5F);
    EXPECT_NEAR(r.z, 0.0F, 1e-5F);
    EXPECT_NEAR(r.w, 1.0F, 1e-5F);
}

TEST(SkeletonEdge, TopologicalOrderIsIdentityIndexing)
{
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    cd::anim::Joint child;
    child.name = "child";
    child.parent = 0;
    cd::anim::Skeleton s { { root, child } };
    const auto order = s.topological_order();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
}

TEST(SkeletonEdge, ComputeSkinningMatricesEmptySkeletonProducesEmpty)
{
    cd::anim::Skeleton s;
    cd::anim::Pose p;
    std::vector<cd::math::Mat4f> mats;
    cd::anim::compute_skinning_matrices(s, p, mats);
    EXPECT_TRUE(mats.empty());
}

TEST(SkeletonEdge, ComputeSkinningTipRotationAccumulatesAlongChain)
{
    // root rotated 90° about Z; child at (1,0,0) local. At pose==bind the
    // skinning matrix is identity, so this asserts the bind-pose invariant
    // for a rotated bind (math unchanged — pure assertion).
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    root.local_bind.rotation = cd::math::Quatf::from_axis_angle(
        { 0.0F, 0.0F, 1.0F }, cd::math::pi_v<float> * 0.5F);
    cd::anim::Joint child;
    child.name = "child";
    child.parent = 0;
    child.local_bind.position = { 1.0F, 0.0F, 0.0F };
    cd::anim::Skeleton s { { root, child } };
    auto pose = cd::anim::Pose::bind_pose(s);
    std::vector<cd::math::Mat4f> mats;
    cd::anim::compute_skinning_matrices(s, pose, mats);
    ASSERT_EQ(mats.size(), 2u);
    const auto id = cd::math::Mat4f::identity();
    for (const auto& m : mats)
        for (std::size_t c = 0; c < 4; ++c)
            for (std::size_t r = 0; r < 4; ++r)
                EXPECT_NEAR(m[c][r], id[c][r], 1e-5F);
}

// ===========================================================================
// GpuSkinning — host data-prep matrix-palette correctness
// ===========================================================================

TEST(GpuSkinningEdge, PackLeavesUnusedSlotsAtIdentity)
{
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    cd::anim::Skeleton s { { root } };  // 1 joint
    auto pose = cd::anim::Pose::bind_pose(s);
    cd::anim::SkinningMatricesUbo ubo {};
    const auto n = cd::anim::pack_skinning_matrices(s, pose, ubo);
    ASSERT_EQ(n, 1u);
    // Slot 0 used; slots 1..kMaxBones-1 must be identity.
    EXPECT_FLOAT_EQ(ubo.bones[5][0][0], 1.0F);
    EXPECT_FLOAT_EQ(ubo.bones[5][1][1], 1.0F);
    EXPECT_FLOAT_EQ(ubo.bones[5][0][1], 0.0F);
    EXPECT_FLOAT_EQ(ubo.bones[cd::anim::kMaxBones - 1][2][2], 1.0F);
}

TEST(GpuSkinningEdge, PackTranslatedRootGivesNonIdentityMatrix)
{
    // A non-bind pose: shift the root joint local by +X, so the skinning
    // matrix becomes world(+X) * inverse_bind(identity) = translation(+X).
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    cd::anim::Skeleton s { { root } };
    auto pose = cd::anim::Pose::bind_pose(s);
    pose.joint_locals[0].position = { 3.0F, 0.0F, 0.0F };  // animate away from bind
    cd::anim::SkinningMatricesUbo ubo {};
    const auto n = cd::anim::pack_skinning_matrices(s, pose, ubo);
    ASSERT_EQ(n, 1u);
    // Translation column (column 3, row 0) should hold +3.
    EXPECT_NEAR(ubo.bones[0][3][0], 3.0F, 1e-5F);
}

TEST(GpuSkinningEdge, PackCapsAtMaxBones)
{
    // Build a flat fan of kMaxBones+8 root joints; pack must cap at kMaxBones.
    const std::size_t over = static_cast<std::size_t>(cd::anim::kMaxBones) + 8;
    std::vector<cd::anim::Joint> joints(over);
    for (std::size_t i = 0; i < over; ++i)
    {
        joints[i].name = "j" + std::to_string(i);
        joints[i].parent = -1;
    }
    cd::anim::Skeleton s { std::move(joints) };
    auto pose = cd::anim::Pose::bind_pose(s);
    cd::anim::SkinningMatricesUbo ubo {};
    const auto n = cd::anim::pack_skinning_matrices(s, pose, ubo);
    EXPECT_EQ(n, cd::anim::kMaxBones);
}

TEST(GpuSkinningEdge, PackMatchesComputeSkinningMatricesElementwise)
{
    // Host data-prep correctness: pack_skinning_matrices must equal
    // compute_skinning_matrices for the in-range joints.
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    cd::anim::Joint child;
    child.name = "child";
    child.parent = 0;
    child.local_bind.position = { 1.0F, 0.0F, 0.0F };
    cd::anim::Skeleton s { { root, child } };
    auto pose = cd::anim::Pose::bind_pose(s);
    pose.joint_locals[1].position = { 1.0F, 2.0F, 0.0F };  // animate child

    std::vector<cd::math::Mat4f> reference;
    cd::anim::compute_skinning_matrices(s, pose, reference);

    cd::anim::SkinningMatricesUbo ubo {};
    const auto n = cd::anim::pack_skinning_matrices(s, pose, ubo);
    ASSERT_EQ(n, 2u);
    for (std::size_t j = 0; j < 2; ++j)
        for (std::size_t c = 0; c < 4; ++c)
            for (std::size_t r = 0; r < 4; ++r)
                EXPECT_NEAR(ubo.bones[j][c][r], reference[j][c][r], 1e-6F)
                    << "joint=" << j << " col=" << c << " row=" << r;
}

}  // namespace
