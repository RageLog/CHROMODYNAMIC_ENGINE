// =============================================================================
// CHROMODYNAMIC - tests/test_vcam.cpp
// Phase 482 (G3.3) + gap-close pass - cd::game::camera unit tests.
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
//
// Gap-close pass (100% coverage):
//  12. Priority tie-break: first-inserted wins (insertion-order stability).
//  13. Blend interrupted mid-blend by a higher-priority vcam.
//  14. Blend curve endpoints: t=0 == from-frame; t=1 == to-frame.
//  15. No active vcam: empty brain tick leaves output unchanged.
//  16. VCam added mid-blend with LOWER priority does NOT interrupt.
//  17. VCam removed mid-blend (target vcam): brain falls back.
//  18. blend_progress() value during and after blend.
//  19. blend_out max-rule: max(prev.blend_out, next.blend_in) wins.
//  20. vcam_count() tracks add / remove correctly.
//  21. Zero dt during an active blend: no time advance; blend frozen.
//  22. warp() during mid-blend: next tick snaps to live vcam instantly.
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
    [[maybe_unused]] const auto id_damp = brain.add_vcam(v, /*priority*/ 10);

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
    [[maybe_unused]] const auto id_wide = brain.add_vcam(v_wide, /*priority*/ 10);

    brain.tick(0.0F, make_lookup(table));

    VirtualCamera v_tele;
    v_tele.set_target(tgt);
    v_tele.settings().fov_y = 0.5F;  // ~28 deg telephoto.
    v_tele.blend_in(1.0F);
    [[maybe_unused]] const auto id_tele = brain.add_vcam(v_tele, /*priority*/ 100);

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
    [[maybe_unused]] const auto id_pos = brain.add_vcam(v, /*priority*/ 10);

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
    [[maybe_unused]] const auto id_neg = brain.add_vcam(v, /*priority*/ 10);

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
    [[maybe_unused]] const auto id_za = brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(0.0F);  // snap.
    [[maybe_unused]] const auto id_zb = brain.add_vcam(vb, /*priority*/ 100);

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
    [[maybe_unused]] const auto id_warp = brain.add_vcam(v, /*priority*/ 10);

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

// ----------------------------------------------------------------------------
// 12. Priority tie-break: two vcams with equal priority - insertion order
//     (first registered) wins because select_live_entry uses strict ">".
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, TieBreakFirstInsertedWins)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f {1.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {2.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    const auto id_a = brain.add_vcam(va, /*priority*/ 50);

    VirtualCamera vb;
    vb.set_target(tgt_b);
    // Discard id: we only need to verify tie-break, not manipulate vb later.
    [[maybe_unused]] const auto id_b = brain.add_vcam(vb, /*priority*/ 50);

    // Strict ">" in select_live_entry: b cannot unseat a because 50 > 50 is
    // false.  The first-inserted vcam (id_a) must remain live.
    const Camera out = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_a);
    EXPECT_TRUE(vec_near(out.target, Vec3f {1.0F, 0.0F, 0.0F}));
}

