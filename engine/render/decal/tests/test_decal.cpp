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

// ---- COMPREHENSIVE ADD-ONLY host tests (charter-complete floor-raise) -------
//
// All tests below verify EXISTING behaviour only — no header change, no golden
// impact.  Groups:  P = projection,  B = binning,  G = GLSL contract,
//                   S = struct/enum field invariants.

// P16. A point outside ALL THREE axes simultaneously is rejected and the local
//      coords for all three axes are still populated.
TEST(Decal, ProjectDiagonallyOutsideRejectsAllAxes)
{
    Decal d {};  // half_extents = (0.5, 0.5, 0.5)
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    const bool inside = project_world_to_decal(d, { 1.0F, 1.0F, 1.0F }, local, uv);
    EXPECT_FALSE(inside);
    EXPECT_NEAR(local.x, 1.0F, kEps);
    EXPECT_NEAR(local.y, 1.0F, kEps);
    EXPECT_NEAR(local.z, 1.0F, kEps);
}

// P17. When the point is outside the OBB the out_uv array must NOT be written
//      (the impl returns false before filling uv; the caller's array stays at
//      its initialised value of {9,9}).
TEST(Decal, ProjectOutsideDoesNotWriteUv)
{
    Decal d {};
    cd::math::Vec3f local {};
    std::array<float, 2> uv { 9.0F, 9.0F };
    const bool inside = project_world_to_decal(d, { 5.0F, 0, 0 }, local, uv);
    EXPECT_FALSE(inside);
    // uv must be untouched because the early-return fires before the uv assign.
    EXPECT_NEAR(uv[0], 9.0F, kEps);
    EXPECT_NEAR(uv[1], 9.0F, kEps);
}

// P18. The +X/+Y OBB face corner maps to UV (1, 1).
TEST(Decal, ProjectPlusXPlusYCornerUvIsOneOne)
{
    Decal d {};
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 0.5F, 0.5F, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 1.0F, kEps);
    EXPECT_NEAR(uv[1], 1.0F, kEps);
}

// P19. The -X/+Y OBB face corner maps to UV (0, 1).
TEST(Decal, ProjectMinusXPlusYCornerUvIsZeroOne)
{
    Decal d {};
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { -0.5F, 0.5F, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.0F, kEps);
    EXPECT_NEAR(uv[1], 1.0F, kEps);
}

// P20. Large non-uniform extents: fractional local coordinates produce the
//      correct UV fractions.
//      hx=4, hy=2; point at (1, -0.5, 0): lx=1 -> u=1/4*0.5+0.5=0.625;
//      ly=-0.5 -> v=-0.5/2*0.5+0.5=0.375.
TEST(Decal, ProjectFractionalUvWithLargeExtents)
{
    Decal d {};
    d.half_extents = { 4.0F, 2.0F, 1.0F };
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 1.0F, -0.5F, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.625F, kEps);
    EXPECT_NEAR(uv[1], 0.375F, kEps);
}

// P21. A decal at a negative world-space position correctly subtracts the
//      centre: the point AT the decal centre projects to UV (0.5, 0.5).
TEST(Decal, ProjectNegativePositionCentreIsHalf)
{
    Decal d {};
    d.position = { -100.0F, -200.0F, -50.0F };
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(
        d, { -100.0F, -200.0F, -50.0F }, local, uv));
    EXPECT_NEAR(uv[0], 0.5F, kEps);
    EXPECT_NEAR(uv[1], 0.5F, kEps);
}

// P22. Two separate decal volumes project the same world point
//      independently; each decal gives its own UV answer.
TEST(Decal, ProjectTwoDecalsIndependent)
{
    Decal d1 {};
    d1.position = { 0, 0, 0 };

    Decal d2 {};
    d2.position     = { 3.0F, 0, 0 };
    d2.half_extents = { 1.0F, 1.0F, 1.0F };

    const cd::math::Vec3f p { 3.0F, 0, 0 };  // inside d2, outside d1

    cd::math::Vec3f local {};
    std::array<float, 2> uv {};

    EXPECT_FALSE(project_world_to_decal(d1, p, local, uv));
    EXPECT_TRUE(project_world_to_decal(d2, p, local, uv));
    EXPECT_NEAR(uv[0], 0.5F, kEps);
    EXPECT_NEAR(uv[1], 0.5F, kEps);
}

