#include <cd/brdf_ltc/Ltc.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::brdf_ltc::edge_integral;
using cd::brdf_ltc::ltc_inverse_matrix;
using cd::brdf_ltc::Matrix;
using cd::brdf_ltc::polygon_irradiance;
using cd::brdf_ltc::transform;

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
    EXPECT_FALSE(cd::brdf_ltc::kLtcGlsl.empty());
    EXPECT_NE(cd::brdf_ltc::kLtcGlsl.find("ltc_polygon_irradiance"),
              std::string_view::npos);
}

}  // namespace
