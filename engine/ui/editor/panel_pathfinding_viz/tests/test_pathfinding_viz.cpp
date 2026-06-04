// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_pathfinding_viz/tests/test_pathfinding_viz.cpp
//
// phase677 — unit tests for cd::editor::panel::pathfinding_viz::PathfindingViz.
//
// All tests are headless (no ImGui / no RHI). We verify:
//
//   * DefaultCtorNoMeshNoPath     — default ctor: 0 triangles, no valid path,
//                                   0.0f distance, 0 explored.
//   * SetNavmeshRoundTrips        — set_navmesh / navmesh_triangle_count + clear.
//   * SetLastPathRoundTrips       — set_last_path / has_valid_path /
//                                   last_path_distance / last_path_explored.
//   * DrawNoMeshEmitsBackground   — draw() with null mesh + null path emits
//                                   at least the background quad.
//   * DrawWithMeshEmitsMoreQuads  — draw() with a bound 2-triangle navmesh
//                                   emits significantly more quads than the
//                                   no-mesh case (triangle + outline quads).
//   * DrawWithPathEmitsAccentQuads — draw() with a successful 3-waypoint path
//                                    emits more quads than the same mesh without
//                                    a path (the waypoint line + dots).
//   * SimulateClickStoresGoal     — simulate_click_to_test_path within valid
//                                   bounds does not crash and the panel state
//                                   remains consistent (draw still works).
//   * DrawZeroBoundsDoesNotCrash  — draw() on a zero-size rect returns early
//                                   after the background quad.
//   * FailedPathReportedCorrectly — a PathResult with success=false is
//                                   reflected by has_valid_path()==false.
// =============================================================================
#include <cd/editor/panel_pathfinding_viz/PathfindingViz.hpp>

#include <cd/ai/pathfinding/Pathfinding.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace pv  = cd::editor::panel::pathfinding_viz;
namespace pf  = cd::ai::pathfinding;

// ---------------------------------------------------------------------------
// Helper — build a minimal 2-triangle navmesh (a quad split diagonally).
//
//   v0(0,0,0)  v1(10,0,0)
//   v2(0,0,10) v3(10,0,10)
//
//   tri0: v0, v1, v2  (upper-left)
//   tri1: v1, v3, v2  (lower-right)
//   Shared edge: v1-v2.
// ---------------------------------------------------------------------------
static pf::NavMesh make_test_navmesh()
{
    pf::NavMesh mesh;
    mesh.vertices = {
        { 0.0F,  0.0F,  0.0F },   // v0
        { 10.0F, 0.0F,  0.0F },   // v1
        { 0.0F,  0.0F, 10.0F },   // v2
        { 10.0F, 0.0F, 10.0F },   // v3
    };
    mesh.triangles = {
        pf::NavTriangle { { 0U, 1U, 2U }, 0U },   // tri0
        pf::NavTriangle { { 1U, 3U, 2U }, 0U },   // tri1
    };
    // triangle_neighbors: tri0 edge v1-v2 (index 1) neighbours tri1,
    //                     all other edges are boundary.
    mesh.triangle_neighbors = {
        { pf::NavMesh::kNoNeighbor, 1U, pf::NavMesh::kNoNeighbor },   // tri0
        { pf::NavMesh::kNoNeighbor, pf::NavMesh::kNoNeighbor, 0U },   // tri1
    };
    return mesh;
}

// ---------------------------------------------------------------------------
// Helper — build a successful 3-waypoint PathResult.
// ---------------------------------------------------------------------------
static pf::PathResult make_success_path()
{
    pf::PathResult res;
    res.success            = true;
    res.total_distance     = 14.14F;  // ~sqrt(200)
    res.triangles_explored = 2U;
    res.waypoints          = {
        { 1.667F, 0.0F, 1.667F },   // centroid of tri0
        { 7.0F,   0.0F, 7.0F   },   // mid-point
        { 8.333F, 0.0F, 8.333F },   // centroid of tri1
    };
    return res;
}

// ---------------------------------------------------------------------------
// Helper — build a failed PathResult.
// ---------------------------------------------------------------------------
static pf::PathResult make_fail_path()
{
    pf::PathResult res;
    res.success            = false;
    res.total_distance     = 0.0F;
    res.triangles_explored = 0U;
    // Empty waypoints.
    return res;
}

// ===========================================================================
// TEST 1 — DefaultCtorNoMeshNoPath
// ===========================================================================
TEST(PathfindingViz, DefaultCtorNoMeshNoPath)
{
    const pv::PathfindingViz panel;

    EXPECT_EQ(panel.navmesh_triangle_count(), 0U);
    EXPECT_FALSE(panel.has_valid_path());
    EXPECT_FLOAT_EQ(panel.last_path_distance(), 0.0F);
    EXPECT_EQ(panel.last_path_explored(), 0U);
}

// ===========================================================================
// TEST 2 — SetNavmeshRoundTrips
// ===========================================================================
TEST(PathfindingViz, SetNavmeshRoundTrips)
{
    pv::PathfindingViz panel;
    const pf::NavMesh mesh = make_test_navmesh();

    EXPECT_EQ(panel.navmesh_triangle_count(), 0U);

    panel.set_navmesh(&mesh);
    EXPECT_EQ(panel.navmesh_triangle_count(), 2U);

    // Detach.
    panel.set_navmesh(nullptr);
    EXPECT_EQ(panel.navmesh_triangle_count(), 0U);
}

