// =============================================================================
// CHROMODYNAMIC - tests/test_anim_graph.cpp
// Phase 481 - cd::game::anim_graph unit tests.
//
// Covers the 8 contract requirements from the G2.3 brief:
//   1. PlayClip returns clip pose at a given time.
//   2. Blend1D at param=0 returns input A; at param=1 returns input B.
//   3. Blend1D at param=0.5 returns the mid LERP.
//   4. Blend2D 4-corner mix at the rectangle's center equals the average.
//   5. StateMachine transitions when its predicate fires.
//   6. Param accessor round-trips read / write through the blackboard.
//   7. Missing root (or null node) returns bind-pose without crashing.
//   8. Time scrubbing forward AND backward through PlayClipNode.
// + Extra: GC-safety (a node going out of scope while the graph holds
//   the OTHER root must not crash on subsequent ticks).
// =============================================================================
#include <cd/anim/Animation.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/game/anim_graph/AnimGraph.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace
{

using cd::anim::Joint;
using cd::anim::Keyframe;
using cd::anim::Pose;
using cd::anim::Skeleton;
using cd::anim::SkinnedClip;

using cd::game::anim_graph::AnimGraph;
using cd::game::anim_graph::AnimNode;
using cd::game::anim_graph::Blackboard;
using cd::game::anim_graph::Blend1DNode;
using cd::game::anim_graph::Blend2DNode;
using cd::game::anim_graph::PlayClipNode;
using cd::game::anim_graph::StateMachineNode;

constexpr float kEps = 1e-4F;

[[nodiscard]] bool eq(float a, float b) noexcept
{
    return std::fabs(a - b) < kEps;
}

// One-joint skeleton: the simplest pose carrier (one Transform).
[[nodiscard]] Skeleton make_one_joint_skel()
{
    std::vector<Joint> joints;
    Joint j;
    j.name = "root";
    j.parent = -1;
    joints.push_back(j);
    return Skeleton { std::move(joints) };
}

[[nodiscard]] cd::math::Transformf at_x(float x)
{
    cd::math::Transformf t;
    t.position = { x, 0.0F, 0.0F };
    return t;
}

// Linear clip on joint 0: x = lo at t=0, x = hi at t=1 (one second).
[[nodiscard]] SkinnedClip make_linear_clip(float lo, float hi)
{
    SkinnedClip c(/*joint_count=*/1);
    c.set_track(0, std::vector<Keyframe> {
        Keyframe { 0.0F, at_x(lo) },
        Keyframe { 1.0F, at_x(hi) },
    });
    return c;
}

// -----------------------------------------------------------------------------
// 1) PlayClip returns the clip pose at a given time.
// -----------------------------------------------------------------------------
TEST(AnimGraphPlayClip, ReturnsClipPoseAtGivenTime)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 10.0F);

    PlayClipNode node { &clip, /*loop=*/false, /*speed=*/1.0F };
    Blackboard bb;

    const auto p0   = node.tick(0.0F,  bb, skel);
    const auto p_mid = node.tick(0.5F, bb, skel);
    const auto p1   = node.tick(1.0F,  bb, skel);

    ASSERT_EQ(p0.joint_locals.size(),   1U);
    ASSERT_EQ(p_mid.joint_locals.size(), 1U);
    ASSERT_EQ(p1.joint_locals.size(),   1U);

    EXPECT_TRUE(eq(p0.joint_locals[0].position.x,   0.0F));
    EXPECT_TRUE(eq(p_mid.joint_locals[0].position.x, 5.0F));
    EXPECT_TRUE(eq(p1.joint_locals[0].position.x,  10.0F));
}

