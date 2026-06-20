// =============================================================================
// CHROMODYNAMIC — engine/game/ai_pathfinding/tests/test_triangulator.cpp
// Phase 761 — cd::ai::pathfinding::Triangulator unit tests.
//
// Arrange / Act / Assert. No sleeps, all deterministic (CLAUDE.md §5).
//
// Test inventory:
//   T1  — plane mesh (XZ quad split into 2 triangles): both walkable, 1 shared
//          edge -> NavMesh has 2 triangles and 1 neighbour link.
//   T2  — sloped wall (XY vertical quad): normal.y == 0, NO triangle survives
//          the 45-degree slope filter; NavMesh is empty.
//   T3  — small bump (30-degree ramp): triangle survives with the default
//          45-degree slope budget; rejected when slope budget is lowered to
//          15 degrees.
//   T4  — neighbour connectivity across a strip of 3 triangles: chain T0-T1-T2
//          has T0<->T1 and T1<->T2 shared edges, no other links.
//   T5  — agent_radius filters out narrow corridor: very-thin triangle is
//          rejected; same triangle with smaller radius is accepted.
//   T6  — pathfinder integration: triangulated plane feeds Pathfinder and
//          produces a successful path from one corner to the other.
//   T7  — empty input (zero triangles): build returns a valid empty NavMesh
//          (no throw, invariants satisfied).
//   T8  — degenerate collinear triangle (zero area): silently dropped;
//          resulting NavMesh is empty.
//   T9  — out-of-range vertex index in input triangle: triangle silently
//          dropped; good triangles in the same call survive.
//   T10 — configure/config round-trip: values set via configure are readable
//          via config() without modification.
//   T11 — agent_radius == 0 disables the radius filter: a sliver triangle
//          that would otherwise be rejected is accepted.
//   T12 — non-manifold edge (3 triangles sharing one edge): build does NOT
//          throw; only the first two incident triangles are linked; the graph
//          is valid and Pathfinder can load it.
//   T13 — winding-independence of slope filter: a flat triangle with reversed
//          vertex order (CW vs CCW) produces the same |normal.y| and is
//          accepted by the slope filter.
// =============================================================================
#include <cd/ai/pathfinding/Pathfinding.hpp>
#include <cd/ai/pathfinding/Triangulator.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace
{

using cd::ai::pathfinding::MeshInput;
using cd::ai::pathfinding::NavMesh;
using cd::ai::pathfinding::PathRequest;
using cd::ai::pathfinding::PathResult;
using cd::ai::pathfinding::Pathfinder;
using cd::ai::pathfinding::TriangulationConfig;
using cd::ai::pathfinding::Triangulator;

using Vec3 = std::array<float, 3>;
using Tri  = std::array<uint32_t, 3>;

// ---------------------------------------------------------------------------
// Small helper to count non-boundary neighbour links in a NavMesh.
// ---------------------------------------------------------------------------
[[nodiscard]] uint32_t count_links(const NavMesh& m)
{
    uint32_t links = 0U;
    for (const auto& nbrs : m.triangle_neighbors)
    {
        for (const uint32_t nb : nbrs)
        {
            if (nb != NavMesh::kNoNeighbor) { ++links; }
        }
    }
    return links;
}

// ---------------------------------------------------------------------------
// T1 — plane mesh: 2 walkable triangles, 1 shared interior edge.
// ---------------------------------------------------------------------------
TEST(Triangulator, PlaneMeshProducesWalkableNavMesh)
{
    // Arrange — unit XZ quad split into 2 CCW triangles.
    //   v0(0,0,0) -- v1(1,0,0)
    //          \   T1   |
    //    T0     \       |
    //   v2(0,0,1) -- v3(1,0,1)
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {1.0F, 0.0F, 1.0F},
    };
    const std::vector<Tri> tris = {
        {0U, 1U, 2U},
        {1U, 3U, 2U},
    };

    Triangulator t;
    // Default 45-degree slope, but lower agent_radius for the unit quad.
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.1F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act
    const NavMesh out = t.build(in);

    // Assert — both triangles survived, share the v1-v2 edge.
    EXPECT_EQ(out.triangles.size(), 2U);
    EXPECT_EQ(out.triangle_neighbors.size(), 2U);
    EXPECT_EQ(count_links(out), 2U);  // each shared edge contributes 2 links
}

