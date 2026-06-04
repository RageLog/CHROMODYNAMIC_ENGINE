// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_ik_chain_editor/src/IkChainEditor.cpp
//
// phase710 — cd::editor::panel::ik_chain_editor implementation
// phase743 — 3D VP projection + draggable target manipulation
// =============================================================================
#include <cd/editor/panel_ik_chain_editor/IkChainEditor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cd::editor::panel::ik_chain_editor
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void IkChainEditor::set_chain(const cd::animation::ik::IkChain* chain) noexcept
{
    chain_ = chain;
    // Invalidate selection when the chain changes.
    selected_.reset();
}

void IkChainEditor::set_chain_mutable(cd::animation::ik::IkChain* chain) noexcept
{
    chain_mutable_ = chain;
    // Invalidate selection when the chain changes.
    selected_.reset();
    // Cancel any in-progress drag.
    is_dragging_ = false;
}

void IkChainEditor::set_last_result(
    const cd::animation::ik::IkResult* result) noexcept
{
    result_ = result;
}

void IkChainEditor::set_camera(const std::array<float, 16>& view_proj) noexcept
{
    view_proj_  = view_proj;
    has_camera_ = true;
}

void IkChainEditor::clear_camera() noexcept
{
    has_camera_ = false;
}

std::optional<std::size_t> IkChainEditor::selected_joint() const noexcept
{
    return selected_;
}

bool IkChainEditor::is_dragging_target() const noexcept
{
    return is_dragging_;
}

// ---------------------------------------------------------------------------
// Active chain accessor
// ---------------------------------------------------------------------------

const cd::animation::ik::IkChain* IkChainEditor::active_chain() const noexcept
{
    if (chain_mutable_ != nullptr)
        return chain_mutable_;
    return chain_;
}

// ---------------------------------------------------------------------------
// simulate_click
// ---------------------------------------------------------------------------

void IkChainEditor::simulate_click(
    float x, float y,
    const cd::ui::widgets::Rect& bounds) noexcept
{
    const auto* chain = active_chain();
    if (chain == nullptr || chain->joints.empty())
        return;

    // Outside panel?
    if (x < bounds.x || x > bounds.x + bounds.w ||
        y < bounds.y || y > bounds.y + bounds.h)
    {
        return;
    }

    const auto screen_pos = compute_screen_positions(*chain, bounds);

    // Hit-test each joint circle.
    float   best_dist  = kJointRadius;
    std::optional<std::size_t> best_idx;

    for (std::size_t i = 0; i < screen_pos.size(); ++i)
    {
        const float dx   = x - screen_pos[i][0];
        const float dy   = y - screen_pos[i][1];
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_idx  = i;
        }
    }

    if (best_idx.has_value())
        selected_ = best_idx;
}

// ---------------------------------------------------------------------------
// Target drag API
// ---------------------------------------------------------------------------

bool IkChainEditor::begin_target_drag(
    float x, float y,
    const cd::ui::widgets::Rect& bounds) noexcept
{
    if (chain_mutable_ == nullptr || chain_mutable_->joints.empty())
        return false;

    // Check if (x, y) is within kDragRadius of the projected target.
    const auto tgt_screen = compute_target_screen_pos(*chain_mutable_, bounds);
    const float dx   = x - tgt_screen[0];
    const float dy   = y - tgt_screen[1];
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist > kDragRadius)
        return false;

    is_dragging_  = true;
    drag_start_x_ = x;
    drag_start_y_ = y;
    return true;
}

