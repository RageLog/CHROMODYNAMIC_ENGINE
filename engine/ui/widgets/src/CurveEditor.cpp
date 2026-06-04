// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/CurveEditor.cpp
//
// Phase 5.17 implementation of the CurveEditor widget (header: CurveEditor.hpp).
//
// Eval strategy:
//   Piecewise cubic Hermite spline. For each segment [L, R]:
//     h = R.time - L.time              (segment width)
//     p0 = L.value, p1 = R.value
//     m0 = L.out_tangent * h           (scaled to [0..1] local param)
//     m1 = R.in_tangent  * h
//     t_local = (t - L.time) / h       in [0..1]
//     Hermite basis:
//       h00 =  2t^3 - 3t^2 + 1
//       h10 =    t^3 - 2t^2 + t
//       h01 = -2t^3 + 3t^2
//       h11 =    t^3 - t^2
//     result = h00*p0 + h10*m0 + h01*p1 + h11*m1
//
//   kLinear:  result = p0 + t_local * (p1 - p0)
//   kStepped: result = p0 for all t_local in [0..1)
//
// Auto-tangent formula (Catmull-Rom):
//   interior i:  slope = 0.5 * (v[i+1] - v[i-1]) / (t[i+1] - t[i-1])
//   left  end:   slope = (v[1]   - v[0]) / (t[1]   - t[0])
//   right end:   slope = (v[n-1] - v[n-2]) / (t[n-1] - t[n-2])
//   Degenerate (two identical times) -> 0.
//
// Drawing:
//   Background fill + border, then each curve drawn as N line segments
//   (DrawBatcher::quad used as thin horizontal strips approximating a poly-
//   line because DrawBatcher has no native polyline primitive). Keyframe knobs
//   are small square quads. Context menu is a vertical list of quads + text.
// =============================================================================
#include <cd/ui/widgets/CurveEditor.hpp>

#include <cd/ui/font/Font.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <utility>