// -----------------------------------------------------------------------------
// 2 + 3) Blend1D: param=0 -> A, param=1 -> B, param=0.5 -> mid LERP.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend1D, EndpointsAndMidpoint)
{
    const auto skel = make_one_joint_skel();
    const auto clip_a = make_linear_clip(0.0F, 0.0F);   // constant 0
    const auto clip_b = make_linear_clip(10.0F, 10.0F); // constant 10

    Blend1DNode blend { "speed" };
    ASSERT_TRUE(blend.add_input(0.0F, std::make_unique<PlayClipNode>(&clip_a, false)));
    ASSERT_TRUE(blend.add_input(1.0F, std::make_unique<PlayClipNode>(&clip_b, false)));

    Blackboard bb;

    bb.set("speed", 0.0F);
    auto pa = blend.tick(0.0F, bb, skel);
    EXPECT_TRUE(eq(pa.joint_locals[0].position.x, 0.0F));

    bb.set("speed", 1.0F);
    auto pb = blend.tick(0.0F, bb, skel);
    EXPECT_TRUE(eq(pb.joint_locals[0].position.x, 10.0F));

    bb.set("speed", 0.5F);
    auto pm = blend.tick(0.0F, bb, skel);
    EXPECT_TRUE(eq(pm.joint_locals[0].position.x, 5.0F));
}

// -----------------------------------------------------------------------------
// 4) Blend2D 4-corner mix at the rectangle's center equals the corner
// average (bilinear blend collapses to (P00+P10+P01+P11)/4 at u=v=0.5).
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend2D, CenterEqualsCornerAverage)
{
    const auto skel = make_one_joint_skel();
    // Four constant clips with x = 1, 3, 5, 7. Average = 4.
    const auto c00 = make_linear_clip(1.0F, 1.0F);
    const auto c10 = make_linear_clip(3.0F, 3.0F);
    const auto c01 = make_linear_clip(5.0F, 5.0F);
    const auto c11 = make_linear_clip(7.0F, 7.0F);

    Blend2DNode blend { "x", "y" };
    blend.set_bounds(0.0F, 1.0F, 0.0F, 1.0F);
    blend.set_corner(0, std::make_unique<PlayClipNode>(&c00, false));
    blend.set_corner(1, std::make_unique<PlayClipNode>(&c10, false));
    blend.set_corner(2, std::make_unique<PlayClipNode>(&c01, false));
    blend.set_corner(3, std::make_unique<PlayClipNode>(&c11, false));

    Blackboard bb;
    bb.set("x", 0.5F);
    bb.set("y", 0.5F);

    auto p = blend.tick(0.0F, bb, skel);
    ASSERT_EQ(p.joint_locals.size(), 1U);
    EXPECT_TRUE(eq(p.joint_locals[0].position.x, 4.0F));

    // Verify the corners themselves while we're here.
    bb.set("x", 0.0F); bb.set("y", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 1.0F));
    bb.set("x", 1.0F); bb.set("y", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 3.0F));
    bb.set("x", 0.0F); bb.set("y", 1.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 5.0F));
    bb.set("x", 1.0F); bb.set("y", 1.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 7.0F));
}

// -----------------------------------------------------------------------------
// 5) StateMachine transitions on predicate.
// -----------------------------------------------------------------------------
TEST(AnimGraphStateMachine, TransitionsOnPredicate)
{
    const auto skel = make_one_joint_skel();
    const auto clip_idle = make_linear_clip(0.0F, 0.0F);    // constant 0
    const auto clip_walk = make_linear_clip(100.0F, 100.0F); // constant 100

    auto fsm = std::make_unique<StateMachineNode>();
    const auto idle = fsm->add_state("idle", std::make_unique<PlayClipNode>(&clip_idle, true));
    const auto walk = fsm->add_state("walk", std::make_unique<PlayClipNode>(&clip_walk, true));
    ASSERT_EQ(fsm->state_count(), 2U);
    EXPECT_EQ(fsm->current_state_index(), idle);
    // No blend duration -> hard switch on the next tick.
    ASSERT_TRUE(fsm->add_transition(idle, walk,
        [](const Blackboard& bb, float /*elapsed*/) {
            return bb.get("speed", 0.0F) > 0.1F;
        },
        /*blend_duration=*/0.0F));

    Blackboard bb;
    // First tick: still in idle, speed=0.
    auto p0 = fsm->tick(0.0F, bb, skel);
    EXPECT_TRUE(eq(p0.joint_locals[0].position.x, 0.0F));
    EXPECT_EQ(fsm->current_state_index(), idle);

    // Raise speed and tick again -> transition fires -> walk pose.
    bb.set("speed", 1.0F);
    auto p1 = fsm->tick(0.1F, bb, skel);
    EXPECT_EQ(fsm->current_state_index(), walk);
    EXPECT_TRUE(eq(p1.joint_locals[0].position.x, 100.0F));
}

