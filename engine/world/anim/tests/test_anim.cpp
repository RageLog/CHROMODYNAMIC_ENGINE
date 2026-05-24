// =============================================================================
// CHROMODYNAMIC — cd::anim tests
// =============================================================================
#include <cd/anim/Animation.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace
{

constexpr float kEps = 1e-4F;

[[nodiscard]] bool eq(float a, float b) noexcept
{
    return std::fabs(a - b) < kEps;
}

cd::math::Transformf at(float x)
{
    cd::math::Transformf t;
    t.position = { x, 0.0F, 0.0F };
    return t;
}

TEST(AnimationClip, EmptyClipReturnsIdentity)
{
    cd::anim::AnimationClip c {};
    auto v = c.sample(1.0F);
    EXPECT_TRUE(eq(v.position.x, 0.0F));
    EXPECT_EQ(c.duration(), 0.0F);
}

TEST(AnimationClip, SingleKeyframeReturnsItself)
{
    cd::anim::AnimationClip c { { { 0.5F, at(7.0F) } } };
    EXPECT_TRUE(eq(c.sample(0.0F).position.x, 7.0F));
    EXPECT_TRUE(eq(c.sample(10.0F).position.x, 7.0F));
}

TEST(AnimationClip, InterpolatesBetweenKeyframes)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) } }
    };
    EXPECT_TRUE(eq(c.sample(0.0F).position.x, 0.0F));
    EXPECT_TRUE(eq(c.sample(0.5F).position.x, 5.0F));
    EXPECT_TRUE(eq(c.sample(1.0F).position.x, 10.0F));
}

TEST(AnimationClip, ClampsOutsideRange)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) } }
    };
    EXPECT_TRUE(eq(c.sample(-1.0F).position.x, 0.0F));
    EXPECT_TRUE(eq(c.sample(2.0F).position.x, 10.0F));
}

TEST(AnimationClip, ThreeKeyframesSegmented)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) }, { 2.0F, at(0.0F) } }
    };
    EXPECT_TRUE(eq(c.sample(0.5F).position.x, 5.0F));
    EXPECT_TRUE(eq(c.sample(1.5F).position.x, 5.0F));
}

TEST(AnimationPlayer, AdvanceLoops)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) } }
    };
    cd::anim::AnimationPlayer p { &c };
    p.set_loop_mode(cd::anim::LoopMode::kLoop);
    p.update(0.5F);
    EXPECT_TRUE(eq(p.update(0.0F).position.x, 5.0F));
    // After 1.5s total, loop wraps to 0.5s into the cycle → x ≈ 5.
    p.update(1.0F);
    EXPECT_TRUE(eq(p.update(0.0F).position.x, 5.0F));
}

TEST(AnimationPlayer, ClampStopsAtEnd)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) } }
    };
    cd::anim::AnimationPlayer p { &c };
    p.set_loop_mode(cd::anim::LoopMode::kClamp);
    p.update(5.0F);
    EXPECT_TRUE(eq(p.update(0.0F).position.x, 10.0F));
}

TEST(AnimationPlayer, PauseStopsAdvancing)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) } }
    };
    cd::anim::AnimationPlayer p { &c };
    p.update(0.25F);
    p.pause();
    const float frozen = p.update(0.5F).position.x;
    // Pause means: no time advance during this update; we sample at t=0.25.
    EXPECT_TRUE(eq(frozen, 2.5F));
}

TEST(AnimationPlayer, NegativeSpeedRunsBackward)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) } }
    };
    cd::anim::AnimationPlayer p { &c };
    p.set_loop_mode(cd::anim::LoopMode::kLoop);
    p.set_speed(-1.0F);
    // Step -0.25s puts time at -0.25 which loops to 0.75 → x ≈ 7.5.
    p.update(0.25F);
    EXPECT_TRUE(eq(p.update(0.0F).position.x, 7.5F));
}

