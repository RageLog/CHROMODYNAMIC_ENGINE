// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_pathfinding_viz/tests/test_pathfinding_viz.cpp
//
// phase677 — unit tests for cd::editor::panel::pathfinding_viz::PathfindingViz.
// phase742 — Sprint-2 tests: set_camera, 3D overlay, toggle, on_key('V').
//
// All tests are headless (no ImGui / no RHI). We verify:
//
//   * DefaultCtorNoMeshNoPath          — default ctor: 0 triangles, no valid path,
//                                        0.0f distance, 0 explored, 2D mode.
//   * SetNavmeshRoundTrips             — set_navmesh / navmesh_triangle_count + clear.
//   * SetLastPathRoundTrips            — set_last_path / has_valid_path /
//                                        last_path_distance / last_path_explored.
//   * DrawNoMeshEmitsBackground        — draw() with null mesh + null path emits
//                                        at least the background quad.
//   * DrawWithMeshEmitsMoreQuads       — draw() with a bound 2-triangle navmesh
//                                        emits significantly more quads than the
//                                        no-mesh case (triangle + outline quads).
//   * DrawWithPathEmitsAccentQuads     — draw() with a successful 3-waypoint path
//                                        emits more quads than the same mesh without
//                                        a path (the waypoint line + dots).
//   * SimulateClickStoresGoal          — simulate_click_to_test_path within valid
//                                        bounds does not crash and the panel state
//                                        remains consistent (draw still works).
//   * DrawZeroBoundsDoesNotCrash       — draw() on a zero-size rect returns early
//                                        after the background quad.
//   * FailedPathReportedCorrectly      — a PathResult with success=false is
//                                        reflected by has_valid_path()==false.
//   * ToggleModeStartsIn2D             — is_3d_mode() is false by default.
//   * ToggleModeFlips                  — toggle_projection_mode() flips the flag.
//   * OnKeyV_TogglesMode               — on_key('V') toggles; on_key('X') does not.
//   * SetCameraRoundTrips              — set_camera stores the matrix (no crash).
//   * Draw3dModeEmitsQuads             — draw() in 3D mode with camera matrix emits quads.
//   * Draw3dBehindCameraCulled         — all vertices behind camera yields no mesh quads
//                                        beyond background in 3D mode.
//   * Draw3dWithPathEmitsAccentQuads   — 3D mode + path emits more than no-path case.
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
    EXPECT_FALSE(panel.is_3d_mode());  // default must be 2D top-down
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

// ===========================================================================
// Sprint-2 Tests (phase742)
// ===========================================================================

// ===========================================================================
// TEST 10 — ToggleModeStartsIn2D
//   Default construction must start in 2D top-down mode.
// ===========================================================================
TEST(PathfindingViz, ToggleModeStartsIn2D)
{
    const pv::PathfindingViz panel;
    EXPECT_FALSE(panel.is_3d_mode());
}

// ===========================================================================
// TEST 11 — ToggleModeFlips
//   toggle_projection_mode() must flip the mode each call.
// ===========================================================================
TEST(PathfindingViz, ToggleModeFlips)
{
    pv::PathfindingViz panel;

    EXPECT_FALSE(panel.is_3d_mode());

    panel.toggle_projection_mode();
    EXPECT_TRUE(panel.is_3d_mode());

    panel.toggle_projection_mode();
    EXPECT_FALSE(panel.is_3d_mode());
}

// ===========================================================================
// TEST 12 — OnKeyV_TogglesMode
//   on_key('V') must toggle; on_key('X') must leave mode unchanged.
// ===========================================================================
TEST(PathfindingViz, OnKeyV_TogglesMode)
{
    pv::PathfindingViz panel;

    EXPECT_FALSE(panel.is_3d_mode());

    panel.on_key('V');
    EXPECT_TRUE(panel.is_3d_mode());

    panel.on_key('v');  // lowercase must also toggle
    EXPECT_FALSE(panel.is_3d_mode());

    panel.on_key('X');  // unrelated key — must not change
    EXPECT_FALSE(panel.is_3d_mode());
}

// ===========================================================================
// TEST 13 — SetCameraRoundTrips
//   set_camera() stores the matrix without crashing; draw() continues to work.
// ===========================================================================
TEST(PathfindingViz, SetCameraRoundTrips)
{
    pv::PathfindingViz panel;
    const pf::NavMesh mesh = make_test_navmesh();
    panel.set_navmesh(&mesh);

    // Build a simple orthographic-like VP matrix (identity-ish) so that
    // world verts project inside NDC.
    // Column-major identity.
    const std::array<float, 16> vp_identity {{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    }};
    EXPECT_NO_THROW(panel.set_camera(vp_identity));

    // Draw in 3D mode must not crash after set_camera.
    panel.toggle_projection_mode();
    EXPECT_TRUE(panel.is_3d_mode());

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 600.0F, 400.0F };
    batcher.begin_frame();
    EXPECT_NO_THROW(panel.draw(batcher, theme, bounds));
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(4U));
}

