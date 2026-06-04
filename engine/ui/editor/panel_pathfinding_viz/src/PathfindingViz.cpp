// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_pathfinding_viz/src/PathfindingViz.cpp
//
// phase677 — cd::editor::panel::pathfinding_viz  implementation
// =============================================================================
#include <cd/editor/panel_pathfinding_viz/PathfindingViz.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace cd::editor::panel::pathfinding_viz
{

// ---------------------------------------------------------------------------
// NavMesh binding API
// ---------------------------------------------------------------------------

void PathfindingViz::set_navmesh(const cd::ai::pathfinding::NavMesh* navmesh) noexcept
{
    navmesh_ = navmesh;
}

std::size_t PathfindingViz::navmesh_triangle_count() const noexcept
{
    if (navmesh_ == nullptr)
        return 0U;
    return navmesh_->triangles.size();
}

// ---------------------------------------------------------------------------
// Path result binding API
// ---------------------------------------------------------------------------

void PathfindingViz::set_last_path(const cd::ai::pathfinding::PathResult* result) noexcept
{
    last_path_ = result;
}

bool PathfindingViz::has_valid_path() const noexcept
{
    return (last_path_ != nullptr) && last_path_->success;
}

float PathfindingViz::last_path_distance() const noexcept
{
    if (last_path_ == nullptr)
        return 0.0F;
    return last_path_->total_distance;
}

std::uint32_t PathfindingViz::last_path_explored() const noexcept
{
    if (last_path_ == nullptr)
        return 0U;
    return last_path_->triangles_explored;
}

// ---------------------------------------------------------------------------
// Interaction API
// ---------------------------------------------------------------------------

void PathfindingViz::simulate_click_to_test_path(float x, float y,
                                                  const cd::ui::widgets::Rect& bounds) noexcept
{
    // Clamp to bounds and store for Sprint-2 live path query wiring.
    if (!bounds.is_valid())
        return;

    const float clamped_x = std::clamp(x, bounds.x, bounds.x + bounds.w);
    const float clamped_y = std::clamp(y, bounds.y, bounds.y + bounds.h);
    goal_panel_x_ = clamped_x - bounds.x;
    goal_panel_y_ = clamped_y - bounds.y;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/*static*/ std::pair<float, float>
PathfindingViz::world_to_panel(float wx, float wz,
                                float min_x, float min_z,
                                float scale,
                                const cd::ui::widgets::Rect& view) noexcept
{
    const float px = view.x + (wx - min_x) * scale;
    const float py = view.y + (wz - min_z) * scale;
    return { px, py };
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void PathfindingViz::draw(cd::ui::renderer::DrawBatcher&  batcher,
                           const cd::ui::widgets::Theme&   theme,
                           const cd::ui::widgets::Rect&    bounds) const
{
    // ---- Background fill ----------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kPad  = 6.0F;
    constexpr float kBarH = 4.0F;
    const float     row_w = bounds.w - 2.0F * kPad;

    // ---- Separator bar (accent colour) ---------------------------------------
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    // ---- View area (below separator, above stats strip) ---------------------
    constexpr float kStatsH = 18.0F;  // height reserved for the stats strip
    const float view_x = bounds.x + kPad;
    const float view_y = bounds.y + kPad * 2.0F + kBarH;
    const float view_w = row_w;
    const float view_h = bounds.h - kPad * 3.0F - kBarH - kStatsH;

    if (view_h <= 0.0F || view_w <= 0.0F)
        return;

    // ---- Compute navmesh AABB (XZ plane) for projection --------------------
    // Default to a 10-unit square centred at origin when no mesh is bound.
    float mesh_min_x = -5.0F;
    float mesh_min_z = -5.0F;
    float mesh_max_x =  5.0F;
    float mesh_max_z =  5.0F;

    if (navmesh_ != nullptr && !navmesh_->vertices.empty())
    {
        mesh_min_x = std::numeric_limits<float>::max();
        mesh_min_z = std::numeric_limits<float>::max();
        mesh_max_x = std::numeric_limits<float>::lowest();
        mesh_max_z = std::numeric_limits<float>::lowest();

        for (const auto& v : navmesh_->vertices)
        {
            mesh_min_x = std::min(mesh_min_x, v[0]);
            mesh_min_z = std::min(mesh_min_z, v[2]);
            mesh_max_x = std::max(mesh_max_x, v[0]);
            mesh_max_z = std::max(mesh_max_z, v[2]);
        }

        // Guard against degenerate (point / line) meshes.
        if (mesh_max_x - mesh_min_x < 1e-4F)
            mesh_max_x = mesh_min_x + 1.0F;
        if (mesh_max_z - mesh_min_z < 1e-4F)
            mesh_max_z = mesh_min_z + 1.0F;
    }

    const float world_w = mesh_max_x - mesh_min_x;
    const float world_h = mesh_max_z - mesh_min_z;

    // Uniform scale to fit the view with a small inner margin.
    constexpr float kInnerMargin = 4.0F;
    const float avail_w = view_w - 2.0F * kInnerMargin;
    const float avail_h = view_h - 2.0F * kInnerMargin;
    const float scale_x = avail_w / world_w;
    const float scale_z = avail_h / world_h;
    const float scale   = std::min(scale_x, scale_z);

    // Centred offset inside the view.
    const float proj_w = world_w * scale;
    const float proj_h = world_h * scale;
    const float off_x  = (view_w - proj_w) * 0.5F;
    const float off_z  = (view_h - proj_h) * 0.5F;

    const cd::ui::widgets::Rect centred_view {
        view_x + off_x,
        view_y + off_z,
        proj_w,
        proj_h
    };

    // ---- Draw NavMesh triangles (top-down XZ projection) -------------------
    if (navmesh_ != nullptr && !navmesh_->triangles.empty())
    {
        // Triangle fill colour (surface_hover with moderate opacity).
        const cd::ui::renderer::Color tri_fill {
            theme.surface_hover.r,
            theme.surface_hover.g,
            theme.surface_hover.b,
            120U };

        // Outline / divider colour.
        const cd::ui::renderer::Color outline_col {
            theme.divider.r,
            theme.divider.g,
            theme.divider.b,
            180U };

        constexpr float kOutlineT = 1.5F;

        for (const auto& tri : navmesh_->triangles)
        {
            // Project all three vertices to panel coords.
            std::array<float, 3> xs {};
            std::array<float, 3> ys {};
            bool valid = true;

            for (std::size_t k = 0; k < 3U; ++k)
            {
                const uint32_t vi = tri.vertex_indices[k];
                if (vi >= navmesh_->vertices.size())
                {
                    valid = false;
                    break;
                }
                const auto& vert = navmesh_->vertices[vi];
                auto [px, py] = world_to_panel(vert[0], vert[2],
                                               mesh_min_x, mesh_min_z,
                                               scale, centred_view);
                xs[k] = px;
                ys[k] = py;
            }

            if (!valid)
                continue;

            // Bounding box of the projected triangle as the fill quad.
            const float tri_min_x = std::min({ xs[0], xs[1], xs[2] });
            const float tri_min_y = std::min({ ys[0], ys[1], ys[2] });
            const float tri_max_x = std::max({ xs[0], xs[1], xs[2] });
            const float tri_max_y = std::max({ ys[0], ys[1], ys[2] });
            const float tri_w = tri_max_x - tri_min_x;
            const float tri_h = tri_max_y - tri_min_y;

            if (tri_w < 0.5F && tri_h < 0.5F)
                continue;  // Degenerate / too small — skip.

            // Fill quad.
            batcher.quad(tri_min_x, tri_min_y, tri_w, tri_h, tri_fill);

            // Outline: four border quads (top, bottom, left, right).
            batcher.quad(tri_min_x, tri_min_y,        tri_w, kOutlineT, outline_col); // top
            batcher.quad(tri_min_x, tri_max_y - kOutlineT, tri_w, kOutlineT, outline_col); // bottom
            batcher.quad(tri_min_x, tri_min_y,        kOutlineT, tri_h, outline_col); // left
            batcher.quad(tri_max_x - kOutlineT, tri_min_y, kOutlineT, tri_h, outline_col); // right
        }
    }
    else
    {
        // No mesh bound — draw a placeholder dim rectangle.
        batcher.quad(centred_view.x, centred_view.y,
                     centred_view.w, centred_view.h,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         30U });
    }

    // ---- Draw last path waypoints as accent line ----------------------------
    if (last_path_ != nullptr && last_path_->success &&
        last_path_->waypoints.size() >= 2U)
    {
        constexpr float kPathT = 3.5F;  // thicker than outline for visibility

        const cd::ui::renderer::Color path_col {
            theme.accent.r,
            theme.accent.g,
            theme.accent.b,
            230U };

        for (std::size_t i = 0; i + 1U < last_path_->waypoints.size(); ++i)
        {
            const auto& wp_a = last_path_->waypoints[i];
            const auto& wp_b = last_path_->waypoints[i + 1U];

            auto [ax, ay] = world_to_panel(wp_a[0], wp_a[2],
                                           mesh_min_x, mesh_min_z,
                                           scale, centred_view);
            auto [bx, by] = world_to_panel(wp_b[0], wp_b[2],
                                           mesh_min_x, mesh_min_z,
                                           scale, centred_view);

            // Draw as axis-aligned L-shaped connector (horizontal + vertical).
            // This avoids a true line-primitive dependency in the DrawBatcher.
            const float seg_min_x = std::min(ax, bx);
            const float seg_max_x = std::max(ax, bx);
            const float seg_min_y = std::min(ay, by);
            const float seg_max_y = std::max(ay, by);

            const float hlen = seg_max_x - seg_min_x;
            const float vlen = seg_max_y - seg_min_y;

            if (hlen >= 0.5F)
            {
                batcher.quad(seg_min_x, ay - kPathT * 0.5F,
                             hlen, kPathT, path_col);
            }
            if (vlen >= 0.5F)
            {
                batcher.quad(bx - kPathT * 0.5F, seg_min_y,
                             kPathT, vlen, path_col);
            }

            // Waypoint dot at position A.
            constexpr float kDotR = 3.5F;
            batcher.quad(ax - kDotR, ay - kDotR, kDotR * 2.0F, kDotR * 2.0F, path_col);
        }

        // Dot at the final waypoint.
        {
            constexpr float kDotR = 3.5F;
            const auto& wp_last = last_path_->waypoints.back();
            auto [lx, ly] = world_to_panel(wp_last[0], wp_last[2],
                                           mesh_min_x, mesh_min_z,
                                           scale, centred_view);
            batcher.quad(lx - kDotR, ly - kDotR, kDotR * 2.0F, kDotR * 2.0F, path_col);
        }
    }

    // ---- Goal point marker (from simulate_click_to_test_path) ---------------
    if (goal_panel_x_ >= 0.0F && goal_panel_y_ >= 0.0F)
    {
        constexpr float kGoalR = 5.0F;
        const float gx = view_x + goal_panel_x_;
        const float gy = view_y + goal_panel_y_;
        batcher.quad(gx - kGoalR, gy - kGoalR, kGoalR * 2.0F, kGoalR * 2.0F,
                     cd::ui::renderer::Color { 255U, 80U, 80U, 200U });
    }

    // ---- Stats strip ---------------------------------------------------------
    // Visual indicator bars for: triangles, path success, distance, explored.
    // Rendered as a row of coloured quads at the bottom of the panel.
    // Each "segment" is a filled bar sized proportionally to its value.
    {
        const float stats_y = bounds.y + bounds.h - kPad - kStatsH;
        const float stats_x = bounds.x + kPad;
        constexpr float kSegH = 8.0F;
        constexpr float kSegGap = 4.0F;

        const std::size_t tri_count = navmesh_triangle_count();

        // Segment 1: triangle count bar (up to 64 triangles = full width).
        {
            constexpr float kMaxTri = 64.0F;
            const float fill_frac = std::min(static_cast<float>(tri_count) / kMaxTri, 1.0F);
            const float seg_w = (row_w * 0.25F - kSegGap) * fill_frac;
            batcher.quad(stats_x, stats_y, row_w * 0.25F - kSegGap, kSegH,
                         cd::ui::renderer::Color {
                             theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 40U });
            if (seg_w > 0.5F)
            {
                batcher.quad(stats_x, stats_y, seg_w, kSegH,
                             cd::ui::renderer::Color { 100U, 160U, 230U, 200U });
            }
        }

        // Segment 2: path success / fail indicator.
        {
            const float seg_x = stats_x + row_w * 0.25F;
            const cd::ui::renderer::Color col = has_valid_path()
                ? cd::ui::renderer::Color { 80U, 200U, 80U, 220U }   // success — green
                : cd::ui::renderer::Color { 200U, 80U, 80U, 220U };  // fail — red
            batcher.quad(seg_x, stats_y, row_w * 0.2F - kSegGap, kSegH, col);
        }

        // Segment 3: path distance bar (up to 100 world units = full).
        {
            constexpr float kMaxDist = 100.0F;
            const float seg_x = stats_x + row_w * 0.45F;
            const float fill_frac = std::min(last_path_distance() / kMaxDist, 1.0F);
            const float seg_w = (row_w * 0.275F - kSegGap) * fill_frac;
            batcher.quad(seg_x, stats_y, row_w * 0.275F - kSegGap, kSegH,
                         cd::ui::renderer::Color {
                             theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 40U });
            if (seg_w > 0.5F)
            {
                batcher.quad(seg_x, stats_y, seg_w, kSegH,
                             cd::ui::renderer::Color { 230U, 160U, 60U, 200U });
            }
        }

        // Segment 4: explored triangles bar (up to 128 = full).
        {
            constexpr float kMaxExplored = 128.0F;
            const float seg_x = stats_x + row_w * 0.725F;
            const float fill_frac = std::min(
                static_cast<float>(last_path_explored()) / kMaxExplored, 1.0F);
            const float seg_w = (row_w * 0.275F) * fill_frac;
            batcher.quad(seg_x, stats_y, row_w * 0.275F, kSegH,
                         cd::ui::renderer::Color {
                             theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 40U });
            if (seg_w > 0.5F)
            {
                batcher.quad(seg_x, stats_y, seg_w, kSegH,
                             cd::ui::renderer::Color { 180U, 100U, 230U, 200U });
            }
        }
    }
}

}  // namespace cd::editor::panel::pathfinding_viz