void IkChainEditor::update_target_drag(
    float x, float y,
    const cd::ui::widgets::Rect& bounds) noexcept
{
    if (!is_dragging_ || chain_mutable_ == nullptr)
        return;

    if (has_camera_)
    {
        // 3D mode: back-project mouse position to the world plane at the
        // current target Y height.
        const float plane_y = chain_mutable_->end_effector_target[1];
        const auto  wp      = unproject_to_plane(x, y, bounds, plane_y);
        if (wp.has_value())
            chain_mutable_->end_effector_target = *wp;
    }
    else
    {
        // 2D mode: map panel pixel delta back to 2D world coordinates.
        // Reuse the same projection that draw() uses.
        const auto positions = compute_joint_positions_2d(*chain_mutable_);

        float min_x = positions[0][0];
        float max_x = positions[0][0];
        float min_y = positions[0][1];
        float max_y = positions[0][1];
        for (const auto& p : positions)
        {
            min_x = std::min(min_x, p[0]);
            max_x = std::max(max_x, p[0]);
            min_y = std::min(min_y, p[1]);
            max_y = std::max(max_y, p[1]);
        }
        const auto& old_tgt = chain_mutable_->end_effector_target;
        min_x = std::min(min_x, old_tgt[0]);
        max_x = std::max(max_x, old_tgt[0]);
        min_y = std::min(min_y, old_tgt[1]);
        max_y = std::max(max_y, old_tgt[1]);

        constexpr float kMinRange = 1.0F;
        const float range_x = std::max(max_x - min_x, kMinRange);
        const float range_y = std::max(max_y - min_y, kMinRange);

        const float content_x = bounds.x + kPad;
        const float content_y = bounds.y + kHeaderH;
        const float content_w = bounds.w - 2.0F * kPad;
        const float content_h = bounds.h - kHeaderH - kPad;

        // Invert the projection:
        //   project_x: screen_x = content_x + (wx - min_x) / range_x * content_w
        //   => wx = min_x + (screen_x - content_x) / content_w * range_x
        const float world_x = (content_w > 0.0F)
            ? (min_x + (x - content_x) / content_w * range_x)
            : old_tgt[0];

        // project_y: screen_y = content_y + (1 - (wy - min_y)/range_y) * content_h
        //   => wy = min_y + (1 - (screen_y - content_y)/content_h) * range_y
        const float world_y = (content_h > 0.0F)
            ? (min_y + (1.0F - (y - content_y) / content_h) * range_y)
            : old_tgt[1];

        chain_mutable_->end_effector_target[0] = world_x;
        chain_mutable_->end_effector_target[1] = world_y;
        // Z component unchanged in 2D mode.
    }

    // Re-solve the IK chain with the updated target.
    drag_result_ = solver_.solve(*chain_mutable_);
}

void IkChainEditor::end_target_drag() noexcept
{
    is_dragging_ = false;
}

// ---------------------------------------------------------------------------
// Projection helpers — 2D
// ---------------------------------------------------------------------------

float IkChainEditor::project_x(float world_x,
                                float world_min_x, float world_range_x,
                                float panel_x,     float panel_w) noexcept
{
    const float t = (world_x - world_min_x) / world_range_x;
    return panel_x + t * panel_w;
}

float IkChainEditor::project_y(float world_y,
                                float world_min_y, float world_range_y,
                                float panel_y,     float panel_h) noexcept
{
    // Flip Y: world +Y → screen upward → smaller panel Y.
    const float t = (world_y - world_min_y) / world_range_y;
    return panel_y + (1.0F - t) * panel_h;
}

// ---------------------------------------------------------------------------
// Projection helpers — 3D
// ---------------------------------------------------------------------------