// ---------------------------------------------------------------------------
// T2 — vertical wall (sloped 90 deg): NO triangle is walkable, NavMesh empty.
// ---------------------------------------------------------------------------
TEST(Triangulator, SlopedWallIsNotWalkable)
{
    // Arrange — unit XY vertical quad: normal points along +Z, |normal.y| = 0.
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {1.0F, 1.0F, 0.0F},
    };
    const std::vector<Tri> tris = {
        {0U, 1U, 2U},
        {1U, 3U, 2U},
    };

    Triangulator t;
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.1F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act
    const NavMesh out = t.build(in);

    // Assert — no triangle survives the 45-deg slope filter.
    EXPECT_TRUE(out.triangles.empty());
    EXPECT_TRUE(out.vertices.empty());
    EXPECT_TRUE(out.triangle_neighbors.empty());
}

// ---------------------------------------------------------------------------
// T3 — gentle bump (30-deg ramp): walkable at default budget, NOT walkable
//      when slope budget is tightened to 15 degrees.
// ---------------------------------------------------------------------------
TEST(Triangulator, SmallBumpWalkableUnderSlope)
{
    // Arrange — XZ strip tilted ~30 degrees: y rises with z.
    // Triangle normal makes ~30 deg with +Y.
    constexpr float kRise = std::numbers::inv_sqrt3_v<float>;  // tan(30 deg) = 1/sqrt(3)
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F,  0.0F},
        {1.0F, 0.0F,  0.0F},
        {0.0F, kRise, 1.0F},
        {1.0F, kRise, 1.0F},
    };
    const std::vector<Tri> tris = {
        {0U, 1U, 2U},
        {1U, 3U, 2U},
    };

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act 1 — default 45-deg slope: triangle survives.
    Triangulator t;
    TriangulationConfig cfg_default {};
    cfg_default.agent_radius = 0.1F;
    t.configure(cfg_default);
    const NavMesh out_ok = t.build(in);

    // Act 2 — 15-deg slope budget: triangle rejected.
    TriangulationConfig cfg_tight {};
    cfg_tight.max_walkable_slope = 0.261799F;  // 15 deg
    cfg_tight.agent_radius       = 0.1F;
    t.configure(cfg_tight);
    const NavMesh out_rejected = t.build(in);

    // Assert
    EXPECT_EQ(out_ok.triangles.size(), 2U);
    EXPECT_TRUE(out_rejected.triangles.empty());
}

// ---------------------------------------------------------------------------
// T4 — 3-triangle strip: chain neighbour connectivity is correct.
// ---------------------------------------------------------------------------
TEST(Triangulator, NeighborConnectivityCorrectOnStrip)
{
    // Arrange — three coplanar triangles, T0-T1 share v1-v2, T1-T2 share v2-v3.
    //   v0(0,0,0) -- v1(1,0,0) -- v3(2,0,0)
    //                  \    T1     |
    //         T0        \    T2    |
    //   v_(0,0,1) -- v2(1,0,1) -- v4(2,0,1)
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},  // 0
        {1.0F, 0.0F, 0.0F},  // 1
        {1.0F, 0.0F, 1.0F},  // 2
        {2.0F, 0.0F, 0.0F},  // 3
        {2.0F, 0.0F, 1.0F},  // 4
        {0.0F, 0.0F, 1.0F},  // 5
    };
    // T0 = v0,v1,v5 ... wait, we want explicit edge sharing.
    // T0 = (0,1,2)  edges: 0-1, 1-2, 2-0
    // T1 = (1,3,2)  edges: 1-3, 3-2, 2-1   -> shares 1-2 with T0
    // T2 = (3,4,2)  edges: 3-4, 4-2, 2-3   -> shares 3-2 with T1
    const std::vector<Tri> tris = {
        {0U, 1U, 2U},
        {1U, 3U, 2U},
        {3U, 4U, 2U},
    };

    Triangulator t;
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.1F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act
    const NavMesh out = t.build(in);

    // Assert — 3 triangles, 4 neighbour entries (2 shared edges * 2 sides).
    ASSERT_EQ(out.triangles.size(), 3U);
    ASSERT_EQ(out.triangle_neighbors.size(), 3U);
    EXPECT_EQ(count_links(out), 4U);

    // Verify pathfinder accepts the produced mesh.
    Pathfinder pf;
    EXPECT_NO_THROW(pf.load_navmesh(out));
    EXPECT_TRUE(pf.has_navmesh());
}

