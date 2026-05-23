// =============================================================================
// CHROMODYNAMIC — cd::math tests (Sprint S2.7)
// =============================================================================
#include <cd/math/Constants.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <gtest/gtest.h>

namespace
{

// --- Constants --------------------------------------------------------------
TEST(MathConstants, PiTauHalfPi)
{
    EXPECT_NEAR(cd::math::pi, 3.14159265f, 1e-6f);
    EXPECT_NEAR(cd::math::tau, 6.28318530f, 1e-6f);
    EXPECT_NEAR(cd::math::half_pi, 1.57079632f, 1e-6f);
}

TEST(MathConstants, DoublePrecisionPi)
{
    EXPECT_NEAR(cd::math::pi_d, 3.141592653589793, 1e-15);
}

// --- Functions --------------------------------------------------------------
TEST(MathFunctions, DegRadRoundTrip)
{
    EXPECT_NEAR(cd::math::deg_to_rad(180.0f), cd::math::pi, 1e-6f);
    EXPECT_NEAR(cd::math::rad_to_deg(cd::math::pi), 180.0f, 1e-4f);
}

TEST(MathFunctions, ClampSaturate)
{
    EXPECT_EQ(cd::math::clamp(5, 0, 10), 5);
    EXPECT_EQ(cd::math::clamp(-3, 0, 10), 0);
    EXPECT_EQ(cd::math::clamp(99, 0, 10), 10);
    EXPECT_FLOAT_EQ(cd::math::saturate(1.5f), 1.0f);
    EXPECT_FLOAT_EQ(cd::math::saturate(-0.2f), 0.0f);
    EXPECT_FLOAT_EQ(cd::math::saturate(0.3f), 0.3f);
}

TEST(MathFunctions, LerpAndInverse)
{
    EXPECT_FLOAT_EQ(cd::math::lerp(0.0f, 10.0f, 0.5f), 5.0f);
    EXPECT_FLOAT_EQ(cd::math::lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(cd::math::lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(cd::math::inverse_lerp(0.0f, 10.0f, 5.0f), 0.5f);
}

TEST(MathFunctions, Remap)
{
    EXPECT_FLOAT_EQ(cd::math::remap(0.0f, 10.0f, -1.0f, 1.0f, 5.0f), 0.0f);
    EXPECT_FLOAT_EQ(cd::math::remap(0.0f, 10.0f, 100.0f, 200.0f, 7.5f), 175.0f);
}

TEST(MathFunctions, Smoothstep)
{
    EXPECT_FLOAT_EQ(cd::math::smoothstep(0.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(cd::math::smoothstep(0.0f, 1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(cd::math::smoothstep(0.0f, 1.0f, 0.5f), 0.5f);
    // Slope at endpoints must be zero (smoothstep property): bracket sampling.
    EXPECT_LT(cd::math::smoothstep(0.0f, 1.0f, 0.01f), 0.01f);
}

TEST(MathFunctions, ApproxEqual)
{
    EXPECT_TRUE(cd::math::approx_equal(1.0f, 1.0f + cd::math::epsilon));
    EXPECT_FALSE(cd::math::approx_equal(1.0f, 1.001f));
}

TEST(MathFunctions, Sign)
{
    EXPECT_EQ(cd::math::sign(5), 1);
    EXPECT_EQ(cd::math::sign(-5), -1);
    EXPECT_EQ(cd::math::sign(0), 0);
    EXPECT_EQ(cd::math::sign(-0.0f), 0);
}

// --- Vec arithmetic ---------------------------------------------------------
TEST(MathVec, Construction)
{
    cd::math::Vec2f a { 1.0f, 2.0f };
    EXPECT_FLOAT_EQ(a.x, 1.0f);
    EXPECT_FLOAT_EQ(a.y, 2.0f);

    cd::math::Vec3f b { a, 3.0f };
    EXPECT_FLOAT_EQ(b.z, 3.0f);

    cd::math::Vec4f c { b, 4.0f };
    EXPECT_FLOAT_EQ(c.w, 4.0f);

    cd::math::Vec3f filled { 7.0f };
    EXPECT_FLOAT_EQ(filled.x, 7.0f);
    EXPECT_FLOAT_EQ(filled.y, 7.0f);
    EXPECT_FLOAT_EQ(filled.z, 7.0f);
}

TEST(MathVec, IndexAccess)
{
    cd::math::Vec3f v { 1.0f, 2.0f, 3.0f };
    EXPECT_FLOAT_EQ(v[0], 1.0f);
    EXPECT_FLOAT_EQ(v[1], 2.0f);
    EXPECT_FLOAT_EQ(v[2], 3.0f);
    v[1] = 5.0f;
    EXPECT_FLOAT_EQ(v.y, 5.0f);
}

TEST(MathVec, ArithmeticBinaryOps)
{
    cd::math::Vec3f a { 1.0f, 2.0f, 3.0f };
    cd::math::Vec3f b { 4.0f, 5.0f, 6.0f };
    EXPECT_EQ(a + b, (cd::math::Vec3f { 5.0f, 7.0f, 9.0f }));
    EXPECT_EQ(b - a, (cd::math::Vec3f { 3.0f, 3.0f, 3.0f }));
    EXPECT_EQ(a * 2.0f, (cd::math::Vec3f { 2.0f, 4.0f, 6.0f }));
    EXPECT_EQ(2.0f * a, (cd::math::Vec3f { 2.0f, 4.0f, 6.0f }));
    EXPECT_EQ(b / 2.0f, (cd::math::Vec3f { 2.0f, 2.5f, 3.0f }));
    EXPECT_EQ(-a, (cd::math::Vec3f { -1.0f, -2.0f, -3.0f }));
}

TEST(MathVec, CompoundAssignment)
{
    cd::math::Vec3f a { 1.0f, 2.0f, 3.0f };
    a += cd::math::Vec3f { 1.0f, 1.0f, 1.0f };
    EXPECT_EQ(a, (cd::math::Vec3f { 2.0f, 3.0f, 4.0f }));
    a *= 2.0f;
    EXPECT_EQ(a, (cd::math::Vec3f { 4.0f, 6.0f, 8.0f }));
}

TEST(MathVec, Dot)
{
    cd::math::Vec3f a { 1.0f, 2.0f, 3.0f };
    cd::math::Vec3f b { 4.0f, -5.0f, 6.0f };
    // 1*4 + 2*(-5) + 3*6 = 4 - 10 + 18 = 12
    EXPECT_FLOAT_EQ(cd::math::dot(a, b), 12.0f);
}

TEST(MathVec, Cross)
{
    cd::math::Vec3f x { 1.0f, 0.0f, 0.0f };
    cd::math::Vec3f y { 0.0f, 1.0f, 0.0f };
    cd::math::Vec3f z { 0.0f, 0.0f, 1.0f };
    EXPECT_EQ(cd::math::cross(x, y), z);
    EXPECT_EQ(cd::math::cross(y, z), x);
    EXPECT_EQ(cd::math::cross(z, x), y);
}

TEST(MathVec, LengthAndNormalize)
{
    cd::math::Vec3f v { 3.0f, 4.0f, 0.0f };
    EXPECT_FLOAT_EQ(cd::math::length_squared(v), 25.0f);
    EXPECT_FLOAT_EQ(cd::math::length(v), 5.0f);
    auto n = cd::math::normalize(v);
    EXPECT_TRUE(cd::math::approx_equal(cd::math::length(n), 1.0f, 1e-5f));
    EXPECT_TRUE(cd::math::approx_equal(n, cd::math::Vec3f { 0.6f, 0.8f, 0.0f }, 1e-5f));
}

TEST(MathVec, NormalizeZeroIsNoop)
{
    cd::math::Vec3f zero {};
    auto n = cd::math::normalize(zero);
    EXPECT_EQ(n, zero);
}

TEST(MathVec, LerpMinMax)
{
    cd::math::Vec3f a { 0.0f, 0.0f, 0.0f };
    cd::math::Vec3f b { 10.0f, 20.0f, 30.0f };
    EXPECT_EQ(cd::math::lerp(a, b, 0.5f), (cd::math::Vec3f { 5.0f, 10.0f, 15.0f }));

    cd::math::Vec3f p { 1.0f, 5.0f, 3.0f };
    cd::math::Vec3f q { 4.0f, 2.0f, 6.0f };
    EXPECT_EQ(cd::math::min(p, q), (cd::math::Vec3f { 1.0f, 2.0f, 3.0f }));
    EXPECT_EQ(cd::math::max(p, q), (cd::math::Vec3f { 4.0f, 5.0f, 6.0f }));
}

TEST(MathVec, Swizzles)
{
    cd::math::Vec4f v { 1.0f, 2.0f, 3.0f, 4.0f };
    EXPECT_EQ(v.xyz(), (cd::math::Vec3f { 1.0f, 2.0f, 3.0f }));
    EXPECT_EQ(v.xyz().xy(), (cd::math::Vec2f { 1.0f, 2.0f }));
}

TEST(MathVec, IntegerSpecialization)
{
    cd::math::Vec3i a { 1, 2, 3 };
    cd::math::Vec3i b { 10, 20, 30 };
    EXPECT_EQ(a + b, (cd::math::Vec3i { 11, 22, 33 }));
    EXPECT_EQ(cd::math::dot(a, b), 1 * 10 + 2 * 20 + 3 * 30);
}

TEST(MathVec, ConstexprUsable)
{
    constexpr cd::math::Vec3f a { 1.0f, 2.0f, 3.0f };
    constexpr cd::math::Vec3f b { 4.0f, 5.0f, 6.0f };
    constexpr auto sum = a + b;
    constexpr auto dotted = cd::math::dot(a, b);
    static_assert(sum.x == 5.0f);
    static_assert(dotted == 32.0f);
    EXPECT_FLOAT_EQ(dotted, 32.0f);
}

// --- Mat --------------------------------------------------------------------
TEST(MathMat, IdentityIsIdentity)
{
    auto I = cd::math::Mat4f::identity();
    cd::math::Vec4f v { 1.0f, 2.0f, 3.0f, 1.0f };
    EXPECT_EQ(I * v, v);

    auto M = cd::math::Mat4f::identity();
    EXPECT_EQ(I * M, I);
}

TEST(MathMat, Transpose)
{
    cd::math::Mat3f m {
        { 1.0f, 2.0f, 3.0f },
        { 4.0f, 5.0f, 6.0f },
        { 7.0f, 8.0f, 9.0f }
    };
    auto t = cd::math::transpose(m);
    // column 0 of m is {1,2,3}; column 0 of t should be {1,4,7}
    EXPECT_EQ(t[0], (cd::math::Vec3f { 1.0f, 4.0f, 7.0f }));
    EXPECT_EQ(t[1], (cd::math::Vec3f { 2.0f, 5.0f, 8.0f }));
    EXPECT_EQ(t[2], (cd::math::Vec3f { 3.0f, 6.0f, 9.0f }));
    // double-transpose is identity
    EXPECT_EQ(cd::math::transpose(t), m);
}

TEST(MathMat, TranslationApplies)
{
    auto T = cd::math::translation(cd::math::Vec3f { 10.0f, 20.0f, 30.0f });
    cd::math::Vec4f p { 1.0f, 2.0f, 3.0f, 1.0f };  // w=1 → point
    auto out = T * p;
    EXPECT_EQ(out, (cd::math::Vec4f { 11.0f, 22.0f, 33.0f, 1.0f }));

    // Direction vectors (w=0) must be unaffected by translation.
    cd::math::Vec4f d { 1.0f, 0.0f, 0.0f, 0.0f };
    EXPECT_EQ(T * d, d);
}

TEST(MathMat, ScalingApplies)
{
    auto S = cd::math::scaling(cd::math::Vec3f { 2.0f, 3.0f, 4.0f });
    cd::math::Vec4f p { 1.0f, 1.0f, 1.0f, 1.0f };
    EXPECT_EQ(S * p, (cd::math::Vec4f { 2.0f, 3.0f, 4.0f, 1.0f }));
}

TEST(MathMat, MultiplyAssociative)
{
    auto T = cd::math::translation(cd::math::Vec3f { 1.0f, 0.0f, 0.0f });
    auto S = cd::math::scaling(cd::math::Vec3f { 2.0f, 2.0f, 2.0f });
    // Affine: first scale, then translate — translation is applied last in
    // column-major convention as `T * S * p`.
    auto TS = T * S;
    cd::math::Vec4f p { 1.0f, 1.0f, 1.0f, 1.0f };
    EXPECT_EQ(TS * p, (cd::math::Vec4f { 3.0f, 2.0f, 2.0f, 1.0f }));
}

// --- Quat -------------------------------------------------------------------
TEST(MathQuat, IdentityRotation)
{
    auto q = cd::math::Quatf::identity();
    cd::math::Vec3f v { 1.0f, 2.0f, 3.0f };
    EXPECT_TRUE(cd::math::approx_equal(cd::math::rotate(q, v), v, 1e-5f));
}

TEST(MathQuat, AxisAngle90DegYRotatesXToMinusZ)
{
    // RH coordinate system, Y-up. 90° rotation about Y takes +X → -Z.
    auto q = cd::math::Quatf::from_axis_angle({ 0.0f, 1.0f, 0.0f }, cd::math::half_pi);
    cd::math::Vec3f x { 1.0f, 0.0f, 0.0f };
    auto rotated = cd::math::rotate(q, x);
    EXPECT_TRUE(cd::math::approx_equal(rotated, cd::math::Vec3f { 0.0f, 0.0f, -1.0f }, 1e-5f))
        << "got (" << rotated.x << "," << rotated.y << "," << rotated.z << ")";
}

TEST(MathQuat, AxisAngle180DegZInvertsXY)
{
    // 180° about Z negates X and Y.
    auto q = cd::math::Quatf::from_axis_angle({ 0.0f, 0.0f, 1.0f }, cd::math::pi);
    auto v = cd::math::rotate(q, cd::math::Vec3f { 1.0f, 1.0f, 0.0f });
    EXPECT_TRUE(cd::math::approx_equal(v, cd::math::Vec3f { -1.0f, -1.0f, 0.0f }, 1e-5f));
}

TEST(MathQuat, MultiplyCombinesRotations)
{
    auto qx = cd::math::Quatf::from_axis_angle({ 1.0f, 0.0f, 0.0f }, cd::math::half_pi);
    auto qy = cd::math::Quatf::from_axis_angle({ 0.0f, 1.0f, 0.0f }, cd::math::half_pi);
    auto qcomposite = qy * qx;
    // Both routes should give the same rotated point.
    cd::math::Vec3f p { 0.0f, 1.0f, 0.0f };
    auto via_composite = cd::math::rotate(qcomposite, p);
    auto via_chain = cd::math::rotate(qy, cd::math::rotate(qx, p));
    EXPECT_TRUE(cd::math::approx_equal(via_composite, via_chain, 1e-5f));
}

TEST(MathQuat, ConjugateUndoesRotation)
{
    auto q = cd::math::Quatf::from_axis_angle({ 1.0f, 2.0f, 3.0f }, 0.7f);
    auto qc = cd::math::conjugate(q);
    cd::math::Vec3f p { 4.0f, -2.0f, 1.5f };
    auto out = cd::math::rotate(qc, cd::math::rotate(q, p));
    EXPECT_TRUE(cd::math::approx_equal(out, p, 1e-5f));
}

TEST(MathQuat, NormalizeProducesUnit)
{
    cd::math::Quatf q { 2.0f, 4.0f, 6.0f, 8.0f };
    auto n = cd::math::normalize(q);
    EXPECT_NEAR(cd::math::length(n), 1.0f, 1e-6f);
}

TEST(MathQuat, NormalizeZeroFallsBackToIdentity)
{
    cd::math::Quatf zero { 0.0f, 0.0f, 0.0f, 0.0f };
    auto n = cd::math::normalize(zero);
    EXPECT_EQ(n, cd::math::Quatf::identity());
}

// --- Transform / projections / look_at / slerp ----------------------------
TEST(MathTransform, IdentityIsNoOp)
{
    cd::math::Transformf t;
    cd::math::Vec3f p { 1.5f, -2.0f, 3.25f };
    EXPECT_TRUE(cd::math::approx_equal(t.apply_to_point(p), p, 1e-6f));
}

TEST(MathTransform, TRSOrder)
{
    // S=2, R=90° about Y, T=(10,0,0). Apply to (1,0,0):
    //   S → (2,0,0); R(Y,90°) → (0,0,-2); T → (10,0,-2)
    cd::math::Transformf t;
    t.scale = { 2.0f, 2.0f, 2.0f };
    t.rotation = cd::math::Quatf::from_axis_angle({ 0.0f, 1.0f, 0.0f }, cd::math::half_pi);
    t.position = { 10.0f, 0.0f, 0.0f };
    auto out = t.apply_to_point({ 1.0f, 0.0f, 0.0f });
    EXPECT_TRUE(cd::math::approx_equal(out, cd::math::Vec3f { 10.0f, 0.0f, -2.0f }, 1e-5f));
}

TEST(MathTransform, ToMat4ConsistentWithApply)
{
    cd::math::Transformf t;
    t.scale = { 1.0f, 2.0f, 3.0f };
    t.rotation = cd::math::Quatf::from_axis_angle({ 0.0f, 0.0f, 1.0f }, 0.4f);
    t.position = { -5.0f, 7.0f, 2.0f };
    cd::math::Vec3f p { 1.0f, 1.0f, 1.0f };
    auto via_apply = t.apply_to_point(p);
    auto M = cd::math::to_mat4(t);
    auto via_mat = M * cd::math::Vec4f { p, 1.0f };
    EXPECT_TRUE(cd::math::approx_equal(via_apply, cd::math::Vec3f { via_mat.x, via_mat.y, via_mat.z }, 1e-5f));
}

TEST(MathTransform, ToMat3MatchesQuatRotation)
{
    auto q = cd::math::Quatf::from_axis_angle({ 1.0f, 0.0f, 0.0f }, cd::math::half_pi);
    auto M = cd::math::to_mat3(q);
    cd::math::Vec3f p { 0.0f, 1.0f, 0.0f };
    auto via_q = cd::math::rotate(q, p);
    auto via_m = M * p;
    EXPECT_TRUE(cd::math::approx_equal(via_q, via_m, 1e-5f));
}

TEST(MathLookAt, EyeMinusTargetIsForward)
{
    // Camera at (0,0,5) looking at origin, world-up (0,1,0). In camera space,
    // the origin should be at (0,0,-5) (since RH camera looks down -Z).
    auto V = cd::math::look_at(
        cd::math::Vec3f { 0.0f, 0.0f, 5.0f },
        cd::math::Vec3f { 0.0f, 0.0f, 0.0f },
        cd::math::Vec3f { 0.0f, 1.0f, 0.0f }
    );
    auto out = V * cd::math::Vec4f { 0.0f, 0.0f, 0.0f, 1.0f };
    EXPECT_NEAR(out.x, 0.0f, 1e-5f);
    EXPECT_NEAR(out.y, 0.0f, 1e-5f);
    EXPECT_NEAR(out.z, -5.0f, 1e-5f);
}

TEST(MathPerspective, NearPointMapsToZeroDepth)
{
    auto P = cd::math::perspective(cd::math::deg_to_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    // A point on the near plane along the camera forward (RH: -Z) should map
    // to clip-space z = 0 after perspective divide.
    cd::math::Vec4f near_pt { 0.0f, 0.0f, -0.1f, 1.0f };
    auto clip = P * near_pt;
    EXPECT_NEAR(clip.z / clip.w, 0.0f, 1e-4f);
}

TEST(MathPerspective, FarPointMapsToOneDepth)
{
    auto P = cd::math::perspective(cd::math::deg_to_rad(60.0f), 1.0f, 0.1f, 100.0f);
    cd::math::Vec4f far_pt { 0.0f, 0.0f, -100.0f, 1.0f };
    auto clip = P * far_pt;
    EXPECT_NEAR(clip.z / clip.w, 1.0f, 1e-4f);
}

TEST(MathOrtho, CornersMapToNDC)
{
    auto O = cd::math::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f);
    // (left, bottom, -near) → (-1, -1, 0)
    auto p1 = O * cd::math::Vec4f { -1.0f, -1.0f, 0.0f, 1.0f };
    EXPECT_NEAR(p1.x, -1.0f, 1e-6f);
    EXPECT_NEAR(p1.y, -1.0f, 1e-6f);
    EXPECT_NEAR(p1.z, 0.0f, 1e-6f);
    // (right, top, -far) → (1, 1, 1)
    auto p2 = O * cd::math::Vec4f { 1.0f, 1.0f, -10.0f, 1.0f };
    EXPECT_NEAR(p2.x, 1.0f, 1e-6f);
    EXPECT_NEAR(p2.y, 1.0f, 1e-6f);
    EXPECT_NEAR(p2.z, 1.0f, 1e-6f);
}

TEST(MathSlerp, EndpointsReturnInputs)
{
    auto a = cd::math::Quatf::from_axis_angle({ 0.0f, 1.0f, 0.0f }, 0.3f);
    auto b = cd::math::Quatf::from_axis_angle({ 0.0f, 1.0f, 0.0f }, 1.2f);
    EXPECT_TRUE(cd::math::approx_equal(cd::math::slerp(a, b, 0.0f), a, 1e-5f));
    EXPECT_TRUE(cd::math::approx_equal(cd::math::slerp(a, b, 1.0f), b, 1e-5f));
}

TEST(MathSlerp, HalfwayHasHalfAngle)
{
    // slerp(I, R(axis,θ), 0.5) ≡ R(axis, θ/2)
    cd::math::Vec3f axis = cd::math::normalize(cd::math::Vec3f { 1.0f, 2.0f, 3.0f });
    auto a = cd::math::Quatf::identity();
    auto b = cd::math::Quatf::from_axis_angle(axis, 1.0f);
    auto mid = cd::math::slerp(a, b, 0.5f);
    auto expected = cd::math::Quatf::from_axis_angle(axis, 0.5f);
    EXPECT_TRUE(cd::math::approx_equal(mid, expected, 1e-5f));
}

TEST(MathSlerp, TakesShorterArc)
{
    // Negate b — slerp should still pick the short way.
    cd::math::Vec3f axis { 0.0f, 1.0f, 0.0f };
    auto a = cd::math::Quatf::from_axis_angle(axis, 0.1f);
    auto b = cd::math::Quatf::from_axis_angle(axis, 0.5f);
    cd::math::Quatf b_neg { -b.x, -b.y, -b.z, -b.w };
    auto r1 = cd::math::slerp(a, b, 0.5f);
    auto r2 = cd::math::slerp(a, b_neg, 0.5f);
    // Both should produce the same world-space rotation.
    cd::math::Vec3f p { 1.0f, 0.0f, 0.0f };
    EXPECT_TRUE(cd::math::approx_equal(cd::math::rotate(r1, p), cd::math::rotate(r2, p), 1e-5f));
}

}  // namespace

// --- Mat4 inverse (backlog: Phase 3 wart, requested by hello_skybox) -------

TEST(Matrix, IdentityInverseEqualsIdentity)
{
    const auto id = cd::math::Mat4f::identity();
    const auto inv = cd::math::inverse(id);
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(inv[c][r], id[c][r], 1e-6f);
}

TEST(Matrix, InverseTimesOriginalEqualsIdentity)
{
    cd::math::Transformf xf;
    xf.position = { 2.5f, -1.0f, 4.0f };
    xf.scale = { 2.0f, 0.5f, 1.5f };
    xf.rotation = cd::math::Quatf::from_axis_angle({ 0.0f, 1.0f, 0.0f }, 0.7f);
    const auto m = cd::math::to_mat4(xf);
    const auto inv = cd::math::inverse(m);
    const auto product = m * inv;
    const auto id = cd::math::Mat4f::identity();
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(product[c][r], id[c][r], 1e-5f);
}

TEST(Matrix, SingularMatrixInverseFallsBackToIdentity)
{
    cd::math::Mat4f zero {};
    const auto inv = cd::math::inverse(zero);
    const auto id = cd::math::Mat4f::identity();
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            EXPECT_NEAR(inv[c][r], id[c][r], 1e-6f);
}

// ---------------------------------------------------------------------------
// Phase 21.E — Easing tests (Wave 184)
// ---------------------------------------------------------------------------
#include <cd/math/Easing.hpp>

TEST(Easing, LinearIsIdentityInRange)
{
    EXPECT_FLOAT_EQ(cd::math::linear(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(cd::math::linear(0.5F), 0.5F);
    EXPECT_FLOAT_EQ(cd::math::linear(1.0F), 1.0F);
}

TEST(Easing, LinearClampsOutsideRange)
{
    EXPECT_FLOAT_EQ(cd::math::linear(-0.5F), 0.0F);
    EXPECT_FLOAT_EQ(cd::math::linear(1.5F), 1.0F);
}

TEST(Easing, SmoothstepSymmetricAroundHalf)
{
    EXPECT_FLOAT_EQ(cd::math::smoothstep(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(cd::math::smoothstep(1.0F), 1.0F);
    // Smoothstep(0.5) = 0.5 by symmetry.
    EXPECT_NEAR(cd::math::smoothstep(0.5F), 0.5F, 1e-6F);
}

TEST(Easing, EaseInQuadIsZeroAtZero)
{
    EXPECT_FLOAT_EQ(cd::math::ease_in_quad(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(cd::math::ease_in_quad(1.0F), 1.0F);
    EXPECT_FLOAT_EQ(cd::math::ease_in_quad(0.5F), 0.25F);
}

TEST(Easing, EaseOutQuadMirrorsEaseIn)
{
    EXPECT_NEAR(cd::math::ease_out_quad(0.5F), 0.75F, 1e-6F);
}

TEST(Easing, EaseInOutCubicHalfPointIsHalf)
{
    EXPECT_NEAR(cd::math::ease_in_out_cubic(0.5F), 0.5F, 1e-6F);
}

// ---------------------------------------------------------------------------
// Phase 22.D — CubicBezier tests (Wave 186)
// ---------------------------------------------------------------------------
#include <cd/math/CubicBezier.hpp>

TEST(CubicBezier, EndpointsExact)
{
    cd::math::CubicBezier b;
    b.p0 = cd::math::Vec3f { 0, 0, 0 };
    b.p3 = cd::math::Vec3f { 10, 0, 0 };
    EXPECT_NEAR(b.at(0.0F).x,  0.0F, 1e-5F);
    EXPECT_NEAR(b.at(1.0F).x, 10.0F, 1e-5F);
}

TEST(CubicBezier, ArcLengthOfStraightLine)
{
    cd::math::CubicBezier b;
    b.p0 = cd::math::Vec3f { 0, 0, 0 };
    b.p1 = cd::math::Vec3f { 3.33F, 0, 0 };
    b.p2 = cd::math::Vec3f { 6.66F, 0, 0 };
    b.p3 = cd::math::Vec3f { 10, 0, 0 };
    EXPECT_NEAR(b.arc_length(64), 10.0F, 0.1F);
}

// ---------------------------------------------------------------------------
// Phase 23.A — Plane tests (Wave 188)
// ---------------------------------------------------------------------------
#include <cd/math/Plane.hpp>

TEST(Plane, SignedDistanceForAxisAlignedPlane)
{
    cd::math::Plane p { cd::math::Vec3f { 0, 1, 0 }, 0.0F };
    EXPECT_FLOAT_EQ(cd::math::signed_distance(p, cd::math::Vec3f { 0, 5, 0 }),  5.0F);
    EXPECT_FLOAT_EQ(cd::math::signed_distance(p, cd::math::Vec3f { 0, -2, 0 }), -2.0F);
}

TEST(Plane, ClassifyPointPositiveNegativeOn)
{
    cd::math::Plane p { cd::math::Vec3f { 0, 1, 0 }, 0.0F };
    EXPECT_EQ(cd::math::classify_point(p, cd::math::Vec3f { 0, 1, 0 }),  1);
    EXPECT_EQ(cd::math::classify_point(p, cd::math::Vec3f { 0, -1, 0 }), -1);
    EXPECT_EQ(cd::math::classify_point(p, cd::math::Vec3f { 0, 0, 0 }),  0);
}

// ---------------------------------------------------------------------------
// Phase 23.B — HSV ↔ RGB tests (Wave 188)
// ---------------------------------------------------------------------------
#include <cd/math/Color.hpp>

TEST(Color, RedHsvIsExpected)
{
    auto rgb = cd::math::hsv_to_rgb(0.0F, 1.0F, 1.0F);
    EXPECT_NEAR(rgb.x, 1.0F, 1e-5F);
    EXPECT_NEAR(rgb.y, 0.0F, 1e-5F);
    EXPECT_NEAR(rgb.z, 0.0F, 1e-5F);
}

TEST(Color, RgbRoundTripsThroughHsv)
{
    const cd::math::Vec3f rgb0 { 0.6F, 0.3F, 0.1F };
    const auto hsv = cd::math::rgb_to_hsv(rgb0.x, rgb0.y, rgb0.z);
    const auto back = cd::math::hsv_to_rgb(hsv.x, hsv.y, hsv.z);
    EXPECT_NEAR(back.x, rgb0.x, 1e-4F);
    EXPECT_NEAR(back.y, rgb0.y, 1e-4F);
    EXPECT_NEAR(back.z, rgb0.z, 1e-4F);
}

// ---------------------------------------------------------------------------
// Phase 24.A — Random (PCG32) tests (Wave 190)
// ---------------------------------------------------------------------------
#include <cd/math/Random.hpp>

TEST(Random, DeterministicWithSeed)
{
    cd::math::Random a { 42 };
    cd::math::Random b { 42 };
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(a.next_u32(), b.next_u32());
}

TEST(Random, FloatInUnitRange)
{
    cd::math::Random r { 7 };
    for (int i = 0; i < 1024; ++i)
    {
        const float v = r.next_float();
        EXPECT_GE(v, 0.0F);
        EXPECT_LT(v, 1.0F);
    }
}

// ---------------------------------------------------------------------------
// Phase 24.B — SmoothingFilter tests
// ---------------------------------------------------------------------------
#include <cd/math/SmoothingFilter.hpp>

TEST(SmoothingFilter, EmaConvergesToTarget)
{
    cd::math::EmaSmoother ema { 0.5F, 0.0F };
    for (int i = 0; i < 32; ++i)
        ema.update(10.0F);
    EXPECT_NEAR(ema.value(), 10.0F, 0.01F);
}

TEST(SmoothingFilter, LinearAdvancesByMaxStep)
{
    cd::math::LinearSmoother lin { 1.0F, 0.0F };
    EXPECT_FLOAT_EQ(lin.update(5.0F), 1.0F);
    EXPECT_FLOAT_EQ(lin.update(5.0F), 2.0F);
}

TEST(SmoothingFilter, LinearSnapsWhenInRange)
{
    cd::math::LinearSmoother lin { 2.0F, 0.0F };
    EXPECT_FLOAT_EQ(lin.update(1.5F), 1.5F);
}