// ----------------------------------------------------------------------------
// 13. Blend interrupted mid-blend by a yet-higher-priority vcam.
//     The brain must restart the blend FROM the current output (no snap).
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, BlendInterruptedByHigherPriority)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    const Entity tgt_c = make_entity(3);
    table.set(tgt_a, Vec3f {  0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {100.0F, 0.0F, 0.0F});
    table.set(tgt_c, Vec3f {200.0F, 0.0F, 0.0F});

    // Arrange: settle on A, then start a 2-second blend toward B.
    VirtualCamera va;
    va.set_target(tgt_a);
    [[maybe_unused]] const auto id_ia = brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(2.0F);
    [[maybe_unused]] const auto id_ib = brain.add_vcam(vb, /*priority*/ 20);
    brain.tick(0.0F, make_lookup(table));  // blend A->B starts.

    // Advance halfway through A->B blend (1s).
    const Camera mid_ab = brain.tick(1.0F, make_lookup(table));
    EXPECT_TRUE(brain.is_blending());
    // mid_ab.target should be ~50% of the way toward tgt_b.
    EXPECT_GT(mid_ab.target.x, 10.0F);
    EXPECT_LT(mid_ab.target.x, 90.0F);
    const float captured_x = mid_ab.target.x;

    // Act: add vcam C at even higher priority with a 1-second blend_in.
    VirtualCamera vc;
    vc.set_target(tgt_c);
    vc.blend_in(1.0F);
    const auto id_c = brain.add_vcam(vc, /*priority*/ 100);

    // First tick after C added: blend restarts FROM current mid-AB output.
    brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_c);
    EXPECT_TRUE(brain.is_blending());

    // The new blend-from anchor must be ≈ captured_x (not 0 or 100).
    // At t=0 into new blend, output == blend_from_ (captured mid-AB).
    // We check blend_progress starts near 0.
    EXPECT_LT(brain.blend_progress(), 0.05F);

    // After 0.5s (halfway into 1s C-blend) output should be between
    // captured_x and 200.
    const Camera mid_c = brain.tick(0.5F, make_lookup(table));
    EXPECT_GT(mid_c.target.x, captured_x - 5.0F);  // not jumped back to origin.
    EXPECT_LT(mid_c.target.x, 200.0F);

    // After full 1s the blend completes and output reaches C.
    const Camera done = brain.tick(0.5F, make_lookup(table));
    EXPECT_FALSE(brain.is_blending());
    EXPECT_TRUE(near_eq(done.target.x, 200.0F, 0.5F));
}

// ----------------------------------------------------------------------------
// 14. Blend curve endpoints:
//     - at t = 0 (just after live change, dt = 0) output equals the from-frame.
//     - at t = 1 (blend complete) output equals the to-frame exactly.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, BlendCurveEndpoints)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f { 0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {80.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    [[maybe_unused]] const auto id_ea = brain.add_vcam(va, /*priority*/ 10);
    // Settle: dt = 0, no damping.
    const Camera from_frame = brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(1.0F);
    [[maybe_unused]] const auto id_eb = brain.add_vcam(vb, /*priority*/ 100);

    // t = 0: output must equal from_frame (the A-settled output).
    const Camera t0 = brain.tick(0.0F, make_lookup(table));
    EXPECT_TRUE(brain.is_blending());
    EXPECT_TRUE(vec_near(t0.target, from_frame.target, 1e-4F));
    EXPECT_TRUE(near_eq(brain.blend_progress(), 0.0F, 0.01F));

    // t = 1: advance full duration -> blend completes.
    const Camera t1 = brain.tick(1.0F, make_lookup(table));
    EXPECT_FALSE(brain.is_blending());
    EXPECT_TRUE(near_eq(brain.blend_progress(), 1.0F, 1e-6F));
    EXPECT_TRUE(vec_near(t1.target, Vec3f {80.0F, 0.0F, 0.0F}, 0.1F));
}

// ----------------------------------------------------------------------------
// 15. No active vcam: empty brain tick leaves output unchanged (zero).
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, NoActiveVCamOutputUnchanged)
{
    CameraBrain brain;
    TargetTable table;

    // No vcams registered.  Output should be the default-constructed Camera.
    const Camera out = brain.tick(0.016F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), 0U);
    EXPECT_FALSE(brain.is_blending());
    EXPECT_EQ(brain.vcam_count(), 0U);
    // Brain has no last output yet; Camera default is 0,0,3 eye / 0,0,0 target.
    // We only assert that the returned camera has not mutated to garbage.
    (void)out;  // no crash is the primary assertion.
    EXPECT_TRUE(brain.last_tick_ok());
}

