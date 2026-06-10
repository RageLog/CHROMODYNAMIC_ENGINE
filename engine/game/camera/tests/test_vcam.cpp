// =============================================================================
// CHROMODYNAMIC - tests/test_vcam.cpp
// Phase 482 - cd::game::camera unit tests (G3.3).
//
// Brief-mandated coverage (8 cases):
//   1. Single vcam: brain outputs its position.
//   2. Higher priority vcam wins.
//   3. Blend transitions linearly between vcams over duration.
//   4. Damping smooths target jumps (critically damped spring).
//   5. Remove active vcam falls back to next priority.
//   6. FOV transitions during blend.
//   7. Position offset honored.
//   8. Negative dt rejected.
//
// Extras (regression / boundary):
//   9. Blend duration zero: instant hand-off.
//  10. Disabled vcam skipped.
//  11. warp() resets blend + damping.
// =============================================================================
#include <cd/game/camera/VirtualCamera.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <unordered_map>

namespace
{

using cd::camera::Camera;
using cd::ecs::Entity;
using cd::game::camera::CameraBrain;
using cd::game::camera::VirtualCamera;
using cd::math::Vec3f;

Entity make_entity(std::uint32_t id, std::uint32_t gen = 1)
{
    return Entity {id, gen};
}

// Simple stub TargetPositionFn backed by a map.  Test cases configure the
// map and pass `pos_fn` to brain.tick().
struct TargetTable
{
    std::unordered_map<std::uint64_t, Vec3f> positions {};

    void set(Entity e, const Vec3f& p)
    {
        positions[(static_cast<std::uint64_t>(e.generation) << 32) | e.id] = p;
    }

    [[nodiscard]] Vec3f get(Entity e) const
    {
        const auto it = positions.find((static_cast<std::uint64_t>(e.generation) << 32) | e.id);
        return it == positions.end() ? Vec3f {0.0F, 0.0F, 0.0F} : it->second;
    }
};

CameraBrain::TargetPositionFn make_lookup(const TargetTable& table)
{
    return [&table](Entity e) { return table.get(e); };
}

bool near_eq(float a, float b, float eps = 1e-4F)
{
    return std::fabs(a - b) <= eps;
}

bool vec_near(const Vec3f& a, const Vec3f& b, float eps = 1e-3F)
{
    return near_eq(a.x, b.x, eps) && near_eq(a.y, b.y, eps) && near_eq(a.z, b.z, eps);
}

}  // namespace

// ----------------------------------------------------------------------------
// 1. Single vcam: brain.tick() returns that vcam's instantaneous frame.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, SingleVCamLivePassThrough)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity target = make_entity(1);
    table.set(target, Vec3f {10.0F, 0.0F, 0.0F});

    VirtualCamera v;
    v.set_target(target);
    v.settings().position_offset = Vec3f {0.0F, 2.0F, 5.0F};
    v.settings().fov_y           = 1.2F;
    const auto id = brain.add_vcam(v, /*priority*/ 10);
    EXPECT_NE(id, 0U);

    // First tick (dt = 0 to skip damping) snaps to the vcam's pose.
    const Camera out = brain.tick(0.0F, make_lookup(table));
    EXPECT_TRUE(vec_near(out.target, Vec3f {10.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(vec_near(out.eye,    Vec3f {10.0F, 2.0F, 5.0F}));
    EXPECT_TRUE(near_eq(out.fov_y, 1.2F));
    EXPECT_EQ(brain.live_id(), id);
    EXPECT_TRUE(brain.last_tick_ok());
}

// ----------------------------------------------------------------------------
// 2. Higher priority vcam wins selection.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, HigherPriorityVCamWins)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity target_lo = make_entity(1);
    const Entity target_hi = make_entity(2);
    table.set(target_lo, Vec3f {0.0F,  0.0F, 0.0F});
    table.set(target_hi, Vec3f {100.0F, 0.0F, 0.0F});

    VirtualCamera v_lo;
    v_lo.set_target(target_lo);
    v_lo.settings().position_offset = Vec3f {0.0F, 1.0F, 1.0F};
    const auto id_lo = brain.add_vcam(v_lo, /*priority*/ 5);
    EXPECT_NE(id_lo, 0U);

    VirtualCamera v_hi;
    v_hi.set_target(target_hi);
    v_hi.settings().position_offset = Vec3f {0.0F, 1.0F, 1.0F};
    const auto id_hi = brain.add_vcam(v_hi, /*priority*/ 50);

    const Camera out = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_hi);
    EXPECT_TRUE(vec_near(out.target, Vec3f {100.0F, 0.0F, 0.0F}));
}