// P23. A 90° roll (swap up/right sense) — up=(1,0,0), right=(0,1,0) — maps
//      world Y onto local X (U axis) and world X onto local Y (V axis).
TEST(Decal, ProjectRolled90DegreeMapsAxesCorrectly)
{
    Decal d {};
    d.right   = { 0, 1, 0 };
    d.up      = { 1, 0, 0 };
    d.forward = { 0, 0, 1 };
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    // World point (0.3, 0, 0): lx = dot({0,1,0},{0.3,0,0}) = 0
    //                          ly = dot({1,0,0},{0.3,0,0}) = 0.3
    EXPECT_TRUE(project_world_to_decal(d, { 0.3F, 0, 0 }, local, uv));
    EXPECT_NEAR(local.x, 0.0F, kEps);
    EXPECT_NEAR(local.y, 0.3F, kEps);
    EXPECT_NEAR(uv[1], 0.5F + 0.3F / 0.5F * 0.5F, kEps);  // = 0.8
}

// P24. atlas_uv_rect with u0==u1 (degenerate — zero-width atlas slot) maps
//      every X position to the same u value.
TEST(Decal, ProjectDegenerateAtlasRectZeroWidth)
{
    Decal d {};
    d.atlas_uv_rect = { 0.4F, 0.0F, 0.4F, 1.0F };  // zero u-width
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_TRUE(project_world_to_decal(d, { 0.0F, 0, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.4F, kEps);
    EXPECT_TRUE(project_world_to_decal(d, { 0.5F, 0, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 0.4F, kEps);
}

// B16. Conservative false-positive SEAL: a 45° Z-rotated OBB's world-AABB
//      covers the corner region [0.5,0.6]^2 × z-thin, but the OBB itself
//      does NOT cover that corner (the point (0.55,0.55,0) has lx≈0.78 >
//      half_ext.x=0.5 under the OBB axes). The conservative AABB-vs-AABB
//      test returns TRUE — that is the documented INTENTIONAL SEAL behaviour;
//      a full 15-axis OBB-SAT would return FALSE.
//      This test LOCKS the conservative semantics so they cannot be silently
//      changed to a full SAT without surfacing here.
TEST(Decal, AabbIntersectionConservativeFalsePositiveSeal)
{
    Decal d {};
    constexpr float kC = 0.70710678F;  // cos/sin 45 deg
    d.right   = { kC,  kC, 0 };
    d.up      = { -kC, kC, 0 };
    d.forward = { 0, 0, 1 };
    // decal world-AABB is approx [-0.707,0.707]^2 × [-0.5,0.5]
    // AABB [0.5,0.6]^2 × [-0.05,0.05] is inside the world-AABB corner
    // but outside the actual OBB volume.
    // Conservative test MUST return true (world-AABB overlap).
    EXPECT_TRUE(decal_intersects_aabb(d,
        { 0.5F, 0.5F, -0.05F }, { 0.6F, 0.6F, 0.05F }));
    // Verify the point is actually outside the OBB (seal the false-positive).
    cd::math::Vec3f local {};
    std::array<float, 2> uv {};
    EXPECT_FALSE(project_world_to_decal(d, { 0.55F, 0.55F, 0 }, local, uv));
}

// B17. AABB that fully WRAPS the decal (decal fully inside the tile) — overlap.
TEST(Decal, AabbIntersectionDecalFullyInsideAabb)
{
    Decal d {};  // world AABB = [-0.5, 0.5]^3
    EXPECT_TRUE(decal_intersects_aabb(d, { -10, -10, -10 }, { 10, 10, 10 }));
}

// B18. AABB shares only the single corner point (0.5,0.5,0.5) with the decal
//      world-AABB (the decal's mx equals the tile's mn on all axes).
//      The NaN-preserving negated form (!(mx<mn) && !(mn>mx)) is inclusive
//      on both sides, so touching at a corner counts as overlap.
TEST(Decal, AabbIntersectionSingleCornerTouchIsOverlap)
{
    Decal d {};  // mx = (0.5, 0.5, 0.5)
    EXPECT_TRUE(decal_intersects_aabb(d,
        { 0.5F, 0.5F, 0.5F }, { 1.0F, 1.0F, 1.0F }));
}

// B19. Gap on -X side: AABB at x in [-3,-2] is cleanly separated from the
//      decal world-AABB (mn.x = -0.5), so conservative test must return false.
TEST(Decal, AabbIntersectionNegativeXGapRejected)
{
    Decal d {};
    EXPECT_FALSE(decal_intersects_aabb(d,
        { -3.0F, -0.1F, -0.1F }, { -2.0F, 0.1F, 0.1F }));
}

// B20. Large negative-coordinate decal: world-AABB tracks its position in
//      negative octant correctly.
TEST(Decal, AabbIntersectionNegativeCoordDecal)
{
    Decal d {};
    d.position = { -50.0F, -50.0F, -50.0F };
    // world AABB = [-50.5, -49.5]^3
    EXPECT_TRUE(decal_intersects_aabb(d,
        { -51, -51, -51 }, { -49, -49, -49 }));
    EXPECT_FALSE(decal_intersects_aabb(d,
        { -48, -48, -48 }, { -47, -47, -47 }));
}

// B21. Asymmetric half-extents with non-trivial rotation — world-AABB shape
//      must reflect all three axis contributions.
//      90° yaw: right=(0,0,1), up=(0,1,0), forward=(1,0,0).
//      half_extents=(2,0.5,0.5). The world-x span comes only from forward
//      (0.5) and right (0 in x). Wait — right=(0,0,1) so right contributes
//      nothing to world-x. forward=(1,0,0) contributes hz=0.5 to world-x.
//      So world-AABB x is [-0.5, 0.5].  z-span: right.z=1 → 2 units → [-2,2].
//      An AABB at x=[1,2] (no x-overlap) must be rejected.
TEST(Decal, AabbIntersectionAsymmetricExtentsRotated)
{
    Decal d {};
    d.right       = { 0, 0, 1 };
    d.up          = { 0, 1, 0 };
    d.forward     = { 1, 0, 0 };
    d.half_extents = { 2.0F, 0.5F, 0.5F };
    // world-AABB: x in [-0.5,0.5], y in [-0.5,0.5], z in [-2,2]
    EXPECT_FALSE(decal_intersects_aabb(d,
        { 1.0F, -0.1F, -0.1F }, { 2.0F, 0.1F, 0.1F }));
    // AABB straddling z-range (z in [1,3]): overlaps world-z [−2,2].
    EXPECT_TRUE(decal_intersects_aabb(d,
        { -0.1F, -0.1F, 1.0F }, { 0.1F, 0.1F, 3.0F }));
}

// B22. The 6 face-touching cases: one AABB touching each face of the axis-
//      aligned decal world-AABB; all six must return true (inclusive boundary).
TEST(Decal, AabbIntersectionAllSixFacesTouchOverlap)
{
    Decal d {};  // world AABB = [-0.5,0.5]^3
    // +X face touch
    EXPECT_TRUE(decal_intersects_aabb(d, { 0.5F,-0.1F,-0.1F },{ 1.0F,0.1F,0.1F }));
    // -X face touch
    EXPECT_TRUE(decal_intersects_aabb(d, {-1.0F,-0.1F,-0.1F },{-0.5F,0.1F,0.1F }));
    // +Y face touch
    EXPECT_TRUE(decal_intersects_aabb(d, {-0.1F, 0.5F,-0.1F },{ 0.1F,1.0F,0.1F }));
    // -Y face touch
    EXPECT_TRUE(decal_intersects_aabb(d, {-0.1F,-1.0F,-0.1F },{ 0.1F,-0.5F,0.1F }));
    // +Z face touch
    EXPECT_TRUE(decal_intersects_aabb(d, {-0.1F,-0.1F, 0.5F },{ 0.1F,0.1F,1.0F }));
    // -Z face touch
    EXPECT_TRUE(decal_intersects_aabb(d, {-0.1F,-0.1F,-1.0F },{ 0.1F,0.1F,-0.5F }));
}

// S1.  BlendMode enum: the four values must match their documented integer
//      constants so that GPU UBO packing is stable.
TEST(Decal, BlendModeEnumValuesAreStable)
{
    using cd::decal::BlendMode;
    EXPECT_EQ(static_cast<std::uint8_t>(BlendMode::kAlbedoOnly),   std::uint8_t{0});
    EXPECT_EQ(static_cast<std::uint8_t>(BlendMode::kNormalOnly),    std::uint8_t{1});
    EXPECT_EQ(static_cast<std::uint8_t>(BlendMode::kAlbedoNormal),  std::uint8_t{2});
    EXPECT_EQ(static_cast<std::uint8_t>(BlendMode::kFull),          std::uint8_t{3});
}

// S2.  Decal default-initialised field values: opacity=1, blend=kFull,
//      position at origin, unit half_extents, identity axes.
TEST(Decal, DecalDefaultFieldValues)
{
    const Decal d {};
    EXPECT_NEAR(d.opacity, 1.0F, kEps);
    EXPECT_EQ(d.blend, cd::decal::BlendMode::kFull);
    EXPECT_NEAR(d.half_extents.x, 0.5F, kEps);
    EXPECT_NEAR(d.half_extents.y, 0.5F, kEps);
    EXPECT_NEAR(d.half_extents.z, 0.5F, kEps);
    EXPECT_NEAR(d.position.x, 0.0F, kEps);
    EXPECT_NEAR(d.right.x,   1.0F, kEps);
    EXPECT_NEAR(d.up.y,      1.0F, kEps);
    EXPECT_NEAR(d.forward.z, 1.0F, kEps);
    EXPECT_NEAR(d.atlas_uv_rect[0], 0.0F, kEps);
    EXPECT_NEAR(d.atlas_uv_rect[2], 1.0F, kEps);
}

// S3.  atlas_uv_rect full-atlas default: the rect covers [0,0]->[1,1].
TEST(Decal, AtlasUvRectDefaultCoversFullAtlas)
{
    const Decal d {};
    EXPECT_NEAR(d.atlas_uv_rect[0], 0.0F, kEps);
    EXPECT_NEAR(d.atlas_uv_rect[1], 0.0F, kEps);
    EXPECT_NEAR(d.atlas_uv_rect[2], 1.0F, kEps);
    EXPECT_NEAR(d.atlas_uv_rect[3], 1.0F, kEps);
}

// G2.  kDecalGlsl contains the struct layout tokens that match Decal's fields:
//      pos, right, up, forward, half_ext, uv_rect, opacity.
TEST(Decal, GlslStructLayoutTokensPresent)
{
    constexpr std::string_view g = cd::decal::kDecalGlsl;
    EXPECT_NE(g.find("vec3  pos"),     std::string_view::npos);
    EXPECT_NE(g.find("vec3  right"),   std::string_view::npos);
    EXPECT_NE(g.find("vec3  up"),      std::string_view::npos);
    EXPECT_NE(g.find("vec3  forward"), std::string_view::npos);
    EXPECT_NE(g.find("vec3  half_ext"),std::string_view::npos);
    EXPECT_NE(g.find("vec4  uv_rect"), std::string_view::npos);
    EXPECT_NE(g.find("opacity"),       std::string_view::npos);
}

// G3.  kDecalGlsl contains the four std140 padding fields (_pad0.._pad3) that
//      align vec3 members to 16-byte boundaries in the UBO.
TEST(Decal, GlslPaddingFieldsPresent)
{
    constexpr std::string_view g = cd::decal::kDecalGlsl;
    EXPECT_NE(g.find("_pad0"), std::string_view::npos);
    EXPECT_NE(g.find("_pad1"), std::string_view::npos);
    EXPECT_NE(g.find("_pad2"), std::string_view::npos);
    EXPECT_NE(g.find("_pad3"), std::string_view::npos);
}

// G4.  kDecalGlsl contains the function signature `bool decal_project(`.
TEST(Decal, GlslFunctionSignaturePresent)
{
    constexpr std::string_view g = cd::decal::kDecalGlsl;
    EXPECT_NE(g.find("bool decal_project("), std::string_view::npos);
    EXPECT_NE(g.find("out vec2 uv"),         std::string_view::npos);
}

// G5.  kDecalGlsl: the UV remapping formula uses the 0.5 bias (NDC→UV), both
//      for U and V, matching the host formula exactly.
TEST(Decal, GlslUvRemapFormulaHasBias)
{
    constexpr std::string_view g = cd::decal::kDecalGlsl;
    // Both UV lines contain "* 0.5 + 0.5" pattern (with possible spaces).
    EXPECT_NE(g.find("0.5 + 0.5"), std::string_view::npos);
    EXPECT_NE(g.find("uv.x"), std::string_view::npos);
    EXPECT_NE(g.find("uv.y"), std::string_view::npos);
}

}  // namespace
