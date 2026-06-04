// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_pathfinding_viz/PathfindingViz.hpp
//
// phase677 — cd::editor::panel::pathfinding_viz  (panel_pathfinding_viz library)
//
// Editor panel that renders a cd::ai::pathfinding::NavMesh as a top-down 2D
// projection and overlays the last A* PathResult as an accent-coloured path
// line. Provides a DrawBatcher-based draw path for the DockSpace shell
// (apps/editor).
//
// Sprint-1 renders:
//
//   * A coloured panel background.
//   * A separator bar under the title area (accent colour).
//   * Each NavMesh triangle as a filled quad with a divider-colour outline
//     (top-down 2D projection: XZ plane → panel XY).
//   * The last_path waypoints as a thicker accent-coloured line connecting
//     each consecutive pair of waypoints.
//   * A stats strip at the bottom of the panel:
//       "triangles=N  last path: success/fail  distance=M  explored=K"
//     rendered as coloured indicator quads (no font dependency).
//
// Sprint-2 will add pan/zoom, interactive start/goal placement, and live
// Pathfinder re-query on drag.
//
// State API:
//   set_navmesh(const NavMesh*)                        — bind navmesh (null = detach).
//   set_last_path(const PathResult*)                   — bind last path result (null = clear).
//   simulate_click_to_test_path(x, y, bounds)          — set test goal point; fires a
//                                                         synthetic path query from the
//                                                         panel centre, stores the result
//                                                         internally. (Sprint-2 will wire
//                                                         to a live Pathfinder.)
//   navmesh_triangle_count() const noexcept            — triangles in bound mesh (0 = none).
//   has_valid_path()         const noexcept            — true when last path succeeded.
//   last_path_distance()     const noexcept            — total_distance of last path.
//   last_path_explored()     const noexcept            — triangles_explored of last path.
//
// Lifetime contract:
//   PathfindingViz is default-constructible; it holds non-owning raw pointers
//   to the NavMesh and PathResult. The caller is responsible for ensuring these
//   outlive the panel. Calling set_navmesh(nullptr) or set_last_path(nullptr)
//   detaches safely.
//
// MOMENT: A game designer drops a navmesh + clicks start/goal in the panel;
// the A* path is painted on top of the mesh — they see the algorithm WORK
// visually, debug coverage gaps, and iterate without leaving the editor.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ai/pathfinding/Pathfinding.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <cstdint>

namespace cd::editor::panel::pathfinding_viz
{

// ---------------------------------------------------------------------------
// PathfindingViz
// ---------------------------------------------------------------------------
class PathfindingViz
{
public:
    // Default-constructible; starts with no navmesh and no path.
    PathfindingViz() noexcept = default;

    // ---- NavMesh binding API ------------------------------------------------

    /// Bind to a NavMesh for top-down rendering. The panel holds a non-owning
    /// pointer; pass nullptr to detach. Does not modify or copy the mesh.
    void set_navmesh(const cd::ai::pathfinding::NavMesh* navmesh) noexcept;

    /// Returns the number of triangles in the currently bound NavMesh (0 if none).
    [[nodiscard]] std::size_t navmesh_triangle_count() const noexcept;

    // ---- Path result binding API --------------------------------------------

    /// Bind the most recent PathResult for overlay rendering. The panel holds
    /// a non-owning pointer; pass nullptr to clear. Does not copy the result.
    void set_last_path(const cd::ai::pathfinding::PathResult* result) noexcept;

    /// Returns true when a path result is bound and result->success == true.
    [[nodiscard]] bool has_valid_path() const noexcept;

    /// Returns the total_distance of the bound PathResult, or 0.0f if none.
    [[nodiscard]] float last_path_distance() const noexcept;

    /// Returns the triangles_explored count of the bound PathResult, or 0 if none.
    [[nodiscard]] std::uint32_t last_path_explored() const noexcept;

    // ---- Interaction API ----------------------------------------------------

    /// Synthetic click-to-test-path: maps the click point (x, y) in panel-local
    /// coordinates to a world-space XZ point and stores it as the test goal.
    /// Sprint-1: stores the goal point for future path overlay placement.
    /// Sprint-2 will wire this to a live Pathfinder query.
    /// `bounds` must match the bounds passed to the most recent draw() call.
    void simulate_click_to_test_path(float x, float y,
                                     const cd::ui::widgets::Rect& bounds) noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders:
    ///   * Panel background fill.
    ///   * Separator bar (accent colour).
    ///   * NavMesh triangles as top-down 2D projected quads with outline.
    ///   * Last path waypoints as thicker accent-coloured connecting lines.
    ///   * Stats bar: triangle count, path success/fail, distance, explored.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    // ---- Internal helpers ---------------------------------------------------

    /// Map a world-space XZ position to panel-local XY pixel coords within
    /// the given view rect, using the current scale/offset computed from the
    /// bound navmesh AABB.
    [[nodiscard]] static std::pair<float, float>
    world_to_panel(float wx, float wz,
                   float min_x, float min_z,
                   float scale,
                   const cd::ui::widgets::Rect& view) noexcept;

    // ---- State --------------------------------------------------------------
    const cd::ai::pathfinding::NavMesh*      navmesh_    { nullptr }; ///< Non-owning.
    const cd::ai::pathfinding::PathResult*   last_path_  { nullptr }; ///< Non-owning.

    /// Stored test goal (panel-local coords from simulate_click_to_test_path).
    float goal_panel_x_ { -1.0F };
    float goal_panel_y_ { -1.0F };
};

}  // namespace cd::editor::panel::pathfinding_viz
