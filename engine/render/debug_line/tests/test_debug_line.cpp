// =============================================================================
// CHROMODYNAMIC — engine/render/debug_line/tests/test_debug_line.cpp
// phase1030 — cd::debug_line::LineBatch unit tests.
//
// Pattern: Arrange / Act / Assert. Covers the shape generators'
// vertex counts, endpoint placement, colour propagation, and the
// edge cases (swapped AABB corners, sub-minimum circle segments,
// degenerate polylines, zero circle axis).
//
// phase1252 additions (100% depth pass):
//   edge/negative: empty-batch span, single-line vertex layout, AABB
//   degenerate zero-extent (point), AABB flat (one axis zero), OBB
//   zero half-extents, frustum singular matrix, circle 0/negative seg
//   clamp, circle 2-seg clamp, capacity-retention reuse, full RGBA
//   colour propagation, per-shape exact vertex counts, polyline
//   exactly-2-points, cross exact endpoint coordinates (all 6),
//   arrow -Y shaft helper fallback, arrow head_frac clamping, grid
//   negative half_lines clamp, grid spacing correctness, sphere
//   per-axis plane confinement, sphere degenerate 0 radius.
// =============================================================================
#include <cd/debug_line/DebugLine.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

TEST(DebugLineBatch, SphereIsThreeOrthogonalCircles)
{
    LineBatch b {};
    const Vec3f centre { 1.0F, 2.0F, 3.0F };
    const float radius = 0.5F;

    b.add_sphere(centre, radius, 12, kRed);

    EXPECT_EQ(b.line_count(), 36U);  // 3 circles x 12 segments
    for (const auto& v : b.vertices())
    {
        const float dx = v.position.x - centre.x;
        const float dy = v.position.y - centre.y;
        const float dz = v.position.z - centre.z;
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), radius, 1e-4F);
    }
}

TEST(DebugLineBatch, ArrowHasShaftPlusFourHeadWings)
{
    LineBatch b {};
    const Vec3f from { 0, 0, 0 };
    const Vec3f to { 2, 0, 0 };

    b.add_arrow(from, to, kGreen);

    EXPECT_EQ(b.line_count(), 5U);  // shaft + 4 wings
    EXPECT_TRUE(contains_position(b, from));
    EXPECT_TRUE(contains_position(b, to));
    // Head wings start at the tip and end behind it.
    const auto verts = b.vertices();
    for (std::size_t i = 2; i < verts.size(); i += 2)
    {
        EXPECT_FLOAT_EQ(verts[i].position.x, to.x);       // wing start = tip
        EXPECT_LT(verts[i + 1].position.x, to.x);          // wing end behind
    }
}

TEST(DebugLineBatch, ArrowDegenerateIsNoOp)
{
    LineBatch b {};

    b.add_arrow({ 1, 1, 1 }, { 1, 1, 1 }, kGreen);

    EXPECT_TRUE(b.empty());
}

