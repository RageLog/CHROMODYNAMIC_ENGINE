// =============================================================================
// CHROMODYNAMIC — engine/render/debug_line/tests/test_debug_line.cpp
// phase1030 — cd::debug_line::LineBatch unit tests.
//
// Pattern: Arrange / Act / Assert. Covers the shape generators'
// vertex counts, endpoint placement, colour propagation, and the
// edge cases (swapped AABB corners, sub-minimum circle segments,
// degenerate polylines, zero circle axis).
// =============================================================================
#include <cd/debug_line/DebugLine.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{

using cd::debug_line::LineBatch;
using cd::math::Mat4f;
using cd::math::Vec3f;
using cd::math::Vec4f;

constexpr Vec4f kRed { 1.0F, 0.0F, 0.0F, 1.0F };
constexpr Vec4f kGreen { 0.0F, 1.0F, 0.0F, 1.0F };

[[nodiscard]] bool contains_position(const LineBatch& b, const Vec3f& p,
                                     float eps = 1e-5F)
{
    const auto verts = b.vertices();
    return std::ranges::any_of(verts, [&](const auto& v) {
        return std::abs(v.position.x - p.x) < eps &&
               std::abs(v.position.y - p.y) < eps &&
               std::abs(v.position.z - p.z) < eps;
    });
}

TEST(DebugLineBatch, StartsEmpty)
{
    const LineBatch b {};
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(b.vertex_count(), 0U);
    EXPECT_EQ(b.line_count(), 0U);
}

TEST(DebugLineBatch, AddLineProducesTwoVerticesWithEndpointsAndColor)
{
    LineBatch b {};
    const Vec3f a { 1.0F, 2.0F, 3.0F };
    const Vec3f c { -4.0F, 5.0F, -6.0F };

    b.add_line(a, c, kRed);

    ASSERT_EQ(b.vertex_count(), 2U);
    EXPECT_EQ(b.line_count(), 1U);
    const auto verts = b.vertices();
    EXPECT_FLOAT_EQ(verts[0].position.x, a.x);
    EXPECT_FLOAT_EQ(verts[1].position.z, c.z);
    EXPECT_FLOAT_EQ(verts[0].color.x, 1.0F);
    EXPECT_FLOAT_EQ(verts[1].color.y, 0.0F);
    EXPECT_FLOAT_EQ(verts[1].color.w, 1.0F);
}

TEST(DebugLineBatch, ClearEmptiesButBatchIsReusable)
{
    LineBatch b {};
    b.add_line({ 0, 0, 0 }, { 1, 1, 1 }, kRed);

    b.clear();

    EXPECT_TRUE(b.empty());
    b.add_line({ 0, 0, 0 }, { 1, 1, 1 }, kRed);
    EXPECT_EQ(b.line_count(), 1U);
}

TEST(DebugLineBatch, AabbProducesTwelveEdges)
{
    LineBatch b {};

    b.add_aabb({ -1, -2, -3 }, { 1, 2, 3 }, kGreen);

    EXPECT_EQ(b.line_count(), 12U);
    EXPECT_EQ(b.vertex_count(), 24U);
    // All 8 corners must appear among the vertices.
    for (const float sx : { -1.0F, 1.0F })
    for (const float sy : { -2.0F, 2.0F })
    for (const float sz : { -3.0F, 3.0F })
    {
        EXPECT_TRUE(contains_position(b, { sx, sy, sz }))
            << "missing corner (" << sx << ", " << sy << ", " << sz << ")";
    }
}

TEST(DebugLineBatch, AabbNormalisesSwappedCorners)
{
    LineBatch swapped {};
    LineBatch ordered {};

    swapped.add_aabb({ 1, 2, 3 }, { -1, -2, -3 }, kGreen);
    ordered.add_aabb({ -1, -2, -3 }, { 1, 2, 3 }, kGreen);

    ASSERT_EQ(swapped.vertex_count(), ordered.vertex_count());
    const auto sv = swapped.vertices();
    const auto ov = ordered.vertices();
    for (std::size_t i = 0; i < sv.size(); ++i)
    {
        EXPECT_FLOAT_EQ(sv[i].position.x, ov[i].position.x);
        EXPECT_FLOAT_EQ(sv[i].position.y, ov[i].position.y);
        EXPECT_FLOAT_EQ(sv[i].position.z, ov[i].position.z);
    }
}