namespace cd::ui::widgets
{

// ============================================================================
// Curve -- construction
// ============================================================================

Curve::Curve(std::string name)
    : name_(std::move(name))
{
}

Curve::Curve(std::string name, std::vector<KeyFrame> keyframes)
    : name_(std::move(name))
    , keyframes_(std::move(keyframes))
{
    sort_keyframes();
    recompute_auto_tangents();
}

// ============================================================================
// Curve -- keyframe mutation
// ============================================================================

void Curve::set_keyframes(std::vector<KeyFrame> kfs)
{
    keyframes_ = std::move(kfs);
    sort_keyframes();
    recompute_auto_tangents();
}

std::size_t Curve::add_keyframe(KeyFrame kf)
{
    keyframes_.push_back(kf);
    sort_keyframes();
    recompute_auto_tangents();
    // Find the new index (first keyframe with matching time + value).
    for (std::size_t i = 0U; i < keyframes_.size(); ++i)
    {
        // Use time equality with small epsilon to locate the just-inserted key.
        if (std::abs(keyframes_[i].time - kf.time) < 1e-6F &&
            std::abs(keyframes_[i].value - kf.value) < 1e-6F)
        {
            return i;
        }
    }
    return keyframes_.size() - 1U;  // Fallback (should not reach here)
}

void Curve::remove_keyframe(std::size_t index)
{
    if (index >= keyframes_.size())
    {
        return;
    }
    keyframes_.erase(keyframes_.begin() + static_cast<std::ptrdiff_t>(index));
    recompute_auto_tangents();
}

std::size_t Curve::set_keyframe(std::size_t index, KeyFrame kf)
{
    if (index >= keyframes_.size())
    {
        return index;
    }
    keyframes_[index] = kf;
    sort_keyframes();
    recompute_auto_tangents();
    // Locate the (possibly moved) keyframe.
    for (std::size_t i = 0U; i < keyframes_.size(); ++i)
    {
        if (std::abs(keyframes_[i].time - kf.time) < 1e-6F &&
            std::abs(keyframes_[i].value - kf.value) < 1e-6F)
        {
            return i;
        }
    }
    return index;
}

// ============================================================================
// Curve -- evaluation
// ============================================================================

[[nodiscard]] float Curve::evaluate(float t) const noexcept
{
    if (keyframes_.empty())
    {
        return 0.0F;
    }
    if (keyframes_.size() == 1U)
    {
        return keyframes_[0].value;
    }

    // Out-of-range clamp.
    if (t <= keyframes_.front().time)
    {
        return keyframes_.front().value;
    }
    if (t >= keyframes_.back().time)
    {
        return keyframes_.back().value;
    }

    const std::size_t left_idx = find_segment(t);
    if (left_idx == static_cast<std::size_t>(-1) || left_idx + 1U >= keyframes_.size())
    {
        return keyframes_.back().value;
    }

    const KeyFrame& L = keyframes_[left_idx];
    const KeyFrame& R = keyframes_[left_idx + 1U];
    const float h = R.time - L.time;

    if (h <= 0.0F)
    {
        return L.value;
    }

    const float t_local = (t - L.time) / h;

    // kStepped: hold left value.
    if (L.mode == TangentMode::kStepped)
    {
        return L.value;
    }

    // kLinear: straight segment.
    if (L.mode == TangentMode::kLinear)
    {
        return L.value + t_local * (R.value - L.value);
    }

    // kAuto / kFree: cubic Hermite.
    const float t2 = t_local * t_local;
    const float t3 = t2 * t_local;

    const float h00 =  2.0F * t3 - 3.0F * t2 + 1.0F;
    const float h10 =         t3 - 2.0F * t2 + t_local;
    const float h01 = -2.0F * t3 + 3.0F * t2;
    const float h11 =         t3 -        t2;

    const float m0 = L.out_tangent * h;
    const float m1 = R.in_tangent  * h;

    return h00 * L.value + h10 * m0 + h01 * R.value + h11 * m1;
}

// ============================================================================
// Curve -- private helpers
// ============================================================================

void Curve::sort_keyframes() noexcept
{
    std::ranges::sort(keyframes_,
                      [](const KeyFrame& a, const KeyFrame& b) { return a.time < b.time; });
}

void Curve::recompute_auto_tangents() noexcept
{
    const std::size_t n = keyframes_.size();
    if (n < 2U)
    {
        return;
    }

    for (std::size_t i = 0U; i < n; ++i)
    {
        if (keyframes_[i].mode != TangentMode::kAuto)
        {
            continue;
        }

        float slope = 0.0F;

        if (i == 0U)
        {
            // Left endpoint: one-sided forward difference.
            const float dt = keyframes_[1].time - keyframes_[0].time;
            if (dt > 1e-7F)
            {
                slope = (keyframes_[1].value - keyframes_[0].value) / dt;
            }
        }
        else if (i == n - 1U)
        {
            // Right endpoint: one-sided backward difference.
            const float dt = keyframes_[n - 1U].time - keyframes_[n - 2U].time;
            if (dt > 1e-7F)
            {
                slope = (keyframes_[n - 1U].value - keyframes_[n - 2U].value) / dt;
            }
        }
        else
        {
            // Interior: central difference.
            const float dt = keyframes_[i + 1U].time - keyframes_[i - 1U].time;
            if (dt > 1e-7F)
            {
                slope = 0.5F * (keyframes_[i + 1U].value - keyframes_[i - 1U].value) / dt;
            }
        }

        keyframes_[i].in_tangent  = slope;
        keyframes_[i].out_tangent = slope;
    }
}

std::size_t Curve::find_segment(float t) const noexcept
{
    // Binary search for the last keyframe with time <= t.
    std::size_t lo = 0U;
    std::size_t hi = keyframes_.size();

    while (lo + 1U < hi)
    {
        const std::size_t mid = lo + (hi - lo) / 2U;
        if (keyframes_[mid].time <= t)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }

    // lo is now the index of the left keyframe.
    if (lo + 1U >= keyframes_.size())
    {
        return static_cast<std::size_t>(-1);
    }
    return lo;
}

// ============================================================================
// CurveEditor -- construction helpers
// ============================================================================

namespace
{

/// Convert a cd::ui::widgets::Color to a cd::ui::renderer::Color.
[[nodiscard]] cd::ui::renderer::Color to_rc(Color c) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, c.a };
}

/// Draw a filled rectangle via batcher (convenience wrapper).
void fill_rect(cd::ui::renderer::DrawBatcher& b,
               float x, float y, float w, float h,
               Color c)
{
    if (w <= 0.0F || h <= 0.0F)
    {
        return;
    }
    b.quad(x, y, w, h, to_rc(c));
}

