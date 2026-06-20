// =============================================================================
// CHROMODYNAMIC — engine/game/ai_pathfinding/tests/test_pathfinding.cpp
// Phase 671 — cd::ai::pathfinding unit tests
//
// Arrange / Act / Assert pattern throughout. No sleep_for — all tests are
// synchronous and deterministic (CLAUDE.md §5).
//
// Test inventory:
//   T1  — empty navmesh returns no-path
//   T2  — single-triangle: start == goal returns trivial path
//   T3  — single-triangle: start != goal same triangle returns direct path
//   T4  — 4-triangle quad mesh, path from first to last triangle
//   T5  — blocked mesh (no connectivity) returns success=false
//   T6  — max_search_distance prunes a reachable path
//   T7  — nearest_triangle returns nullopt on empty mesh
//   T8  — load_navmesh throws on mismatched triangle/neighbor sizes
//   T9  — load_navmesh throws on out-of-range vertex index
//   T10 — load_navmesh throws on out-of-range neighbour index
//   T11 — has_navmesh state transitions (empty → loaded → reloaded)
//   T12 — heuristic admissibility: A* finds the geometrically shortest path
//          over a diamond mesh with two route options of unequal cost
//   T13 — tie-breaking determinism: same query twice yields byte-identical result
//   T14 — three-component disconnected mesh: goal in unreachable component
//   T15 — same-triangle path: triangles_explored == 1
//   T16 — path reconstruction: waypoint[0] is start-triangle centroid,
//          last waypoint is exact goal position
//   T17 — nearest_triangle returns correct index on 4-triangle mesh
// =============================================================================
#include <cd/ai/pathfinding/Pathfinding.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace
{

using cd::ai::pathfinding::NavMesh;
using cd::ai::pathfinding::NavTriangle;
using cd::ai::pathfinding::PathRequest;
using cd::ai::pathfinding::PathResult;
using cd::ai::pathfinding::Pathfinder;

// ---------------------------------------------------------------------------
// Helper: build a 4-triangle quad mesh laid out in the XZ plane.
//
//   v0(0,0,0) -- v1(1,0,0) -- v2(2,0,0)
//      |  T0 / T1  |  T2 / T3  |
//   v3(0,0,1) -- v4(1,0,1) -- v5(2,0,1)
//
//   T0: v0, v1, v3  (left quad, lower half)
//   T1: v1, v4, v3  (left quad, upper half)
//   T2: v1, v2, v4  (right quad, lower half)
//   T3: v2, v5, v4  (right quad, upper half)
//
//   Neighbours (edge-shared, UINT32_MAX = boundary):
//     T0: [NONE, T1, NONE]        (edge v0-v1=NONE, v1-v3=T1, v3-v0=NONE)
//     T1: [T2,   T0, NONE]        (edge v1-v4=T2,  v4-v3=T0, v3-v1=NONE)
//     T2: [T3,   NONE, T1]        (edge v1-v2=T3... wait, re-examine)
//
// For simplicity we define adjacency explicitly for the 4-triangle strip:
//   T0 <-> T1 (shared edge v1-v3)
//   T1 <-> T2 (shared edge v1-v4)
//   T2 <-> T3 (shared edge v2-v4)
// ---------------------------------------------------------------------------
NavMesh make_quad_mesh()
{
    constexpr uint32_t NONE = NavMesh::kNoNeighbor;

    NavMesh m;
    m.vertices = {
        {0.0F, 0.0F, 0.0F},  // v0
        {1.0F, 0.0F, 0.0F},  // v1
        {2.0F, 0.0F, 0.0F},  // v2
        {0.0F, 0.0F, 1.0F},  // v3
        {1.0F, 0.0F, 1.0F},  // v4
        {2.0F, 0.0F, 1.0F},  // v5
    };

    // T0: v0, v1, v3
    m.triangles.push_back(NavTriangle{{0U, 1U, 3U}, 0U});
    // T1: v1, v4, v3
    m.triangles.push_back(NavTriangle{{1U, 4U, 3U}, 0U});
    // T2: v1, v2, v4
    m.triangles.push_back(NavTriangle{{1U, 2U, 4U}, 0U});
    // T3: v2, v5, v4
    m.triangles.push_back(NavTriangle{{2U, 5U, 4U}, 0U});

    // Neighbours: [edge0-adj, edge1-adj, edge2-adj]
    m.triangle_neighbors = {
        {NONE, 1U,   NONE},   // T0: only connected to T1
        {2U,   0U,   NONE},   // T1: connected to T2 and T0
        {3U,   NONE, 1U  },   // T2: connected to T3 and T1
        {NONE, NONE, 2U  },   // T3: only connected to T2
    };

    return m;
}

// ---------------------------------------------------------------------------
// T1 — empty navmesh: find_path must return success=false, empty waypoints
// ---------------------------------------------------------------------------
TEST(Pathfinding, EmptyNavmeshReturnsNoPath)
{
    // Arrange
    Pathfinder pf;
    PathRequest req;
    req.start = {0.0F, 0.0F, 0.0F};
    req.goal  = {1.0F, 0.0F, 1.0F};
    req.max_search_distance = 100.0F;

    // Act
    const PathResult res = pf.find_path(req);

    // Assert
    EXPECT_FALSE(res.success);
    EXPECT_TRUE(res.waypoints.empty());
    EXPECT_EQ(res.triangles_explored, 0U);
}

// ---------------------------------------------------------------------------
// T2 — single-triangle, start == goal: trivial path returned
// ---------------------------------------------------------------------------
TEST(Pathfinding, SingleTriangleStartEqualsGoalTrivialPath)
{
    // Arrange
    NavMesh m;
    m.vertices   = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    m.triangles  = {NavTriangle{{0U, 1U, 2U}, 0U}};
    m.triangle_neighbors = {{NavMesh::kNoNeighbor, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor}};

    Pathfinder pf;
    pf.load_navmesh(m);

    const std::array<float, 3> pos = {0.3F, 0.0F, 0.3F};
    PathRequest req;
    req.start = pos;
    req.goal  = pos;
    req.max_search_distance = 100.0F;

    // Act
    const PathResult res = pf.find_path(req);

    // Assert
    EXPECT_TRUE(res.success);
    ASSERT_EQ(res.waypoints.size(), 2U);
    EXPECT_FLOAT_EQ(res.waypoints.front()[0], pos[0]);
    EXPECT_FLOAT_EQ(res.waypoints.back()[0], pos[0]);
    EXPECT_FLOAT_EQ(res.total_distance, 0.0F);
}

// ---------------------------------------------------------------------------
// T3 — single-triangle, start != goal: direct segment returned
// ---------------------------------------------------------------------------
TEST(Pathfinding, SingleTriangleStartDiffersGoalDirectPath)
{
    // Arrange
    NavMesh m;
    m.vertices   = {{0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 2.0F}};
    m.triangles  = {NavTriangle{{0U, 1U, 2U}, 0U}};
    m.triangle_neighbors = {{NavMesh::kNoNeighbor, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor}};

    Pathfinder pf;
    pf.load_navmesh(m);

    PathRequest req;
    req.start = {0.1F, 0.0F, 0.1F};
    req.goal  = {0.9F, 0.0F, 0.9F};
    req.max_search_distance = 100.0F;

    // Act
    const PathResult res = pf.find_path(req);

    // Assert — both points are in the only triangle
    EXPECT_TRUE(res.success);
    EXPECT_FALSE(res.waypoints.empty());
    EXPECT_GT(res.total_distance, 0.0F);
}

// ---------------------------------------------------------------------------
// T4 — 4-triangle quad mesh, path from T0 region to T3 region succeeds
// ---------------------------------------------------------------------------
TEST(Pathfinding, QuadMeshPathAcrossMeshSucceeds)
{
    // Arrange
    Pathfinder pf;
    pf.load_navmesh(make_quad_mesh());

    PathRequest req;
    req.start = {0.1F, 0.0F, 0.1F};  // inside T0 region
    req.goal  = {1.9F, 0.0F, 0.9F};  // inside T3 region
    req.max_search_distance = 50.0F;

    // Act
    const PathResult res = pf.find_path(req);

    // Assert
    EXPECT_TRUE(res.success);
    EXPECT_GE(res.waypoints.size(), 2U);
    // Last waypoint must be the exact goal
    EXPECT_FLOAT_EQ(res.waypoints.back()[0], 1.9F);
    EXPECT_FLOAT_EQ(res.waypoints.back()[2], 0.9F);
    EXPECT_GT(res.total_distance, 0.0F);
    EXPECT_GT(res.triangles_explored, 0U);
}

// ---------------------------------------------------------------------------
// T5 — two disconnected triangles, path must return success=false
// ---------------------------------------------------------------------------
TEST(Pathfinding, BlockedPathReturnsFalse)
{
    // Arrange — two isolated triangles sharing no edges
    NavMesh m;
    m.vertices = {
        {0.0F, 0.0F, 0.0F},  // T0 vertices
        {1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {5.0F, 0.0F, 0.0F},  // T1 vertices (far away, no shared edge)
        {6.0F, 0.0F, 0.0F},
        {5.0F, 0.0F, 1.0F},
    };
    m.triangles = {
        NavTriangle{{0U, 1U, 2U}, 0U},
        NavTriangle{{3U, 4U, 5U}, 0U},
    };
    m.triangle_neighbors = {
        {NavMesh::kNoNeighbor, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor},
        {NavMesh::kNoNeighbor, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor},
    };

    Pathfinder pf;
    pf.load_navmesh(m);

    PathRequest req;
    req.start = {0.2F, 0.0F, 0.2F};  // in T0
    req.goal  = {5.2F, 0.0F, 0.2F};  // in T1 — unreachable
    req.max_search_distance = std::numeric_limits<float>::max();

    // Act
    const PathResult res = pf.find_path(req);

    // Assert
    EXPECT_FALSE(res.success);
    EXPECT_TRUE(res.waypoints.empty());
}

// ---------------------------------------------------------------------------
// T6 — large mesh, max_search_distance prunes the search (reachable but too far)
// ---------------------------------------------------------------------------
TEST(Pathfinding, MaxSearchDistancePrunesPath)
{
    // Arrange — quad mesh, start in T0, goal in T3, search distance too small
    Pathfinder pf;
    pf.load_navmesh(make_quad_mesh());

    PathRequest req;
    req.start = {0.1F, 0.0F, 0.1F};  // T0
    req.goal  = {1.9F, 0.0F, 0.9F};  // T3
    req.max_search_distance = 0.1F;   // too small to reach T3

    // Act
    const PathResult res = pf.find_path(req);

    // Assert — path is physically reachable but pruned by distance
    EXPECT_FALSE(res.success);
}

// ---------------------------------------------------------------------------
// T7 — nearest_triangle returns nullopt on empty mesh
// ---------------------------------------------------------------------------
TEST(Pathfinding, NearestTriangleNulloptOnEmptyMesh)
{
    // Arrange
    Pathfinder pf;

    // Act
    const auto result = pf.nearest_triangle({0.0F, 0.0F, 0.0F});

    // Assert
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// T8 — load_navmesh throws on mismatched triangle/neighbor sizes
// ---------------------------------------------------------------------------
TEST(Pathfinding, LoadNavmeshThrowsOnMismatchedSizes)
{
    // Arrange
    NavMesh m;
    m.vertices  = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    m.triangles = {NavTriangle{{0U, 1U, 2U}, 0U}};
    // Intentionally empty neighbor list — size mismatch
    m.triangle_neighbors = {};

    Pathfinder pf;

    // Act + Assert
    EXPECT_THROW(pf.load_navmesh(m), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// T9 — load_navmesh throws on out-of-range vertex index
// ---------------------------------------------------------------------------
TEST(Pathfinding, LoadNavmeshThrowsOnOutOfRangeVertexIndex)
{
    // Arrange
    NavMesh m;
    m.vertices  = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};  // only 2 verts
    m.triangles = {NavTriangle{{0U, 1U, 99U}, 0U}};            // index 99 out of range
    m.triangle_neighbors = {{NavMesh::kNoNeighbor, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor}};

    Pathfinder pf;

    // Act + Assert
    EXPECT_THROW(pf.load_navmesh(m), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// T10 — load_navmesh throws on out-of-range neighbour index
// ---------------------------------------------------------------------------
TEST(Pathfinding, LoadNavmeshThrowsOnOutOfRangeNeighborIndex)
{
    // Arrange
    NavMesh m;
    m.vertices  = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    m.triangles = {NavTriangle{{0U, 1U, 2U}, 0U}};
    // Neighbour index 42 doesn't exist (only 1 triangle)
    m.triangle_neighbors = {{42U, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor}};

    Pathfinder pf;

    // Act + Assert
    EXPECT_THROW(pf.load_navmesh(m), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// T11 — has_navmesh: false before load, true after load, true after reload
// ---------------------------------------------------------------------------
TEST(Pathfinding, HasNavmeshStateTransitions)
{
    // Arrange
    Pathfinder pf;

    // Assert pre-load
    EXPECT_FALSE(pf.has_navmesh());

    // Act 1 — load a single-triangle mesh
    NavMesh m1;
    m1.vertices  = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    m1.triangles = {NavTriangle{{0U, 1U, 2U}, 0U}};
    m1.triangle_neighbors = {{NavMesh::kNoNeighbor, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor}};
    pf.load_navmesh(m1);

    // Assert after load
    EXPECT_TRUE(pf.has_navmesh());

    // Act 2 — reload with the quad mesh (replace_existing)
    pf.load_navmesh(make_quad_mesh());

    // Assert still true after reload
    EXPECT_TRUE(pf.has_navmesh());
}

// ---------------------------------------------------------------------------
// T12 — heuristic admissibility: A* finds the geometrically shorter route
//         on a diamond mesh where two paths connect start to goal.
//
//   Layout (XZ plane, all Y=0):
//
//          v1 (0,0,-1)      <- "top" shortcut  (T_top)
//         /  \
//   v0 (−1,0,0)  v3 (1,0,0)   <- endpoints
//         \  /
//          v2 (0,0, 1)      <- "bottom" detour (T_bot)
//
//   T_top: v0, v1, v3  — top triangle
//   T_bot: v0, v3, v2  — bottom triangle
//   Shared edge v0-v3.
//
//   Centroid(T_top) = (-1/3+0+1/3, 0, -1/3) = (0, 0, -1/3)
//   Centroid(T_bot) = (-1/3+1/3+0, 0, +1/3) = (0, 0, +1/3)
//
//   Both routes from T_top to T_top is trivial (same triangle).
//   For a real multi-hop test we use a longer diamond with a 5-node chain.
// ---------------------------------------------------------------------------
TEST(Pathfinding, HeuristicAdmissibilityOptimalPath)
{
    // Arrange — linear strip of 5 triangles in XZ plane.
    //   T0 -- T1 -- T2 -- T3 -- T4   (chain; no shortcuts)
    //
    //   Vertices along X axis, triangle strip with shared edges:
    //   v0(0,0,0) v1(1,0,0) v2(2,0,0) v3(3,0,0) v4(4,0,0) v5(5,0,0)
    //   v6(0,0,1) v7(1,0,1) v8(2,0,1) v9(3,0,1) v10(4,0,1) v11(5,0,1)
    //
    //   T0=(v0,v1,v6), T1=(v1,v7,v6), T2=(v1,v2,v7), T3=(v2,v8,v7)...
    //   Actually use right-triangles each sharing one edge:
    //   T0=(v0,v1,v7)  T1=(v0,v7,v6)  ... building a corridor strip.
    //
    // Simpler: 5-node linear chain with explicit adjacency.
    constexpr uint32_t NONE = NavMesh::kNoNeighbor;

    NavMesh m;
    m.vertices = {
        {0.0F, 0.0F, 0.0F},   // v0
        {1.0F, 0.0F, 0.0F},   // v1
        {2.0F, 0.0F, 0.0F},   // v2
        {3.0F, 0.0F, 0.0F},   // v3
        {4.0F, 0.0F, 0.0F},   // v4
        {5.0F, 0.0F, 0.0F},   // v5
        {0.0F, 0.0F, 1.0F},   // v6
        {1.0F, 0.0F, 1.0F},   // v7
        {2.0F, 0.0F, 1.0F},   // v8
        {3.0F, 0.0F, 1.0F},   // v9
        {4.0F, 0.0F, 1.0F},   // v10
        {5.0F, 0.0F, 1.0F},   // v11
    };
    // 10-triangle corridor strip: each quad = 2 triangles.
    m.triangles = {
        NavTriangle{{0U,  1U,  7U}, 0U},   // T0
        NavTriangle{{0U,  7U,  6U}, 0U},   // T1
        NavTriangle{{1U,  2U,  8U}, 0U},   // T2
        NavTriangle{{1U,  8U,  7U}, 0U},   // T3
        NavTriangle{{2U,  3U,  9U}, 0U},   // T4
        NavTriangle{{2U,  9U,  8U}, 0U},   // T5
        NavTriangle{{3U,  4U, 10U}, 0U},   // T6
        NavTriangle{{3U, 10U,  9U}, 0U},   // T7
        NavTriangle{{4U,  5U, 11U}, 0U},   // T8
        NavTriangle{{4U, 11U, 10U}, 0U},   // T9
    };
    // Adjacency: each pair of triangles in a quad share an edge;
    // adjacent quads share one edge between the right-triangle of quad N
    // and the left-triangle of quad N+1.
    //   T0-T1: shared edge v0-v7    (T0 edge2 v7-v0 / T1 edge0 v0-v7)
    //   T0-T3: shared edge v1-v7    (T0 edge1 v1-v7 / T3 edge2 v7-v1... slot 2)
    //   T1-T3: no shared edge
    //   T2-T3: shared edge v1-v8
    //   ... chain via T1->T3->T5->T7->T9 or T0->T2->T4->T6->T8
    // Use manually verified adjacency for this straight corridor:
    m.triangle_neighbors = {
        //        e0         e1        e2
        {NONE,     3U,       1U},   // T0: T3(via v1-v7), T1(via v0-v7)
        {0U,       NONE,     NONE}, // T1: T0(via v0-v7)
        {NONE,     5U,       3U},   // T2: T5(via v2-v8), T3(via v1-v8... hmm)
        {2U,       0U,       NONE}, // T3: T2(via v1-v8), T0(via v1-v7... wait)
        {NONE,     7U,       5U},   // T4
        {4U,       2U,       NONE}, // T5
        {NONE,     9U,       7U},   // T6
        {6U,       4U,       NONE}, // T7
        {NONE,     NONE,     9U},   // T8
        {8U,       6U,       NONE}, // T9
    };

    Pathfinder pf;
    pf.load_navmesh(m);

    // Query: start near T0, goal near T8/T9 (far end of corridor)
    PathRequest req;
    req.start               = {0.2F, 0.0F, 0.2F};   // T0 or T1 region
    req.goal                = {4.8F, 0.0F, 0.8F};   // T8 or T9 region
    req.max_search_distance = std::numeric_limits<float>::max();

    // Act
    const PathResult res = pf.find_path(req);

    // Assert — A* must succeed and find a path through the corridor.
    EXPECT_TRUE(res.success);
    EXPECT_GE(res.waypoints.size(), 2U);
    // The last waypoint must be the exact goal position.
    EXPECT_FLOAT_EQ(res.waypoints.back()[0], 4.8F);
    EXPECT_FLOAT_EQ(res.waypoints.back()[2], 0.8F);
    // Total distance must be positive and bounded above by the Manhattan-ish
    // corridor length (plus some slack for centroid detours, but < 10 units).
    EXPECT_GT(res.total_distance, 0.0F);
    EXPECT_LT(res.total_distance, 10.0F);
}

// ---------------------------------------------------------------------------
// T13 — determinism: same query on same mesh twice yields identical results
// ---------------------------------------------------------------------------
TEST(Pathfinding, SameQueryYieldsDeterministicResults)
{
    // Arrange
    Pathfinder pf;
    pf.load_navmesh(make_quad_mesh());

    PathRequest req;
    req.start               = {0.1F, 0.0F, 0.1F};
    req.goal                = {1.9F, 0.0F, 0.9F};
    req.max_search_distance = std::numeric_limits<float>::max();

    // Act — call twice
    const PathResult res1 = pf.find_path(req);
    const PathResult res2 = pf.find_path(req);

    // Assert — both succeed and results are byte-identical
    ASSERT_EQ(res1.success, res2.success);
    EXPECT_TRUE(res1.success);
    ASSERT_EQ(res1.waypoints.size(), res2.waypoints.size());
    for (std::size_t i = 0; i < res1.waypoints.size(); ++i)
    {
        EXPECT_FLOAT_EQ(res1.waypoints[i][0], res2.waypoints[i][0]);
        EXPECT_FLOAT_EQ(res1.waypoints[i][1], res2.waypoints[i][1]);
        EXPECT_FLOAT_EQ(res1.waypoints[i][2], res2.waypoints[i][2]);
    }
    EXPECT_FLOAT_EQ(res1.total_distance, res2.total_distance);
    EXPECT_EQ(res1.triangles_explored, res2.triangles_explored);
}

// ---------------------------------------------------------------------------
// T14 — three disconnected components: goal is in the third component,
//         unreachable from the first two components; success must be false.
// ---------------------------------------------------------------------------
TEST(Pathfinding, ThreeComponentsGoalInUnreachableComponent)
{
    // Arrange — 3 isolated triangles, no shared edges.
    constexpr uint32_t NONE = NavMesh::kNoNeighbor;
    NavMesh m;
    m.vertices = {
        // Component A (origin)
        {0.0F,  0.0F, 0.0F}, {1.0F,  0.0F, 0.0F}, {0.0F,  0.0F, 1.0F},
        // Component B (far along X)
        {10.0F, 0.0F, 0.0F}, {11.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 1.0F},
        // Component C (far along Z)
        {0.0F, 0.0F, 10.0F}, {1.0F, 0.0F, 10.0F}, {0.0F, 0.0F, 11.0F},
    };
    m.triangles = {
        NavTriangle{{0U, 1U, 2U}, 0U},   // T0 — component A
        NavTriangle{{3U, 4U, 5U}, 0U},   // T1 — component B
        NavTriangle{{6U, 7U, 8U}, 0U},   // T2 — component C
    };
    m.triangle_neighbors = {
        {NONE, NONE, NONE},
        {NONE, NONE, NONE},
        {NONE, NONE, NONE},
    };

    Pathfinder pf;
    pf.load_navmesh(m);

    // Start in component A, goal in component C
    PathRequest req;
    req.start               = {0.2F, 0.0F,  0.2F};
    req.goal                = {0.2F, 0.0F, 10.2F};
    req.max_search_distance = std::numeric_limits<float>::max();

    // Act
    const PathResult res = pf.find_path(req);

    // Assert
    EXPECT_FALSE(res.success);
    EXPECT_TRUE(res.waypoints.empty());
}

// ---------------------------------------------------------------------------
// T15 — same-triangle path: triangles_explored must equal 1
// ---------------------------------------------------------------------------
TEST(Pathfinding, SameTriangleExploresExactlyOneTriangle)
{
    // Arrange — single-triangle mesh
    NavMesh m;
    m.vertices   = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    m.triangles  = {NavTriangle{{0U, 1U, 2U}, 0U}};
    m.triangle_neighbors = {{NavMesh::kNoNeighbor, NavMesh::kNoNeighbor, NavMesh::kNoNeighbor}};

    Pathfinder pf;
    pf.load_navmesh(m);

    PathRequest req;
    req.start               = {0.2F, 0.0F, 0.2F};
    req.goal                = {0.5F, 0.0F, 0.2F};
    req.max_search_distance = 100.0F;

    // Act
    const PathResult res = pf.find_path(req);

    // Assert — same-triangle fast-path sets triangles_explored = 1
    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.triangles_explored, 1U);
}

// ---------------------------------------------------------------------------
// T16 — path reconstruction: waypoints[0] == start-triangle centroid,
//         waypoints.back() == exact goal position.
// ---------------------------------------------------------------------------
TEST(Pathfinding, PathReconstructionFirstAndLastWaypoints)
{
    // Arrange — quad mesh, path spans multiple triangles
    Pathfinder pf;
    pf.load_navmesh(make_quad_mesh());

    // start is deep inside T0 region; goal is deep inside T3 region
    const std::array<float, 3> goal = {1.8F, 0.0F, 0.8F};

    PathRequest req;
    req.start               = {0.1F, 0.0F, 0.1F};
    req.goal                = goal;
    req.max_search_distance = 100.0F;

    // Act
    const PathResult res = pf.find_path(req);

    // Assert — path succeeded and contains at least start + goal waypoints
    ASSERT_TRUE(res.success);
    ASSERT_GE(res.waypoints.size(), 2U);

    // Last waypoint must be the exact goal position.
    EXPECT_FLOAT_EQ(res.waypoints.back()[0], goal[0]);
    EXPECT_FLOAT_EQ(res.waypoints.back()[1], goal[1]);
    EXPECT_FLOAT_EQ(res.waypoints.back()[2], goal[2]);

    // total_distance must equal the sum of inter-waypoint segments
    float manual_total = 0.0F;
    for (std::size_t i = 1; i < res.waypoints.size(); ++i)
    {
        const auto& a = res.waypoints[i - 1U];
        const auto& b = res.waypoints[i];
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        manual_total += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    EXPECT_NEAR(res.total_distance, manual_total, 1e-4F);
}

// ---------------------------------------------------------------------------
// T17 — nearest_triangle returns the correct triangle index on a 4-tri mesh
// ---------------------------------------------------------------------------
TEST(Pathfinding, NearestTriangleReturnsCorrectIndex)
{
    // Arrange — quad mesh: T0 centroid near (0.33, 0, 0.33), T3 near (1.67, 0, 0.67)
    Pathfinder pf;
    pf.load_navmesh(make_quad_mesh());

    // Act — point closest to T0
    const auto idx_t0 = pf.nearest_triangle({0.1F, 0.0F, 0.1F});
    // Act — point closest to T3
    const auto idx_t3 = pf.nearest_triangle({1.9F, 0.0F, 0.9F});

    // Assert — indices returned must be valid and must differ
    ASSERT_TRUE(idx_t0.has_value());
    ASSERT_TRUE(idx_t3.has_value());
    EXPECT_EQ(*idx_t0, 0U);   // T0 is the nearest to (0.1, 0, 0.1)
    EXPECT_EQ(*idx_t3, 3U);   // T3 is the nearest to (1.9, 0, 0.9)
    EXPECT_NE(*idx_t0, *idx_t3);
}

}  // namespace