// -----------------------------------------------------------------------------
// 6) Param accessor read / write through the AnimGraph blackboard.
// -----------------------------------------------------------------------------
TEST(AnimGraphParams, AccessorRoundTrips)
{
    AnimGraph g;
    EXPECT_FALSE(g.has_param("speed"));
    EXPECT_TRUE(eq(g.get_param("speed", -1.0F), -1.0F));

    g.set_param("speed", 2.5F);
    EXPECT_TRUE(g.has_param("speed"));
    EXPECT_TRUE(eq(g.get_param("speed"), 2.5F));

    g.set_param("speed", 3.0F);
    EXPECT_TRUE(eq(g.get_param("speed"), 3.0F));

    g.clear_params();
    EXPECT_FALSE(g.has_param("speed"));

    // Direct blackboard handle still works after clear.
    g.blackboard().set("aim", 0.75F);
    EXPECT_TRUE(eq(g.blackboard().get("aim"), 0.75F));
}

// -----------------------------------------------------------------------------
// 7) Missing node returns the default (bind) pose without crashing.
// -----------------------------------------------------------------------------
TEST(AnimGraphDefaults, MissingNodeReturnsBindPose)
{
    const auto skel = make_one_joint_skel();

    // (a) Empty AnimGraph (no root assigned).
    AnimGraph g;
    auto p_empty = g.evaluate(skel, 0.0F);
    ASSERT_EQ(p_empty.joint_locals.size(), 1U);
    EXPECT_TRUE(eq(p_empty.joint_locals[0].position.x, 0.0F));

    // (b) PlayClipNode with a null clip.
    PlayClipNode null_clip { nullptr };
    Blackboard bb;
    auto p_null = null_clip.tick(0.5F, bb, skel);
    ASSERT_EQ(p_null.joint_locals.size(), 1U);
    EXPECT_TRUE(eq(p_null.joint_locals[0].position.x, 0.0F));

    // (c) Empty Blend1DNode and Blend2DNode.
    Blend1DNode b1d { "speed" };
    auto p_b1d = b1d.tick(0.0F, bb, skel);
    ASSERT_EQ(p_b1d.joint_locals.size(), 1U);
    EXPECT_TRUE(eq(p_b1d.joint_locals[0].position.x, 0.0F));

    Blend2DNode b2d { "x", "y" };  // all four corners null
    auto p_b2d = b2d.tick(0.0F, bb, skel);
    ASSERT_EQ(p_b2d.joint_locals.size(), 1U);
    EXPECT_TRUE(eq(p_b2d.joint_locals[0].position.x, 0.0F));

    // (d) StateMachineNode with no states.
    StateMachineNode empty_fsm;
    auto p_fsm = empty_fsm.tick(0.0F, bb, skel);
    ASSERT_EQ(p_fsm.joint_locals.size(), 1U);
}