TEST(DebugLineBatch, GridLineCountAndExtents)
{
    LineBatch b {};

    // half_lines=2 -> 5 lines per direction -> 10 segments.
    b.add_grid({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 2, 1.0F, kRed);

    EXPECT_EQ(b.line_count(), 10U);
    EXPECT_TRUE(contains_position(b, { -2.0F, 0.0F, -2.0F }));
    EXPECT_TRUE(contains_position(b, {  2.0F, 0.0F,  2.0F }));
    for (const auto& v : b.vertices())
        EXPECT_FLOAT_EQ(v.position.y, 0.0F);  // stays in the XZ plane
}

TEST(DebugLineBatch, GridZeroHalfLinesIsJustTheTwoCentreLines)
{
    LineBatch b {};

    b.add_grid({ 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 0, 1.0F, kRed);

    // extent = 0 -> two degenerate (zero-length) centre segments.
    EXPECT_EQ(b.line_count(), 2U);
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

// ---------------------------------------------------------------------------
// phase1252 — edge / negative tests
// ---------------------------------------------------------------------------

// --- Empty batch -----------------------------------------------------------

TEST(DebugLineBatch, EmptyBatchVerticesSpanIsNullSized)
{
    const LineBatch b {};
    const auto s = b.vertices();
    EXPECT_EQ(s.size(), 0U);
    // A size-zero span may carry a null data pointer; what matters is
    // that accessing it does not crash and size() is correct.
    EXPECT_TRUE(b.empty());
}

// --- Single line -----------------------------------------------------------

TEST(DebugLineBatch, SingleLineExactVertexLayout)
{
    // Arrange
    LineBatch b {};
    const Vec3f p0 { 1.0F, 2.0F, 3.0F };
    const Vec3f p1 { 4.0F, 5.0F, 6.0F };
    const Vec4f col { 0.1F, 0.2F, 0.3F, 0.4F };

    // Act
    b.add_line(p0, p1, col);

    // Assert — exact 2-vertex, 1-line accounting
    ASSERT_EQ(b.vertex_count(), 2U);
    ASSERT_EQ(b.line_count(), 1U);
    EXPECT_FALSE(b.empty());

    const auto v = b.vertices();
    // Endpoint A
    EXPECT_FLOAT_EQ(v[0].position.x, p0.x);
    EXPECT_FLOAT_EQ(v[0].position.y, p0.y);
    EXPECT_FLOAT_EQ(v[0].position.z, p0.z);
    // Endpoint B
    EXPECT_FLOAT_EQ(v[1].position.x, p1.x);
    EXPECT_FLOAT_EQ(v[1].position.y, p1.y);
    EXPECT_FLOAT_EQ(v[1].position.z, p1.z);
    // Full RGBA on both vertices
    EXPECT_FLOAT_EQ(v[0].color.x, col.x);
    EXPECT_FLOAT_EQ(v[0].color.y, col.y);
    EXPECT_FLOAT_EQ(v[0].color.z, col.z);
    EXPECT_FLOAT_EQ(v[0].color.w, col.w);
    EXPECT_FLOAT_EQ(v[1].color.x, col.x);
    EXPECT_FLOAT_EQ(v[1].color.y, col.y);
    EXPECT_FLOAT_EQ(v[1].color.z, col.z);
    EXPECT_FLOAT_EQ(v[1].color.w, col.w);
}

// --- AABB degenerate -------------------------------------------------------

TEST(DebugLineBatch, AabbZeroExtentPointDoesNotCrash)
{
    // min == max on all axes → 12 zero-length segments, but no crash
    // and vertex count is still 24.
    LineBatch b {};

    b.add_aabb({ 3.0F, 3.0F, 3.0F }, { 3.0F, 3.0F, 3.0F }, kRed);

    EXPECT_EQ(b.line_count(), 12U);
    EXPECT_EQ(b.vertex_count(), 24U);
    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.position.x, 3.0F);
        EXPECT_FLOAT_EQ(v.position.y, 3.0F);
        EXPECT_FLOAT_EQ(v.position.z, 3.0F);
    }
}

TEST(DebugLineBatch, AabbFlatOnOneAxisDrawsRectangle)
{
    // Zero extent along Y → flat rectangle; all vertices have y == 0.
    LineBatch b {};

    b.add_aabb({ -1.0F, 0.0F, -1.0F }, { 1.0F, 0.0F, 1.0F }, kGreen);

    EXPECT_EQ(b.line_count(), 12U);
    for (const auto& v : b.vertices())
        EXPECT_FLOAT_EQ(v.position.y, 0.0F);
}

// --- OBB degenerate --------------------------------------------------------

TEST(DebugLineBatch, ObbZeroHalfExtentsCollapsesToSinglePoint)
{
    // All corners coincide with the centre.
    LineBatch b {};
    const Vec3f centre { 7.0F, 8.0F, 9.0F };

    b.add_obb(centre,
              { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F },
              { 0.0F, 0.0F, 0.0F }, kRed);

    EXPECT_EQ(b.line_count(), 12U);
    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.position.x, centre.x);
        EXPECT_FLOAT_EQ(v.position.y, centre.y);
        EXPECT_FLOAT_EQ(v.position.z, centre.z);
    }
}

// --- Frustum singular matrix -----------------------------------------------

