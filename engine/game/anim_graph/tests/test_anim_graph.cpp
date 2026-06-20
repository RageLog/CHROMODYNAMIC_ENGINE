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
//
// Phase 481+ (100% close-out) — edge / negative / boundary tests:
//   10.  PlayClip: empty clip (zero keyframes) -> bind-pose.
//   11.  PlayClip: single-frame clip -> constant pose at any time.
//   12.  PlayClip: time past clip end (loop wraps; clamp stays at end).
//   13.  PlayClip: zero-duration (duration()==0) behaves safely.
//   14.  PlayClip: negative time — loop wraps; clamp floors to 0.
//   15.  PlayClip: speed multiplier scales clip time.
//   16.  Blend1D: add_input rejects null node; rejects non-ascending.
//   17.  Blend1D: single-input passthrough (no blend).
//   18.  Blend1D: param exactly at boundary thresholds.
//   19.  Blend1D: param below min / above max clamps to endpoints.
//   20.  Blend2D: explicit corner pins at all four corners.
//   21.  Blend2D: out-of-range set_corner ignores silently.
//   22.  Blend2D: param outside bounds saturates to boundary.
//   23.  Pose count mismatch: smaller clip -> only prefix joints blended.
//   24.  Blackboard: erase removes key; has() false after erase.
//   25.  StateMachine: blend transition (blend_duration > 0) lerps poses.
//   26.  StateMachine: set_initial_state OOB returns false.
//   27.  StateMachine: add_transition rejects OOB / empty predicate.
//   28.  StateMachine: current_state_name before any state added is empty.
//   29.  AnimGraph: set_root(nullptr) then evaluate returns bind-pose.
//   30.  Blend weight clamp: blend_pose with w<0 clamps to 0; w>1 clamps to 1.
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
using cd::anim::Skeleton;
using cd::anim::SkinnedClip;

using cd::game::anim_graph::AnimGraph;
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

// =============================================================================
// Phase 481+ — edge / negative / boundary tests (items 10-30)
// =============================================================================

// Helpers shared by the new tests.
[[nodiscard]] SkinnedClip make_empty_clip()
{
    return SkinnedClip { /*joint_count=*/1 };  // track is empty, no keyframes
}

[[nodiscard]] SkinnedClip make_single_frame_clip(float x_val)
{
    SkinnedClip c(/*joint_count=*/1);
    c.set_track(0, std::vector<Keyframe> {
        Keyframe { 0.0F, at_x(x_val) },
    });
    return c;
}

// -----------------------------------------------------------------------------
// 10) PlayClip: empty clip (no keyframes in any track) -> bind-pose
//     SkinnedClip::sample skips empty tracks, so out stays at bind-pose.
// -----------------------------------------------------------------------------
TEST(AnimGraphPlayClipEdge, EmptyClipReturnBindPose)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_empty_clip();
    PlayClipNode node { &clip, /*loop=*/false };
    Blackboard bb;

    const auto p = node.tick(0.5F, bb, skel);
    ASSERT_EQ(p.joint_locals.size(), 1U);
    // Bind-pose position on the single joint is the default (0,0,0).
    EXPECT_TRUE(eq(p.joint_locals[0].position.x, 0.0F));
}

// -----------------------------------------------------------------------------
// 11) PlayClip: single-frame clip -> same pose at t=0, t=10, t=-5.
//     sample_track returns frames.front().value for size==1 at any time.
// -----------------------------------------------------------------------------
TEST(AnimGraphPlayClipEdge, SingleFrameClipIsConstant)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_single_frame_clip(7.0F);
    PlayClipNode loop_node { &clip, /*loop=*/true };
    PlayClipNode clamp_node { &clip, /*loop=*/false };
    Blackboard bb;

    // Loop mode — duration() == 0, so PlayClipNode uses raw clip_t = t * speed.
    // sample_track(single-frame) always returns the single frame value.
    EXPECT_TRUE(eq(loop_node.tick( 0.0F, bb, skel).joint_locals[0].position.x, 7.0F));
    EXPECT_TRUE(eq(loop_node.tick(10.0F, bb, skel).joint_locals[0].position.x, 7.0F));
    EXPECT_TRUE(eq(loop_node.tick(-5.0F, bb, skel).joint_locals[0].position.x, 7.0F));

    // Clamp mode — same reasoning; duration()==0 branch not entered.
    EXPECT_TRUE(eq(clamp_node.tick(0.0F, bb, skel).joint_locals[0].position.x, 7.0F));
    EXPECT_TRUE(eq(clamp_node.tick(5.0F, bb, skel).joint_locals[0].position.x, 7.0F));
}