TEST(DebugLineBatch, ObbWithIdentityAxesMatchesAabbCorners)
{
    LineBatch b {};
    const Vec3f centre { 5.0F, 6.0F, 7.0F };
    const Vec3f half { 1.0F, 2.0F, 3.0F };

    b.add_obb(centre,
              { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
              half, kRed);

    EXPECT_EQ(b.line_count(), 12U);
    for (const float sx : { -1.0F, 1.0F })
    for (const float sy : { -1.0F, 1.0F })
    for (const float sz : { -1.0F, 1.0F })
    {
        const Vec3f corner { centre.x + sx * half.x,
                             centre.y + sy * half.y,
                             centre.z + sz * half.z };
        EXPECT_TRUE(contains_position(b, corner));
    }
}

TEST(DebugLineBatch, ObbRotatedAxesPlaceCornersAlongAxes)
{
    LineBatch b {};
    // 90-degree yaw: right = +Z, forward = -X.
    const Vec3f centre { 0, 0, 0 };

    b.add_obb(centre,
              { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 },
              { 2.0F, 0.5F, 1.0F }, kRed);

    // +right * 2 + +up * 0.5 + +forward * 1 = (−1, 0.5, 2).
    EXPECT_TRUE(contains_position(b, { -1.0F, 0.5F, 2.0F }));
    // −right −up −forward = (1, −0.5, −2).
    EXPECT_TRUE(contains_position(b, { 1.0F, -0.5F, -2.0F }));
}

TEST(DebugLineBatch, FrustumWithIdentityMatrixYieldsNdcCube)
{
    LineBatch b {};

    b.add_frustum(Mat4f::identity(), kGreen);  // Vulkan z in [0, 1]

    EXPECT_EQ(b.line_count(), 12U);
    EXPECT_TRUE(contains_position(b, { -1.0F, -1.0F, 0.0F }));
    EXPECT_TRUE(contains_position(b, {  1.0F,  1.0F, 1.0F }));
    // GL convention variant.
    LineBatch gl {};
    gl.add_frustum(Mat4f::identity(), kGreen, -1.0F, 1.0F);
    EXPECT_TRUE(contains_position(gl, { -1.0F, -1.0F, -1.0F }));
}

TEST(DebugLineBatch, CircleClampsSegmentsToThree)
{
    LineBatch b {};

    b.add_circle({ 0, 0, 0 }, { 0, 1, 0 }, 1.0F, 1, kRed);

    EXPECT_EQ(b.line_count(), 3U);
}

TEST(DebugLineBatch, CircleVerticesLieOnRadiusInAxisPlane)
{
    LineBatch b {};
    const Vec3f centre { 2.0F, 3.0F, 4.0F };
    const float radius = 1.5F;

    b.add_circle(centre, { 0, 0, 1 }, radius, 16, kRed);

    EXPECT_EQ(b.line_count(), 16U);
    for (const auto& v : b.vertices())
    {
        // In-plane: z stays at the centre's z (axis = +Z).
        EXPECT_NEAR(v.position.z, centre.z, 1e-4F);
        const float dx = v.position.x - centre.x;
        const float dy = v.position.y - centre.y;
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), radius, 1e-4F);
    }
}

TEST(DebugLineBatch, CircleZeroAxisFallsBackToYPlane)
{
    LineBatch b {};

    b.add_circle({ 0, 5, 0 }, { 0, 0, 0 }, 2.0F, 8, kRed);

    EXPECT_EQ(b.line_count(), 8U);
    for (const auto& v : b.vertices())
        EXPECT_NEAR(v.position.y, 5.0F, 1e-4F);
}

TEST(DebugLineBatch, PolylineProducesNMinusOneSegments)
{
    LineBatch b {};
    const std::array<Vec3f, 4> pts {{
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 } }};

    b.add_polyline(pts, kGreen);

    EXPECT_EQ(b.line_count(), 3U);
    // Interior points appear twice (segment end + next start).
    const auto verts = b.vertices();
    EXPECT_FLOAT_EQ(verts[1].position.x, verts[2].position.x);
}

TEST(DebugLineBatch, PolylineDegenerateInputsAreNoOps)
{
    LineBatch b {};
    const std::array<Vec3f, 1> one {{ { 1, 2, 3 } }};

    b.add_polyline({}, kGreen);
    b.add_polyline(one, kGreen);

    EXPECT_TRUE(b.empty());
}

TEST(DebugLineBatch, CrossProducesThreeAxisSegments)
{
    LineBatch b {};
    const Vec3f centre { 1.0F, 2.0F, 3.0F };

    b.add_cross(centre, 0.5F, kRed);

    EXPECT_EQ(b.line_count(), 3U);
    EXPECT_TRUE(contains_position(b, { 0.5F, 2.0F, 3.0F }));
    EXPECT_TRUE(contains_position(b, { 1.5F, 2.0F, 3.0F }));
    EXPECT_TRUE(contains_position(b, { 1.0F, 2.5F, 3.0F }));
    EXPECT_TRUE(contains_position(b, { 1.0F, 2.0F, 3.5F }));
}

TEST(DebugLineBatch, MixedShapesAccumulateAndColoursStayPerVertex)
{
    LineBatch b {};

    b.add_line({ 0, 0, 0 }, { 1, 0, 0 }, kRed);
    b.add_aabb({ 0, 0, 0 }, { 1, 1, 1 }, kGreen);

    ASSERT_EQ(b.line_count(), 13U);
    const auto verts = b.vertices();
    EXPECT_FLOAT_EQ(verts[0].color.x, 1.0F);   // red line
    EXPECT_FLOAT_EQ(verts[2].color.x, 0.0F);   // green box starts
    EXPECT_FLOAT_EQ(verts[2].color.y, 1.0F);
}

}  // namespace