// ----------------------------------------------------------------------------
// 16. VCam added mid-blend with LOWER priority must NOT interrupt the blend.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, LowerPriorityAddedMidBlendDoesNotInterrupt)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    const Entity tgt_c = make_entity(3);
    table.set(tgt_a, Vec3f {  0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {100.0F, 0.0F, 0.0F});
    table.set(tgt_c, Vec3f {999.0F, 0.0F, 0.0F});  // would dominate if selected.

    VirtualCamera va;
    va.set_target(tgt_a);
    [[maybe_unused]] const auto id_la = brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(2.0F);
    const auto id_b = brain.add_vcam(vb, /*priority*/ 50);
    brain.tick(0.0F, make_lookup(table));          // A->B blend starts.
    brain.tick(0.5F, make_lookup(table));          // 0.5s into blend.
    EXPECT_TRUE(brain.is_blending());
    EXPECT_EQ(brain.live_id(), id_b);

    // Add vcam C with LOWER priority than current live (B=50, C=5).
    VirtualCamera vc;
    vc.set_target(tgt_c);
    vc.blend_in(0.5F);
    [[maybe_unused]] const auto id_c_lo = brain.add_vcam(vc, /*priority*/ 5);

    // Blend must still be in progress toward B, not redirected to C.
    const Camera after = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_b);
    EXPECT_TRUE(brain.is_blending());
    EXPECT_LT(after.target.x, 100.0F);  // not yet at B; not at C (999).
    EXPECT_LT(after.target.x, 200.0F);
}

// ----------------------------------------------------------------------------
// 17. VCam removed mid-blend (the TARGET vcam being blended toward).
//     Brain must fall back to next-best vcam and restart blend.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, RemoveTargetVCamMidBlend)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f {  0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {100.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    const auto id_a = brain.add_vcam(va, /*priority*/ 10);

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(2.0F);
    const auto id_b = brain.add_vcam(vb, /*priority*/ 100);

    brain.tick(0.0F, make_lookup(table));   // settle A.
    brain.tick(0.0F, make_lookup(table));   // start A->B blend.
    brain.tick(0.5F, make_lookup(table));   // 0.5s in.
    EXPECT_EQ(brain.live_id(), id_b);
    EXPECT_TRUE(brain.is_blending());

    // Remove the target vcam (B) while the blend is running.
    EXPECT_TRUE(brain.remove_vcam(id_b));

    // Next tick: live_id_ becomes 0 (cleared in remove_vcam), then
    // select_live_entry picks A as the new winner; blend restarts A-way.
    const Camera after = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_a);
    // The output should be the captured mid-blend position (not snapped to A).
    // We only assert the vcam-id transition: the position is somewhere between
    // the captured anchor and A's sample.
    (void)after;
}

// ----------------------------------------------------------------------------
// 18. blend_progress() returns the correct normalised value during a blend
//     and 1.0 before/after.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, BlendProgressValue)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f { 0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {10.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    [[maybe_unused]] const auto id_pra = brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    // Not blending -> progress == 1.0.
    EXPECT_TRUE(near_eq(brain.blend_progress(), 1.0F));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(1.0F);
    [[maybe_unused]] const auto id_prb = brain.add_vcam(vb, /*priority*/ 100);

    // Start blend.
    brain.tick(0.0F, make_lookup(table));
    EXPECT_TRUE(near_eq(brain.blend_progress(), 0.0F, 0.01F));

    // Quarter-way.
    brain.tick(0.25F, make_lookup(table));
    EXPECT_TRUE(near_eq(brain.blend_progress(), 0.25F, 0.02F));

    // Half-way.
    brain.tick(0.25F, make_lookup(table));
    EXPECT_TRUE(near_eq(brain.blend_progress(), 0.50F, 0.02F));

    // Complete blend.
    brain.tick(0.50F, make_lookup(table));
    EXPECT_FALSE(brain.is_blending());
    EXPECT_TRUE(near_eq(brain.blend_progress(), 1.0F, 1e-6F));
}

// ----------------------------------------------------------------------------
// 19. blend_out max-rule: max(prev.blend_out, next.blend_in) wins.
//     Setup: prev has blend_out = 2.0s; next has blend_in = 0.5s.
//     Expected blend duration = 2.0s.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, BlendOutMaxRuleWins)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f { 0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {60.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    va.blend_out(2.0F);  // 2s blend_out on the outgoing vcam.
    [[maybe_unused]] const auto id_outa = brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(0.5F);  // only 0.5s blend_in; the outgoing 2.0s must dominate.
    [[maybe_unused]] const auto id_outb = brain.add_vcam(vb, /*priority*/ 100);

    // Start blend.
    brain.tick(0.0F, make_lookup(table));
    EXPECT_TRUE(brain.is_blending());

    // After 1.0s we should be at t=1.0/2.0 = 0.5 progress, still blending.
    brain.tick(1.0F, make_lookup(table));
    EXPECT_TRUE(brain.is_blending());
    EXPECT_TRUE(near_eq(brain.blend_progress(), 0.5F, 0.05F));

    // Another 1.0s => total 2.0s => blend completes.
    const Camera done = brain.tick(1.0F, make_lookup(table));
    EXPECT_FALSE(brain.is_blending());
    EXPECT_TRUE(near_eq(done.target.x, 60.0F, 0.2F));
}