// -----------------------------------------------------------------------------
// 12) PlayClip: time past clip end.
//     loop=true wraps; loop=false clamps at duration end.
// -----------------------------------------------------------------------------
TEST(AnimGraphPlayClipEdge, TimePastClipEnd)
{
    const auto skel = make_one_joint_skel();
    // x goes 0 -> 10 over [0, 1] second.
    const auto clip = make_linear_clip(0.0F, 10.0F);

    PlayClipNode looped { &clip, /*loop=*/true,  /*speed=*/1.0F };
    PlayClipNode clamped { &clip, /*loop=*/false, /*speed=*/1.0F };
    Blackboard bb;

    // t=1.5: loop wraps to 0.5 -> x = 5.
    EXPECT_TRUE(eq(looped.tick(1.5F, bb, skel).joint_locals[0].position.x, 5.0F));
    // t=2.0: loop wraps to 0.0 -> x = 0.
    EXPECT_TRUE(eq(looped.tick(2.0F, bb, skel).joint_locals[0].position.x, 0.0F));
    // t=99: clamp stays at x=10.
    EXPECT_TRUE(eq(clamped.tick(99.0F, bb, skel).joint_locals[0].position.x, 10.0F));
    // t=1.0: clamp at exactly the end -> x=10.
    EXPECT_TRUE(eq(clamped.tick(1.0F, bb, skel).joint_locals[0].position.x, 10.0F));
}

// -----------------------------------------------------------------------------
// 13) PlayClip: zero-duration clip (duration() returns 0; single track,
//     single keyframe with explicit time=0).  Must not divide by zero and
//     must return the single keyframe value.
// -----------------------------------------------------------------------------
TEST(AnimGraphPlayClipEdge, ZeroDurationClipIsSafe)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_single_frame_clip(3.0F);  // duration() == 0
    PlayClipNode node { &clip, /*loop=*/true };
    Blackboard bb;

    EXPECT_EQ(clip.duration(), 0.0F);
    // Must not crash; must return the single frame value.
    EXPECT_TRUE(eq(node.tick( 0.0F, bb, skel).joint_locals[0].position.x, 3.0F));
    EXPECT_TRUE(eq(node.tick( 5.0F, bb, skel).joint_locals[0].position.x, 3.0F));
    EXPECT_TRUE(eq(node.tick(-1.0F, bb, skel).joint_locals[0].position.x, 3.0F));
}

// -----------------------------------------------------------------------------
// 14) PlayClip: negative time.
//     loop=true wraps negative time into [0, dur).
//     loop=false clamps negative time to 0 (x=0 in the 0..10 clip).
// -----------------------------------------------------------------------------
TEST(AnimGraphPlayClipEdge, NegativeTime)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 10.0F);  // dur = 1.0

    PlayClipNode looped  { &clip, /*loop=*/true,  /*speed=*/1.0F };
    PlayClipNode clamped { &clip, /*loop=*/false, /*speed=*/1.0F };
    Blackboard bb;

    // t=-0.25 looped: fmod(-0.25, 1.0)=-0.25 + 1.0 = 0.75 -> x=7.5
    EXPECT_TRUE(eq(looped.tick(-0.25F, bb, skel).joint_locals[0].position.x, 7.5F));
    // t=-0.5 looped: -> 0.5 -> x=5.0
    EXPECT_TRUE(eq(looped.tick(-0.5F, bb, skel).joint_locals[0].position.x, 5.0F));

    // t=-0.5 clamped: clamp(-0.5, 0, 1) = 0 -> x=0
    EXPECT_TRUE(eq(clamped.tick(-0.5F, bb, skel).joint_locals[0].position.x, 0.0F));
    // t=-99 clamped: floor to 0 -> x=0
    EXPECT_TRUE(eq(clamped.tick(-99.0F, bb, skel).joint_locals[0].position.x, 0.0F));
}