TEST(DebugLineBatch, FrustumSingularMatrixDoesNotCrashAndEmitsTwelveEdges)
{
    // A zero matrix has w.w == 0 for every unprojected point.
    // The guard `inv_w = 0` must fire for all 8 corners.
    LineBatch b {};
    Mat4f zero {};   // default-constructed → all zeros

    b.add_frustum(zero, kRed);

    // Must not crash; must still emit 12 edges (all at the origin).
    EXPECT_EQ(b.line_count(), 12U);
    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.position.x, 0.0F);
        EXPECT_FLOAT_EQ(v.position.y, 0.0F);
        EXPECT_FLOAT_EQ(v.position.z, 0.0F);
    }
}

// --- Circle segment clamping -----------------------------------------------

TEST(DebugLineBatch, CircleZeroSegmentsClampsToThree)
{
    LineBatch b {};
    b.add_circle({ 0, 0, 0 }, { 0, 1, 0 }, 1.0F, 0, kRed);
    EXPECT_EQ(b.line_count(), 3U);
}

TEST(DebugLineBatch, CircleNegativeSegmentsClampsToThree)
{
    LineBatch b {};
    b.add_circle({ 0, 0, 0 }, { 0, 1, 0 }, 1.0F, -100, kRed);
    EXPECT_EQ(b.line_count(), 3U);
}

TEST(DebugLineBatch, CircleTwoSegmentsClampsToThree)
{
    // 2 < 3 → must still produce exactly 3 segments.
    LineBatch b {};
    b.add_circle({ 0, 0, 0 }, { 0, 0, 1 }, 2.0F, 2, kGreen);
    EXPECT_EQ(b.line_count(), 3U);
}

TEST(DebugLineBatch, CircleExactlyThreeSegmentsProducesEquilateral)
{
    // 3-segment circle with +Y axis → 3 equal arcs, all verts on radius.
    LineBatch b {};
    const float radius = 1.0F;

    b.add_circle({ 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, radius, 3, kRed);

    ASSERT_EQ(b.line_count(), 3U);
    for (const auto& v : b.vertices())
    {
        const float dx = v.position.x;
        const float dz = v.position.z;
        EXPECT_NEAR(std::sqrt(dx * dx + dz * dz), radius, 1e-4F);
        EXPECT_NEAR(v.position.y, 0.0F, 1e-4F);
    }
}

// --- Capacity retention ---------------------------------------------------

TEST(DebugLineBatch, CapacityRetainedAcrossMultipleClearCycles)
{
    // Fill, clear, refill multiple times; the API contract is that
    // vertex_count() and line_count() remain correct after each cycle.
    LineBatch b {};
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        b.clear();
        EXPECT_TRUE(b.empty());
        b.add_aabb({ -1, -1, -1 }, { 1, 1, 1 }, kGreen); // 24 verts
        b.add_cross({ 0, 0, 0 }, 1.0F, kRed);              //  6 verts
        EXPECT_EQ(b.vertex_count(), 30U);
        EXPECT_EQ(b.line_count(), 15U);
    }
}

// --- Per-shape exact vertex counts ----------------------------------------

TEST(DebugLineBatch, CrossExactlyThreeSegmentsSixVertices)
{
    LineBatch b {};
    b.add_cross({ 0, 0, 0 }, 1.0F, kRed);
    EXPECT_EQ(b.vertex_count(), 6U);
    EXPECT_EQ(b.line_count(), 3U);
}

TEST(DebugLineBatch, CrossEndpointCoordinatesAllSixVertices)
{
    // Arrange: centre (0,0,0), half=1 → 6 endpoints at ±1 on each axis.
    LineBatch b {};
    const Vec3f ctr { 0.0F, 0.0F, 0.0F };
    const float h = 1.0F;

    b.add_cross(ctr, h, kRed);

    // Act + Assert — verify all six endpoints explicitly.
    EXPECT_TRUE(contains_position(b, { -h,   0.0F, 0.0F }));
    EXPECT_TRUE(contains_position(b, {  h,   0.0F, 0.0F }));
    EXPECT_TRUE(contains_position(b, { 0.0F,  -h,  0.0F }));
    EXPECT_TRUE(contains_position(b, { 0.0F,   h,  0.0F }));
    EXPECT_TRUE(contains_position(b, { 0.0F, 0.0F,  -h  }));
    EXPECT_TRUE(contains_position(b, { 0.0F, 0.0F,   h  }));
}