std::array<float, 2> IkChainEditor::project_3d(
    const std::array<float, 3>& wp,
    const cd::ui::widgets::Rect& bounds) const noexcept
{
    // Multiply [wx, wy, wz, 1] by the column-major 4×4 VP matrix.
    // Column-major: m[col*4 + row].
    // clip_x = m[0]*wx + m[4]*wy + m[8]*wz  + m[12]
    // clip_y = m[1]*wx + m[5]*wy + m[9]*wz  + m[13]
    // clip_w = m[3]*wx + m[7]*wy + m[11]*wz + m[15]

    const auto& m = view_proj_;
    const float wx = wp[0];
    const float wy = wp[1];
    const float wz = wp[2];

    const float clip_x = m[0] * wx + m[4] * wy + m[8]  * wz + m[12];
    const float clip_y = m[1] * wx + m[5] * wy + m[9]  * wz + m[13];
    const float clip_w = m[3] * wx + m[7] * wy + m[11] * wz + m[15];

    // Perspective divide → NDC.
    constexpr float kEps = 1e-6F;
    const float inv_w  = (std::abs(clip_w) > kEps) ? (1.0F / clip_w) : 1.0F;
    const float ndc_x  = clip_x * inv_w;  // [-1, +1]
    const float ndc_y  = clip_y * inv_w;  // [-1, +1]  (+1 = top in NDC)

    // Map NDC [-1,+1] → panel pixels.
    // NDC x: -1 → bounds.x,  +1 → bounds.x + bounds.w
    // NDC y:  +1 → bounds.y, -1 → bounds.y + bounds.h  (flip Y)
    const float px = bounds.x + (ndc_x + 1.0F) * 0.5F * bounds.w;
    const float py = bounds.y + (1.0F - ndc_y)  * 0.5F * bounds.h;

    return { px, py };
}

// ---------------------------------------------------------------------------
// Unproject (screen → world plane)
// ---------------------------------------------------------------------------

