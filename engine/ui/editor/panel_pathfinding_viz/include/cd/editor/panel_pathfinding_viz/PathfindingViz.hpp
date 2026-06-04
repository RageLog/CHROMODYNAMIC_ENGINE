// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_pathfinding_viz/PathfindingViz.hpp
//
// phase677 — cd::editor::panel::pathfinding_viz  (panel_pathfinding_viz library)
// phase742 — Sprint-2: VP-projected 3D overlay mode + 2D/3D toggle (key 'V').
//
// Editor panel that renders a cd::ai::pathfinding::NavMesh either as a
// top-down 2D projection (Sprint-1) or as a VP-projected 3D overlay painted
// directly on top of the viewport scene (Sprint-2).
//
// Sprint-1 (2D top-down) renders:
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
// Sprint-2 (3D overlay) adds:
//
//   * set_camera(view_proj) — upload a column-major 4x4 combined VP matrix.
//   * Each triangle vertex is transformed: clip = VP * world; NDC = clip/w.
//     Triangles with any vertex behind the camera (w <= 0) are culled.
//   * NDC ([-1,+1]²) is mapped to panel pixel coords.
//   * Last-path waypoints projected identically and drawn as a thicker accent
//     line in 3D space.
//   * Toggle 2D ↔ 3D by calling toggle_projection_mode() or pressing key 'V'
//     (the panel tracks the keystroke via on_key('V')).
//
// State API:
//   set_navmesh(const NavMesh*)                        — bind navmesh (null = detach).
//   set_last_path(const PathResult*)                   — bind last path result (null = clear).
//   set_camera(const std::array<float,16>& view_proj)  — upload VP matrix (column-major).
//   toggle_projection_mode()                           — switch 2D ↔ 3D.
//   on_key(char key)                                   — forward keyboard event; 'V' toggles.
//   is_3d_mode() const noexcept                        — query current mode.
//   simulate_click_to_test_path(x, y, bounds)          — set test goal point.
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
// MOMENT: A game designer sees the navmesh painted ON TOP of the actual viewport
// scene — debug pathfinding in context, not on a separate 2D screen. Press 'V'
// to switch between the top-down overview and the full 3D-projected overlay.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ai/pathfinding/Pathfinding.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <array>
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

    /// Bind to a NavMesh for rendering. The panel holds a non-owning pointer;
    /// pass nullptr to detach. Does not modify or copy the mesh.
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

    // ---- Camera API (Sprint-2: 3D overlay) ----------------------------------

    /// Upload the combined view-projection matrix (column-major, 16 floats).
    /// Used by the 3D overlay mode to project world-space navmesh vertices to
    /// screen-space panel coordinates. Call once per frame before draw().
    /// Has no effect in 2D top-down mode.
    void set_camera(const std::array<float, 16>& view_proj) noexcept;

    // ---- Projection mode toggle (Sprint-2) ----------------------------------

    /// Switch between 2D top-down (Sprint-1) and 3D viewport overlay (Sprint-2).
    void toggle_projection_mode() noexcept;

    /// Forward a keyboard event. Key 'V' calls toggle_projection_mode().
    void on_key(char key) noexcept;

    /// Returns true when the 3D VP-projected overlay mode is active.
    [[nodiscard]] bool is_3d_mode() const noexcept;

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
    ///
    /// 2D mode (default):
    ///   * Panel background fill + separator bar (accent colour).
    ///   * NavMesh triangles as top-down XZ-projected quads with outline.
    ///   * Last path waypoints as thicker accent-coloured connecting lines.
    ///   * Stats bar: triangle count, path success/fail, distance, explored.
    ///
    /// 3D mode (set_camera + toggle_projection_mode / key 'V'):
    ///   * Panel background fill + separator bar.
    ///   * Each triangle vertex transformed: clip = VP * world; NDC = clip/w.
    ///     Triangles with any vertex behind the camera (w <= 0) are culled.
    ///   * NDC mapped to panel pixel coords; triangle AABB rendered as quad.
    ///   * Path waypoints projected identically; accent-coloured line + dots.
    ///   * Stats bar identical to 2D mode.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    // ---- Internal helpers ---------------------------------------------------

    /// Map a world-space XZ position to panel-local XY pixel coords within
    /// the given view rect, using the current scale/offset computed from the
    /// bound navmesh AABB. Used in 2D top-down mode.
    [[nodiscard]] static std::pair<float, float>
    world_to_panel(float wx, float wz,
                   float min_x, float min_z,
                   float scale,
                   const cd::ui::widgets::Rect& view) noexcept;

    /// Transform a world-space position [wx, wy, wz] through the stored VP
    /// matrix and map the resulting NDC to panel pixel coords within `view`.
    /// Returns {px, py, w} where w is the clip-space w component.
    /// Callers must cull when w <= 0 (behind the near plane).
    [[nodiscard]] std::array<float, 3>
    world_to_panel_3d(float wx, float wy, float wz,
                      const cd::ui::widgets::Rect& view) const noexcept;

    /// Draw navmesh triangles and path waypoints in 2D top-down mode.
    void draw_2d(cd::ui::renderer::DrawBatcher& batcher,
                 const cd::ui::widgets::Theme&  theme,
                 const cd::ui::widgets::Rect&   view,
                 float mesh_min_x, float mesh_min_z,
                 float scale) const;

    /// Draw navmesh triangles and path waypoints in 3D VP-overlay mode.
    void draw_3d(cd::ui::renderer::DrawBatcher& batcher,
                 const cd::ui::widgets::Theme&  theme,
                 const cd::ui::widgets::Rect&   view) const;

    // ---- State --------------------------------------------------------------
    const cd::ai::pathfinding::NavMesh*      navmesh_    { nullptr }; ///< Non-owning.
    const cd::ai::pathfinding::PathResult*   last_path_  { nullptr }; ///< Non-owning.

    /// Combined view-projection matrix (column-major, 4x4).
    /// Identity by default — safe even if set_camera() is never called.
    std::array<float, 16> vp_ {{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    }};

    /// When true, use VP-projected 3D overlay mode instead of 2D top-down.
    bool mode_3d_ { false };

    /// Stored test goal (panel-local coords from simulate_click_to_test_path).
    float goal_panel_x_ { -1.0F };
    float goal_panel_y_ { -1.0F };
};

}  // namespace cd::editor::panel::pathfinding_viz