TEST(AnimationPlayer, PingPongReflects)
{
    cd::anim::AnimationClip c {
        { { 0.0F, at(0.0F) }, { 1.0F, at(10.0F) } }
    };
    cd::anim::AnimationPlayer p { &c };
    p.set_loop_mode(cd::anim::LoopMode::kPingPong);
    // At t=1.5 the ping-pong cycle is reflecting back: 1.5 in [1,2] maps
    // to 0.5 going backward → x ≈ 5.
    p.update(1.5F);
    EXPECT_TRUE(eq(p.update(0.0F).position.x, 5.0F));
}

}  // namespace

// =============================================================================
// Skeleton + SkinnedClip — Phase 5 / S4.2.b
// =============================================================================

#include <cd/anim/Skeleton.hpp>

namespace
{

cd::anim::Skeleton make_two_joint_chain()
{
    // Root at origin, child translated +1 along X. Both bind transforms
    // identity rotation/scale.
    cd::anim::Joint root;
    root.name = "root";
    root.parent = -1;
    root.local_bind = cd::math::Transformf {};

    cd::anim::Joint child;
    child.name = "child";
    child.parent = 0;
    child.local_bind.position = { 1.0F, 0.0F, 0.0F };

    return cd::anim::Skeleton { { root, child } };
}

}  // namespace

TEST(Skeleton, BuildAndLookup)
{
    auto s = make_two_joint_chain();
    EXPECT_EQ(s.joint_count(), 2u);
    EXPECT_EQ(s.find("root"), 0);
    EXPECT_EQ(s.find("child"), 1);
    EXPECT_EQ(s.find("missing"), -1);
}

TEST(Skeleton, InverseBindMatricesAreInverseOfWorldBind)
{
    auto s = make_two_joint_chain();
    // Child world bind = parent_world(=I) * local_translation(+X)
    // → inverse_bind should translate by -X.
    const auto& child_ib = s.joint(1).inverse_bind_matrix;
    // Multiply inv_bind by [1,0,0,1]: should give origin (close to zero
    // since world bind translates the joint to (+1,0,0)).
    const cd::math::Vec4f probe { 1.0F, 0.0F, 0.0F, 1.0F };
    const cd::math::Vec4f r = child_ib * probe;
    EXPECT_NEAR(r.x, 0.0F, 1e-5F);
    EXPECT_NEAR(r.y, 0.0F, 1e-5F);
    EXPECT_NEAR(r.z, 0.0F, 1e-5F);
    EXPECT_NEAR(r.w, 1.0F, 1e-5F);
}

TEST(SkinnedClip, SampleAdvancesTrackedJointOnly)
{
    auto s = make_two_joint_chain();
    cd::anim::SkinnedClip clip { 2 };
    // Track only the child joint: move from (1,0,0) at t=0 to (1,2,0) at t=1.
    std::vector<cd::anim::Keyframe> child_track;
    cd::math::Transformf a {};
    a.position = { 1.0F, 0.0F, 0.0F };
    cd::math::Transformf b {};
    b.position = { 1.0F, 2.0F, 0.0F };
    child_track.push_back({ 0.0F, a });
    child_track.push_back({ 1.0F, b });
    clip.set_track(1, std::move(child_track));

    auto pose = cd::anim::Pose::bind_pose(s);
    clip.sample(0.5F, pose);
    // Root untouched (bind = identity).
    EXPECT_NEAR(pose.joint_locals[0].position.x, 0.0F, 1e-5F);
    // Child interpolated halfway.
    EXPECT_NEAR(pose.joint_locals[1].position.x, 1.0F, 1e-5F);
    EXPECT_NEAR(pose.joint_locals[1].position.y, 1.0F, 1e-5F);
}

