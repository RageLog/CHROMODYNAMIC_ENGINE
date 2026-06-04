// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_pathfinding_viz/src/PathfindingViz.cpp
//
// phase677 — cd::editor::panel::pathfinding_viz  implementation
// phase742 — Sprint-2: VP-projected 3D overlay mode + 2D/3D toggle (key 'V').
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
// Camera API (Sprint-2)
// ---------------------------------------------------------------------------

void PathfindingViz::set_camera(const std::array<float, 16>& view_proj) noexcept
{
    vp_ = view_proj;
}

// ---------------------------------------------------------------------------
// Projection mode toggle (Sprint-2)
// ---------------------------------------------------------------------------

void PathfindingViz::toggle_projection_mode() noexcept
{
    mode_3d_ = !mode_3d_;
}

void PathfindingViz::on_key(char key) noexcept
{
    if (key == 'V' || key == 'v')
        toggle_projection_mode();
}

bool PathfindingViz::is_3d_mode() const noexcept
{
    return mode_3d_;
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

std::array<float, 3>
PathfindingViz::world_to_panel_3d(float wx, float wy, float wz,
                                   const cd::ui::widgets::Rect& view) const noexcept
{
    // VP is column-major: vp_[col*4 + row]
    // clip = VP * [wx, wy, wz, 1]^T
    const float cx = vp_[0]*wx + vp_[4]*wy + vp_[8]*wz  + vp_[12];
    const float cy = vp_[1]*wx + vp_[5]*wy + vp_[9]*wz  + vp_[13];
    // cz unused — only need w for culling and xy for projection
    const float cw = vp_[3]*wx + vp_[7]*wy + vp_[11]*wz + vp_[15];

    if (cw <= 0.0F)
        return { 0.0F, 0.0F, cw };  // Behind camera — caller must cull.

    // NDC in [-1, +1]
    const float ndc_x =  cx / cw;
    const float ndc_y = -cy / cw;  // Flip Y: NDC +Y = up, panel +Y = down.

    // Map NDC [-1,+1] → panel pixel coords.
    const float px = view.x + (ndc_x + 1.0F) * 0.5F * view.w;
    const float py = view.y + (ndc_y + 1.0F) * 0.5F * view.h;

    return { px, py, cw };
}

// ---------------------------------------------------------------------------
// draw_2d — top-down XZ projection (Sprint-1 path)
// ---------------------------------------------------------------------------

void PathfindingViz::draw_2d(cd::ui::renderer::DrawBatcher& batcher,
                              const cd::ui::widgets::Theme&  theme,
                              const cd::ui::widgets::Rect&   view,
                              float mesh_min_x, float mesh_min_z,
                              float scale) const
{
    // ---- Draw NavMesh triangles (top-down XZ projection) -------------------
    if (navmesh_ != nullptr && !navmesh_->triangles.empty())
    {
        const cd::ui::renderer::Color tri_fill {
            theme.surface_hover.r,
            theme.surface_hover.g,
            theme.surface_hover.b,
            120U };

        const cd::ui::renderer::Color outline_col {
            theme.divider.r,
            theme.divider.g,
            theme.divider.b,
            180U };

        constexpr float kOutlineT = 1.5F;

        for (const auto& tri : navmesh_->triangles)
        {
            std::array<float, 3> xs {};
            std::array<float, 3> ys {};
            bool valid = true;

            for (std::size_t k = 0; k < 3U; ++k)
            {
                const std::uint32_t vi = tri.vertex_indices[k];
                if (vi >= navmesh_->vertices.size())
                {
                    valid = false;
                    break;
                }
                const auto& vert = navmesh_->vertices[vi];
                auto [px, py] = world_to_panel(vert[0], vert[2],
                                               mesh_min_x, mesh_min_z,
                                               scale, view);
                xs[k] = px;
                ys[k] = py;
            }

            if (!valid)
                continue;

            const float tri_min_x = std::min({ xs[0], xs[1], xs[2] });
            const float tri_min_y = std::min({ ys[0], ys[1], ys[2] });
            const float tri_max_x = std::max({ xs[0], xs[1], xs[2] });
            const float tri_max_y = std::max({ ys[0], ys[1], ys[2] });
            const float tri_w = tri_max_x - tri_min_x;
            const float tri_h = tri_max_y - tri_min_y;

            if (tri_w < 0.5F && tri_h < 0.5F)
                continue;

            batcher.quad(tri_min_x, tri_min_y, tri_w, tri_h, tri_fill);
            batcher.quad(tri_min_x, tri_min_y,              tri_w, kOutlineT, outline_col);
            batcher.quad(tri_min_x, tri_max_y - kOutlineT,  tri_w, kOutlineT, outline_col);
            batcher.quad(tri_min_x, tri_min_y,              kOutlineT, tri_h, outline_col);
            batcher.quad(tri_max_x - kOutlineT, tri_min_y,  kOutlineT, tri_h, outline_col);
        }
    }
    else
    {
        batcher.quad(view.x, view.y, view.w, view.h,
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
        constexpr float kPathT = 3.5F;

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
                                           scale, view);
            auto [bx, by] = world_to_panel(wp_b[0], wp_b[2],
                                           mesh_min_x, mesh_min_z,
                                           scale, view);

            const float seg_min_x = std::min(ax, bx);
            const float seg_max_x = std::max(ax, bx);
            const float seg_min_y = std::min(ay, by);
            const float seg_max_y = std::max(ay, by);

            const float hlen = seg_max_x - seg_min_x;
            const float vlen = seg_max_y - seg_min_y;

            if (hlen >= 0.5F)
                batcher.quad(seg_min_x, ay - kPathT * 0.5F, hlen, kPathT, path_col);
            if (vlen >= 0.5F)
                batcher.quad(bx - kPathT * 0.5F, seg_min_y, kPathT, vlen, path_col);

            constexpr float kDotR = 3.5F;
            batcher.quad(ax - kDotR, ay - kDotR, kDotR * 2.0F, kDotR * 2.0F, path_col);
        }

        constexpr float kDotR = 3.5F;
        const auto& wp_last = last_path_->waypoints.back();
        auto [lx, ly] = world_to_panel(wp_last[0], wp_last[2],
                                       mesh_min_x, mesh_min_z,
                                       scale, view);
        batcher.quad(lx - kDotR, ly - kDotR, kDotR * 2.0F, kDotR * 2.0F, path_col);
    }
}

