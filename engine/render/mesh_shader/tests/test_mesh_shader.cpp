#include <cd/mesh_shader/Meshlet.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace
{

using cd::mesh_shader::build_meshlets;
using cd::mesh_shader::cone_cull;
using cd::mesh_shader::kTrianglesPerMeshlet;
using cd::mesh_shader::kVerticesPerMeshlet;
using cd::mesh_shader::Meshlet;


TEST(MeshShader, EmptyInputProducesEmptyOutput)
{
    const auto d = build_meshlets({}, {});
    EXPECT_TRUE(d.meshlets.empty());
}

TEST(MeshShader, SingleTriangleProducesOneMeshletWithThreeVertices)
{
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    EXPECT_EQ(d.meshlets[0].vertex_count, 3U);
    EXPECT_EQ(d.meshlets[0].triangle_count, 1U);
}

TEST(MeshShader, MeshletsRespectVertexCap)
{
    // Build a strip with 200 unique vertices, 200 - 2 = 198 triangles.
    std::vector<std::uint32_t> idx;
    std::vector<cd::math::Vec3f> pos;
    pos.reserve(200);
for (std::uint32_t i = 0; i < 200; ++i)
        pos.emplace_back( static_cast<float>(i), 0.0F, 0.0F );
    for (std::uint32_t i = 0; i + 2 < 200; ++i)
    {
        idx.push_back(i);
        idx.push_back(i + 1);
        idx.push_back(i + 2);
    }
    const auto d = build_meshlets(idx, pos);
    EXPECT_FALSE(d.meshlets.empty());
    for (const auto& m : d.meshlets)
    {
        EXPECT_LE(m.vertex_count,   kVerticesPerMeshlet);
        EXPECT_LE(m.triangle_count, kTrianglesPerMeshlet);
    }
}

TEST(MeshShader, MeshletBoundingSpheresContainAllPoints)
{
    std::vector<std::uint32_t> idx { 0, 1, 2, 0, 2, 3 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    const auto& s = d.meshlets[0].bounds_sphere;
    for (const auto& p : pos)
    {
        const float dx = p.x - s.x;
        const float dy = p.y - s.y;
        const float dz = p.z - s.z;
        EXPECT_LE(std::sqrt(dx*dx + dy*dy + dz*dz), s.w + 1e-3F);
    }
}

TEST(MeshShader, TriangleIndicesPointToValidLocalSlots)
{
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    for (const auto& m : d.meshlets)
    {
        for (std::uint32_t i = 0; i < m.triangle_count; ++i)
        {
            for (std::uint32_t j = 0; j < 3; ++j)
            {
                const auto slot = d.triangle_indices[m.triangle_offset + i * 3 + j];
                EXPECT_LT(slot, m.vertex_count);
            }
        }
    }
}

TEST(MeshShader, GlslSkeletonsNonEmpty)
{
    EXPECT_FALSE(cd::mesh_shader::kMeshletTaskGlsl.empty());
    EXPECT_FALSE(cd::mesh_shader::kMeshletMeshGlsl.empty());
    EXPECT_NE(cd::mesh_shader::kMeshletMeshGlsl.find("SetMeshOutputsEXT"),
              std::string_view::npos);
}

// =============================================================================
// BAND 3 — genuinely-untested clusterizer edge branches. The greedy
// CPU-clusterizer-v1 scope (cone/spatial optimisation = promote-on-need) is
// sealed in ADR-20260616-band3-render-core-scope.md §2.3; these pin the
// degenerate-input + cap-boundary + vertex-dedup branches the original 6 tests
// never reached.
// =============================================================================

TEST(MeshShader, IndicesPresentButPositionsEmptyProducesEmptyOutput)
{
    // The early-out is `indices.empty() || positions.empty()`. Only the
    // both-empty case was tested; pin the positions-empty leg.
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    const auto d = build_meshlets(idx, {});
    EXPECT_TRUE(d.meshlets.empty());
    EXPECT_TRUE(d.vertex_indices.empty());
    EXPECT_TRUE(d.triangle_indices.empty());
}

TEST(MeshShader, TrailingPartialTriangleIsIgnored)
{
    // 5 indices = one whole triangle + a 2-index dangling remainder. The
    // `tri + 2 < indices.size()` loop bound must drop the partial triangle.
    std::vector<std::uint32_t> idx { 0, 1, 2, /* dangling */ 0, 1 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    EXPECT_EQ(d.meshlets[0].triangle_count, 1U);
    EXPECT_EQ(d.meshlets[0].vertex_count, 3U);
}

TEST(MeshShader, SharedVerticesAreDedupedWithinMeshlet)
{
    // Two triangles of a quad share the diagonal edge (verts 0 and 2). The
    // slot_for() dedup path must reuse slots so vertex_count is 4, not 6.
    std::vector<std::uint32_t> idx { 0, 1, 2, 0, 2, 3 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    EXPECT_EQ(d.meshlets[0].triangle_count, 2U);
    EXPECT_EQ(d.meshlets[0].vertex_count, 4U);  // deduped diagonal
    EXPECT_EQ(d.vertex_indices.size(), 4U);
}

TEST(MeshShader, VertexCapForcesFlushAndReslot)
{
    // Independent (non-shared) triangles each contribute 3 fresh vertices.
    // With the 64-vertex cap, the 22nd triangle's 3 new verts (66) overflow
    // → the cap-boundary flush + new_count=3 reslot branch fires. Every
    // meshlet must stay within both caps and triangles must be conserved.
    constexpr std::uint32_t kTris = 30;
    std::vector<std::uint32_t> idx;
    std::vector<cd::math::Vec3f> pos;
    for (std::uint32_t t = 0; t < kTris; ++t)
    {
        const std::uint32_t base = t * 3;
        idx.push_back(base);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        pos.emplace_back(static_cast<float>(t),        0.0F, 0.0F);
        pos.emplace_back(static_cast<float>(t) + 0.3F, 1.0F, 0.0F);
        pos.emplace_back(static_cast<float>(t) + 0.6F, 0.0F, 1.0F);
    }
    const auto d = build_meshlets(idx, pos);
    ASSERT_GE(d.meshlets.size(), 2U);  // 90 verts / 64-cap → at least 2 meshlets
    std::uint32_t total_tris = 0;
    for (const auto& m : d.meshlets)
    {
        EXPECT_LE(m.vertex_count,   kVerticesPerMeshlet);
        EXPECT_LE(m.triangle_count, kTrianglesPerMeshlet);
        EXPECT_GT(m.vertex_count, 0U);
        total_tris += m.triangle_count;
    }
    EXPECT_EQ(total_tris, kTris);  // no triangle dropped across the flush
}

TEST(MeshShader, DegenerateTriangleStillProducesBoundedSphere)
{
    // Collinear (zero-area) triangle: the Ritter bounds must still contain all
    // three points and produce a finite, non-negative radius.
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };  // all on the x-axis
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    const auto& s = d.meshlets[0].bounds_sphere;
    EXPECT_GE(s.w, 0.0F);
    EXPECT_TRUE(std::isfinite(s.w));
    for (const auto& p : pos)
    {
        const float dx = p.x - s.x;
        const float dy = p.y - s.y;
        const float dz = p.z - s.z;
        EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), s.w + 1e-3F);
    }
}

TEST(MeshShader, FreshMeshletConeCutoffDefaultsToZero)
{
    // build_meshlets() fills bounds_sphere but leaves cone_axis_cutoff at its
    // default (greedy v1 does no cone fit — that's the sealed promote-on-need
    // item). Pin the documented default so a future cone-fit can't silently
    // regress callers relying on the zero sentinel.
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    const auto& c = d.meshlets[0].cone_axis_cutoff;
    EXPECT_FLOAT_EQ(c.x, 0.0F);
    EXPECT_FLOAT_EQ(c.y, 0.0F);
    EXPECT_FLOAT_EQ(c.z, 0.0F);
    EXPECT_FLOAT_EQ(c.w, 0.0F);
}

TEST(MeshShader, TaskGlslSkeletonCarriesConeCullAndPayload)
{
    // The original GLSL test only inspected the MESH skeleton. Pin that the
    // TASK skeleton (the cull stage) is non-empty and references its
    // load-bearing tokens.
    const auto task = cd::mesh_shader::kMeshletTaskGlsl;
    EXPECT_FALSE(task.empty());
    EXPECT_NE(task.find("EmitMeshTasksEXT"), std::string_view::npos);
    EXPECT_NE(task.find("cone_axis_cutoff"), std::string_view::npos);
}

// =============================================================================
// BAND 4 — clusterizer COVERAGE invariants + bounds-sphere known-value (Karis
// Nanite model) + the host-mirror cone_cull() backface predicate. The greedy
// CPU-clusterizer-v1 scope (cone/spatial fit = promote-on-need) stays SEALED in
// ADR-20260616-band3-render-core-scope.md §2.3; build_meshlets() output is
// behaviour-preserving (hello_engine draws the meshlet overlay from it). These
// pin (a) the partition contract — every source triangle lands in exactly one
// meshlet and reconstructs its original global vertex triple; (b) the 124-tri
// cap leg the 64-vert tests never reached; (c) large-mesh meshlet-count bounds;
// (d) Ritter bounds-sphere centre/radius on a known cube; (e) cone_cull() vs
// known visible / hidden / sentinel directions.
// =============================================================================

namespace
{

// Build a fan/strip of `tri_count` independent (non-shared) triangles so each
// contributes 3 fresh vertices; returns flat index + position buffers.
struct SyntheticMesh
{
    std::vector<std::uint32_t> idx;
    std::vector<cd::math::Vec3f> pos;
};

[[nodiscard]] SyntheticMesh make_independent_tris(std::uint32_t tri_count)
{
    SyntheticMesh m {};
    m.idx.reserve(static_cast<std::size_t>(tri_count) * 3U);
    m.pos.reserve(static_cast<std::size_t>(tri_count) * 3U);
    for (std::uint32_t t = 0; t < tri_count; ++t)
    {
        const std::uint32_t base = t * 3U;
        m.idx.push_back(base);
        m.idx.push_back(base + 1U);
        m.idx.push_back(base + 2U);
        m.pos.emplace_back(static_cast<float>(t),         0.0F, 0.0F);
        m.pos.emplace_back(static_cast<float>(t) + 0.3F,  1.0F, 0.0F);
        m.pos.emplace_back(static_cast<float>(t) + 0.6F,  0.0F, 1.0F);
    }
    return m;
}

}  // namespace

TEST(MeshShader, EveryTriangleReconstructsToExactlyOneGlobalTriple)
{
    // Partition contract: across a multi-meshlet split, decode each meshlet's
    // local triangle (slot -> meshlet vertex_indices -> global index) and assert
    // the multiset of decoded global triples equals the original triangle set —
    // no triangle is dropped, duplicated, or corrupted across a flush.
    const auto mesh = make_independent_tris(40U);  // 120 verts -> ≥2 meshlets
    const auto d = build_meshlets(mesh.idx, mesh.pos);
    ASSERT_GE(d.meshlets.size(), 2U);

    std::set<std::array<std::uint32_t, 3>> expected;
    for (std::size_t t = 0; t + 2U < mesh.idx.size(); t += 3U)
        expected.insert({ mesh.idx[t], mesh.idx[t + 1U], mesh.idx[t + 2U] });

    std::set<std::array<std::uint32_t, 3>> decoded;
    std::uint32_t total_tris = 0;
    for (const auto& m : d.meshlets)
    {
        for (std::uint32_t i = 0; i < m.triangle_count; ++i)
        {
            std::array<std::uint32_t, 3> tri {};
            for (std::uint32_t j = 0; j < 3U; ++j)
            {
                const std::uint8_t slot =
                    d.triangle_indices[m.triangle_offset + i * 3U + j];
                ASSERT_LT(slot, m.vertex_count);
                tri[j] = d.vertex_indices[m.vertex_offset + slot];
            }
            decoded.insert(tri);
            ++total_tris;
        }
    }
    EXPECT_EQ(total_tris, 40U);          // every triangle present, none dropped
    EXPECT_EQ(decoded.size(), 40U);      // none duplicated
    EXPECT_EQ(decoded, expected);        // each reconstructs its global triple
}

TEST(MeshShader, TriangleCapBoundaryFiresWhenVerticesAreFullyShared)
{
    // The 64-vertex tests never reach the 124-tri cap because each fresh tri
    // adds verts. To exercise the OTHER cap leg (current.triangle_count + 1 >
    // kTrianglesPerMeshlet) we draw many triangles from a FIXED 64-vertex pool:
    // the vertex cap is never exceeded (all 64 slots fit) so the only flush
    // trigger is the triangle cap. Emit 124 + 50 = 174 such triangles.
    constexpr std::uint32_t kPoolVerts = kVerticesPerMeshlet;        // 64
    constexpr std::uint32_t kTris      = kTrianglesPerMeshlet + 50U; // 174
    std::vector<cd::math::Vec3f> pos;
    pos.reserve(kPoolVerts);
    for (std::uint32_t v = 0; v < kPoolVerts; ++v)
        pos.emplace_back(static_cast<float>(v), 0.0F, 0.0F);
    std::vector<std::uint32_t> idx;
    idx.reserve(static_cast<std::size_t>(kTris) * 3U);
    for (std::uint32_t t = 0; t < kTris; ++t)
    {
        // Three distinct pool slots, all < 64 -> never grows past the vertex cap.
        idx.push_back(t % kPoolVerts);
        idx.push_back((t + 1U) % kPoolVerts);
        idx.push_back((t + 2U) % kPoolVerts);
    }
    const auto d = build_meshlets(idx, pos);
    ASSERT_GE(d.meshlets.size(), 2U);  // 174 tris / 124-cap -> ≥2 meshlets
    std::uint32_t total_tris = 0;
    bool saw_tri_cap_hit = false;
    for (const auto& m : d.meshlets)
    {
        EXPECT_LE(m.triangle_count, kTrianglesPerMeshlet);
        EXPECT_LE(m.vertex_count, kVerticesPerMeshlet);
        if (m.triangle_count == kTrianglesPerMeshlet) saw_tri_cap_hit = true;
        total_tris += m.triangle_count;
    }
    EXPECT_EQ(total_tris, kTris);   // none dropped across the tri-cap flush
    EXPECT_TRUE(saw_tri_cap_hit);   // the 124-triangle cap actually packed full
}

TEST(MeshShader, LargeMeshMeshletCountWithinPartitionBounds)
{
    // 1000 independent triangles (3000 unique verts). Vertex cap dominates: a
    // meshlet packs floor(64/3)=21 such triangles. Meshlet count must sit in
    // [ceil(1000/124), ceil(1000/21)] = [9, 48]; assert that and full coverage.
    const auto mesh = make_independent_tris(1000U);
    const auto d = build_meshlets(mesh.idx, mesh.pos);
    EXPECT_GE(d.meshlets.size(), 9U);
    EXPECT_LE(d.meshlets.size(), 48U);
    std::uint32_t total_tris = 0;
    for (const auto& m : d.meshlets) total_tris += m.triangle_count;
    EXPECT_EQ(total_tris, 1000U);
}

TEST(MeshShader, BoundingSphereMatchesKnownCubeCentreAndRadius)
{
    // Two tris on the [0,1]^3 cube's diagonal corners. Ritter (min/max AABB)
    // centre = (0.5,0.5,0.5); the farthest packed vertex is a unit-cube corner
    // -> radius = sqrt(3)/2 ≈ 0.8660. Pin the Karis bounding model's value.
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 1, 1 }, { 1, 0, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    const auto& s = d.meshlets[0].bounds_sphere;
    EXPECT_NEAR(s.x, 0.5F, 1e-5F);
    EXPECT_NEAR(s.y, 0.5F, 1e-5F);
    EXPECT_NEAR(s.z, 0.5F, 1e-5F);
    EXPECT_NEAR(s.w, std::sqrt(3.0F) * 0.5F, 1e-5F);
    for (const auto& p : pos)
    {
        const float dx = p.x - s.x;
        const float dy = p.y - s.y;
        const float dz = p.z - s.z;
        EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), s.w + 1e-4F);
    }
}

