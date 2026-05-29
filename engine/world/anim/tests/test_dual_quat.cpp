// =============================================================================
// CHROMODYNAMIC — cd::anim::DualQuat tests
// Phase D-F10 — Kavan 2008 Dual-Quaternion Skinning
//
// Kavan et al. 2008 "Geometric Skinning with Approximate Dual Quaternion
// Blending" (ACM TOG 27:4, DOI: 10.1145/1409625.1409627).
//
// Test cases
// ----------
//  1. Round-trip: from_rigid(R, t) → to_mat4 → compare translation column +
//     rotation block to the originals.
//  2. from_mat4 round-trip: build a pure-rotation Mat4, convert to DualQuat,
//     back to Mat4; all 16 elements within FP precision.
//  3. Identity blend: blend of a single DQ with weight 1 is itself.
//  4. Antipodality: blending q and (-q) does NOT cancel — the sign flip
//     in blend() keeps the result meaningful.
//  5. Blend at weight=0/1 endpoints reproduces the respective DQ.
//  6. Volume preservation at 90° elbow: DQS preserves the vertex distance
//     from the joint axis better than the LBS equivalent on a simple
//     2-joint 90° bend scenario.
// =============================================================================
#include <cd/anim/DualQuat.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{

constexpr float kEps = 1e-4F;