std::optional<std::array<float, 3>> IkChainEditor::unproject_to_plane(
    float x, float y,
    const cd::ui::widgets::Rect& bounds,
    float world_y_plane) const noexcept
{
    if (!has_camera_)
        return std::nullopt;

    // Convert pixel (x, y) to NDC.
    const float ndc_x = (bounds.w > 0.0F) ? (2.0F * (x - bounds.x) / bounds.w - 1.0F) : 0.0F;
    const float ndc_y = (bounds.h > 0.0F) ? (1.0F - 2.0F * (y - bounds.y) / bounds.h) : 0.0F;

    // Invert the VP matrix to go from clip → world.
    // We need to find world pos for two clip-z values (-1 and +1) to get a ray.
    // Compute VP_inverse using Cramer's rule for a 4×4 matrix.
    // Abbreviated: extract world-space ray from NDC point.
    //
    // Strategy: solve VP * world = clip for two points:
    //   near: [ndc_x, ndc_y, -1, 1]  (NDC near plane)
    //   far:  [ndc_x, ndc_y, +1, 1]
    // Then parametrically intersect with world_y = world_y_plane.
    //
    // Since computing the full 4×4 inverse inline is feasible but verbose,
    // we use the standard approach:
    //   inv_vp = adjugate(vp) / det(vp)
    // For a 16-float array. We only need the result applied to two clip points.

    // Helper: compute the full 4×4 inverse (column-major).
    const auto& m = view_proj_;

    // Cofactors (using standard 4×4 inverse via cofactor expansion).
    // Notation: mij = m[col*4 + row] → m[c][r] with c in [0,3], r in [0,3].
    // We store column-major, so m[c*4+r].

    auto M = [&](int c, int r) -> float { return m[static_cast<std::size_t>(c * 4 + r)]; };

    // Compute inverse using Gauss-Jordan or cofactor formula.
    // Using the 2x2 sub-determinant helper.
    auto det2 = [](float a, float b, float c, float d) -> float {
        return a * d - b * c;
    };

    // 3×3 sub-determinants for cofactors.
    auto det3 = [&det2](float a0, float a1, float a2,
                        float b0, float b1, float b2,
                        float c0, float c1, float c2) -> float
    {
        return a0 * det2(b1, b2, c1, c2)
             - a1 * det2(b0, b2, c0, c2)
             + a2 * det2(b0, b1, c0, c1);
    };

    // Cofactor matrix (transposed = adjugate, column-major output).
    // adj[r][c] = (-1)^(r+c) * det3(minor(c, r)).
    // We only need the 4 rows of inv applied to a 4-vector, so compute
    // inv * clip_near and inv * clip_far.

    // Full determinant of VP.
    const float det =
          M(0,0) * det3(M(1,1), M(1,2), M(1,3),
                        M(2,1), M(2,2), M(2,3),
                        M(3,1), M(3,2), M(3,3))
        - M(1,0) * det3(M(0,1), M(0,2), M(0,3),
                        M(2,1), M(2,2), M(2,3),
                        M(3,1), M(3,2), M(3,3))
        + M(2,0) * det3(M(0,1), M(0,2), M(0,3),
                        M(1,1), M(1,2), M(1,3),
                        M(3,1), M(3,2), M(3,3))
        - M(3,0) * det3(M(0,1), M(0,2), M(0,3),
                        M(1,1), M(1,2), M(1,3),
                        M(2,1), M(2,2), M(2,3));

    constexpr float kDetEps = 1e-10F;
    if (std::abs(det) < kDetEps)
        return std::nullopt;

    const float inv_det = 1.0F / det;

    // Build the full 4×4 inverse matrix (row-major for convenience, then we
    // apply it to a column vector).
    // inv_row[r][c] = cofactor(c, r) * inv_det
    // i.e. inv[r][c] = (-1)^(r+c) * det3(minor of col c, row r) * inv_det

    // We'll compute inv applied to a 4-vector in-place.
    // inv * v = row dot v for each output row.

    auto cofactor = [&](int col, int row) -> float
    {
        // Minor: the 3×3 matrix excluding column `col` and row `row`.
        // Returns cofactor = (-1)^(row+col) * det3(minor).
        std::array<int, 3> cols_left {};
        std::array<int, 3> rows_left {};
        int ci = 0;
        int ri = 0;
        for (int c2 = 0; c2 < 4; ++c2) if (c2 != col) cols_left[static_cast<std::size_t>(ci++)] = c2;
        for (int r2 = 0; r2 < 4; ++r2) if (r2 != row) rows_left[static_cast<std::size_t>(ri++)] = r2;

        const float minor_det = det3(
            M(cols_left[0], rows_left[0]), M(cols_left[1], rows_left[0]), M(cols_left[2], rows_left[0]),
            M(cols_left[0], rows_left[1]), M(cols_left[1], rows_left[1]), M(cols_left[2], rows_left[1]),
            M(cols_left[0], rows_left[2]), M(cols_left[1], rows_left[2]), M(cols_left[2], rows_left[2])
        );
        // Sign: (-1)^(row+col)
        const float sign = ((row + col) % 2 == 0) ? 1.0F : -1.0F;
        return sign * minor_det * inv_det;
    };

    // Apply inv_vp to a clip-space point [cx, cy, cz, 1] → world [wx, wy, wz, ww].
    // inv[output_row] dotted with [cx, cy, cz, 1].
    // inv[r][c] = cofactor(c, r)  (note: adjugate is transpose of cofactor matrix).
    auto apply_inv = [&](float cx, float cy, float cz) -> std::array<float, 4>
    {
        std::array<float, 4> result {};
        for (int r = 0; r < 4; ++r)
        {
            result[static_cast<std::size_t>(r)] =
                  cofactor(0, r) * cx
                + cofactor(1, r) * cy
                + cofactor(2, r) * cz
                + cofactor(3, r) * 1.0F;
        }
        return result;
    };

    // Unproject near and far clip plane points.
    const auto near_h = apply_inv(ndc_x, ndc_y, -1.0F);
    const auto far_h  = apply_inv(ndc_x, ndc_y,  1.0F);

    constexpr float kWEps = 1e-7F;
    if (std::abs(near_h[3]) < kWEps || std::abs(far_h[3]) < kWEps)
        return std::nullopt;

    // Perspective divide → world positions.
    const float inv_nw = 1.0F / near_h[3];
    const float inv_fw = 1.0F / far_h[3];

    const float near_wx = near_h[0] * inv_nw;
    const float near_wy = near_h[1] * inv_nw;
    const float near_wz = near_h[2] * inv_nw;

    const float far_wx  = far_h[0]  * inv_fw;
    const float far_wy  = far_h[1]  * inv_fw;
    const float far_wz  = far_h[2]  * inv_fw;

    // Ray direction in world space.
    const float ray_dy = far_wy - near_wy;
    if (std::abs(ray_dy) < kWEps)
        return std::nullopt;  // Ray parallel to Y plane.

    // Parametric intersection: near + t * dir at Y = world_y_plane.
    const float t = (world_y_plane - near_wy) / ray_dy;

    return std::array<float, 3>{
        near_wx + t * (far_wx - near_wx),
        world_y_plane,
        near_wz + t * (far_wz - near_wz)
    };
}