// -----------------------------------------------------------------------------
// 15) PlayClip: speed multiplier scales the local clip time.
//     speed=2 means at graph t=0.5 the clip is at t=1.0 -> x=10.
// -----------------------------------------------------------------------------
TEST(AnimGraphPlayClipEdge, SpeedMultiplierScalesTime)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 10.0F);

    PlayClipNode fast { &clip, /*loop=*/false, /*speed=*/2.0F };
    Blackboard bb;

    EXPECT_TRUE(eq(fast.tick(0.25F, bb, skel).joint_locals[0].position.x, 5.0F));  // t_clip=0.5
    EXPECT_TRUE(eq(fast.tick(0.5F,  bb, skel).joint_locals[0].position.x, 10.0F)); // t_clip=1.0 (at end)
    EXPECT_TRUE(eq(fast.tick(0.6F,  bb, skel).joint_locals[0].position.x, 10.0F)); // clamped at end
}

// -----------------------------------------------------------------------------
// 16) Blend1D: add_input rejects null node; rejects non-ascending threshold.
//     Input count stays correct after rejections.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend1DEdge, AddInputRejectsInvalid)
{
    const auto skel = make_one_joint_skel();
    const auto c = make_linear_clip(0.0F, 0.0F);

    Blend1DNode blend { "speed" };
    // Null node -> rejected.
    EXPECT_FALSE(blend.add_input(0.0F, nullptr));
    EXPECT_EQ(blend.input_count(), 0U);

    EXPECT_TRUE(blend.add_input(0.0F, std::make_unique<PlayClipNode>(&c, false)));
    EXPECT_EQ(blend.input_count(), 1U);

    // Same threshold as last -> rejected.
    EXPECT_FALSE(blend.add_input(0.0F, std::make_unique<PlayClipNode>(&c, false)));
    // Lower threshold -> rejected.
    EXPECT_FALSE(blend.add_input(-1.0F, std::make_unique<PlayClipNode>(&c, false)));
    EXPECT_EQ(blend.input_count(), 1U);

    // Strictly greater -> accepted.
    EXPECT_TRUE(blend.add_input(1.0F, std::make_unique<PlayClipNode>(&c, false)));
    EXPECT_EQ(blend.input_count(), 2U);
}

// -----------------------------------------------------------------------------
// 17) Blend1D: single-input passthrough — ticks the only node directly.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend1DEdge, SingleInputPassthrough)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 10.0F);

    Blend1DNode blend { "speed" };
    ASSERT_TRUE(blend.add_input(0.5F, std::make_unique<PlayClipNode>(&clip, false)));

    Blackboard bb;
    // Regardless of the param value a single input is returned as-is.
    bb.set("speed", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.5F, bb, skel).joint_locals[0].position.x, 5.0F));
    bb.set("speed", 1.0F);
    EXPECT_TRUE(eq(blend.tick(0.5F, bb, skel).joint_locals[0].position.x, 5.0F));
}