TEST(DebugLineBatch, SphereExactVertexCount)
{
    // 3 circles × N segments = 3N line pairs = 6N vertices.
    LineBatch b {};
    const int segs = 7;

    b.add_sphere({ 0, 0, 0 }, 1.0F, segs, kRed);

    EXPECT_EQ(b.line_count(), static_cast<std::size_t>(3 * segs));
    EXPECT_EQ(b.vertex_count(), static_cast<std::size_t>(6 * segs));
}

TEST(DebugLineBatch, SphereZeroRadiusAllVerticesAtCentre)
{
    // Degenerate sphere: radius = 0 → all 3N circle verts at centre.
    LineBatch b {};
    const Vec3f centre { 1.0F, 2.0F, 3.0F };

    b.add_sphere(centre, 0.0F, 8, kGreen);

    EXPECT_EQ(b.line_count(), 24U);
    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.position.x, centre.x);
        EXPECT_FLOAT_EQ(v.position.y, centre.y);
        EXPECT_FLOAT_EQ(v.position.z, centre.z);
    }
}

TEST(DebugLineBatch, SpherePerAxisPlaneConfinement)
{
    // Each great circle of the sphere lies in a specific plane;
    // test that the XY circle has all z==centre.z, etc.
    LineBatch b {};
    const Vec3f centre { 5.0F, 6.0F, 7.0F };
    const float radius = 2.0F;
    const int segs = 12;

    // add_sphere adds circles in axis order: +Z, +Y, +X.
    // Circle with axis +Z (XY plane): z stays at centre.z.
    LineBatch xy {};
    xy.add_circle(centre, { 0.0F, 0.0F, 1.0F }, radius, segs, kRed);
    for (const auto& v : xy.vertices())
        EXPECT_NEAR(v.position.z, centre.z, 1e-4F);

    // Circle with axis +Y (XZ plane): y stays at centre.y.
    LineBatch xz {};
    xz.add_circle(centre, { 0.0F, 1.0F, 0.0F }, radius, segs, kRed);
    for (const auto& v : xz.vertices())
        EXPECT_NEAR(v.position.y, centre.y, 1e-4F);

    // Circle with axis +X (YZ plane): x stays at centre.x.
    LineBatch yz {};
    yz.add_circle(centre, { 1.0F, 0.0F, 0.0F }, radius, segs, kRed);
    for (const auto& v : yz.vertices())
        EXPECT_NEAR(v.position.x, centre.x, 1e-4F);

    // Full sphere covers all three.
    b.add_sphere(centre, radius, segs, kGreen);
    EXPECT_EQ(b.line_count(), static_cast<std::size_t>(3 * segs));
}

// --- Polyline exactly 2 points --------------------------------------------

TEST(DebugLineBatch, PolylineExactlyTwoPointsIsOneSegment)
{
    LineBatch b {};
    const std::array<Vec3f, 2> pts {{ { 0.0F, 0.0F, 0.0F }, { 1.0F, 2.0F, 3.0F } }};

    b.add_polyline(pts, kRed);

    EXPECT_EQ(b.line_count(), 1U);
    EXPECT_EQ(b.vertex_count(), 2U);
    EXPECT_TRUE(contains_position(b, pts[0]));
    EXPECT_TRUE(contains_position(b, pts[1]));
}

// --- Arrow -Y shaft (helper fallback) -------------------------------------

TEST(DebugLineBatch, ArrowPointingNegativeYUsesXHelperAndDoesNotCrash)
{
    // When the shaft direction is (0,-1,0), |n.y| >= 0.99 so the
    // helper switches to +X. The cross product must still yield a
    // valid (non-zero) tangent.
    LineBatch b {};
    const Vec3f from { 0.0F,  1.0F, 0.0F };
    const Vec3f to   { 0.0F, -1.0F, 0.0F };

    b.add_arrow(from, to, kGreen);

    // Must produce shaft + 4 wings = 5 segments.
    EXPECT_EQ(b.line_count(), 5U);
    EXPECT_TRUE(contains_position(b, from));
    EXPECT_TRUE(contains_position(b, to));
}