/// Draw a 1-pixel-wide border around a rect (four thin quads).
void draw_border(cd::ui::renderer::DrawBatcher& b,
                 float x, float y, float w, float h,
                 Color c)
{
    fill_rect(b, x,         y,         w,    1.0F, c);  // top
    fill_rect(b, x,         y + h - 1.0F, w, 1.0F, c);  // bottom
    fill_rect(b, x,         y,         1.0F, h,    c);  // left
    fill_rect(b, x + w - 1.0F, y,         1.0F, h,    c);  // right
}

/// Draw a text string using the font (null-font safe -- skips silently).
/// `baseline_y` is the Y coordinate of the text baseline.
void draw_text(cd::ui::renderer::DrawBatcher& b,
               cd::ui::font::Font*             font,
               const char*                     text,
               float                           x,
               float                           baseline_y,
               Color                           c,
               std::uint32_t                   atlas_slot = 0U)
{
    if (font == nullptr || text == nullptr)
    {
        return;
    }
    float pen = x;
    const cd::ui::renderer::Color rc = to_rc(c);
    std::uint32_t prev_cp = 0U;
    for (const char* p = text; *p != '\0'; ++p)
    {
        auto cp = static_cast<std::uint32_t>(static_cast<unsigned char>(*p));
        const auto g = font->glyph_uv(cp);
        if (!g.has_value())
        {
            prev_cp = cp;
            continue;
        }
        if (prev_cp != 0U)
        {
            pen += font->kerning(prev_cp, cp);
        }
        const float gx = pen + g->bearing_x;
        const float gy = baseline_y - g->bearing_y;
        const cd::ui::renderer::AtlasUv uv { g->u0, g->v0, g->u1, g->v1 };
        if (g->width > 0.0F && g->height > 0.0F)
        {
            b.glyph(gx, gy, g->width, g->height, atlas_slot, uv, rc);
        }
        pen    += g->advance;
        prev_cp = cp;
    }
}

}  // namespace

// ============================================================================
// CurveEditor -- curve management
// ============================================================================

std::size_t CurveEditor::add_curve(Curve c)
{
    const std::size_t idx = curves_.size();
    curves_.push_back(std::move(c));
    return idx;
}

void CurveEditor::remove_curve(std::size_t index)
{
    if (index >= curves_.size())
    {
        return;
    }
    curves_.erase(curves_.begin() + static_cast<std::ptrdiff_t>(index));
    if (active_curve_ >= curves_.size() && !curves_.empty())
    {
        active_curve_ = curves_.size() - 1U;
    }
    if (curves_.empty())
    {
        active_curve_ = 0U;
    }
}

void CurveEditor::set_active_curve(std::size_t index) noexcept
{
    if (index < curves_.size())
    {
        active_curve_ = index;
    }
}

void CurveEditor::set_view_range(float t_min, float t_max,
                                  float v_min, float v_max) noexcept
{
    if (t_max > t_min)
    {
        view_t_min_ = t_min;
        view_t_max_ = t_max;
    }
    if (v_max > v_min)
    {
        view_v_min_ = v_min;
        view_v_max_ = v_max;
    }
}

// ============================================================================
// CurveEditor -- coordinate mapping helpers
// ============================================================================

std::pair<float, float>
CurveEditor::curve_to_widget(float t, float v) const noexcept
{
    const float t_range = view_t_max_ - view_t_min_;
    const float v_range = view_v_max_ - view_v_min_;

    const float norm_t = (t_range > 1e-7F) ? (t - view_t_min_) / t_range : 0.0F;
    const float norm_v = (v_range > 1e-7F) ? (v - view_v_min_) / v_range : 0.0F;

    // X maps left-to-right; Y maps bottom-to-top (value increases upward).
    const float px = rect_.x + norm_t * rect_.w;
    const float py = rect_.y + rect_.h - norm_v * rect_.h;

    return { px, py };
}

std::pair<float, float>
CurveEditor::widget_to_curve(float px, float py) const noexcept
{
    const float norm_t = (rect_.w > 1e-7F) ? (px - rect_.x) / rect_.w : 0.0F;
    const float norm_v = (rect_.h > 1e-7F) ? (rect_.y + rect_.h - py) / rect_.h : 0.0F;

    const float t = view_t_min_ + norm_t * (view_t_max_ - view_t_min_);
    const float v = view_v_min_ + norm_v * (view_v_max_ - view_v_min_);

    return { t, v };
}