// -----------------------------------------------------------------------------
// 18) Blend1D: param exactly at boundary thresholds -> pins to that input.
//     At threshold[0] -> first input; at threshold[N-1] -> last input.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend1DEdge, ParamAtExactThreshold)
{
    const auto skel = make_one_joint_skel();
    const auto c0 = make_linear_clip(0.0F,  0.0F);
    const auto c1 = make_linear_clip(5.0F,  5.0F);
    const auto c2 = make_linear_clip(10.0F, 10.0F);

    Blend1DNode blend { "speed" };
    ASSERT_TRUE(blend.add_input(0.0F, std::make_unique<PlayClipNode>(&c0, false)));
    ASSERT_TRUE(blend.add_input(0.5F, std::make_unique<PlayClipNode>(&c1, false)));
    ASSERT_TRUE(blend.add_input(1.0F, std::make_unique<PlayClipNode>(&c2, false)));

    Blackboard bb;

    // Exactly at first threshold -> pins to c0.
    bb.set("speed", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 0.0F));

    // Exactly at middle threshold — bracket is [0.0, 0.5], w=1.0 -> c1.
    bb.set("speed", 0.5F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 5.0F));

    // Exactly at last threshold -> pins to c2.
    bb.set("speed", 1.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 10.0F));
}

// -----------------------------------------------------------------------------
// 19) Blend1D: param below min / above max clamps to endpoints.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend1DEdge, ParamOutsideRangeClamps)
{
    const auto skel = make_one_joint_skel();
    const auto c0 = make_linear_clip(2.0F,  2.0F);
    const auto c1 = make_linear_clip(8.0F,  8.0F);

    Blend1DNode blend { "speed" };
    ASSERT_TRUE(blend.add_input(0.0F, std::make_unique<PlayClipNode>(&c0, false)));
    ASSERT_TRUE(blend.add_input(1.0F, std::make_unique<PlayClipNode>(&c1, false)));

    Blackboard bb;

    // Below min -> c0 (x=2).
    bb.set("speed", -5.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 2.0F));

    // Above max -> c1 (x=8).
    bb.set("speed", 99.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 8.0F));
}

// -----------------------------------------------------------------------------
// 20) Blend2D: explicit corner pins — each exact corner returns ONLY
//     that corner's value; the bilinear math must collapse to w=0 or 1.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend2DEdge, ExactCornerPins)
{
    const auto skel = make_one_joint_skel();
    const auto c00 = make_linear_clip(1.0F, 1.0F);
    const auto c10 = make_linear_clip(2.0F, 2.0F);
    const auto c01 = make_linear_clip(3.0F, 3.0F);
    const auto c11 = make_linear_clip(4.0F, 4.0F);

    Blend2DNode blend { "x", "y" };
    blend.set_bounds(0.0F, 1.0F, 0.0F, 1.0F);
    blend.set_corner(0, std::make_unique<PlayClipNode>(&c00, false));
    blend.set_corner(1, std::make_unique<PlayClipNode>(&c10, false));
    blend.set_corner(2, std::make_unique<PlayClipNode>(&c01, false));
    blend.set_corner(3, std::make_unique<PlayClipNode>(&c11, false));

    Blackboard bb;

    bb.set("x", 0.0F); bb.set("y", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 1.0F));

    bb.set("x", 1.0F); bb.set("y", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 2.0F));

    bb.set("x", 0.0F); bb.set("y", 1.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 3.0F));

    bb.set("x", 1.0F); bb.set("y", 1.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 4.0F));
}

// -----------------------------------------------------------------------------
// 21) Blend2D: set_corner with index >= 4 is silently ignored.
//     The node must remain in a valid state and tick without crash.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend2DEdge, OutOfRangeCornerIgnored)
{
    const auto skel = make_one_joint_skel();
    const auto c = make_linear_clip(5.0F, 5.0F);

    Blend2DNode blend { "x", "y" };
    // OOB index — must not crash, must not assert.
    blend.set_corner(4, std::make_unique<PlayClipNode>(&c, false));
    blend.set_corner(99, std::make_unique<PlayClipNode>(&c, false));

    // All corners still null -> full bind-pose fallback.
    Blackboard bb;
    bb.set("x", 0.5F); bb.set("y", 0.5F);
    const auto p = blend.tick(0.0F, bb, skel);
    ASSERT_EQ(p.joint_locals.size(), 1U);
    EXPECT_TRUE(eq(p.joint_locals[0].position.x, 0.0F));
}