// ---------------------------------------------------------------------------
// T5 — agent_radius rejects a narrow corridor triangle.
// ---------------------------------------------------------------------------
TEST(Triangulator, AgentRadiusRespectedNarrowCorridorFiltered)
{
    // Arrange — single sliver triangle, 1.0 long but only 0.05 wide.
    // Min altitude = 0.05.
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.5F, 0.0F, 0.05F},
    };
    const std::vector<Tri> tris = { {0U, 1U, 2U} };

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act 1 — radius 0.25: filtered.
    Triangulator t;
    TriangulationConfig cfg_wide {};
    cfg_wide.agent_radius = 0.25F;
    t.configure(cfg_wide);
    const NavMesh out_filtered = t.build(in);

    // Act 2 — radius 0.01: accepted.
    TriangulationConfig cfg_narrow {};
    cfg_narrow.agent_radius = 0.01F;
    t.configure(cfg_narrow);
    const NavMesh out_accepted = t.build(in);

    // Assert
    EXPECT_TRUE(out_filtered.triangles.empty());
    EXPECT_EQ(out_accepted.triangles.size(), 1U);
}

// ---------------------------------------------------------------------------
// T6 — end-to-end: triangulated plane feeds Pathfinder for a real A* query.
// ---------------------------------------------------------------------------
TEST(Triangulator, EndToEndPathfindingOnTriangulatedPlane)
{
    // Arrange — 2x2 quad grid (8 walkable triangles), all in XZ plane.
    std::vector<Vec3> verts;
    verts.reserve(9U);
    for (int z = 0; z < 3; ++z)
    {
        for (int x = 0; x < 3; ++x)
        {
            verts.push_back({static_cast<float>(x), 0.0F, static_cast<float>(z)});
        }
    }
    // 4 quads, each 2 triangles, indexed via x + 3*z.
    std::vector<Tri> tris;
    tris.reserve(8U);
    for (int z = 0; z < 2; ++z)
    {
        for (int x = 0; x < 2; ++x)
        {
            const auto i00 = static_cast<uint32_t>(x + 3 * z);
            const auto i10 = static_cast<uint32_t>(i00 + 1U);
            const auto i01 = static_cast<uint32_t>(i00 + 3U);
            const auto i11 = static_cast<uint32_t>(i01 + 1U);
            tris.push_back({i00, i10, i01});
            tris.push_back({i10, i11, i01});
        }
    }

    Triangulator t;
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.1F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act — build navmesh, then run A* across the grid.
    const NavMesh nm = t.build(in);
    ASSERT_EQ(nm.triangles.size(), 8U);

    Pathfinder pf;
    pf.load_navmesh(nm);

    PathRequest req;
    req.start = {0.1F, 0.0F, 0.1F};
    req.goal  = {1.9F, 0.0F, 1.9F};
    req.max_search_distance = 100.0F;

    const PathResult res = pf.find_path(req);

    // Assert
    EXPECT_TRUE(res.success);
    EXPECT_GE(res.waypoints.size(), 2U);
    EXPECT_GT(res.total_distance, 0.0F);
}