// ---------------------------------------------------------------------------
// Screen position computation
// ---------------------------------------------------------------------------

std::vector<std::array<float, 2>> IkChainEditor::compute_screen_positions(
    const cd::animation::ik::IkChain& chain,
    const cd::ui::widgets::Rect& bounds) const noexcept
{
    if (has_camera_)
    {
        const auto world3d = compute_joint_positions_3d(chain);
        std::vector<std::array<float, 2>> screen;
        screen.reserve(world3d.size());
        for (const auto& wp : world3d)
            screen.push_back(project_3d(wp, bounds));
        return screen;
    }

    // 2D side-view fallback.
    const auto positions2d = compute_joint_positions_2d(chain);

    float min_x = positions2d[0][0];
    float max_x = positions2d[0][0];
    float min_y = positions2d[0][1];
    float max_y = positions2d[0][1];
    for (const auto& p : positions2d)
    {
        min_x = std::min(min_x, p[0]);
        max_x = std::max(max_x, p[0]);
        min_y = std::min(min_y, p[1]);
        max_y = std::max(max_y, p[1]);
    }
    const auto& tgt = chain.end_effector_target;
    min_x = std::min(min_x, tgt[0]);
    max_x = std::max(max_x, tgt[0]);
    min_y = std::min(min_y, tgt[1]);
    max_y = std::max(max_y, tgt[1]);

    constexpr float kMinRange = 1.0F;
    const float range_x = std::max(max_x - min_x, kMinRange);
    const float range_y = std::max(max_y - min_y, kMinRange);

    const float content_x = bounds.x + kPad;
    const float content_y = bounds.y + kHeaderH;
    const float content_w = bounds.w - 2.0F * kPad;
    const float content_h = bounds.h - kHeaderH - kPad;

    std::vector<std::array<float, 2>> screen;
    screen.reserve(positions2d.size());
    for (const auto& p : positions2d)
    {
        screen.push_back({
            project_x(p[0], min_x, range_x, content_x, content_w),
            project_y(p[1], min_y, range_y, content_y, content_h)
        });
    }
    return screen;
}

std::array<float, 2> IkChainEditor::compute_target_screen_pos(
    const cd::animation::ik::IkChain& chain,
    const cd::ui::widgets::Rect& bounds) const noexcept
{
    if (has_camera_)
        return project_3d(chain.end_effector_target, bounds);

    // 2D fallback: recompute extents.
    const auto positions2d = compute_joint_positions_2d(chain);

    float min_x = positions2d[0][0];
    float max_x = positions2d[0][0];
    float min_y = positions2d[0][1];
    float max_y = positions2d[0][1];
    for (const auto& p : positions2d)
    {
        min_x = std::min(min_x, p[0]);
        max_x = std::max(max_x, p[0]);
        min_y = std::min(min_y, p[1]);
        max_y = std::max(max_y, p[1]);
    }
    const auto& tgt = chain.end_effector_target;
    min_x = std::min(min_x, tgt[0]);
    max_x = std::max(max_x, tgt[0]);
    min_y = std::min(min_y, tgt[1]);
    max_y = std::max(max_y, tgt[1]);

    constexpr float kMinRange = 1.0F;
    const float range_x = std::max(max_x - min_x, kMinRange);
    const float range_y = std::max(max_y - min_y, kMinRange);

    const float content_x = bounds.x + kPad;
    const float content_y = bounds.y + kHeaderH;
    const float content_w = bounds.w - 2.0F * kPad;
    const float content_h = bounds.h - kHeaderH - kPad;

    return {
        project_x(tgt[0], min_x, range_x, content_x, content_w),
        project_y(tgt[1], min_y, range_y, content_y, content_h)
    };
}

