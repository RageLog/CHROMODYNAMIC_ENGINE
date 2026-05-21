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