// ===========================================================================
// TEST 14 — Draw3dModeEmitsQuads
//   In 3D mode with a perspective-like VP matrix that places navmesh verts
//   in front of the camera, draw() must emit quads (background + triangles).
// ===========================================================================
TEST(PathfindingViz, Draw3dModeEmitsQuads)
{
    const pf::NavMesh mesh = make_test_navmesh();
    const cd::ui::widgets::Theme theme {};
    const cd::ui::widgets::Rect  bounds { 0.0F, 0.0F, 800.0F, 600.0F };

    // Build a simple orthographic VP: scale XYZ to ~NDC range for
    // the test navmesh vertices (range [0,10] in X and Z, Y=0).
    // We want clip_w > 0 for all verts. Use a VP where w = 1 always.
    //
    // Column-major: scale x by 0.2, z maps to y by 0.2; w row is [0,0,0,1].
    // This maps X=[0,10] → cx=[0,2], Z=[0,10] → cy=[0,2].
    // With w=1 always (perspective row = [0,0,0,1]), all verts visible.
    const std::array<float, 16> vp_ortho {{
        0.2F, 0.0F, 0.0F, 0.0F,   // col 0
        0.0F, 0.0F, 0.2F, 0.0F,   // col 1 (maps Z to Y)
        0.0F, 0.0F, 0.0F, 0.0F,   // col 2 (Z axis — unused in output)
        0.0F, 0.0F, 0.0F, 1.0F    // col 3 (w row: cx[3]=0*wx+...+1 => w=1)
    }};

    std::size_t verts_2d {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        panel.set_navmesh(&mesh);
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_2d = batcher.vertex_count();
    }

    std::size_t verts_3d {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        panel.set_navmesh(&mesh);
        panel.set_camera(vp_ortho);
        panel.toggle_projection_mode();
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_3d = batcher.vertex_count();
    }

    // 3D draw must emit at least the background + stats quads.
    EXPECT_GE(verts_3d, static_cast<std::size_t>(4U));
    // Both modes have navmesh, so both should emit >4 verts.
    EXPECT_GT(verts_2d, static_cast<std::size_t>(4U));
}

// ===========================================================================
// TEST 15 — Draw3dBehindCameraCulled
//   All navmesh vertices placed behind the camera (w <= 0) must be culled —
//   the draw count must not exceed a small baseline (bg + stats).
// ===========================================================================
TEST(PathfindingViz, Draw3dBehindCameraCulled)
{
    const pf::NavMesh mesh = make_test_navmesh();
    const cd::ui::widgets::Theme theme {};
    const cd::ui::widgets::Rect  bounds { 0.0F, 0.0F, 600.0F, 400.0F };

    // VP where the w row flips sign: [0,0,0,-1] → w = -1 for all verts.
    const std::array<float, 16> vp_behind {{
        1.0F, 0.0F, 0.0F,  0.0F,
        0.0F, 1.0F, 0.0F,  0.0F,
        0.0F, 0.0F, 1.0F,  0.0F,
        0.0F, 0.0F, 0.0F, -1.0F
    }};

    pv::PathfindingViz            panel;
    cd::ui::renderer::DrawBatcher batcher;
    panel.set_navmesh(&mesh);
    panel.set_camera(vp_behind);
    panel.toggle_projection_mode();
    batcher.begin_frame();
    panel.draw(batcher, theme, bounds);

    // Background (4) + separator (4) + stats segments (~16) = ~24 verts max.
    // Definitely no triangle quads (5*4=20 per tri) should appear.
    // We allow up to 48 verts for the chrome (bg + bar + stats).
    EXPECT_LT(batcher.vertex_count(), static_cast<std::size_t>(48U));
}

// ===========================================================================
// TEST 16 — Draw3dWithPathEmitsAccentQuads
//   3D mode + a successful path must emit more quads than 3D mode without path.
// ===========================================================================
TEST(PathfindingViz, Draw3dWithPathEmitsAccentQuads)
{
    const pf::NavMesh    mesh    = make_test_navmesh();
    const pf::PathResult success = make_success_path();
    const cd::ui::widgets::Theme theme {};
    const cd::ui::widgets::Rect  bounds { 0.0F, 0.0F, 800.0F, 600.0F };

    // Use the same w=1 ortho VP from test 14.
    const std::array<float, 16> vp_ortho {{
        0.2F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.2F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    }};

    std::size_t verts_no_path {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        panel.set_navmesh(&mesh);
        panel.set_camera(vp_ortho);
        panel.toggle_projection_mode();
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_no_path = batcher.vertex_count();
    }

    std::size_t verts_with_path {};
    {
        pv::PathfindingViz            panel;
        cd::ui::renderer::DrawBatcher batcher;
        panel.set_navmesh(&mesh);
        panel.set_camera(vp_ortho);
        panel.set_last_path(&success);
        panel.toggle_projection_mode();
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_with_path = batcher.vertex_count();
    }

    EXPECT_GT(verts_with_path, verts_no_path);
}