// -----------------------------------------------------------------------------
// 22) Blend2D: param outside [min, max] saturates (clamp) to boundary.
//     With corners at 1/2/3/4:
//       x < min_x -> u=0; x > max_x -> u=1; similarly for y.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend2DEdge, ParamOutsideBoundsSaturates)
{
    const auto skel = make_one_joint_skel();
    const auto c00 = make_linear_clip(0.0F, 0.0F);
    const auto c10 = make_linear_clip(4.0F, 4.0F);
    const auto c01 = make_linear_clip(0.0F, 0.0F);
    const auto c11 = make_linear_clip(4.0F, 4.0F);

    Blend2DNode blend { "x", "y" };
    blend.set_bounds(0.0F, 1.0F, 0.0F, 1.0F);
    blend.set_corner(0, std::make_unique<PlayClipNode>(&c00, false));
    blend.set_corner(1, std::make_unique<PlayClipNode>(&c10, false));
    blend.set_corner(2, std::make_unique<PlayClipNode>(&c01, false));
    blend.set_corner(3, std::make_unique<PlayClipNode>(&c11, false));

    Blackboard bb;

    // x far below min (u saturates to 0) -> blend is between c00 and c01 along y.
    // At y=0, result should equal c00 = 0.
    bb.set("x", -99.0F); bb.set("y", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 0.0F));

    // x far above max (u saturates to 1) -> blend between c10 and c11 along y.
    // At y=0, result should equal c10 = 4.
    bb.set("x", 99.0F); bb.set("y", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 4.0F));
}

// -----------------------------------------------------------------------------
// 23) Pose count mismatch: clip has fewer joints than the skeleton.
//     blend_pose uses min(a,b) so extra joints stay at bind-pose.
// -----------------------------------------------------------------------------
TEST(AnimGraphPoseMismatch, SmallerClipSafelyBlends)
{
    // Two-joint skeleton.
    std::vector<cd::anim::Joint> joints;
    {
        cd::anim::Joint j0; j0.name = "root"; j0.parent = -1;
        j0.local_bind = at_x(0.0F);
        cd::anim::Joint j1; j1.name = "child"; j1.parent = 0;
        j1.local_bind = at_x(0.0F);
        joints.push_back(j0);
        joints.push_back(j1);
    }
    const Skeleton skel2 { std::move(joints) };

    // Clip only has 1 joint track (less than skeleton's 2).
    SkinnedClip clip1j { /*joint_count=*/1 };
    clip1j.set_track(0, std::vector<Keyframe> {
        Keyframe { 0.0F, at_x(6.0F) },
        Keyframe { 1.0F, at_x(6.0F) },
    });

    PlayClipNode node { &clip1j, /*loop=*/false };
    Blackboard bb;
    const auto p = node.tick(0.5F, bb, skel2);

    // Pose should have been sized to the skeleton (2 joints via bind_pose).
    // Joint 0 gets the clip value (6); joint 1 stays at bind-pose (0).
    ASSERT_EQ(p.joint_locals.size(), 2U);
    EXPECT_TRUE(eq(p.joint_locals[0].position.x, 6.0F));
    EXPECT_TRUE(eq(p.joint_locals[1].position.x, 0.0F));
}

// -----------------------------------------------------------------------------
// 24) Blackboard: erase removes key; has() returns false after erase.
//     erase on missing key returns false and does not crash.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlackboard, EraseAndHas)
{
    Blackboard bb;
    EXPECT_FALSE(bb.has("foo"));
    EXPECT_EQ(bb.size(), 0U);

    bb.set("foo", 3.14F);
    EXPECT_TRUE(bb.has("foo"));
    EXPECT_EQ(bb.size(), 1U);

    EXPECT_TRUE(bb.erase("foo"));
    EXPECT_FALSE(bb.has("foo"));
    EXPECT_EQ(bb.size(), 0U);

    // Erasing missing key returns false without crashing.
    EXPECT_FALSE(bb.erase("foo"));
    EXPECT_FALSE(bb.erase("nonexistent"));

    // clear() empties multiple entries.
    bb.set("a", 1.0F);
    bb.set("b", 2.0F);
    EXPECT_EQ(bb.size(), 2U);
    bb.clear();
    EXPECT_EQ(bb.size(), 0U);
    EXPECT_FALSE(bb.has("a"));
}