// ---------------------------------------------------------------------------
// draw_3d — VP-projected 3D overlay (Sprint-2 path)
// ---------------------------------------------------------------------------

void PathfindingViz::draw_3d(cd::ui::renderer::DrawBatcher& batcher,
                              const cd::ui::widgets::Theme&  theme,
                              const cd::ui::widgets::Rect&   view) const
{
    // ---- Draw NavMesh triangles (VP-projected) ------------------------------
    if (navmesh_ != nullptr && !navmesh_->triangles.empty())
    {
        const cd::ui::renderer::Color tri_fill {
            theme.surface_hover.r,
            theme.surface_hover.g,
            theme.surface_hover.b,
            100U };  // slightly more transparent for overlay

        const cd::ui::renderer::Color outline_col {
            theme.accent.r,
            theme.accent.g,
            theme.accent.b,
            160U };  // accent-tinted outline in 3D mode for contrast

        constexpr float kOutlineT = 1.5F;

        for (const auto& tri : navmesh_->triangles)
        {
            std::array<float, 3> xs {};
            std::array<float, 3> ys {};
            bool visible = true;

            for (std::size_t k = 0; k < 3U; ++k)
            {
                const std::uint32_t vi = tri.vertex_indices[k];
                if (vi >= navmesh_->vertices.size())
                {
                    visible = false;
                    break;
                }
                const auto& vert = navmesh_->vertices[vi];
                const auto proj = world_to_panel_3d(vert[0], vert[1], vert[2], view);

                // proj[2] is the clip-space w; cull if behind camera.
                if (proj[2] <= 0.0F)
                {
                    visible = false;
                    break;
                }

                xs[k] = proj[0];
                ys[k] = proj[1];
            }

            if (!visible)
                continue;

            const float tri_min_x = std::min({ xs[0], xs[1], xs[2] });
            const float tri_min_y = std::min({ ys[0], ys[1], ys[2] });
            const float tri_max_x = std::max({ xs[0], xs[1], xs[2] });
            const float tri_max_y = std::max({ ys[0], ys[1], ys[2] });
            const float tri_w = tri_max_x - tri_min_x;
            const float tri_h = tri_max_y - tri_min_y;

            if (tri_w < 0.5F && tri_h < 0.5F)
                continue;

            batcher.quad(tri_min_x, tri_min_y, tri_w, tri_h, tri_fill);
            batcher.quad(tri_min_x, tri_min_y,              tri_w, kOutlineT, outline_col);
            batcher.quad(tri_min_x, tri_max_y - kOutlineT,  tri_w, kOutlineT, outline_col);
            batcher.quad(tri_min_x, tri_min_y,              kOutlineT, tri_h, outline_col);
            batcher.quad(tri_max_x - kOutlineT, tri_min_y,  kOutlineT, tri_h, outline_col);
        }
    }

    // ---- Draw last path waypoints as VP-projected accent line ---------------
    if (last_path_ != nullptr && last_path_->success &&
        last_path_->waypoints.size() >= 2U)
    {
        constexpr float kPathT = 4.0F;  // slightly thicker than 2D for overlay visibility

        const cd::ui::renderer::Color path_col {
            theme.accent.r,
            theme.accent.g,
            theme.accent.b,
            240U };

        for (std::size_t i = 0; i + 1U < last_path_->waypoints.size(); ++i)
        {
            const auto& wp_a = last_path_->waypoints[i];
            const auto& wp_b = last_path_->waypoints[i + 1U];

            const auto pa = world_to_panel_3d(wp_a[0], wp_a[1], wp_a[2], view);
            const auto pb = world_to_panel_3d(wp_b[0], wp_b[1], wp_b[2], view);

            // Cull segments where either endpoint is behind the camera.
            if (pa[2] <= 0.0F || pb[2] <= 0.0F)
                continue;

            const float ax = pa[0]; const float ay = pa[1];
            const float bx = pb[0]; const float by = pb[1];

            const float seg_min_x = std::min(ax, bx);
            const float seg_max_x = std::max(ax, bx);
            const float seg_min_y = std::min(ay, by);
            const float seg_max_y = std::max(ay, by);

            const float hlen = seg_max_x - seg_min_x;
            const float vlen = seg_max_y - seg_min_y;

            if (hlen >= 0.5F)
                batcher.quad(seg_min_x, ay - kPathT * 0.5F, hlen, kPathT, path_col);
            if (vlen >= 0.5F)
                batcher.quad(bx - kPathT * 0.5F, seg_min_y, kPathT, vlen, path_col);

            constexpr float kDotR = 4.0F;
            batcher.quad(ax - kDotR, ay - kDotR, kDotR * 2.0F, kDotR * 2.0F, path_col);
        }

        // Dot at the final waypoint.
        const auto& wp_last = last_path_->waypoints.back();
        const auto pl = world_to_panel_3d(wp_last[0], wp_last[1], wp_last[2], view);
        if (pl[2] > 0.0F)
        {
            constexpr float kDotR = 4.0F;
            batcher.quad(pl[0] - kDotR, pl[1] - kDotR, kDotR * 2.0F, kDotR * 2.0F, path_col);
        }
    }
}