std::pair<std::size_t, std::size_t>
CurveEditor::hit_test_knob(float px, float py) const noexcept
{
    constexpr float r2 = kKnobRadius * kKnobRadius;

    for (std::size_t ci = 0U; ci < curves_.size(); ++ci)
    {
        const auto& kfs = curves_[ci].keyframes();
        for (std::size_t ki = 0U; ki < kfs.size(); ++ki)
        {
            const auto [kx, ky] = curve_to_widget(kfs[ki].time, kfs[ki].value);
            const float dx = px - kx;
            const float dy = py - ky;
            if (dx * dx + dy * dy <= r2)
            {
                return { ci, ki };
            }
        }
    }
    return { static_cast<std::size_t>(-1), static_cast<std::size_t>(-1) };
}

// ============================================================================
// CurveEditor -- tick
// ============================================================================

bool CurveEditor::tick(const InputState& input)
{
    if (!rect_.is_valid())
    {
        return false;
    }

    bool mutated = false;

    const float mx = input.pointer.mouse_x;
    const float my = input.pointer.mouse_y;
    const bool  in_rect    = rect_.contains(mx, my);
    const bool  pressed    = input.pointer.left_pressed;
    const bool  down       = input.pointer.left_down;
    const bool  released   = input.pointer.left_released;

    // ---- Context menu handling (right-click is simulated as pressed-outside
    //      the knob while the menu is open -- no right button in PointerState
    //      at Phase 5.17; we use focused flag as a proxy for right-click).
    if (ctx_menu_open_)
    {
        if (pressed)
        {
            // Check which tangent mode row was hit (4 rows of 22px each).
            const float row_h = 22.0F;
            constexpr std::size_t kNumModes = 4U;
            bool committed = false;

            for (std::size_t row = 0U; row < kNumModes; ++row)
            {
                const float ry = ctx_menu_y_ + static_cast<float>(row) * row_h;
                const Rect row_rect { ctx_menu_x_, ry, 120.0F, row_h };
                if (row_rect.contains(mx, my))
                {
                    // Apply the selected tangent mode.
                    if (ctx_menu_kf_ != static_cast<std::size_t>(-1) &&
                        active_curve_ < curves_.size())
                    {
                        Curve& ac = curves_[active_curve_];
                        if (ctx_menu_kf_ < ac.keyframe_count())
                        {
                            KeyFrame kf = ac.keyframes()[ctx_menu_kf_];
                            kf.mode = static_cast<TangentMode>(static_cast<std::uint8_t>(row));
                            ac.set_keyframe(ctx_menu_kf_, kf);
                            mutated = true;
                            if (on_curve_changed_)
                            {
                                on_curve_changed_(active_curve_);
                            }
                        }
                    }
                    committed = true;
                    break;
                }
            }
            // Close the menu regardless.
            (void)committed;
            ctx_menu_open_ = false;
        }
        prev_left_down_ = down;
        return mutated;
    }

    // ---- Press: start drag or pan ------------------------------------------
    if (pressed && in_rect)
    {
        const auto [hit_ci, hit_ki] = hit_test_knob(mx, my);

        if (hit_ci != static_cast<std::size_t>(-1))
        {
            // Hit a knob -- start drag.
            selected_kf_   = hit_ki;
            active_curve_  = hit_ci;
            dragging_kf_   = true;

            // Focused flag used as right-click proxy: open context menu.
            if (input.focused)
            {
                ctx_menu_open_ = true;
                ctx_menu_x_    = mx;
                ctx_menu_y_    = my;
                ctx_menu_kf_   = hit_ki;
                dragging_kf_   = false;
            }
        }
        else if (active_curve_ < curves_.size())
        {
            // Click on empty canvas: add keyframe to active curve.
            const auto [ct, cv] = widget_to_curve(mx, my);
            KeyFrame new_kf;
            new_kf.time  = ct;
            new_kf.value = cv;
            new_kf.mode  = TangentMode::kAuto;

            const std::size_t new_idx = curves_[active_curve_].add_keyframe(new_kf);
            selected_kf_ = new_idx;
            mutated      = true;

            if (on_kf_changed_)
            {
                on_kf_changed_(active_curve_, new_idx,
                               curves_[active_curve_].keyframes()[new_idx]);
            }
            if (on_curve_changed_)
            {
                on_curve_changed_(active_curve_);
            }
        }

        // Pan: Alt modifier is approximated by the pointer being outside all
        // knobs AND within the editor rect without hitting a key. We use a
        // separate panning_ flag toggled by middle-click, which we approximate
        // as no knob hit AND the `focused` InputState flag is true.
        if (!dragging_kf_ && !ctx_menu_open_ && in_rect && input.focused)
        {
            panning_        = true;
            pan_start_mx_   = mx;
            pan_start_my_   = my;
            pan_start_t_min_ = view_t_min_;
            pan_start_t_max_ = view_t_max_;
            pan_start_v_min_ = view_v_min_;
            pan_start_v_max_ = view_v_max_;
        }
    }

    // ---- Drag --------------------------------------------------------------
    if (down)
    {
        if (dragging_kf_ && active_curve_ < curves_.size())
        {
            Curve& ac = curves_[active_curve_];
            if (selected_kf_ < ac.keyframe_count())
            {
                const auto [ct, cv] = widget_to_curve(mx, my);
                KeyFrame kf = ac.keyframes()[selected_kf_];
                kf.time     = ct;
                kf.value    = cv;
                const std::size_t new_idx = ac.set_keyframe(selected_kf_, kf);
                selected_kf_ = new_idx;
                mutated      = true;

                if (on_kf_changed_)
                {
                    on_kf_changed_(active_curve_, new_idx,
                                   ac.keyframes()[new_idx]);
                }
            }
        }

        if (panning_)
        {
            // Translate view by the pixel delta mapped to curve space.
            const float t_range = view_t_max_ - view_t_min_;
            const float v_range = view_v_max_ - view_v_min_;

            const float dt = -(mx - pan_start_mx_) * t_range / rect_.w;
            const float dv =  (my - pan_start_my_) * v_range / rect_.h;

            view_t_min_ = pan_start_t_min_ + dt;
            view_t_max_ = pan_start_t_max_ + dt;
            view_v_min_ = pan_start_v_min_ + dv;
            view_v_max_ = pan_start_v_max_ + dv;
        }
    }

    // ---- Release -----------------------------------------------------------
    if (released)
    {
        dragging_kf_ = false;
        panning_     = false;
        if (on_curve_changed_ && mutated)
        {
            on_curve_changed_(active_curve_);
        }
    }

    prev_left_down_ = down;
    return mutated;
}