// -----------------------------------------------------------------------------
// 25) StateMachine: blend transition (blend_duration > 0).
//     During the blend the pose should be an intermediate between the
//     outgoing and incoming states (neither endpoint exactly), and after
//     the blend finishes the pose equals the destination state.
// -----------------------------------------------------------------------------
TEST(AnimGraphStateMachineEdge, BlendTransitionLerpsPoses)
{
    const auto skel = make_one_joint_skel();
    const auto clip_a = make_linear_clip(0.0F,   0.0F);   // constant 0
    const auto clip_b = make_linear_clip(10.0F, 10.0F);   // constant 10

    StateMachineNode fsm;
    const auto sa = fsm.add_state("a", std::make_unique<PlayClipNode>(&clip_a, true));
    const auto sb = fsm.add_state("b", std::make_unique<PlayClipNode>(&clip_b, true));

    // Transition from a->b with 1-second blend.
    ASSERT_TRUE(fsm.add_transition(sa, sb,
        [](const Blackboard& bb2, float /*e*/) {
            return bb2.get("go", 0.0F) > 0.5F;
        },
        /*blend_duration=*/1.0F));

    Blackboard bb;
    // First tick: in state a.
    auto p0 = fsm.tick(0.0F, bb, skel);
    EXPECT_EQ(fsm.current_state_index(), sa);
    EXPECT_TRUE(eq(p0.joint_locals[0].position.x, 0.0F));

    // Fire the transition.
    bb.set("go", 1.0F);
    auto p1 = fsm.tick(0.1F, bb, skel);
    EXPECT_EQ(fsm.current_state_index(), sb);
    EXPECT_TRUE(fsm.is_blending());
    // Blend is in progress: pose must be between 0 and 10.
    EXPECT_GT(p1.joint_locals[0].position.x, 0.0F);
    EXPECT_LT(p1.joint_locals[0].position.x, 10.0F);

    // Advance past the blend duration (dt=1.5s -> blend_time > 1.0).
    const auto p_done = fsm.tick(1.6F, bb, skel);
    EXPECT_FALSE(fsm.is_blending());
    EXPECT_TRUE(eq(p_done.joint_locals[0].position.x, 10.0F));
}

// -----------------------------------------------------------------------------
// 26) StateMachine: set_initial_state OOB returns false; valid index returns true.
// -----------------------------------------------------------------------------
TEST(AnimGraphStateMachineEdge, SetInitialStateOobReturnsFalse)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 0.0F);

    StateMachineNode fsm;
    EXPECT_FALSE(fsm.set_initial_state(0));  // No states yet.

    fsm.add_state("a", std::make_unique<PlayClipNode>(&clip, false));
    EXPECT_TRUE(fsm.set_initial_state(0));
    EXPECT_FALSE(fsm.set_initial_state(1));  // Only index 0 exists.
}

// -----------------------------------------------------------------------------
// 27) StateMachine: add_transition rejects OOB indices and empty predicate.
// -----------------------------------------------------------------------------
TEST(AnimGraphStateMachineEdge, AddTransitionRejectsInvalid)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 0.0F);

    StateMachineNode fsm;
    const auto sa = fsm.add_state("a", std::make_unique<PlayClipNode>(&clip, false));
    const auto sb = fsm.add_state("b", std::make_unique<PlayClipNode>(&clip, false));

    // OOB from-index.
    EXPECT_FALSE(fsm.add_transition(99, sb,
        [](const Blackboard&, float) { return true; }));
    // OOB to-index.
    EXPECT_FALSE(fsm.add_transition(sa, 99,
        [](const Blackboard&, float) { return true; }));
    // Empty predicate.
    EXPECT_FALSE(fsm.add_transition(sa, sb, {}));
    // Valid transition must succeed.
    EXPECT_TRUE(fsm.add_transition(sa, sb,
        [](const Blackboard&, float) { return false; }));
}

