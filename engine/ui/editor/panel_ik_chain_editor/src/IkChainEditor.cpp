// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_ik_chain_editor/src/IkChainEditor.cpp
//
// phase710 — cd::editor::panel::ik_chain_editor implementation
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

void IkChainEditor::set_last_result(
    const cd::animation::ik::IkResult* result) noexcept
{
    result_ = result;
}

std::optional<std::size_t> IkChainEditor::selected_joint() const noexcept
{
    return selected_;
}

void IkChainEditor::simulate_click(
    float x, float y,
    const cd::ui::widgets::Rect& bounds) noexcept
{
    if (chain_ == nullptr || chain_->joints.empty())
        return;

    // Outside panel?
    if (x < bounds.x || x > bounds.x + bounds.w ||
        y < bounds.y || y > bounds.y + bounds.h)
    {
        return;
    }

    const auto positions = compute_joint_positions_2d(*chain_);

    // Compute world extents for projection.
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

    // Also include the target in the extents so projection matches draw().
    const auto& tgt = chain_->end_effector_target;
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

    // Hit-test each joint circle.
    float   best_dist  = kJointRadius;
    std::optional<std::size_t> best_idx;

    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const float px = project_x(positions[i][0],
                                   min_x, range_x, content_x, content_w);
        const float py = project_y(positions[i][1],
                                   min_y, range_y, content_y, content_h);
        const float dx = x - px;
        const float dy = y - py;
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
// Projection helpers
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

    if (!bounds.is_valid() || chain_ == nullptr || chain_->joints.empty())
        return;

    const float content_x = bounds.x + kPad;
    const float content_y = bounds.y + kHeaderH;
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

    // Compute 2D world-space joint positions.
    const auto positions = compute_joint_positions_2d(*chain_);

    // Compute world extents (include target so projection is consistent).
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

    const auto& tgt = chain_->end_effector_target;
    min_x = std::min(min_x, tgt[0]);
    max_x = std::max(max_x, tgt[0]);
    min_y = std::min(min_y, tgt[1]);
    max_y = std::max(max_y, tgt[1]);

    constexpr float kMinRange = 1.0F;
    const float range_x = std::max(max_x - min_x, kMinRange);
    const float range_y = std::max(max_y - min_y, kMinRange);

    // Lambda: project world XY → panel pixel.
    auto px = [&](float wx) noexcept
    {
        return project_x(wx, min_x, range_x, content_x, content_w);
    };
    auto py = [&](float wy) noexcept
    {
        return project_y(wy, min_y, range_y, content_y, content_h);
    };

    // ---- 3. Chain bone lines ------------------------------------------------
    // Draw a thin quad from joint[i] to joint[i+1].
    constexpr cd::ui::renderer::Color kLineColor { 120U, 140U, 180U, 180U };

    for (std::size_t i = 0; i + 1 < positions.size(); ++i)
    {
        const float x0 = px(positions[i][0]);
        const float y0 = py(positions[i][1]);
        const float x1 = px(positions[i + 1][0]);
        const float y1 = py(positions[i + 1][1]);

        // Represent bone as a bounding-box quad — axis-aligned approximation
        // sufficient for 2D debug view.
        const float lx = std::min(x0, x1);
        const float ly = std::min(y0, y1) - kLineH * 0.5F;
        const float lw = std::max(std::abs(x1 - x0), kLineH);
        const float lh = std::max(std::abs(y1 - y0), kLineH);

        batcher.quad(lx, ly, lw, lh, kLineColor);
    }

    // ---- 4. Joint circles ---------------------------------------------------
    // Root = blue, mid = near-white, end-effector = success green.
    // Circles drawn as small squares centred on the joint position.
    constexpr cd::ui::renderer::Color kColorRoot { 60U,  120U, 220U, 255U };
    constexpr cd::ui::renderer::Color kColorMid  { 210U, 210U, 210U, 255U };
    constexpr cd::ui::renderer::Color kColorEnd  { 80U,  200U, 100U, 255U };

    const std::size_t last_idx = positions.size() - 1U;

    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const float jx = px(positions[i][0]) - kJointR;
        const float jy = py(positions[i][1]) - kJointR;
        const float jd = kJointR * 2.0F;

        cd::ui::renderer::Color color = kColorMid;
        if (i == 0)
            color = kColorRoot;
        else if (i == last_idx)
            color = kColorEnd;

        batcher.quad(jx, jy, jd, jd, color);
    }

    // ---- 5. Selected joint ring (accent outline) ----------------------------
    if (selected_.has_value() && *selected_ < positions.size())
    {
        const float sjx = px(positions[*selected_][0]) - kJointR - 2.0F;
        const float sjy = py(positions[*selected_][1]) - kJointR - 2.0F;
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
    // Two thin crossed quads forming an X at the target position.
    const float tx = px(tgt[0]);
    const float ty = py(tgt[1]);

    constexpr cd::ui::renderer::Color kTargetColor { 230U, 150U, 40U, 230U };

    // Horizontal arm.
    batcher.quad(tx - kTargetHalf, ty - kLineH * 0.5F,
                 kTargetHalf * 2.0F, kLineH,
                 kTargetColor);

    // Vertical arm.
    batcher.quad(tx - kLineH * 0.5F, ty - kTargetHalf,
                 kLineH, kTargetHalf * 2.0F,
                 kTargetColor);

    // ---- 7. Convergence indicator (bottom-left corner) ----------------------
    if (result_ != nullptr)
    {
        const float ind_x = bounds.x + kPad;
        const float ind_y = bounds.y + bounds.h - kPad - kIndicatorW;

        if (result_->converged)
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