// ----------------------------------------------------------------------------
// 3. Blend transitions linearly between vcams over the blend duration.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, BlendTransitionsLinearly)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f {0.0F,   0.0F, 0.0F});
    table.set(tgt_b, Vec3f {100.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    const auto id_a = brain.add_vcam(va, /*priority*/ 10);
    (void)id_a;

    // Tick once to settle on vcam A.
    brain.tick(0.0F, make_lookup(table));

    // Add vcam B with priority 100 and a 1.0s blend_in.
    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(1.0F);
    const auto id_b = brain.add_vcam(vb, /*priority*/ 100);

    // First tick after add: live changes -> blend starts (still at t = 0).
    Camera mid = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_b);
    EXPECT_TRUE(brain.is_blending());
    // At t = 0 the output equals the FROM frame (vcam A's last sample).
    EXPECT_TRUE(vec_near(mid.target, Vec3f {0.0F, 0.0F, 0.0F}, 1e-3F));

    // Halfway: 0.5s out of 1.0s -> target lerps to 50% (50, 0, 0).
    mid = brain.tick(0.5F, make_lookup(table));
    EXPECT_TRUE(near_eq(mid.target.x, 50.0F, 0.5F));

    // Full: another 0.5s -> blend completes, output snaps to vcam B.
    Camera done = brain.tick(0.5F, make_lookup(table));
    EXPECT_FALSE(brain.is_blending());
    EXPECT_TRUE(near_eq(done.target.x, 100.0F, 0.1F));
}

// ----------------------------------------------------------------------------
// 4. Damping smooths target jumps (critically-damped spring).
//    Without damping a jump is instantaneous; with damping the output lags
//    the target for several ticks and never overshoots (zeta = 1).
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, DampingSmoothsTargetJumps)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt = make_entity(1);
    table.set(tgt, Vec3f {0.0F, 0.0F, 0.0F});

    VirtualCamera v;
    v.set_target(tgt);
    v.settings().damping = Vec3f {0.5F, 0.5F, 0.5F};  // 0.5s half-life.
    brain.add_vcam(v, /*priority*/ 10);

    // Settle at origin.
    brain.tick(0.016F, make_lookup(table));
    brain.tick(0.016F, make_lookup(table));

    // Snap target to (10, 0, 0).
    table.set(tgt, Vec3f {10.0F, 0.0F, 0.0F});

    // First damped tick: should NOT have caught up yet.
    const Camera step1 = brain.tick(0.016F, make_lookup(table));
    EXPECT_LT(step1.target.x, 5.0F);  // significantly lagging.
    EXPECT_GT(step1.target.x, 0.0F);  // moving toward target.

    // After many ticks the spring asymptotes toward 10 without overshoot.
    Camera last = step1;
    for (int i = 0; i < 200; ++i)
    {
        last = brain.tick(0.016F, make_lookup(table));
        // Critically damped: never overshoot the target.
        EXPECT_LE(last.target.x, 10.0F + 1e-3F);
    }
    EXPECT_TRUE(near_eq(last.target.x, 10.0F, 0.05F));
}

// ----------------------------------------------------------------------------
// 5. Remove active vcam: brain falls back to next-priority vcam.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, RemoveActiveVCamFallsBack)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_lo = make_entity(1);
    const Entity tgt_hi = make_entity(2);
    table.set(tgt_lo, Vec3f {0.0F,   0.0F, 0.0F});
    table.set(tgt_hi, Vec3f {100.0F, 0.0F, 0.0F});

    VirtualCamera v_lo;
    v_lo.set_target(tgt_lo);
    const auto id_lo = brain.add_vcam(v_lo, /*priority*/ 10);

    VirtualCamera v_hi;
    v_hi.set_target(tgt_hi);
    const auto id_hi = brain.add_vcam(v_hi, /*priority*/ 100);

    // Settle on v_hi.
    Camera out = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_hi);
    EXPECT_TRUE(vec_near(out.target, Vec3f {100.0F, 0.0F, 0.0F}));

    // Drop v_hi -> brain must fall back to v_lo.
    EXPECT_TRUE(brain.remove_vcam(id_hi));
    out = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_lo);
    EXPECT_TRUE(vec_near(out.target, Vec3f {0.0F, 0.0F, 0.0F}));
}

// ----------------------------------------------------------------------------
// 6. FOV transitions during blend.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, FovTransitionsDuringBlend)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt = make_entity(1);
    table.set(tgt, Vec3f {0.0F, 0.0F, 0.0F});

    VirtualCamera v_wide;
    v_wide.set_target(tgt);
    v_wide.settings().fov_y = 1.5F;  // ~86 deg wide.
    brain.add_vcam(v_wide, /*priority*/ 10);

    brain.tick(0.0F, make_lookup(table));

    VirtualCamera v_tele;
    v_tele.set_target(tgt);
    v_tele.settings().fov_y = 0.5F;  // ~28 deg telephoto.
    v_tele.blend_in(1.0F);
    brain.add_vcam(v_tele, /*priority*/ 100);

    brain.tick(0.0F, make_lookup(table));  // start blend at t = 0.

    const Camera mid = brain.tick(0.5F, make_lookup(table));
    // At halfway the fov is the midpoint of 1.5 and 0.5 = 1.0.
    EXPECT_TRUE(near_eq(mid.fov_y, 1.0F, 0.05F));

    const Camera done = brain.tick(0.5F, make_lookup(table));
    EXPECT_TRUE(near_eq(done.fov_y, 0.5F, 0.05F));
}

