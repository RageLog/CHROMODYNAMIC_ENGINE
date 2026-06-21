#include <cd/decal/Decal.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::decal::Decal;
using cd::decal::decal_intersects_aabb;
using cd::decal::project_world_to_decal;

constexpr float kEps = 1e-3F;

TEST(Decal, ProjectCentreSampleAtUvHalf)
{
    Decal d {};
    cd::math::Vec3f local;
    std::array<float, 2> uv {};
    const bool inside = project_world_to_decal(d, { 0, 0, 0 }, local, uv);
    EXPECT_TRUE(inside);
    EXPECT_NEAR(uv[0], 0.5F, kEps);
    EXPECT_NEAR(uv[1], 0.5F, kEps);
}

TEST(Decal, ProjectOutsideRejected)
{
    Decal d {};
    cd::math::Vec3f local;
    std::array<float, 2> uv {};
    EXPECT_FALSE(project_world_to_decal(d, { 5, 0, 0 }, local, uv));
}

TEST(Decal, ProjectAtEdgeProducesEdgeUv)
{
    Decal d {};  // half_extents = (0.5, 0.5, 0.5)
    cd::math::Vec3f local;
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 0.5F, 0, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 1.0F, kEps);
}

TEST(Decal, AabbIntersectionTrueForOverlap)
{
    Decal d {};
    EXPECT_TRUE(decal_intersects_aabb(d, { -1, -1, -1 }, { 1, 1, 1 }));
}

TEST(Decal, AabbIntersectionFalseForDisjoint)
{
    Decal d {};
    EXPECT_FALSE(decal_intersects_aabb(d, { 10, 10, 10 }, { 11, 11, 11 }));
}

TEST(Decal, GlslHelperNonEmpty)
{
    EXPECT_FALSE(cd::decal::kDecalGlsl.empty());
    EXPECT_NE(cd::decal::kDecalGlsl.find("decal_project"),
              std::string_view::npos);
}

// ---- ADD-ONLY host tests (floor-raise; existing math untouched) -------------
//
// All assertions below verify the EXISTING projection / binning math against
// the actual code in Decal.hpp. No header change, no golden impact. AAA layout
// with explicit edge + negative coverage.

// 1. project: world->decal-local coordinates are the OBB-axis dot products.
//    out_local is filled BEFORE the inside test, so it is valid even outside.
TEST(Decal, ProjectFillsLocalCoordsAlongObbAxes)
{
    Decal d {};  // axis-aligned, half_extents = (0.5, 0.5, 0.5)
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    const bool inside = project_world_to_decal(d, { 0.25F, -0.1F, 0.4F }, local, uv);
    EXPECT_TRUE(inside);
    EXPECT_NEAR(local.x, 0.25F, kEps);
    EXPECT_NEAR(local.y, -0.1F, kEps);
    EXPECT_NEAR(local.z, 0.4F, kEps);
}

// 2. project: a point past +Z (the projection axis = -forward) is culled.
TEST(Decal, ProjectBehindDecalCulledOnForwardAxis)
{
    Decal d {};
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    // lz = 1.0 > half_extents.z (0.5) -> outside.
    EXPECT_FALSE(project_world_to_decal(d, { 0, 0, 1.0F }, local, uv));
    EXPECT_NEAR(local.z, 1.0F, kEps);  // local still computed.
}

// 3. project: opposite-edge UV maps to the rect minimum (lx = -hx -> u0).
TEST(Decal, ProjectAtNegEdgeProducesZeroUv)
{
    Decal d {};
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { -0.5F, -0.5F, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.0F, kEps);
    EXPECT_NEAR(uv[1], 0.0F, kEps);
}

// 4. project: the boundary is inclusive (strict '>' in the cull) -> on-face hit.
TEST(Decal, ProjectBoundaryInclusive)
{
    Decal d {};
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 0, 0, 0.5F }, local, uv));
}

// 5. project: a non-trivial atlas_uv_rect remaps the centre to its midpoint.
TEST(Decal, ProjectHonoursAtlasUvRect)
{
    Decal d {};
    d.atlas_uv_rect = { 0.2F, 0.6F, 0.4F, 1.0F };  // (u0,v0,u1,v1)
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 0, 0, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.3F, kEps);  // midpoint of [0.2, 0.4]
    EXPECT_NEAR(uv[1], 0.8F, kEps);  // midpoint of [0.6, 1.0]
}

// 6. project: a 90deg yaw (right<->forward swap) reads world Z onto local X.
//    right = (0,0,1) so lx = rel.z; a world +Z point lands on the U edge.
TEST(Decal, ProjectRotatedObbReadsWorldZOntoU)
{
    Decal d {};
    d.right   = { 0, 0, 1 };
    d.up      = { 0, 1, 0 };
    d.forward = { 1, 0, 0 };  // projection axis is now world -X
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 0, 0, 0.5F }, local, uv));
    EXPECT_NEAR(local.x, 0.5F, kEps);
    EXPECT_NEAR(uv[0], 1.0F, kEps);  // lx = +hx -> u edge
}