TEST(Skeleton, ComputeSkinningMatricesAtBindGivesIdentity)
{
    // At the bind pose, world(i) == world_bind(i), so
    // world(i) · inverse_bind(i) = I for every joint.
    auto s = make_two_joint_chain();
    auto pose = cd::anim::Pose::bind_pose(s);
    std::vector<cd::math::Mat4f> mats;
    cd::anim::compute_skinning_matrices(s, pose, mats);
    ASSERT_EQ(mats.size(), 2u);
    const auto id = cd::math::Mat4f::identity();
    for (const auto& m : mats)
    {
        for (std::size_t c = 0; c < 4; ++c)
            for (std::size_t r = 0; r < 4; ++r)
                EXPECT_NEAR(m[c][r], id[c][r], 1e-5F);
    }
}

// ---------------------------------------------------------------------------
// Phase 19.F — BlendTree2 tests (Wave 180)
// ---------------------------------------------------------------------------
#include <cd/anim/BlendTree2.hpp>

#include <array>

TEST(BlendTree2, ZeroWeightReturnsPoseA)
{
    std::array<cd::math::Transformf, 1> a {}, b {}, out {};
    a[0].position = cd::math::Vec3f { 1, 0, 0 };
    b[0].position = cd::math::Vec3f { 5, 0, 0 };
    ASSERT_TRUE(cd::anim::blend2(a, b, 0.0F, out));
    EXPECT_NEAR(out[0].position.x, 1.0F, 1e-5F);
}

TEST(BlendTree2, FullWeightReturnsPoseB)
{
    std::array<cd::math::Transformf, 1> a {}, b {}, out {};
    a[0].position = cd::math::Vec3f { 1, 0, 0 };
    b[0].position = cd::math::Vec3f { 5, 0, 0 };
    ASSERT_TRUE(cd::anim::blend2(a, b, 1.0F, out));
    EXPECT_NEAR(out[0].position.x, 5.0F, 1e-5F);
}

TEST(BlendTree2, HalfWeightInterpolatesPosition)
{
    std::array<cd::math::Transformf, 1> a {}, b {}, out {};
    a[0].position = cd::math::Vec3f { 0, 0, 0 };
    b[0].position = cd::math::Vec3f { 10, 0, 0 };
    ASSERT_TRUE(cd::anim::blend2(a, b, 0.5F, out));
    EXPECT_NEAR(out[0].position.x, 5.0F, 1e-5F);
}

TEST(BlendTree2, MismatchedPoseSizesFails)
{
    std::array<cd::math::Transformf, 1> a {};
    std::array<cd::math::Transformf, 2> b {};
    std::array<cd::math::Transformf, 1> out {};
    EXPECT_FALSE(cd::anim::blend2(a, b, 0.5F, out));
}

#include <cd/anim/AdditiveBlend.hpp>

TEST(AdditiveBlend, ZeroWeightReturnsBaseUnchanged)
{
    std::array<cd::math::Transformf, 1> base {};
    base[0].position = { 1.0F, 2.0F, 3.0F };
    base[0].rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
    base[0].scale = { 1.0F, 1.0F, 1.0F };
    std::array<cd::math::Transformf, 1> delta {};
    delta[0].position = { 10.0F, 20.0F, 30.0F };
    delta[0].rotation = { 0.0F, 0.7071F, 0.0F, 0.7071F };
    delta[0].scale = { 2.0F, 2.0F, 2.0F };
    std::array<cd::math::Transformf, 1> out {};
    EXPECT_TRUE(cd::anim::additive_blend(base, delta, 0.0F, out));
    EXPECT_FLOAT_EQ(out[0].position.x, 1.0F);
    EXPECT_FLOAT_EQ(out[0].position.y, 2.0F);
    EXPECT_FLOAT_EQ(out[0].position.z, 3.0F);
    EXPECT_FLOAT_EQ(out[0].scale.x, 1.0F);
}

TEST(AdditiveBlend, FullWeightAddsDeltaPosition)
{
    std::array<cd::math::Transformf, 1> base {};
    base[0].position = { 1.0F, 0.0F, 0.0F };
    base[0].rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
    base[0].scale = { 1.0F, 1.0F, 1.0F };
    std::array<cd::math::Transformf, 1> delta {};
    delta[0].position = { 5.0F, 0.0F, 0.0F };
    delta[0].rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
    delta[0].scale = { 1.0F, 1.0F, 1.0F };
    std::array<cd::math::Transformf, 1> out {};
    EXPECT_TRUE(cd::anim::additive_blend(base, delta, 1.0F, out));
    EXPECT_FLOAT_EQ(out[0].position.x, 6.0F);
}