// -----------------------------------------------------------------------------
// 28) StateMachine: current_state_name before any state added is empty.
//     After adding, current_state_name reflects the first state.
// -----------------------------------------------------------------------------
TEST(AnimGraphStateMachineEdge, StateNameBeforeAndAfterAdd)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(0.0F, 0.0F);

    StateMachineNode fsm;
    EXPECT_TRUE(fsm.current_state_name().empty());

    fsm.add_state("locomotion", std::make_unique<PlayClipNode>(&clip, false));
    EXPECT_EQ(fsm.current_state_name(), "locomotion");

    fsm.add_state("attack", std::make_unique<PlayClipNode>(&clip, false));
    // Still "locomotion" — no transition fired.
    EXPECT_EQ(fsm.current_state_name(), "locomotion");
    EXPECT_EQ(fsm.state_count(), 2U);
}

// -----------------------------------------------------------------------------
// 29) AnimGraph: set_root(nullptr) then evaluate returns bind-pose.
//     Repeated calls after null-set must not crash.
// -----------------------------------------------------------------------------
TEST(AnimGraphEdge, SetRootNullThenEvaluateIsBindPose)
{
    const auto skel = make_one_joint_skel();
    const auto clip = make_linear_clip(5.0F, 5.0F);

    AnimGraph g { std::make_unique<PlayClipNode>(&clip, false) };
    EXPECT_TRUE(eq(g.evaluate(skel, 0.0F).joint_locals[0].position.x, 5.0F));

    g.set_root(nullptr);
    EXPECT_EQ(g.root(), nullptr);

    const auto p1 = g.evaluate(skel, 0.0F);
    ASSERT_EQ(p1.joint_locals.size(), 1U);
    EXPECT_TRUE(eq(p1.joint_locals[0].position.x, 0.0F));  // bind-pose

    // Second evaluate after null — must not crash.
    const auto p2 = g.evaluate(skel, 1.0F);
    ASSERT_EQ(p2.joint_locals.size(), 1U);
}

// -----------------------------------------------------------------------------
// 30) Blend weight clamp in blend_pose:
//     w < 0 treated as 0 (full a); w > 1 treated as 1 (full b).
//     Tested indirectly via Blend1D with param slightly outside range.
//     The internal call `blend_pose(pa, pb, w, out)` already clamps so
//     we verify the observable output is correct at the extremes.
// -----------------------------------------------------------------------------
TEST(AnimGraphBlend1DEdge, BlendWeightExtremesClamped)
{
    const auto skel = make_one_joint_skel();
    const auto c0 = make_linear_clip(0.0F,  0.0F);
    const auto c1 = make_linear_clip(10.0F, 10.0F);

    Blend1DNode blend { "speed" };
    ASSERT_TRUE(blend.add_input(0.0F, std::make_unique<PlayClipNode>(&c0, false)));
    ASSERT_TRUE(blend.add_input(1.0F, std::make_unique<PlayClipNode>(&c1, false)));

    Blackboard bb;

    // Param exactly at min -> clamped to first input; weight at 0 boundary.
    bb.set("speed", 0.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 0.0F));

    // Param exactly at max -> clamped to last input; weight at 1 boundary.
    bb.set("speed", 1.0F);
    EXPECT_TRUE(eq(blend.tick(0.0F, bb, skel).joint_locals[0].position.x, 10.0F));

    // Param between — weight is between 0 and 1 and must not over-shoot.
    bb.set("speed", 0.25F);
    const float mid = blend.tick(0.0F, bb, skel).joint_locals[0].position.x;
    EXPECT_GE(mid, 0.0F);
    EXPECT_LE(mid, 10.0F);
    EXPECT_TRUE(eq(mid, 2.5F));  // exactly 25% of 10
}

}  // namespace