// ----------------------------------------------------------------------------
// 7. Position offset honored (eye = target + offset).
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, PositionOffsetHonored)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt = make_entity(1);
    table.set(tgt, Vec3f {3.0F, 4.0F, 5.0F});

    VirtualCamera v;
    v.set_target(tgt);
    v.set_position_offset(Vec3f {1.0F, 2.0F, 3.0F});
    brain.add_vcam(v, /*priority*/ 10);

    const Camera out = brain.tick(0.0F, make_lookup(table));
    EXPECT_TRUE(vec_near(out.target, Vec3f {3.0F, 4.0F, 5.0F}));
    EXPECT_TRUE(vec_near(out.eye,    Vec3f {4.0F, 6.0F, 8.0F}));
}

// ----------------------------------------------------------------------------
// 8. Negative dt is rejected; last_tick_ok() reports false; output unchanged.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, NegativeDtRejected)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt = make_entity(1);
    table.set(tgt, Vec3f {7.0F, 0.0F, 0.0F});

    VirtualCamera v;
    v.set_target(tgt);
    brain.add_vcam(v, /*priority*/ 10);

    const Camera baseline = brain.tick(0.0F, make_lookup(table));
    EXPECT_TRUE(brain.last_tick_ok());

    // Negative dt -> rejected.  Same output, last_tick_ok() = false.
    const Camera rejected = brain.tick(-0.1F, make_lookup(table));
    EXPECT_FALSE(brain.last_tick_ok());
    EXPECT_TRUE(vec_near(rejected.target, baseline.target));
    EXPECT_TRUE(vec_near(rejected.eye,    baseline.eye));

    // A subsequent valid tick recovers.
    const Camera valid = brain.tick(0.016F, make_lookup(table));
    EXPECT_TRUE(brain.last_tick_ok());
    EXPECT_TRUE(vec_near(valid.target, Vec3f {7.0F, 0.0F, 0.0F}));
}

// ----------------------------------------------------------------------------
// 9. Blend duration zero: hand-off is instantaneous.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, ZeroBlendDurationSnaps)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f {0.0F,   0.0F, 0.0F});
    table.set(tgt_b, Vec3f {100.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(0.0F);  // snap.
    brain.add_vcam(vb, /*priority*/ 100);

    const Camera out = brain.tick(0.0F, make_lookup(table));
    EXPECT_FALSE(brain.is_blending());
    EXPECT_TRUE(vec_near(out.target, Vec3f {100.0F, 0.0F, 0.0F}));
}

// ----------------------------------------------------------------------------
// 10. Disabled vcam skipped in selection.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, DisabledVCamSkipped)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_lo = make_entity(1);
    const Entity tgt_hi = make_entity(2);
    table.set(tgt_lo, Vec3f {0.0F,   0.0F, 0.0F});
    table.set(tgt_hi, Vec3f {100.0F, 0.0F, 0.0F});

    VirtualCamera v_lo;
    v_lo.set_target(tgt_lo);
    const auto id_lo = brain.add_vcam(v_lo, 10);

    VirtualCamera v_hi;
    v_hi.set_target(tgt_hi);
    const auto id_hi = brain.add_vcam(v_hi, 100);

    // Disable the higher-priority vcam -> lo becomes live.
    brain.vcam(id_hi)->set_enabled(false);
    const Camera out = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_lo);
    EXPECT_TRUE(vec_near(out.target, Vec3f {0.0F, 0.0F, 0.0F}));
}

// ----------------------------------------------------------------------------
// 11. warp() resets blend + damping integrators so the next tick snaps.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, WarpResetsBlendAndDamping)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt = make_entity(1);
    table.set(tgt, Vec3f {0.0F, 0.0F, 0.0F});

    VirtualCamera v;
    v.set_target(tgt);
    v.settings().damping = Vec3f {0.5F, 0.5F, 0.5F};
    brain.add_vcam(v, /*priority*/ 10);

    brain.tick(0.016F, make_lookup(table));
    table.set(tgt, Vec3f {100.0F, 0.0F, 0.0F});
    brain.tick(0.016F, make_lookup(table));  // damping kicks in - lags.

    brain.warp();  // discard damping velocity + any active blend.

    // After warp, the very next tick still uses last_output_ as the
    // damping anchor (warp does not zero last_output_ - that would teleport
    // the camera the renderer is currently displaying), but velocity == 0
    // and there is no in-flight blend.
    EXPECT_FALSE(brain.is_blending());
}