TEST(AdditiveBlend, SizeMismatchRejected)
{
    std::array<cd::math::Transformf, 1> base {};
    std::array<cd::math::Transformf, 2> delta {};
    std::array<cd::math::Transformf, 1> out {};
    EXPECT_FALSE(cd::anim::additive_blend(base, delta, 0.5F, out));
}

#include <cd/anim/CurveTrack.hpp>

TEST(CurveTrack, EmptySampleReturnsZero)
{
    cd::anim::CurveTrack c;
    EXPECT_FLOAT_EQ(c.sample(0.5F), 0.0F);
}

TEST(CurveTrack, SingleKeyClampsAround)
{
    cd::anim::CurveTrack c;
    c.add_key(0.5F, 42.0F);
    EXPECT_FLOAT_EQ(c.sample(-10.0F), 42.0F);
    EXPECT_FLOAT_EQ(c.sample(0.5F),   42.0F);
    EXPECT_FLOAT_EQ(c.sample(10.0F),  42.0F);
}

TEST(CurveTrack, LinearMidpoint)
{
    cd::anim::CurveTrack c;
    c.add_key(0.0F, 0.0F);
    c.add_key(1.0F, 10.0F);
    EXPECT_FLOAT_EQ(c.sample(0.5F), 5.0F);
    EXPECT_FLOAT_EQ(c.sample(0.25F), 2.5F);
}

TEST(CurveTrack, ClampsOutsideRange)
{
    cd::anim::CurveTrack c;
    c.add_key(0.0F, 1.0F);
    c.add_key(1.0F, 5.0F);
    EXPECT_FLOAT_EQ(c.sample(-1.0F), 1.0F);
    EXPECT_FLOAT_EQ(c.sample(2.0F),  5.0F);
}

TEST(CurveTrack, AddKeyMaintainsSortOrder)
{
    cd::anim::CurveTrack c;
    c.add_key(2.0F, 20.0F);
    c.add_key(0.0F, 0.0F);
    c.add_key(1.0F, 10.0F);
    ASSERT_EQ(c.size(), 3u);
    EXPECT_FLOAT_EQ(c.keys()[0].t, 0.0F);
    EXPECT_FLOAT_EQ(c.keys()[1].t, 1.0F);
    EXPECT_FLOAT_EQ(c.keys()[2].t, 2.0F);
}

#include <cd/anim/EventTrack.hpp>

TEST(EventTrack, AddMaintainsSortOrder)
{
    cd::anim::EventTrack t;
    t.add(0.5F, 10);
    t.add(0.2F, 20);
    t.add(0.8F, 30);
    ASSERT_EQ(t.size(), 3u);
    EXPECT_FLOAT_EQ(t.events()[0].t, 0.2F);
    EXPECT_FLOAT_EQ(t.events()[1].t, 0.5F);
    EXPECT_FLOAT_EQ(t.events()[2].t, 0.8F);
}

TEST(EventTrack, AdvanceFiresEventsInWindow)
{
    cd::anim::EventTrack t;
    t.add(0.1F, 1);
    t.add(0.3F, 2);
    t.add(0.5F, 3);
    t.add(0.7F, 4);
    std::vector<std::uint32_t> fired;
    auto n = t.advance(0.2F, 0.6F, [&](std::uint32_t id) { fired.push_back(id); });
    EXPECT_EQ(n, 2u);
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_EQ(fired[0], 2u);
    EXPECT_EQ(fired[1], 3u);
}

