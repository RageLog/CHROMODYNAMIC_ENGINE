// =============================================================================
// CHROMODYNAMIC — engine/game/ai_pathfinding/tests/test_triangulator.cpp
// Phase 761 — cd::ai::pathfinding::Triangulator unit tests.
//
// Arrange / Act / Assert. No sleeps, all deterministic (CLAUDE.md §5).
//
// Test inventory:
//   T1 — plane mesh (XZ quad split into 2 triangles): both walkable, 1 shared
//        edge -> NavMesh has 2 triangles and 1 neighbour link.
//   T2 — sloped wall (XY vertical quad): normal.y == 0, NO triangle survives
//        the 45-degree slope filter; NavMesh is empty.
//   T3 — small bump (30-degree ramp): triangle survives with the default
//        45-degree slope budget; rejected when slope budget is lowered to
//        15 degrees.
//   T4 — neighbour connectivity across a strip of 3 triangles: chain T0-T1-T2
//        has T0<->T1 and T1<->T2 shared edges, no other links.
//   T5 — agent_radius filters out narrow corridor: very-thin triangle is
//        rejected; same triangle with smaller radius is accepted.
//   T6 — pathfinder integration: triangulated plane feeds Pathfinder and
//        produces a successful path from one corner to the other.
// =============================================================================
#include <cd/ai/pathfinding/Pathfinding.hpp>
#include <cd/ai/pathfinding/Triangulator.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
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
    constexpr float kRise = 0.577350269F;  // tan(30 deg)
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

}  // namespace