// -----------------------------------------------------------------------------
// 8) Time scrubbing forward AND backward through PlayClipNode.
// -----------------------------------------------------------------------------
TEST(AnimGraphTimeScrub, ForwardAndBackThroughClip)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 10.0F);
    PlayClipNode node { &clip, /*loop=*/false, /*speed=*/1.0F };
    Blackboard bb;

    // Forward scrub.
    EXPECT_TRUE(eq(node.tick(0.0F, bb, skel).joint_locals[0].position.x,  0.0F));
    EXPECT_TRUE(eq(node.tick(0.25F, bb, skel).joint_locals[0].position.x, 2.5F));
    EXPECT_TRUE(eq(node.tick(0.75F, bb, skel).joint_locals[0].position.x, 7.5F));
    EXPECT_TRUE(eq(node.tick(1.0F, bb, skel).joint_locals[0].position.x, 10.0F));

    // Reverse scrub (clip clamps at the boundaries since loop=false; the
    // intermediate values must reproduce the forward samples).
    EXPECT_TRUE(eq(node.tick(0.75F, bb, skel).joint_locals[0].position.x, 7.5F));
    EXPECT_TRUE(eq(node.tick(0.5F, bb, skel).joint_locals[0].position.x,  5.0F));
    EXPECT_TRUE(eq(node.tick(0.0F, bb, skel).joint_locals[0].position.x,  0.0F));

    // Out-of-range scrubs clamp to the clip endpoints.
    EXPECT_TRUE(eq(node.tick(-5.0F, bb, skel).joint_locals[0].position.x,  0.0F));
    EXPECT_TRUE(eq(node.tick(99.0F, bb, skel).joint_locals[0].position.x, 10.0F));

    // Loop=true mode must wrap negative times into [0, dur).
    PlayClipNode looped { &clip, /*loop=*/true, /*speed=*/1.0F };
    // t = -0.25 -> wrapped to 0.75 -> sample at x = 7.5.
    EXPECT_TRUE(eq(looped.tick(-0.25F, bb, skel).joint_locals[0].position.x, 7.5F));
}

// -----------------------------------------------------------------------------
// 9 (extra) - GC: replacing the root after the previous one's tail
// dependency went out of scope must not crash. Demonstrates that
// ownership of child nodes flows through std::unique_ptr correctly and
// that nothing holds a dangling raw pointer to a freed AnimNode.
// -----------------------------------------------------------------------------
TEST(AnimGraphGc, RemovedNodeDoesNotCrash)
{
    const auto skel = make_one_joint_skel();

    AnimGraph g;
    {
        // Build a Blend1DNode that owns two PlayClipNodes.
        const SkinnedClip clip_a = make_linear_clip(0.0F, 0.0F);
        const SkinnedClip clip_b = make_linear_clip(8.0F, 8.0F);
        auto blend = std::make_unique<Blend1DNode>("speed");
        ASSERT_TRUE(blend->add_input(0.0F, std::make_unique<PlayClipNode>(&clip_a, true)));
        ASSERT_TRUE(blend->add_input(1.0F, std::make_unique<PlayClipNode>(&clip_b, true)));
        g.set_root(std::move(blend));
        g.set_param("speed", 0.0F);
        auto p = g.evaluate(skel, 0.0F);
        EXPECT_TRUE(eq(p.joint_locals[0].position.x, 0.0F));

        // Now replace the root with a fresh node before the scope ends.
        // The blend (and the two clip nodes it owned) is freed here.
        const SkinnedClip clip_c = make_linear_clip(42.0F, 42.0F);
        g.set_root(std::make_unique<PlayClipNode>(&clip_c, true));
        auto p2 = g.evaluate(skel, 0.0F);
        EXPECT_TRUE(eq(p2.joint_locals[0].position.x, 42.0F));

        // Drop the root entirely - graph still ticks (returns bind-pose).
        g.set_root(nullptr);
        auto p3 = g.evaluate(skel, 0.0F);
        EXPECT_EQ(p3.joint_locals.size(), skel.joint_count());
    }

    // Outside the scope - re-assign a new node and tick again. None of
    // the previously-freed nodes are referenced by the graph anymore.
    const SkinnedClip clip_d = make_linear_clip(9.0F, 9.0F);
    g.set_root(std::make_unique<PlayClipNode>(&clip_d, true));
    auto p4 = g.evaluate(skel, 0.0F);
    EXPECT_TRUE(eq(p4.joint_locals[0].position.x, 9.0F));
}

}  // namespace