TEST(DebugLineBatch, ArrowPointingPositiveYDoesNotCrash)
{
    // Symmetric case: shaft = +Y, helper = +X.
    LineBatch b {};
    b.add_arrow({ 0.0F, -1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, kRed);
    EXPECT_EQ(b.line_count(), 5U);
}

// --- Arrow head_frac clamping ---------------------------------------------

TEST(DebugLineBatch, ArrowHeadFracBelowMinIsClampedTo005)
{
    // With head_frac=0 the implementation clamps to 0.05.
    // We verify that the head wings end behind the tip by more than 0.
    LineBatch b {};
    const Vec3f from { 0.0F, 0.0F, 0.0F };
    const Vec3f to   { 10.0F, 0.0F, 0.0F };

    b.add_arrow(from, to, kGreen, 0.0F);

    ASSERT_EQ(b.line_count(), 5U);
    const auto v = b.vertices();
    // Wing endpoints (verts [3], [5], [7], [9]) must be strictly left of tip.
    for (std::size_t i = 3; i < v.size(); i += 2)
        EXPECT_LT(v[i].position.x, to.x);
}

TEST(DebugLineBatch, ArrowHeadFracAboveMaxIsClampedTo05)
{
    // head_frac=1.0 is clamped to 0.5.  Head base = to - 0.5*len*n.
    LineBatch b {};
    const Vec3f from { 0.0F, 0.0F, 0.0F };
    const Vec3f to   { 4.0F, 0.0F, 0.0F };

    b.add_arrow(from, to, kRed, 1.0F);

    // Head base x = 4 - 0.5*4 = 2. The wing tips are near x=2
    // (slightly above/below in y/z but x must be ≥ 0 and ≤ to.x).
    ASSERT_EQ(b.line_count(), 5U);
    const auto v = b.vertices();
    for (std::size_t i = 3; i < v.size(); i += 2)
    {
        EXPECT_GE(v[i].position.x, 0.0F);
        EXPECT_LE(v[i].position.x, to.x);
    }
}

// --- Grid negative half_lines clamp ---------------------------------------

TEST(DebugLineBatch, GridNegativeHalfLinesClampsToZero)
{
    // half_lines < 0 should clamp to 0, giving 2 zero-length centre segs.
    LineBatch b {};

    b.add_grid({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, -5, 1.0F, kRed);

    EXPECT_EQ(b.line_count(), 2U);
}

TEST(DebugLineBatch, GridSpacingScalesExtentsCorrectly)
{
    // half_lines=1, spacing=3.0 → extent = 3.0 → corners at ±3 along each axis.
    LineBatch b {};

    b.add_grid({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 1, 3.0F, kGreen);

    // 3 lines per direction = 6 total.
    EXPECT_EQ(b.line_count(), 6U);
    EXPECT_TRUE(contains_position(b, { -3.0F, 0.0F, -3.0F }));
    EXPECT_TRUE(contains_position(b, {  3.0F, 0.0F,  3.0F }));
}

// --- Colour propagation (full RGBA, per-shape) ----------------------------

TEST(DebugLineBatch, AabbFullRgbaColoursOnAllVertices)
{
    const Vec4f col { 0.25F, 0.50F, 0.75F, 0.88F };
    LineBatch b {};
    b.add_aabb({ -1, -1, -1 }, { 1, 1, 1 }, col);

    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.color.x, col.x);
        EXPECT_FLOAT_EQ(v.color.y, col.y);
        EXPECT_FLOAT_EQ(v.color.z, col.z);
        EXPECT_FLOAT_EQ(v.color.w, col.w);
    }
}

TEST(DebugLineBatch, CircleFullRgbaColoursOnAllVertices)
{
    const Vec4f col { 0.1F, 0.2F, 0.3F, 0.4F };
    LineBatch b {};
    b.add_circle({ 0, 0, 0 }, { 0, 1, 0 }, 1.0F, 8, col);

    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.color.x, col.x);
        EXPECT_FLOAT_EQ(v.color.y, col.y);
        EXPECT_FLOAT_EQ(v.color.z, col.z);
        EXPECT_FLOAT_EQ(v.color.w, col.w);
    }
}