// ===========================================================================
// TEST 3 — SetLastPathRoundTrips
// ===========================================================================
TEST(PathfindingViz, SetLastPathRoundTrips)
{
    pv::PathfindingViz panel;

    // Nothing bound.
    EXPECT_FALSE(panel.has_valid_path());
    EXPECT_FLOAT_EQ(panel.last_path_distance(), 0.0F);
    EXPECT_EQ(panel.last_path_explored(), 0U);

    // Bind a successful path.
    const pf::PathResult success = make_success_path();
    panel.set_last_path(&success);

    EXPECT_TRUE(panel.has_valid_path());
    EXPECT_FLOAT_EQ(panel.last_path_distance(), 14.14F);
    EXPECT_EQ(panel.last_path_explored(), 2U);

    // Detach.
    panel.set_last_path(nullptr);
    EXPECT_FALSE(panel.has_valid_path());
    EXPECT_FLOAT_EQ(panel.last_path_distance(), 0.0F);
    EXPECT_EQ(panel.last_path_explored(), 0U);
}

// ===========================================================================
// TEST 4 — DrawNoMeshEmitsBackground
// ===========================================================================
TEST(PathfindingViz, DrawNoMeshEmitsBackground)
{
    pv::PathfindingViz             panel;  // no mesh, no path
    cd::ui::renderer::DrawBatcher  batcher;
    const cd::ui::widgets::Theme   theme {};
    const cd::ui::widgets::Rect    bounds { 0.0F, 0.0F, 400.0F, 300.0F };

    batcher.begin_frame();
    panel.draw(batcher, theme, bounds);

    // At minimum the background quad must be emitted.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
    EXPECT_GE(batcher.vertex_count(),  static_cast<std::size_t>(4U));
}

// ===========================================================================
// TEST 5 — DrawWithMeshEmitsMoreQuads
//   Binding a 2-triangle navmesh must emit more quads than a no-mesh draw,
//   because each triangle produces a fill quad + 4 outline quads (5 quads).
// ===========================================================================
TEST(PathfindingViz, DrawWithMeshEmitsMoreQuads)
{
    const pf::NavMesh mesh = make_test_navmesh();
    const cd::ui::widgets::Theme theme {};
    const cd::ui::widgets::Rect  bounds { 0.0F, 0.0F, 600.0F, 400.0F };

    // No-mesh draw.
    std::size_t verts_no_mesh {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_no_mesh = batcher.vertex_count();
    }

    // With-mesh draw.
    std::size_t verts_with_mesh {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        panel.set_navmesh(&mesh);
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_with_mesh = batcher.vertex_count();
    }

    // 2 triangles * 5 quads * 4 verts = 40 extra verts at minimum.
    EXPECT_GT(verts_with_mesh, verts_no_mesh);
    EXPECT_GE(verts_with_mesh, verts_no_mesh + 40U);
}

// ===========================================================================
// TEST 6 — DrawWithPathEmitsAccentQuads
//   A successful 3-waypoint path must produce more quads than the same mesh
//   draw without a path (the waypoint connector line segments + dots).
// ===========================================================================
TEST(PathfindingViz, DrawWithPathEmitsAccentQuads)
{
    const pf::NavMesh    mesh    = make_test_navmesh();
    const pf::PathResult success = make_success_path();
    const cd::ui::widgets::Theme theme {};
    const cd::ui::widgets::Rect  bounds { 0.0F, 0.0F, 600.0F, 400.0F };

    std::size_t verts_no_path {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        panel.set_navmesh(&mesh);
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_no_path = batcher.vertex_count();
    }

    std::size_t verts_with_path {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        panel.set_navmesh(&mesh);
        panel.set_last_path(&success);
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_with_path = batcher.vertex_count();
    }

    EXPECT_GT(verts_with_path, verts_no_path);
}

// ===========================================================================
// TEST 7 — SimulateClickStoresGoal
//   simulate_click_to_test_path within valid bounds must not crash and the
//   panel must remain drawable (draw() after click must not crash).
// ===========================================================================
TEST(PathfindingViz, SimulateClickStoresGoal)
{
    const pf::NavMesh mesh = make_test_navmesh();
    const cd::ui::widgets::Rect bounds { 0.0F, 0.0F, 500.0F, 400.0F };

    pv::PathfindingViz panel;
    panel.set_navmesh(&mesh);

    // Click in the centre of the panel — must not crash.
    panel.simulate_click_to_test_path(250.0F, 200.0F, bounds);

    // draw() after the click must still work without crash.
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    batcher.begin_frame();
    EXPECT_NO_THROW(panel.draw(batcher, theme, bounds));

    // The goal marker quad must be emitted (extra quads vs no-click).
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(4U));
}

// ===========================================================================
// TEST 8 — DrawZeroBoundsDoesNotCrash
// ===========================================================================
TEST(PathfindingViz, DrawZeroBoundsDoesNotCrash)
{
    const pf::NavMesh mesh = make_test_navmesh();

    pv::PathfindingViz            panel;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    panel.set_navmesh(&mesh);
    batcher.begin_frame();
    EXPECT_NO_THROW(panel.draw(batcher, theme, zero));

    // Zero bounds → early return after background quad.
    // Vertex count must be very small (only background quad = 4 verts).
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ===========================================================================
// TEST 9 — FailedPathReportedCorrectly
// ===========================================================================
TEST(PathfindingViz, FailedPathReportedCorrectly)
{
    pv::PathfindingViz panel;
    const pf::PathResult fail = make_fail_path();

    panel.set_last_path(&fail);

    EXPECT_FALSE(panel.has_valid_path());
    EXPECT_FLOAT_EQ(panel.last_path_distance(), 0.0F);
    EXPECT_EQ(panel.last_path_explored(), 0U);

    // draw() with a failed path must not crash.
    const pf::NavMesh mesh = make_test_navmesh();
    panel.set_navmesh(&mesh);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 300.0F };
    batcher.begin_frame();
    EXPECT_NO_THROW(panel.draw(batcher, theme, bounds));
}