// Build a 4x4 from rotation quaternion + translation (column-major, no scale).
cd::math::Mat4f rigid_mat4(const cd::math::Quatf& rot,
                            const cd::math::Vec3f& trans)
{
    cd::math::Transformf xf;
    xf.position = trans;
    xf.rotation = rot;
    xf.scale    = { 1.0F, 1.0F, 1.0F };
    return cd::math::to_mat4(xf);
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. from_rigid → to_mat4 round-trip
// ---------------------------------------------------------------------------
TEST(DualQuat, FromRigidToMat4RoundTrip)
{
    // 90° rotation around Y, translate by (3, 1, -2).
    const cd::math::Quatf rot =
        cd::math::Quatf::from_axis_angle({ 0.0F, 1.0F, 0.0F },
                                          cd::math::pi_v<float> * 0.5F);
    const cd::math::Vec3f trans { 3.0F, 1.0F, -2.0F };

    const cd::anim::DualQuatf dq = cd::anim::from_rigid(rot, trans);
    const cd::math::Mat4f m = cd::anim::to_mat4(dq);

    // Translation column (column 3).
    EXPECT_NEAR(m[3][0], trans.x, kEps);
    EXPECT_NEAR(m[3][1], trans.y, kEps);
    EXPECT_NEAR(m[3][2], trans.z, kEps);
    EXPECT_NEAR(m[3][3], 1.0F,    kEps);

    // Rotation: compare upper-left 3×3 to the reference matrix built from
    // the same quaternion.
    const cd::math::Mat4f ref = rigid_mat4(rot, trans);
    for (std::size_t c = 0; c < 3; ++c)
        for (std::size_t r = 0; r < 3; ++r)
            EXPECT_NEAR(m[c][r], ref[c][r], kEps)
                << "col=" << c << " row=" << r;
}

// ---------------------------------------------------------------------------
// 2. from_mat4 → to_mat4 round-trip (pure rotation, no translation)
// ---------------------------------------------------------------------------
TEST(DualQuat, FromMat4ToMat4RoundTrip)
{
    const cd::math::Quatf rot =
        cd::math::Quatf::from_axis_angle({ 1.0F, 0.0F, 0.0F },
                                          cd::math::pi_v<float> / 3.0F);  // 60°
    const cd::math::Mat4f src = rigid_mat4(rot, { 0.0F, 0.0F, 0.0F });
    const cd::anim::DualQuatf dq = cd::anim::from_mat4(src);
    const cd::math::Mat4f out = cd::anim::to_mat4(dq);

    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(out[c][r], src[c][r], kEps)
                << "col=" << c << " row=" << r;
}

// ---------------------------------------------------------------------------
// 3. Identity blend: single weight=1 DQ unchanged
// ---------------------------------------------------------------------------
TEST(DualQuat, BlendSingleWeightOneIsIdentity)
{
    const cd::math::Quatf rot =
        cd::math::Quatf::from_axis_angle({ 0.0F, 0.0F, 1.0F },
                                          cd::math::pi_v<float> * 0.25F);
    const cd::anim::DualQuatf dq = cd::anim::from_rigid(rot, { 5.0F, 0.0F, 0.0F });

    const std::array<float, 1>               weights { 1.0F };
    const std::array<cd::anim::DualQuatf, 1> dqs     { dq    };

    const cd::anim::DualQuatf blended = cd::anim::blend(
        std::span<const float>               { weights.data(), 1 },
        std::span<const cd::anim::DualQuatf> { dqs.data(),     1 });

    const cd::math::Mat4f expected = cd::anim::to_mat4(dq);
    const cd::math::Mat4f got      = cd::anim::to_mat4(blended);

    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(got[c][r], expected[c][r], kEps)
                << "col=" << c << " row=" << r;
}

// ---------------------------------------------------------------------------
// 4. Antipodality: q and -q blend should NOT cancel to near-zero
// ---------------------------------------------------------------------------
// If the antipodality flip is absent, the two terms cancel and the
// result collapses.  With the flip, the blended DQ stays near q.
TEST(DualQuat, AntipodablityFlipPreventsCancellation)
{
    const cd::math::Quatf rot =
        cd::math::Quatf::from_axis_angle({ 0.0F, 1.0F, 0.0F },
                                          cd::math::pi_v<float> * 0.3F);
    const cd::anim::DualQuatf dq_pos = cd::anim::from_rigid(rot, { 0.0F, 0.0F, 0.0F });
    // Negate both parts — same rigid transform, antipodal DQ.
    cd::anim::DualQuatf dq_neg = dq_pos;
    dq_neg.real.x = -dq_neg.real.x;
    dq_neg.real.y = -dq_neg.real.y;
    dq_neg.real.z = -dq_neg.real.z;
    dq_neg.real.w = -dq_neg.real.w;
    dq_neg.dual.x = -dq_neg.dual.x;
    dq_neg.dual.y = -dq_neg.dual.y;
    dq_neg.dual.z = -dq_neg.dual.z;
    dq_neg.dual.w = -dq_neg.dual.w;

    const std::array<float, 2>               weights { 0.5F, 0.5F };
    const std::array<cd::anim::DualQuatf, 2> dqs     { dq_pos, dq_neg };

    const cd::anim::DualQuatf blended = cd::anim::blend(
        std::span<const float>               { weights.data(), 2 },
        std::span<const cd::anim::DualQuatf> { dqs.data(),     2 });

    // The real part must not be near zero — both inputs encode the same
    // rotation, so blended rotation should match.
    const float len = std::sqrt(blended.real.x * blended.real.x
                               + blended.real.y * blended.real.y
                               + blended.real.z * blended.real.z
                               + blended.real.w * blended.real.w);
    EXPECT_GT(len, 0.9F)
        << "Antipodality fix must prevent DQ cancellation; |real| = " << len;
}

// ---------------------------------------------------------------------------
// 5. Blend endpoint at w=[1,0] returns first; at w=[0,1] returns second
// ---------------------------------------------------------------------------
TEST(DualQuat, BlendEndpointsReturnRespectiveDQ)
{
    const cd::math::Quatf r0 =
        cd::math::Quatf::from_axis_angle({ 1.0F, 0.0F, 0.0F }, 0.0F);
    const cd::math::Quatf r1 =
        cd::math::Quatf::from_axis_angle({ 0.0F, 0.0F, 1.0F },
                                          cd::math::pi_v<float> * 0.5F);
    const cd::anim::DualQuatf dq0 = cd::anim::from_rigid(r0, { 1.0F, 0.0F, 0.0F });
    const cd::anim::DualQuatf dq1 = cd::anim::from_rigid(r1, { 0.0F, 2.0F, 0.0F });

    const std::array<cd::anim::DualQuatf, 2> dqs { dq0, dq1 };

    {
        const std::array<float, 2> w { 1.0F, 0.0F };
        const cd::anim::DualQuatf b = cd::anim::blend(
            std::span<const float>               { w.data(),   2 },
            std::span<const cd::anim::DualQuatf> { dqs.data(), 2 });
        const cd::math::Mat4f got = cd::anim::to_mat4(b);
        const cd::math::Mat4f exp = cd::anim::to_mat4(dq0);
        // Translation column must match dq0 translation.
        EXPECT_NEAR(got[3][0], exp[3][0], kEps);
        EXPECT_NEAR(got[3][1], exp[3][1], kEps);
        EXPECT_NEAR(got[3][2], exp[3][2], kEps);
    }
    {
        const std::array<float, 2> w { 0.0F, 1.0F };
        const cd::anim::DualQuatf b = cd::anim::blend(
            std::span<const float>               { w.data(),   2 },
            std::span<const cd::anim::DualQuatf> { dqs.data(), 2 });
        const cd::math::Mat4f got = cd::anim::to_mat4(b);
        const cd::math::Mat4f exp = cd::anim::to_mat4(dq1);
        EXPECT_NEAR(got[3][0], exp[3][0], kEps);
        EXPECT_NEAR(got[3][1], exp[3][1], kEps);
        EXPECT_NEAR(got[3][2], exp[3][2], kEps);
    }
}

// ---------------------------------------------------------------------------
// 6. Volume preservation: DQS vs LBS on a 90° two-joint elbow bend
// ---------------------------------------------------------------------------
// Setup: joint0 at origin (no rotation), joint1 at (1,0,0) rotated 90° around
// Z. A vertex at the midpoint (0.5, 0, 0) is influenced 50/50 by both joints.
//
// LBS: the midpoint collapses toward the joint axis (candy-wrapper collapse).
//      The x coordinate of the skinned vertex shrinks significantly.
//
// DQS: preserves the vertex distance from the joint axis better because
//      it interpolates in the rigid-motion space (geodesic approximation)
//      rather than in the matrix space.
//
// Measurable prediction (Kavan 2008 §5, Fig. 3):
//   At exactly 90°, the DQS x-coordinate at the 50/50 midpoint is
//   noticeably larger than LBS's — LBS collapses, DQS does not.
TEST(DualQuat, VolumePreservationVsLBS_90DegElbow)
{
    // Joint 0: identity (no transform)
    const cd::math::Quatf r0 = cd::math::Quatf::identity();
    const cd::math::Vec3f t0 { 0.0F, 0.0F, 0.0F };

    // Joint 1: 90° rotation around Z axis, positioned at (1, 0, 0).
    // The skinning matrix is: world_pose * inverse_bind.
    // For simplicity treat both joints as having identity inverse-bind
    // (bind pose = identity). Joint 1 skinning matrix = rotation(90°, Z).
    const cd::math::Quatf r1 =
        cd::math::Quatf::from_axis_angle({ 0.0F, 0.0F, 1.0F },
                                          cd::math::pi_v<float> * 0.5F);
    const cd::math::Vec3f t1 { 0.0F, 0.0F, 0.0F };

    // Vertex in bind pose: midpoint between the two joints.
    const cd::math::Vec3f vtx_bind { 0.5F, 0.0F, 0.0F };

    // ---- DQS blend ----
    const cd::anim::DualQuatf dq0 = cd::anim::from_rigid(r0, t0);
    const cd::anim::DualQuatf dq1 = cd::anim::from_rigid(r1, t1);

    const std::array<float, 2>               w   { 0.5F, 0.5F };
    const std::array<cd::anim::DualQuatf, 2> dqs { dq0, dq1 };

    const cd::anim::DualQuatf blended_dq = cd::anim::blend(
        std::span<const float>               { w.data(),   2 },
        std::span<const cd::anim::DualQuatf> { dqs.data(), 2 });
    const cd::math::Mat4f dq_mat = cd::anim::to_mat4(blended_dq);

    // ---- LBS blend ----
    const cd::math::Mat4f m0 = rigid_mat4(r0, t0);
    const cd::math::Mat4f m1 = rigid_mat4(r1, t1);
    cd::math::Mat4f lbs_mat {};
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            lbs_mat[c][r] = 0.5F * m0[c][r] + 0.5F * m1[c][r];

    // Transform the vertex using both methods.
    const auto transform_pt = [](const cd::math::Mat4f& m,
                                  const cd::math::Vec3f& p) -> cd::math::Vec3f
    {
        return {
            m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
            m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
            m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2]
        };
    };

    const cd::math::Vec3f v_dqs = transform_pt(dq_mat,  vtx_bind);
    const cd::math::Vec3f v_lbs = transform_pt(lbs_mat, vtx_bind);

    // Distance from origin — DQS should preserve it better.
    const float d_dqs = std::sqrt(v_dqs.x * v_dqs.x + v_dqs.y * v_dqs.y
                                 + v_dqs.z * v_dqs.z);
    const float d_lbs = std::sqrt(v_lbs.x * v_lbs.x + v_lbs.y * v_lbs.y
                                 + v_lbs.z * v_lbs.z);

    // The bind-pose vertex is at distance 0.5 from origin. LBS shrinks it;
    // DQS preserves it (Kavan 2008 Fig. 3: DQS significantly less collapse
    // than LBS at 90° — LBS can lose 10-30% of the distance).
    EXPECT_GT(d_dqs, d_lbs)
        << "DQS must preserve vertex distance better than LBS at 90° bend. "
           "d_dqs=" << d_dqs << " d_lbs=" << d_lbs;

    // DQS distance should be within 5% of the bind-pose distance.
    EXPECT_NEAR(d_dqs, 0.5F, 0.025F)
        << "DQS elbow distance should be close to bind-pose 0.5; got " << d_dqs;
}