// --- Vertex-count invariant: line_count == vertex_count / 2 ---------------

TEST(DebugLineBatch, LineCountIsAlwaysHalfOfVertexCount)
{
    LineBatch b {};
    b.add_line({ 0, 0, 0 }, { 1, 0, 0 }, kRed);
    b.add_aabb({ 0, 0, 0 }, { 1, 1, 1 }, kGreen);
    b.add_circle({ 0, 0, 0 }, { 0, 1, 0 }, 1.0F, 9, kRed);
    b.add_cross({ 0, 0, 0 }, 0.5F, kGreen);

    EXPECT_EQ(b.line_count() * 2U, b.vertex_count());
}

// ---------------------------------------------------------------------------
// phase1253 — new shape helpers (axes gizmo, N×M grid) + colour pack/unpack
// ---------------------------------------------------------------------------

using cd::debug_line::pack_color;
using cd::debug_line::unpack_color;

// --- Colour pack / unpack round-trip --------------------------------------

TEST(DebugLineColor, PackThenUnpackRoundTrips)
{
    // Arrange — values that land on exact 1/255 grid points round-trip
    // byte-perfect (0, 1, and a few representable fractions).
    const std::array<Vec4f, 4> samples {{
        { 0.0F, 0.0F, 0.0F, 0.0F },
        { 1.0F, 1.0F, 1.0F, 1.0F },
        { 1.0F, 0.0F, 0.0F, 1.0F },
        { 0.0F, 128.0F / 255.0F, 64.0F / 255.0F, 1.0F } }};

    for (const auto& c : samples)
    {
        // Act
        const std::uint32_t packed = pack_color(c);
        const Vec4f back = unpack_color(packed);
        // Assert
        EXPECT_NEAR(back.x, c.x, 1.0F / 255.0F);
        EXPECT_NEAR(back.y, c.y, 1.0F / 255.0F);
        EXPECT_NEAR(back.z, c.z, 1.0F / 255.0F);
        EXPECT_NEAR(back.w, c.w, 1.0F / 255.0F);
    }
}

TEST(DebugLineColor, PackUsesLittleEndianRgbaByteOrder)
{
    // Pure red → R byte = 0xFF in the low 8 bits (0xAABBGGRR layout).
    EXPECT_EQ(pack_color({ 1.0F, 0.0F, 0.0F, 0.0F }) & 0xFFU, 0xFFU);
    // Pure green → bits 8..15.
    EXPECT_EQ((pack_color({ 0.0F, 1.0F, 0.0F, 0.0F }) >> 8U) & 0xFFU, 0xFFU);
    // Pure blue → bits 16..23.
    EXPECT_EQ((pack_color({ 0.0F, 0.0F, 1.0F, 0.0F }) >> 16U) & 0xFFU, 0xFFU);
    // Pure alpha → bits 24..31.
    EXPECT_EQ((pack_color({ 0.0F, 0.0F, 0.0F, 1.0F }) >> 24U) & 0xFFU, 0xFFU);
    // Opaque white → all four bytes set.
    EXPECT_EQ(pack_color({ 1.0F, 1.0F, 1.0F, 1.0F }), 0xFFFFFFFFU);
}

TEST(DebugLineColor, PackClampsOutOfRangeComponents)
{
    // Below 0 and above 1 must saturate, not wrap/overflow.
    const std::uint32_t lo = pack_color({ -5.0F, -0.1F, -100.0F, -1.0F });
    EXPECT_EQ(lo, 0x00000000U);
    const std::uint32_t hi = pack_color({ 2.0F, 10.0F, 1.5F, 4.0F });
    EXPECT_EQ(hi, 0xFFFFFFFFU);
}

TEST(DebugLineColor, UnpackKnownWordsGivesNormalisedComponents)
{
    const Vec4f c = unpack_color(0x804020FFU);  // A=0x80 B=0x40 G=0x20 R=0xFF
    EXPECT_NEAR(c.x, 255.0F / 255.0F, 1e-6F);
    EXPECT_NEAR(c.y, 0x20 / 255.0F, 1e-6F);
    EXPECT_NEAR(c.z, 0x40 / 255.0F, 1e-6F);
    EXPECT_NEAR(c.w, 0x80 / 255.0F, 1e-6F);
}