TEST(EventTrack, AdvanceWindowExcludesStartInclusiveOfEnd)
{
    cd::anim::EventTrack t;
    t.add(0.2F, 1);
    std::size_t fired = 0;
    t.advance(0.2F, 0.5F, [&](std::uint32_t) { ++fired; });
    EXPECT_EQ(fired, 0u);   // 0.2 not in (0.2, 0.5]
    t.advance(0.0F, 0.2F, [&](std::uint32_t) { ++fired; });
    EXPECT_EQ(fired, 1u);   // 0.2 IS in (0.0, 0.2]
}

#include <cd/anim/BoneMask.hpp>

TEST(BoneMask, DefaultWeightIsFullAfterResize)
{
    cd::anim::BoneMask m { 8 };
    EXPECT_EQ(m.size(), 8u);
    for (std::size_t i = 0; i < 8; ++i)
        EXPECT_FLOAT_EQ(m.weight(i), 1.0F);
}

TEST(BoneMask, SetWeightClamps)
{
    cd::anim::BoneMask m { 4 };
    m.set_weight(0, -1.0F);
    m.set_weight(1, 2.0F);
    EXPECT_FLOAT_EQ(m.weight(0), 0.0F);
    EXPECT_FLOAT_EQ(m.weight(1), 1.0F);
}

TEST(BoneMask, OutOfRangeReadReturnsFullWeight)
{
    cd::anim::BoneMask m { 4 };
    EXPECT_FLOAT_EQ(m.weight(100), 1.0F);
}

TEST(BoneMask, FillSetsAllWeights)
{
    cd::anim::BoneMask m { 4 };
    m.fill(0.25F);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(m.weight(i), 0.25F);
}

#include <cd/anim/PoseAlign.hpp>

TEST(PoseAlign, AlignSubtractsRestRoot)
{
    std::array<cd::math::Transformf, 2> frames {};
    frames[0].position = { 10, 5, 0 };
    frames[1].position = { 12, 5, 1 };
    cd::math::Vec3f rest { 10, 5, 0 };
    cd::anim::align_root_position(frames, rest);
    EXPECT_FLOAT_EQ(frames[0].position.x, 0.0F);
    EXPECT_FLOAT_EQ(frames[1].position.x, 2.0F);
}

TEST(PoseAlign, ComposeAddsWorldRoot)
{
    std::array<cd::math::Transformf, 1> frames {};
    frames[0].position = { 1, 0, 0 };
    cd::anim::compose_root_motion(frames, { 100, 0, 0 });
    EXPECT_FLOAT_EQ(frames[0].position.x, 101.0F);
}

TEST(PoseAlign, VelocityFromPosDelta)
{
    cd::math::Transformf prev {}, cur {};
    prev.position = { 0, 0, 0 };
    cur.position = { 10, 0, 0 };
    auto v = cd::anim::root_velocity(prev, cur, 0.5F);
    EXPECT_FLOAT_EQ(v.x, 20.0F);   // 10 m / 0.5 s
}

TEST(PoseAlign, VelocityZeroDtSafeFallback)
{
    cd::math::Transformf p {}, c {};
    auto v = cd::anim::root_velocity(p, c, 0.0F);
    EXPECT_FLOAT_EQ(v.x, 0.0F);
}

#include <cd/anim/BoneSocket.hpp>

TEST(BoneSocketSet, AddAndFindByName)
{
    cd::anim::BoneSocketSet s;
    cd::math::Transformf t;
    t.position = { 0, 0.1F, 0 };
    s.add("WeaponR_Slot", 7, t);
    const auto* p = s.find("WeaponR_Slot");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->bone_index, 7u);
    EXPECT_FLOAT_EQ(p->local_offset.position.y, 0.1F);
}

TEST(BoneSocketSet, MissingNameReturnsNull)
{
    cd::anim::BoneSocketSet s;
    EXPECT_EQ(s.find("nope"), nullptr);
}

TEST(BoneSocketSet, ClearEmpties)
{
    cd::anim::BoneSocketSet s;
    s.add("a", 0);
    s.add("b", 1);
    s.clear();
    EXPECT_EQ(s.size(), 0u);
}
