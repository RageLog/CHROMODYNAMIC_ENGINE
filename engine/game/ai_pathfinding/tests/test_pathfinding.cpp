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
// =============================================================================
#include <cd/ai/pathfinding/Pathfinding.hpp>

#include <gtest/gtest.h>

#include <array>
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

}  // namespace