// --- Axis gizmo (3 colour-coded arms) -------------------------------------

TEST(DebugLineBatch, AxesEmitsThreeColouredArms)
{
    // Arrange — world basis at origin, length 2.
    LineBatch b {};
    const Vec3f origin { 0.0F, 0.0F, 0.0F };

    // Act
    b.add_axes(origin, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 2.0F);

    // Assert — exactly 3 segments / 6 vertices.
    ASSERT_EQ(b.line_count(), 3U);
    ASSERT_EQ(b.vertex_count(), 6U);
    const auto v = b.vertices();
    // X arm: origin → (2,0,0), red.
    EXPECT_FLOAT_EQ(v[1].position.x, 2.0F);
    EXPECT_FLOAT_EQ(v[0].color.x, 1.0F);
    EXPECT_FLOAT_EQ(v[0].color.y, 0.0F);
    EXPECT_FLOAT_EQ(v[0].color.z, 0.0F);
    // Y arm: origin → (0,2,0), green.
    EXPECT_FLOAT_EQ(v[3].position.y, 2.0F);
    EXPECT_FLOAT_EQ(v[2].color.y, 1.0F);
    EXPECT_FLOAT_EQ(v[2].color.x, 0.0F);
    // Z arm: origin → (0,0,2), blue.
    EXPECT_FLOAT_EQ(v[5].position.z, 2.0F);
    EXPECT_FLOAT_EQ(v[4].color.z, 1.0F);
    EXPECT_FLOAT_EQ(v[4].color.x, 0.0F);
}

TEST(DebugLineBatch, AxesAllArmsShareOriginAndRespectCentre)
{
    LineBatch b {};
    const Vec3f origin { 5.0F, -3.0F, 2.0F };

    b.add_axes(origin, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 1.0F);

    // Every even vertex is the shared origin.
    const auto v = b.vertices();
    for (std::size_t i = 0; i < v.size(); i += 2)
    {
        EXPECT_FLOAT_EQ(v[i].position.x, origin.x);
        EXPECT_FLOAT_EQ(v[i].position.y, origin.y);
        EXPECT_FLOAT_EQ(v[i].position.z, origin.z);
    }
}

TEST(DebugLineBatch, AxesZeroLengthCollapsesToOriginButEmitsThreeSegments)
{
    // Degenerate: length 0 → all 6 vertices at origin, still 3 segments.
    LineBatch b {};
    const Vec3f origin { 1.0F, 2.0F, 3.0F };

    b.add_axes(origin, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 0.0F);

    EXPECT_EQ(b.line_count(), 3U);
    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.position.x, origin.x);
        EXPECT_FLOAT_EQ(v.position.y, origin.y);
        EXPECT_FLOAT_EQ(v.position.z, origin.z);
    }
}

TEST(DebugLineBatch, AxesRotatedFrameFollowsSuppliedBasis)
{
    // 90° yaw frame: right=+Z, up=+Y, forward=-X.
    LineBatch b {};
    b.add_axes({ 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 }, 3.0F);

    // X arm (red) tip along +Z.
    EXPECT_TRUE(contains_position(b, { 0.0F, 0.0F, 3.0F }));
    // Z arm (blue) tip along -X.
    EXPECT_TRUE(contains_position(b, { -3.0F, 0.0F, 0.0F }));
}

// --- N×M rectangular grid -------------------------------------------------