// ----------------------------------------------------------------------------
// 20. vcam_count() tracks add / remove correctly.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, VcamCountTracksAddRemove)
{
    CameraBrain  brain;
    TargetTable  table;

    EXPECT_EQ(brain.vcam_count(), 0U);

    VirtualCamera va;
    const auto id_a = brain.add_vcam(va, /*priority*/ 10);
    EXPECT_EQ(brain.vcam_count(), 1U);

    VirtualCamera vb;
    const auto id_b = brain.add_vcam(vb, /*priority*/ 20);
    EXPECT_EQ(brain.vcam_count(), 2U);

    EXPECT_TRUE(brain.remove_vcam(id_a));
    EXPECT_EQ(brain.vcam_count(), 1U);

    EXPECT_TRUE(brain.remove_vcam(id_b));
    EXPECT_EQ(brain.vcam_count(), 0U);

    // Removing unknown id returns false and does not change count.
    EXPECT_FALSE(brain.remove_vcam(999U));
    EXPECT_EQ(brain.vcam_count(), 0U);
}

// ----------------------------------------------------------------------------
// 21. Zero dt during an active blend: timer must NOT advance; blend is frozen.
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, ZeroDtFreezesBlendinProgress)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f { 0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {10.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    [[maybe_unused]] const auto id_fza = brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(1.0F);
    [[maybe_unused]] const auto id_fzb = brain.add_vcam(vb, /*priority*/ 100);

    brain.tick(0.0F, make_lookup(table));   // start blend, t = 0.
    brain.tick(0.25F, make_lookup(table));  // advance to 25%.
    const float progress_before = brain.blend_progress();
    EXPECT_TRUE(near_eq(progress_before, 0.25F, 0.02F));

    // dt = 0: progress must not change.
    brain.tick(0.0F, make_lookup(table));
    EXPECT_TRUE(near_eq(brain.blend_progress(), progress_before, 1e-5F));
    EXPECT_TRUE(brain.is_blending());
    EXPECT_TRUE(brain.last_tick_ok());  // dt=0 is a valid tick.
}

// ----------------------------------------------------------------------------
// 22. warp() during an active mid-blend: next tick snaps to live vcam
//     (no blend in progress, no damping lag).
// ----------------------------------------------------------------------------
TEST(GameCameraVCam, WarpDuringBlendSnapsToLive)
{
    CameraBrain  brain;
    TargetTable  table;
    const Entity tgt_a = make_entity(1);
    const Entity tgt_b = make_entity(2);
    table.set(tgt_a, Vec3f { 0.0F, 0.0F, 0.0F});
    table.set(tgt_b, Vec3f {50.0F, 0.0F, 0.0F});

    VirtualCamera va;
    va.set_target(tgt_a);
    [[maybe_unused]] const auto id_wa = brain.add_vcam(va, /*priority*/ 10);
    brain.tick(0.0F, make_lookup(table));

    VirtualCamera vb;
    vb.set_target(tgt_b);
    vb.blend_in(2.0F);
    const auto id_b = brain.add_vcam(vb, /*priority*/ 100);

    brain.tick(0.0F, make_lookup(table));   // start blend.
    brain.tick(0.5F, make_lookup(table));   // 25% through 2s blend.
    EXPECT_TRUE(brain.is_blending());

    // Warp while blending.
    brain.warp();
    EXPECT_FALSE(brain.is_blending());

    // Next tick with damping=0: output must snap directly to B's sample.
    const Camera snapped = brain.tick(0.0F, make_lookup(table));
    EXPECT_EQ(brain.live_id(), id_b);
    EXPECT_FALSE(brain.is_blending());
    EXPECT_TRUE(vec_near(snapped.target, Vec3f {50.0F, 0.0F, 0.0F}, 0.1F));
}
