#include <cd/brdf/ltc/Ltc.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <string_view>

namespace
{

using cd::brdf::ltc::edge_integral;
using cd::brdf::ltc::ltc_inverse_matrix;
using cd::brdf::ltc::Matrix;
using cd::brdf::ltc::polygon_irradiance;
using cd::brdf::ltc::transform;

constexpr float kEps = 1e-2F;

TEST(BrdfLtc, MirrorRoughnessReturnsNearIdentity)
{
    // Roughness ~ 0 (mirror) -> M^-1 ~ I.
    const auto m = ltc_inverse_matrix(0.001F, 1.0F);
    EXPECT_NEAR(m.a, 1.0F, kEps);
    EXPECT_NEAR(m.c, 1.0F, kEps);
}

TEST(BrdfLtc, TransformIdentityPreservesVector)
{
    Matrix I {};  // default: identity-like
    const cd::math::Vec3f v { 0.3F, 0.4F, 0.866F };
    const auto t = transform(I, v);
    EXPECT_NEAR(t.x, 0.3F,   kEps);
    EXPECT_NEAR(t.y, 0.4F,   kEps);
    EXPECT_NEAR(t.z, 0.866F, kEps);
}

TEST(BrdfLtc, EdgeIntegralAntisymmetric)
{
    // I(a, b) = -I(b, a) up to sign convention.
    const cd::math::Vec3f a { 0.0F, 0.0F, 1.0F };
    const cd::math::Vec3f b { 1.0F, 0.0F, 0.0F };
    const float f = edge_integral(a, b);
    const float g = edge_integral(b, a);
    EXPECT_NEAR(f + g, 0.0F, kEps);
}

TEST(BrdfLtc, PolygonZenithReturnsPositiveIrradiance)
{
    // A unit square light at z = 2, above the surface.
    std::array<cd::math::Vec3f, 4> corners {{
        { -0.5F, -0.5F, 2.0F },
        {  0.5F, -0.5F, 2.0F },
        {  0.5F,  0.5F, 2.0F },
        { -0.5F,  0.5F, 2.0F } }};
    Matrix I {};
    const float E = polygon_irradiance(corners, I);
    EXPECT_GT(E, 0.0F);
    EXPECT_LE(E, 1.0F);
}

TEST(BrdfLtc, IrradianceFallsWithDistance)
{
    // Square light receding -> irradiance drops.
    auto make_quad = [](float d) {
        return std::array<cd::math::Vec3f, 4> {{
            { -0.5F, -0.5F, d },
            {  0.5F, -0.5F, d },
            {  0.5F,  0.5F, d },
            { -0.5F,  0.5F, d } }};
    };
    Matrix I {};
    const float near_E = polygon_irradiance(make_quad(1.0F), I);
    const float far_E  = polygon_irradiance(make_quad(5.0F), I);
    EXPECT_GT(near_E, far_E);
}

TEST(BrdfLtc, GlslHelperNonEmpty)
{
    EXPECT_FALSE(cd::brdf::ltc::kLtcGlsl.empty());
    EXPECT_NE(cd::brdf::ltc::kLtcGlsl.find("ltc_polygon_irradiance"),
              std::string_view::npos);
}

// === ADD-ONLY edge/negative coverage (Heitz, Dupuy, Hill, Neubelt 2016) =====
// All reference values verified against the polynomial fit in Ltc.hpp; no lobe
// math is exercised differently, only pinned. Heitz Eq. 11 (edge integral) and
// the sparse 4-parameter M^-1 layout (a,0,b / 0,c,0 / d,0,1) are the contract.

TEST(BrdfLtc, MatrixReferenceValuesAtMaxRoughnessGrazing)
{
    // r = 1 (max roughness), n_dot_v = 0 -> clamped to 0.001 internally.
    // Hand-derived from the fit: a = 1 + 1*(-0.6 + 0.5*(1-0.001)) = 0.8995,
    // b = 1*(1-0.001)*0.5 = 0.4995, c = 1 + 1*(-0.4) = 0.6, d = 1*0.001*-0.3.
    const auto m = ltc_inverse_matrix(1.0F, 0.0F);
    EXPECT_NEAR(m.a, 0.8995F, 1e-4F);
    EXPECT_NEAR(m.b, 0.4995F, 1e-4F);
    EXPECT_NEAR(m.c, 0.6000F, 1e-4F);
    EXPECT_NEAR(m.d, -0.0003F, 1e-4F);
}

TEST(BrdfLtc, MatrixReferenceValuesAtMidpoint)
{
    // r = 0.5, n_dot_v = 0.5: a=0.825, b=0.125, c=0.8, d=-0.075.
    const auto m = ltc_inverse_matrix(0.5F, 0.5F);
    EXPECT_NEAR(m.a, 0.825F, 1e-4F);
    EXPECT_NEAR(m.b, 0.125F, 1e-4F);
    EXPECT_NEAR(m.c, 0.800F, 1e-4F);
    EXPECT_NEAR(m.d, -0.075F, 1e-4F);
}

TEST(BrdfLtc, InputDomainIsClampedNotExtrapolated)
{
    // Out-of-domain (negative / >1) inputs clamp to [0.001, 1] so the fit
    // never extrapolates; identical to the boundary samples.
    const auto lo = ltc_inverse_matrix(-5.0F, -5.0F);
    const auto at = ltc_inverse_matrix(0.001F, 0.001F);
    EXPECT_FLOAT_EQ(lo.a, at.a);
    EXPECT_FLOAT_EQ(lo.b, at.b);
    EXPECT_FLOAT_EQ(lo.c, at.c);
    EXPECT_FLOAT_EQ(lo.d, at.d);

    const auto hi  = ltc_inverse_matrix(5.0F, 5.0F);
    const auto top = ltc_inverse_matrix(1.0F, 1.0F);
    EXPECT_FLOAT_EQ(hi.a, top.a);
    EXPECT_FLOAT_EQ(hi.d, top.d);
}

TEST(BrdfLtc, MatrixAllFiniteAcrossDomainSweep)
{
    // NaN-guard: every sampled M^-1 entry must stay finite over the grid.
    for (int ri = 0; ri <= 10; ++ri)
    {
        for (int vi = 0; vi <= 10; ++vi)
        {
            const float r  = static_cast<float>(ri) / 10.0F;
            const float nv = static_cast<float>(vi) / 10.0F;
            const auto  m  = ltc_inverse_matrix(r, nv);
            EXPECT_TRUE(std::isfinite(m.a));
            EXPECT_TRUE(std::isfinite(m.b));
            EXPECT_TRUE(std::isfinite(m.c));
            EXPECT_TRUE(std::isfinite(m.d));
        }
    }
}

TEST(BrdfLtc, TransformHonoursSparseLayout)
{
    // M^-1 = | a 0 b ; 0 c 0 ; d 0 1 |. The y-row must be untouched by x/z and
    // the x/z rows must not read y -> verifies the sparse structure exactly.
    const Matrix m { 0.8995F, 0.4995F, 0.6F, -0.0003F };
    const auto   ex = transform(m, cd::math::Vec3f { 1.0F, 0.0F, 0.0F });
    EXPECT_NEAR(ex.x, m.a, kEps);   // a*1
    EXPECT_NEAR(ex.y, 0.0F, kEps);  // c*0
    EXPECT_NEAR(ex.z, m.d, kEps);   // d*1
    const auto ez = transform(m, cd::math::Vec3f { 0.0F, 0.0F, 1.0F });
    EXPECT_NEAR(ez.x, m.b, kEps);   // b*1
    EXPECT_NEAR(ez.z, 1.0F, kEps);  // 1*1 (bottom-right fixed to 1)
    const auto ey = transform(m, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
    EXPECT_NEAR(ey.x, 0.0F, kEps);
    EXPECT_NEAR(ey.y, m.c, kEps);   // c*1
    EXPECT_NEAR(ey.z, 0.0F, kEps);
}

TEST(BrdfLtc, EdgeIntegralParallelVerticesIsZero)
{
    // theta -> 0 between identical directions: sin guard returns 0, no NaN
    // from the theta/sin(theta) division (Heitz Eq. 11 degenerate case).
    const cd::math::Vec3f a { 0.0F, 0.0F, 1.0F };
    const float f = edge_integral(a, a);
    EXPECT_TRUE(std::isfinite(f));
    EXPECT_NEAR(f, 0.0F, kEps);
}

TEST(BrdfLtc, EdgeIntegralOrthogonalReferenceValue)
{
    // x_hat, y_hat: theta = pi/2, sin = 1, cross = z_hat so coeff*cr.z = pi/2.
    const cd::math::Vec3f x { 1.0F, 0.0F, 0.0F };
    const cd::math::Vec3f y { 0.0F, 1.0F, 0.0F };
    EXPECT_NEAR(edge_integral(x, y),
                std::numbers::pi_v<float> / 2.0F, kEps);
    EXPECT_NEAR(edge_integral(y, x),
                -std::numbers::pi_v<float> / 2.0F, kEps);
}

TEST(BrdfLtc, EdgeIntegralAcceptsUnnormalisedAntiparallel)
{
    // Anti-parallel (theta = pi): cross magnitude 0 -> integral 0, no blow-up
    // even though sin(pi) ~ 0 in the coeff (cross.z is also 0).
    const cd::math::Vec3f a {  0.0F, 0.0F,  1.0F };
    const cd::math::Vec3f b {  0.0F, 0.0F, -1.0F };
    const float f = edge_integral(a, b);
    EXPECT_TRUE(std::isfinite(f));
    EXPECT_NEAR(f, 0.0F, kEps);
}

TEST(BrdfLtc, PolygonDegenerateZeroAreaGivesZeroIrradiance)
{
    // All four corners coincident -> normalised edges collapse, sum -> 0.
    std::array<cd::math::Vec3f, 4> corners {{
        { 0.0F, 0.0F, 1.0F },
        { 0.0F, 0.0F, 1.0F },
        { 0.0F, 0.0F, 1.0F },
        { 0.0F, 0.0F, 1.0F } }};
    Matrix I {};
    const float E = polygon_irradiance(corners, I);
    EXPECT_TRUE(std::isfinite(E));
    EXPECT_NEAR(E, 0.0F, kEps);
}

TEST(BrdfLtc, PolygonNanGuardAtOriginCorners)
{
    // Corners at the origin hit the L > 1e-5 normalise guard (no /0) and the
    // result is finite (energy-conservation: irradiance never NaN/Inf).
    std::array<cd::math::Vec3f, 4> corners {{
        { 0.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 0.0F } }};
    Matrix I {};
    const float E = polygon_irradiance(corners, I);
    EXPECT_TRUE(std::isfinite(E));
    EXPECT_GE(E, 0.0F);
}

TEST(BrdfLtc, PolygonIrradianceBoundedByEnergyConservation)
{
    // A large overhead light still yields E <= 1 (clamped-cosine integral
    // over the hemisphere never exceeds full coverage).
    std::array<cd::math::Vec3f, 4> corners {{
        { -50.0F, -50.0F, 1.0F },
        {  50.0F, -50.0F, 1.0F },
        {  50.0F,  50.0F, 1.0F },
        { -50.0F,  50.0F, 1.0F } }};
    Matrix I {};
    const float E = polygon_irradiance(corners, I);
    EXPECT_GE(E, 0.0F);
    EXPECT_LE(E, 1.0F);
}

TEST(BrdfLtc, GlslMirrorsCpuConstants)
{
    // The shipped GLSL helper must keep the same 2*pi divisor and the sparse
    // transform form so CPU/GPU stay in lock-step.
    EXPECT_NE(cd::brdf::ltc::kLtcGlsl.find("6.28318530"),
              std::string_view::npos);
    EXPECT_NE(cd::brdf::ltc::kLtcGlsl.find("ltc_edge_integral"),
              std::string_view::npos);
    EXPECT_NE(cd::brdf::ltc::kLtcGlsl.find("ltc_transform"),
              std::string_view::npos);
}

}  // namespace