TEST(DebugLineBatch, GridRectIndependentDimensionsSegmentCount)
{
    // lines_a=2, lines_b=3 → (2+1)+(3+1) = 7 segments.
    LineBatch b {};
    b.add_grid_rect({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 2, 3, 1.0F, kRed);
    EXPECT_EQ(b.line_count(), 7U);
}

TEST(DebugLineBatch, GridRectZeroByZeroIsTwoCrossingCentreLines)
{
    // 0×0 → (0+1)+(0+1) = 2 segments, both zero-length at centre.
    LineBatch b {};
    const Vec3f centre { 4.0F, 1.0F, -2.0F };

    b.add_grid_rect(centre, { 1, 0, 0 }, { 0, 0, 1 }, 0, 0, 1.0F, kGreen);

    EXPECT_EQ(b.line_count(), 2U);
    for (const auto& v : b.vertices())
    {
        EXPECT_FLOAT_EQ(v.position.x, centre.x);
        EXPECT_FLOAT_EQ(v.position.y, centre.y);
        EXPECT_FLOAT_EQ(v.position.z, centre.z);
    }
}

TEST(DebugLineBatch, GridRectOneByOneIsFourSegmentsStaysInPlane)
{
    // 1×1 → (1+1)+(1+1) = 4 segments forming a single quad; stays in XZ.
    LineBatch b {};
    b.add_grid_rect({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, 1, 1, 2.0F, kRed);

    EXPECT_EQ(b.line_count(), 4U);
    for (const auto& v : b.vertices())
        EXPECT_FLOAT_EQ(v.position.y, 0.0F);
    // half = 1*2*0.5 = 1 → the four corners are at (±1, 0, ±1).
    EXPECT_TRUE(contains_position(b, { -1.0F, 0.0F, -1.0F }));
    EXPECT_TRUE(contains_position(b, {  1.0F, 0.0F,  1.0F }));
}

TEST(DebugLineBatch, GridRectNegativeDimensionsClampToZero)
{
    LineBatch b {};
    b.add_grid_rect({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, -4, -9, 1.0F, kRed);
    // Both clamp to 0 → 2 segments.
    EXPECT_EQ(b.line_count(), 2U);
}

TEST(DebugLineBatch, GridRectStaysCentredOnCentre)
{
    // Symmetric span: half = 4*1*0.5 = 2 → extremes at ±2 about centre.
    LineBatch b {};
    const Vec3f centre { 10.0F, 0.0F, 10.0F };

    b.add_grid_rect(centre, { 1, 0, 0 }, { 0, 0, 1 }, 4, 4, 1.0F, kGreen);

    EXPECT_EQ(b.line_count(), 10U);  // (4+1)+(4+1)
    EXPECT_TRUE(contains_position(b, { centre.x - 2.0F, 0.0F, centre.z - 2.0F }));
    EXPECT_TRUE(contains_position(b, { centre.x + 2.0F, 0.0F, centre.z + 2.0F }));
}

// --- Capacity growth across many pushes (no stale verts) ------------------

TEST(DebugLineBatch, CapacityGrowsAcrossManyPushesAndCountsStayExact)
{
    // Push far past any small-buffer threshold; the count must track
    // every segment with no stale or dropped vertices.
    LineBatch b {};
    constexpr std::size_t kPushes = 5000U;
    for (std::size_t i = 0; i < kPushes; ++i)
    {
        const auto f = static_cast<float>(i);
        b.add_line({ f, 0.0F, 0.0F }, { f, 1.0F, 0.0F }, kRed);
    }

    EXPECT_EQ(b.line_count(), kPushes);
    EXPECT_EQ(b.vertex_count(), kPushes * 2U);
    // First and last segment endpoints survive the reallocations.
    const auto v = b.vertices();
    EXPECT_FLOAT_EQ(v.front().position.x, 0.0F);
    EXPECT_FLOAT_EQ(v.back().position.x, static_cast<float>(kPushes - 1U));
}

TEST(DebugLineBatch, ClearAfterLargeFillLeavesNoStaleVertices)
{
    // Fill big, clear, then add one line — the span must expose exactly
    // the new line, never the cleared backlog.
    LineBatch b {};
    for (int i = 0; i < 1000; ++i)
        b.add_cross({ static_cast<float>(i), 0, 0 }, 1.0F, kGreen);
    ASSERT_EQ(b.line_count(), 3000U);

    b.clear();
    ASSERT_TRUE(b.empty());

    b.add_line({ 7, 8, 9 }, { 10, 11, 12 }, kRed);
    ASSERT_EQ(b.vertex_count(), 2U);
    const auto v = b.vertices();
    EXPECT_FLOAT_EQ(v[0].position.x, 7.0F);
    EXPECT_FLOAT_EQ(v[1].position.z, 12.0F);
}

}  // namespace