TEST(MeshShader, AllIdenticalVerticesCollapseToZeroRadiusSphere)
{
    // Fully degenerate: all three indices reference one position. The AABB is a
    // point -> centre == that point, radius == 0, finite.
    std::vector<std::uint32_t> idx { 0, 0, 0 };
    std::vector<cd::math::Vec3f> pos { { 2, -3, 4 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    EXPECT_EQ(d.meshlets[0].vertex_count, 3U);  // builder keeps one slot per triangle corner (no positional dedup)
    const auto& s = d.meshlets[0].bounds_sphere;
    EXPECT_NEAR(s.x, 2.0F, 1e-6F);
    EXPECT_NEAR(s.y, -3.0F, 1e-6F);
    EXPECT_NEAR(s.z, 4.0F, 1e-6F);
    EXPECT_NEAR(s.w, 0.0F, 1e-6F);
    EXPECT_TRUE(std::isfinite(s.w));
}

// ---- cone_cull() host predicate (Karis Nanite backface-cluster model) -------

TEST(MeshShader, ConeCullSentinelMeshletIsNeverCulled)
{
    // Default cone_axis_cutoff == {0,0,0,0}: dot(view, -0) == 0 and 0 > 0 is
    // false. A greedy-v1 meshlet with no fitted cone must survive from EVERY
    // camera direction (the safety guarantee documented on cone_cull()).
    Meshlet m {};
    m.bounds_sphere = { 0, 0, 0, 1 };  // centre at origin
    EXPECT_FALSE(cone_cull(m, { 5, 0, 0 }));
    EXPECT_FALSE(cone_cull(m, { 0, -5, 0 }));
    EXPECT_FALSE(cone_cull(m, { 3, 4, 12 }));
}

TEST(MeshShader, ConeCullHidesClusterWhenViewOpposesNormalCone)
{
    // Cluster centred at origin, normal cone points +Z, cutoff cos = 0 (90°
    // half-cone — cull the whole back hemisphere). View = normalize(centre-cam)
    // = -cam_dir. dot(view, -axis) = dot(view, -Z).
    Meshlet m {};
    m.bounds_sphere = { 0, 0, 0, 1 };
    m.cone_axis_cutoff = { 0, 0, 1, 0 };  // axis +Z, cutoff 0

    // Camera at +Z looking toward origin: view = (0,0,-1). dot(view,-Z)=+1>0 ->
    // we are BEHIND the cone (facing its back) -> CULL.
    EXPECT_TRUE(cone_cull(m, { 0, 0, 10 }));
    // Camera at -Z looking toward origin: view = (0,0,+1). dot(view,-Z)=-1<0 ->
    // we face the cone front -> VISIBLE.
    EXPECT_FALSE(cone_cull(m, { 0, 0, -10 }));
    // Camera exactly side-on (+X): view=(-1,0,0). dot(view,-Z)=0, 0>0 false ->
    // boundary stays VISIBLE (cull is strict-greater, matching the GLSL).
    EXPECT_FALSE(cone_cull(m, { 10, 0, 0 }));
}

TEST(MeshShader, ConeCullCutoffWidthGatesTheBackfaceBand)
{
    // A narrow cone (cutoff cos = 0.5 -> 60° band that must be opposed before
    // culling) keeps a near-side-on view visible that a wide cone would cull.
    Meshlet wide {};
    wide.bounds_sphere = { 0, 0, 0, 1 };
    wide.cone_axis_cutoff = { 0, 0, 1, 0.0F };   // cull when dot > 0
    Meshlet narrow {};
    narrow.bounds_sphere = { 0, 0, 0, 1 };
    narrow.cone_axis_cutoff = { 0, 0, 1, 0.5F };  // cull only when dot > 0.5

    // Camera up and slightly behind: view ≈ normalize((0,-1,-0.3)).
    // dot(view,-Z) ≈ 0.287 -> wide(>0) culls, narrow(>0.5) keeps visible.
    const cd::math::Vec3f cam { 0.0F, 1.0F, 0.3F };
    EXPECT_TRUE(cone_cull(wide, cam));
    EXPECT_FALSE(cone_cull(narrow, cam));
}

}  // namespace