// ============================================================================
// CurveEditor -- draw
// ============================================================================

void CurveEditor::draw(cd::ui::renderer::DrawBatcher& batcher,
                        cd::ui::font::Font*             font,
                        const Theme&                    theme) const
{
    if (!rect_.is_valid())
    {
        return;
    }

    // ---- Background --------------------------------------------------------
    fill_rect(batcher, rect_.x, rect_.y, rect_.w, rect_.h, theme.background);
    draw_border(batcher, rect_.x, rect_.y, rect_.w, rect_.h, theme.surface_hover);

    // ---- Grid lines (time axis subdivisions) --------------------------------
    {
        constexpr int kGridLines = 5;
        for (int i = 0; i <= kGridLines; ++i)
        {
            const auto norm = static_cast<float>(i) / static_cast<float>(kGridLines);
            const float px   = rect_.x + norm * rect_.w;
            fill_rect(batcher, px, rect_.y, 1.0F, rect_.h,
                      Color { theme.surface_hover.r,
                              theme.surface_hover.g,
                              theme.surface_hover.b,
                              80U });
        }
        for (int i = 0; i <= kGridLines; ++i)
        {
            const auto norm = static_cast<float>(i) / static_cast<float>(kGridLines);
            const float py   = rect_.y + norm * rect_.h;
            fill_rect(batcher, rect_.x, py, rect_.w, 1.0F,
                      Color { theme.surface_hover.r,
                              theme.surface_hover.g,
                              theme.surface_hover.b,
                              80U });
        }
    }

    // ---- Curves (each drawn as kSampleCount-1 line segments) ---------------
    constexpr int kSampleCount = 64;

    for (std::size_t ci = 0U; ci < curves_.size(); ++ci)
    {
        const Curve& curve = curves_[ci];
        if (curve.keyframe_count() == 0U)
        {
            continue;
        }

        const Color  line_color = curve.color();
        const bool   is_active  = (ci == active_curve_);
        const float  line_width = is_active ? 2.0F : 1.0F;

        float prev_px = 0.0F;
        float prev_py = 0.0F;

        for (int s = 0; s < kSampleCount; ++s)
        {
            const auto norm_t = static_cast<float>(s) / static_cast<float>(kSampleCount - 1);
            const float ct     = view_t_min_ + norm_t * (view_t_max_ - view_t_min_);
            const float cv     = curve.evaluate(ct);

            const auto [px, py] = curve_to_widget(ct, cv);

            if (s > 0)
            {
                // Draw a thin quad from prev to current point (horizontal-dominant).
                const float dx = px - prev_px;
                const float dy = py - prev_py;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.5F)
                {
                    // Axis-aligned decomposition: emit a horizontal + vertical stub.
                    // This approximates a polyline using axis-aligned quads.
                    if (std::abs(dx) >= std::abs(dy))
                    {
                        const float x0 = (dx >= 0.0F) ? prev_px : px;
                        const float qw = std::abs(dx);
                        fill_rect(batcher, x0, std::min(prev_py, py),
                                  qw + 1.0F, line_width + std::abs(dy),
                                  line_color);
                    }
                    else
                    {
                        const float y0 = (dy >= 0.0F) ? prev_py : py;
                        const float qh = std::abs(dy);
                        fill_rect(batcher, std::min(prev_px, px), y0,
                                  line_width + std::abs(dx), qh + 1.0F,
                                  line_color);
                    }
                }
            }
            prev_px = px;
            prev_py = py;
        }

        // ---- Keyframe knobs ------------------------------------------------
        for (std::size_t ki = 0U; ki < curve.keyframe_count(); ++ki)
        {
            const KeyFrame& kf = curve.keyframes()[ki];
            const auto [kx, ky] = curve_to_widget(kf.time, kf.value);

            const bool selected = (ci == active_curve_ && ki == selected_kf_);
            const Color knob_color = selected ? theme.accent_hover
                                              : (is_active ? theme.accent : theme.text_dim);
            const float kr = kKnobRadius;

            fill_rect(batcher,
                      kx - kr, ky - kr,
                      kr * 2.0F, kr * 2.0F,
                      knob_color);
            draw_border(batcher,
                        kx - kr, ky - kr,
                        kr * 2.0F, kr * 2.0F,
                        theme.background);
        }
    }

    // ---- Context menu ------------------------------------------------------
    if (ctx_menu_open_)
    {
        constexpr float kMenuW = 120.0F;
        constexpr float kRowH  = 22.0F;
        constexpr std::size_t kNumModes = 4U;

        fill_rect(batcher, ctx_menu_x_, ctx_menu_y_,
                  kMenuW, kRowH * static_cast<float>(kNumModes),
                  theme.surface);
        draw_border(batcher, ctx_menu_x_, ctx_menu_y_,
                    kMenuW, kRowH * static_cast<float>(kNumModes),
                    theme.accent);

        static const char* const kLabels[kNumModes] = {
            "Auto", "Linear", "Stepped", "Free"
        };

        // Determine current mode of ctx_menu_kf_ for highlight.
        TangentMode current_mode = TangentMode::kAuto;
        if (ctx_menu_kf_ != static_cast<std::size_t>(-1) &&
            active_curve_ < curves_.size())
        {
            const Curve& ac = curves_[active_curve_];
            if (ctx_menu_kf_ < ac.keyframe_count())
            {
                current_mode = ac.keyframes()[ctx_menu_kf_].mode;
            }
        }

        for (std::size_t row = 0U; row < kNumModes; ++row)
        {
            const float ry  = ctx_menu_y_ + static_cast<float>(row) * kRowH;
            const bool  sel = (static_cast<std::size_t>(current_mode) == row);

            fill_rect(batcher, ctx_menu_x_, ry, kMenuW, kRowH,
                      sel ? theme.surface_press : theme.surface);
            draw_text(batcher, font, kLabels[row],
                      ctx_menu_x_ + 6.0F, ry + 5.0F,
                      sel ? theme.accent : theme.text);
        }
    }
}

}  // namespace cd::ui::widgets