// ---------------------------------------------------------------------------
// T7 — empty input: build returns a valid empty NavMesh (no throw).
// ---------------------------------------------------------------------------
TEST(Triangulator, EmptyInputProducesValidEmptyNavMesh)
{
    // Arrange
    Triangulator t;
    MeshInput in;   // default-constructed: both spans are empty

    // Act — must not throw
    NavMesh out;
    EXPECT_NO_THROW(out = t.build(in));

    // Assert — empty but structurally valid (load_navmesh must accept it)
    EXPECT_TRUE(out.vertices.empty());
    EXPECT_TRUE(out.triangles.empty());
    EXPECT_TRUE(out.triangle_neighbors.empty());

    // Pathfinder must accept the empty NavMesh (validated, has_navmesh=false).
    Pathfinder pf;
    EXPECT_NO_THROW(pf.load_navmesh(out));
    EXPECT_FALSE(pf.has_navmesh());
}

// ---------------------------------------------------------------------------
// T8 — degenerate zero-area triangle (collinear vertices): silently dropped.
// ---------------------------------------------------------------------------
TEST(Triangulator, DegenerateCollinearTriangleSilentlyDropped)
{
    // Arrange — 2 collinear triangles (all 3 verts on the same line → area=0)
    //           plus 1 valid flat triangle so the output is non-empty.
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},  // 0
        {1.0F, 0.0F, 0.0F},  // 1 — collinear with 0 and 2
        {2.0F, 0.0F, 0.0F},  // 2
        {0.0F, 0.0F, 1.0F},  // 3 — forms a valid triangle with 0,1
    };
    const std::vector<Tri> tris = {
        {0U, 1U, 2U},   // degenerate: all on X axis, normal = 0
        {0U, 1U, 3U},   // valid: flat XZ triangle, normal points +Y
    };

    Triangulator t;
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.01F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act
    const NavMesh out = t.build(in);

    // Assert — only the valid triangle survived
    EXPECT_EQ(out.triangles.size(), 1U);
    EXPECT_EQ(out.triangle_neighbors.size(), 1U);

    // Pathfinder accepts the result
    Pathfinder pf;
    EXPECT_NO_THROW(pf.load_navmesh(out));
}

// ---------------------------------------------------------------------------
// T9 — out-of-range vertex index: bad triangle silently dropped;
//       good triangle in the same batch survives.
// ---------------------------------------------------------------------------
TEST(Triangulator, OutOfRangeVertexIndexSilentlyDropped)
{
    // Arrange — 3 vertices, 2 triangles: one uses vertex index 99 (bad).
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
    const std::vector<Tri> tris = {
        {0U, 1U, 99U},   // bad — vertex 99 doesn't exist
        {0U, 1U,  2U},   // good — valid flat triangle
    };

    Triangulator t;
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.01F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act
    const NavMesh out = t.build(in);

    // Assert — only the good triangle survived
    EXPECT_EQ(out.triangles.size(), 1U);

    Pathfinder pf;
    EXPECT_NO_THROW(pf.load_navmesh(out));
}

// ---------------------------------------------------------------------------
// T10 — configure / config round-trip preserves all fields exactly.
// ---------------------------------------------------------------------------
TEST(Triangulator, ConfigureConfigRoundTrip)
{
    // Arrange
    Triangulator t;
    TriangulationConfig cfg {};
    cfg.max_walkable_slope = 0.523598F;   // 30 degrees
    cfg.agent_radius       = 0.42F;
    cfg.agent_height       = 2.10F;

    // Act
    t.configure(cfg);
    const TriangulationConfig& got = t.config();

    // Assert — all three fields survive the round-trip unchanged
    EXPECT_FLOAT_EQ(got.max_walkable_slope, cfg.max_walkable_slope);
    EXPECT_FLOAT_EQ(got.agent_radius,       cfg.agent_radius);
    EXPECT_FLOAT_EQ(got.agent_height,       cfg.agent_height);
}