// ---------------------------------------------------------------------------
// DrawBatcher path — top-level dispatcher
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

    // ---- Separator bar (accent colour) with mode indicator ------------------
    // In 3D mode the bar is slightly brighter (full opacity) to signal the mode.
    const std::uint8_t bar_alpha = mode_3d_ ? 255U : static_cast<std::uint8_t>(theme.accent.a);
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     bar_alpha });

    // ---- View area (below separator, above stats strip) ---------------------
    constexpr float kStatsH = 18.0F;
    const float view_x = bounds.x + kPad;
    const float view_y = bounds.y + kPad * 2.0F + kBarH;
    const float view_w = row_w;
    const float view_h = bounds.h - kPad * 3.0F - kBarH - kStatsH;

    if (view_h <= 0.0F || view_w <= 0.0F)
        return;

    if (mode_3d_)
    {
        // ---- 3D VP-projected overlay mode -----------------------------------
        const cd::ui::widgets::Rect view_rect { view_x, view_y, view_w, view_h };
        draw_3d(batcher, theme, view_rect);
    }
    else
    {
        // ---- 2D top-down mode (Sprint-1) ------------------------------------

        // Compute navmesh AABB in XZ plane.
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

            if (mesh_max_x - mesh_min_x < 1e-4F)
                mesh_max_x = mesh_min_x + 1.0F;
            if (mesh_max_z - mesh_min_z < 1e-4F)
                mesh_max_z = mesh_min_z + 1.0F;
        }

        const float world_w = mesh_max_x - mesh_min_x;
        const float world_h = mesh_max_z - mesh_min_z;

        constexpr float kInnerMargin = 4.0F;
        const float avail_w = view_w - 2.0F * kInnerMargin;
        const float avail_h = view_h - 2.0F * kInnerMargin;
        const float scale   = std::min(avail_w / world_w, avail_h / world_h);

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

        draw_2d(batcher, theme, centred_view, mesh_min_x, mesh_min_z, scale);
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
    {
        const float stats_y = bounds.y + bounds.h - kPad - kStatsH;
        const float stats_x = bounds.x + kPad;
        constexpr float kSegH = 8.0F;
        constexpr float kSegGap = 4.0F;

        const std::size_t tri_count = navmesh_triangle_count();

        // Segment 1: triangle count bar.
        {
            constexpr float kMaxTri = 64.0F;
            const float fill_frac = std::min(static_cast<float>(tri_count) / kMaxTri, 1.0F);
            const float seg_w = (row_w * 0.25F - kSegGap) * fill_frac;
            batcher.quad(stats_x, stats_y, row_w * 0.25F - kSegGap, kSegH,
                         cd::ui::renderer::Color {
                             theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 40U });
            if (seg_w > 0.5F)
                batcher.quad(stats_x, stats_y, seg_w, kSegH,
                             cd::ui::renderer::Color { 100U, 160U, 230U, 200U });
        }

        // Segment 2: path success / fail indicator.
        {
            const float seg_x = stats_x + row_w * 0.25F;
            const cd::ui::renderer::Color col = has_valid_path()
                ? cd::ui::renderer::Color { 80U, 200U, 80U, 220U }
                : cd::ui::renderer::Color { 200U, 80U, 80U, 220U };
            batcher.quad(seg_x, stats_y, row_w * 0.2F - kSegGap, kSegH, col);
        }

        // Segment 3: path distance bar.
        {
            constexpr float kMaxDist = 100.0F;
            const float seg_x = stats_x + row_w * 0.45F;
            const float fill_frac = std::min(last_path_distance() / kMaxDist, 1.0F);
            const float seg_w = (row_w * 0.275F - kSegGap) * fill_frac;
            batcher.quad(seg_x, stats_y, row_w * 0.275F - kSegGap, kSegH,
                         cd::ui::renderer::Color {
                             theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 40U });
            if (seg_w > 0.5F)
                batcher.quad(seg_x, stats_y, seg_w, kSegH,
                             cd::ui::renderer::Color { 230U, 160U, 60U, 200U });
        }

        // Segment 4: explored triangles bar.
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
                batcher.quad(seg_x, stats_y, seg_w, kSegH,
                             cd::ui::renderer::Color { 180U, 100U, 230U, 200U });
        }
    }
}

}  // namespace cd::editor::panel::pathfinding_viz