// 7. project: non-uniform half-extents scale each axis independently.
TEST(Decal, ProjectNonUniformExtents)
{
    Decal d {};
    d.half_extents = { 2.0F, 0.25F, 1.0F };
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    // x = 1.0 is inside (hx = 2) but y = 0.5 is outside (hy = 0.25).
    EXPECT_FALSE(project_world_to_decal(d, { 1.0F, 0.5F, 0 }, local, uv));
    EXPECT_TRUE(project_world_to_decal(d, { 1.0F, 0.2F, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.75F, kEps);  // 1.0 / 2.0 * 0.5 + 0.5
}

// 8. project: a translated decal subtracts its centre before projecting.
TEST(Decal, ProjectRespectsDecalPosition)
{
    Decal d {};
    d.position = { 10, 0, 0 };
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 10, 0, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.5F, kEps);
    EXPECT_FALSE(project_world_to_decal(d, { 0, 0, 0 }, local, uv));  // 10 units away
}

// 9. binning: an AABB that merely touches the decal face counts as overlap
//    (the test uses inclusive '<'/'>' so shared boundaries intersect).
TEST(Decal, AabbIntersectionTouchingFaceIsOverlap)
{
    Decal d {};  // world AABB = [-0.5, 0.5]^3
    EXPECT_TRUE(decal_intersects_aabb(d, { 0.5F, -0.1F, -0.1F },
                                         { 1.5F, 0.1F, 0.1F }));
}

// 10. binning: a contained AABB (fully inside the decal volume) overlaps.
TEST(Decal, AabbIntersectionContainedIsOverlap)
{
    Decal d {};
    EXPECT_TRUE(decal_intersects_aabb(d, { -0.1F, -0.1F, -0.1F },
                                         { 0.1F, 0.1F, 0.1F }));
}

// 11. binning: disjoint only along a single axis (Y) is still rejected.
TEST(Decal, AabbIntersectionSingleAxisGapRejected)
{
    Decal d {};  // world AABB = [-0.5, 0.5]^3
    // X and Z overlap, Y gap of 0.1 -> separating axis on Y.
    EXPECT_FALSE(decal_intersects_aabb(d, { -0.2F, 0.6F, -0.2F },
                                          { 0.2F, 1.0F, 0.2F }));
}

// 12. binning: a 45deg yaw grows the decal's world-AABB to half*sqrt(2),
//    so a point that fell outside the unrotated box now intersects.
TEST(Decal, AabbIntersectionRotatedObbWidensBounds)
{
    Decal d {};
    constexpr float kC = 0.70710678F;  // cos/sin 45deg
    d.right   = { kC, kC, 0 };
    d.up      = { -kC, kC, 0 };
    d.forward = { 0, 0, 1 };
    // Unrotated world AABB x-extent is 0.5; rotated it is ~0.7071.
    // A thin AABB at x in [0.6, 0.65] misses the unrotated box but
    // hits the rotated decal's widened world-AABB.
    EXPECT_TRUE(decal_intersects_aabb(d, { 0.6F, -0.05F, -0.05F },
                                         { 0.65F, 0.05F, 0.05F }));
}

// 13. binning: a degenerate zero-size decal still overlaps an AABB straddling
//    its centre (the 8 corners collapse to the centre point).
TEST(Decal, AabbIntersectionZeroSizeDecalAtCentre)
{
    Decal d {};
    d.half_extents = { 0, 0, 0 };
    EXPECT_TRUE(decal_intersects_aabb(d, { -1, -1, -1 }, { 1, 1, 1 }));
    EXPECT_FALSE(decal_intersects_aabb(d, { 1, 1, 1 }, { 2, 2, 2 }));
}

// 14. binning: an offset decal's world-AABB tracks its position.
TEST(Decal, AabbIntersectionTranslatedDecal)
{
    Decal d {};
    d.position = { 5, 0, 0 };  // world AABB = [4.5,5.5] x [-0.5,0.5]^2
    EXPECT_TRUE(decal_intersects_aabb(d, { 4, -1, -1 }, { 5, 1, 1 }));
    EXPECT_FALSE(decal_intersects_aabb(d, { -1, -1, -1 }, { 0, 1, 1 }));
}

// 15. GLSL contract: the host UV formula is mirrored byte-for-byte in the
//    shader string, so CPU binning and GPU projection stay consistent.
TEST(Decal, GlslMirrorsHostProjectionContract)
{
    constexpr std::string_view g = cd::decal::kDecalGlsl;
    EXPECT_NE(g.find("dot(rel, d.right)"), std::string_view::npos);
    EXPECT_NE(g.find("dot(rel, d.up)"), std::string_view::npos);
    EXPECT_NE(g.find("dot(rel, d.forward)"), std::string_view::npos);
    EXPECT_NE(g.find("d.half_ext.x"), std::string_view::npos);
    EXPECT_NE(g.find("uv_rect"), std::string_view::npos);
    EXPECT_NE(g.find("return false"), std::string_view::npos);
}

}  // namespace