// ---------------------------------------------------------------------------
// T11 — agent_radius == 0 disables the radius filter.
//         A sliver triangle that would be rejected at radius 0.25 must
//         pass through when radius is 0.
// ---------------------------------------------------------------------------
TEST(Triangulator, AgentRadiusZeroDisablesFilter)
{
    // Arrange — narrow sliver triangle, min-altitude ≈ 0.05.
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.5F, 0.0F, 0.05F},
    };
    const std::vector<Tri> tris = { {0U, 1U, 2U} };

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act 1 — radius 0: filter disabled, triangle must survive.
    Triangulator t;
    TriangulationConfig cfg_zero {};
    cfg_zero.agent_radius = 0.0F;
    t.configure(cfg_zero);
    const NavMesh out_pass = t.build(in);

    // Act 2 — radius 0.25: triangle rejected (same as T5).
    TriangulationConfig cfg_wide {};
    cfg_wide.agent_radius = 0.25F;
    t.configure(cfg_wide);
    const NavMesh out_reject = t.build(in);

    // Assert
    EXPECT_EQ(out_pass.triangles.size(), 1U);
    EXPECT_TRUE(out_reject.triangles.empty());
}

// ---------------------------------------------------------------------------
// T12 — non-manifold edge: 3 coplanar triangles all sharing one edge.
//         Build must not throw; only the first two are linked; graph is valid.
// ---------------------------------------------------------------------------
TEST(Triangulator, NonManifoldEdgeDoesNotThrow)
{
    // Arrange — 3 triangles all sharing edge v0-v1 (non-manifold).
    //   T0 = v0, v1, v2   (v2 above the edge)
    //   T1 = v0, v1, v3   (v3 below the edge — same shared edge v0-v1)
    //   T2 = v0, v1, v4   (v4 to the side — third incident triangle)
    // All flat (Y=0), all pass the slope filter.
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},   // 0
        {1.0F, 0.0F, 0.0F},   // 1
        {0.5F, 0.0F, 1.0F},   // 2
        {0.5F, 0.0F,-1.0F},   // 3
        {0.5F, 0.0F, 2.0F},   // 4
    };
    const std::vector<Tri> tris = {
        {0U, 1U, 2U},
        {0U, 1U, 3U},
        {0U, 1U, 4U},
    };

    Triangulator t;
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.1F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act — must not throw
    NavMesh out;
    EXPECT_NO_THROW(out = t.build(in));

    // Assert — all 3 triangles pass the slope filter and are emitted
    EXPECT_EQ(out.triangles.size(), 3U);
    EXPECT_EQ(out.triangle_neighbors.size(), 3U);

    // On the non-manifold edge only 2 of the 3 incident triangles are linked
    // (the contract says first two win, third sees kNoNeighbor on that edge).
    // Total links across all slots: at most 2 (one shared edge, 2 directions).
    const uint32_t links = count_links(out);
    EXPECT_LE(links, 2U);

    // Pathfinder must accept the graph without throwing
    Pathfinder pf;
    EXPECT_NO_THROW(pf.load_navmesh(out));
}

// ---------------------------------------------------------------------------
// T13 — winding-independence of slope filter.
//         Two triangles covering the same flat quad but with opposite vertex
//         orderings (CW vs CCW) must both survive the slope filter.
// ---------------------------------------------------------------------------
TEST(Triangulator, SlopeFilterWindingIndependent)
{
    // Arrange — two triangles with opposite winding order, both in XZ plane.
    //   CCW (when viewed from +Y): v0, v1, v2
    //   CW  (when viewed from +Y): v0, v2, v1  (same triangle, reversed)
    const std::vector<Vec3> verts = {
        {0.0F, 0.0F, 0.0F},   // 0
        {1.0F, 0.0F, 0.0F},   // 1
        {0.0F, 0.0F, 1.0F},   // 2
    };
    const std::vector<Tri> tris = {
        {0U, 1U, 2U},   // CCW viewed from +Y: normal = +Y
        {0U, 2U, 1U},   // CW  viewed from +Y: normal = -Y
    };

    Triangulator t;
    TriangulationConfig cfg {};
    cfg.agent_radius = 0.01F;
    t.configure(cfg);

    MeshInput in;
    in.vertices  = std::span<const Vec3>(verts);
    in.triangles = std::span<const Tri>(tris);

    // Act
    const NavMesh out = t.build(in);

    // Assert — both windings pass because the slope filter uses |normal.y|.
    // The two "triangles" share all 3 edges (they cover the same face from
    // different sides) so the build will emit 2 NavTriangles.
    EXPECT_EQ(out.triangles.size(), 2U);
}

}  // namespace