// ---------------------------------------------------------------------------
// Chain geometry helpers
// ---------------------------------------------------------------------------

std::vector<std::array<float, 2>> IkChainEditor::compute_joint_positions_2d(
    const cd::animation::ik::IkChain& chain) noexcept
{
    std::vector<std::array<float, 2>> positions;
    positions.reserve(chain.joints.size());

    for (std::size_t i = 0; i < chain.joints.size(); ++i)
    {
        const auto& joint = chain.joints[i];
        if (i == 0)
        {
            // Root: local_position is the world-space origin.
            positions.push_back({ joint.local_position[0],
                                   joint.local_position[1] });
        }
        else
        {
            // Subsequent joints offset from parent.
            positions.push_back({ positions[i - 1][0] + joint.local_position[0],
                                   positions[i - 1][1] + joint.local_position[1] });
        }
    }

    return positions;
}

std::vector<std::array<float, 3>> IkChainEditor::compute_joint_positions_3d(
    const cd::animation::ik::IkChain& chain) noexcept
{
    std::vector<std::array<float, 3>> positions;
    positions.reserve(chain.joints.size());

    for (std::size_t i = 0; i < chain.joints.size(); ++i)
    {
        const auto& lp = chain.joints[i].local_position;
        if (i == 0)
        {
            positions.push_back({ lp[0], lp[1], lp[2] });
        }
        else
        {
            positions.push_back({
                positions[i - 1][0] + lp[0],
                positions[i - 1][1] + lp[1],
                positions[i - 1][2] + lp[2]
            });
        }
    }

    return positions;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void IkChainEditor::draw(
    cd::ui::renderer::DrawBatcher& batcher,
    const cd::ui::widgets::Theme&  theme,
    const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Panel background ------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    const auto* chain = active_chain();

    if (!bounds.is_valid() || chain == nullptr || chain->joints.empty())
        return;

    const float content_w = bounds.w - 2.0F * kPad;
    const float content_h = bounds.h - kHeaderH - kPad;

    // ---- 2. Accent separator bar --------------------------------------------
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 content_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    if (content_w <= 0.0F || content_h <= 0.0F)
        return;

    // Compute screen positions for all joints.
    const auto screen_pos = compute_screen_positions(*chain, bounds);
    const auto tgt_screen = compute_target_screen_pos(*chain, bounds);

    // ---- 3. Chain bone lines ------------------------------------------------
    constexpr cd::ui::renderer::Color kLineColor { 120U, 140U, 180U, 180U };

    for (std::size_t i = 0; i + 1 < screen_pos.size(); ++i)
    {
        const float x0 = screen_pos[i][0];
        const float y0 = screen_pos[i][1];
        const float x1 = screen_pos[i + 1][0];
        const float y1 = screen_pos[i + 1][1];

        // Represent bone as a bounding-box quad — axis-aligned approximation
        // sufficient for 2D/projected debug view.
        const float lx = std::min(x0, x1);
        const float ly = std::min(y0, y1) - kLineH * 0.5F;
        const float lw = std::max(std::abs(x1 - x0), kLineH);
        const float lh = std::max(std::abs(y1 - y0), kLineH);

        batcher.quad(lx, ly, lw, lh, kLineColor);
    }

    // ---- 4. Joint circles ---------------------------------------------------
    constexpr cd::ui::renderer::Color kColorRoot { 60U,  120U, 220U, 255U };
    constexpr cd::ui::renderer::Color kColorMid  { 210U, 210U, 210U, 255U };
    constexpr cd::ui::renderer::Color kColorEnd  { 80U,  200U, 100U, 255U };

    const std::size_t last_idx = screen_pos.size() - 1U;

    for (std::size_t i = 0; i < screen_pos.size(); ++i)
    {
        const float jx = screen_pos[i][0] - kJointR;
        const float jy = screen_pos[i][1] - kJointR;
        const float jd = kJointR * 2.0F;

        cd::ui::renderer::Color color = kColorMid;
        if (i == 0)
            color = kColorRoot;
        else if (i == last_idx)
            color = kColorEnd;

        batcher.quad(jx, jy, jd, jd, color);
    }

    // ---- 5. Selected joint ring (accent outline) ----------------------------
    if (selected_.has_value() && *selected_ < screen_pos.size())
    {
        const float sjx = screen_pos[*selected_][0] - kJointR - 2.0F;
        const float sjy = screen_pos[*selected_][1] - kJointR - 2.0F;
        const float sjd = (kJointR + 2.0F) * 2.0F;

        // Outer ring (accent, semi-transparent).
        batcher.quad(sjx, sjy, sjd, sjd,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         160U });
        // Inner cutout (panel background) — re-draw background at joint centre.
        batcher.quad(sjx + 2.0F, sjy + 2.0F, sjd - 4.0F, sjd - 4.0F,
                     cd::ui::renderer::Color {
                         theme.surface.r,
                         theme.surface.g,
                         theme.surface.b,
                         theme.surface.a });
    }

    // ---- 6. Target X marker (accent_warning orange) -------------------------
    // Brighter / larger when dragging to give clear affordance feedback.
    const float tx = tgt_screen[0];
    const float ty = tgt_screen[1];

    const cd::ui::renderer::Color kTargetColor = is_dragging_
        ? cd::ui::renderer::Color { 255U, 200U, 60U, 255U }   // bright drag
        : cd::ui::renderer::Color { 230U, 150U, 40U, 230U };  // normal

    const float half = is_dragging_ ? kTargetHalf * 1.5F : kTargetHalf;

    // Horizontal arm.
    batcher.quad(tx - half, ty - kLineH * 0.5F,
                 half * 2.0F, kLineH,
                 kTargetColor);

    // Vertical arm.
    batcher.quad(tx - kLineH * 0.5F, ty - half,
                 kLineH, half * 2.0F,
                 kTargetColor);

    // ---- 7. Convergence indicator (bottom-left corner) ----------------------
    // Prefer the live drag result when dragging; fall back to bound result.
    const cd::animation::ik::IkResult* active_result =
        is_dragging_ ? &drag_result_ : result_;

    if (active_result != nullptr)
    {
        const float ind_x = bounds.x + kPad;
        const float ind_y = bounds.y + bounds.h - kPad - kIndicatorW;

        if (active_result->converged)
        {
            // Green check: two quads forming a checkmark shape.
            constexpr cd::ui::renderer::Color kGreen { 80U, 200U, 100U, 220U };
            // Short arm (bottom-left tick).
            batcher.quad(ind_x,                   ind_y + kIndicatorW * 0.5F,
                         kIndicatorW * 0.4F,       kIndicatorW * 0.5F, kGreen);
            // Long arm (rising right).
            batcher.quad(ind_x + kIndicatorW * 0.3F, ind_y,
                         kIndicatorW * 0.7F,          kIndicatorW,      kGreen);
        }
        else
        {
            // Red X: two diagonal quads.
            constexpr cd::ui::renderer::Color kRed { 210U, 60U, 60U, 220U };
            batcher.quad(ind_x,              ind_y,
                         kIndicatorW,        kLineH * 1.5F, kRed);
            batcher.quad(ind_x,              ind_y + kIndicatorW - kLineH * 1.5F,
                         kIndicatorW,        kLineH * 1.5F, kRed);
        }
    }
}

}  // namespace cd::editor::panel::ik_chain_editor
