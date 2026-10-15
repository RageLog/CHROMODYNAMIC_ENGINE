// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/EditorAndHudWidgets.cpp
//
// Wave 2.4 concrete implementations for the 15 Editor & In-Game HUD widgets.
// =============================================================================
#include <cd/ui/widgets/EditorAndHudWidgets.hpp>

#include <cd/ui/font/Font.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <utility>

namespace cd::ui::widgets
{

namespace
{

[[nodiscard]] constexpr cd::ui::renderer::Color to_render_color(Color c) noexcept
{
    return { c.r, c.g, c.b, c.a };
}

void draw_text_line(cd::ui::renderer::DrawBatcher& batcher,
                    cd::ui::font::Font* font,
                    std::string_view text,
                    float x, float y,
                    Color color)
{
    if (font == nullptr || !font->is_loaded() || text.empty()) return;
    float pen_x = x;
    const auto r_col = to_render_color(color);
    for (char ch : text)
    {
        const auto cp = static_cast<std::uint8_t>(ch);
        auto g = font->glyph_uv(cp);
        if (!g.has_value()) continue;
        if (g->width > 0.0F && g->height > 0.0F)
        {
            cd::ui::renderer::AtlasUv uv { g->u0, g->v0, g->u1, g->v1 };
            batcher.glyph(pen_x + g->bearing_x, y, g->width, g->height, 0U, uv, r_col);
        }
        pen_x += g->advance;
    }
}

}  // namespace

// ============================================================================
// 1. NumericDragBox
// ============================================================================

NumericDragBox::NumericDragBox(float initial,
                               float min_v,
                               float max_v,
                               float speed,
                               ChangeCallback on_change)
    : min_value_ { std::min(min_v, max_v) }
    , max_value_ { std::max(min_v, max_v) }
    , drag_speed_ { speed > 0.0F ? speed : 0.01F }
    , default_value_ { initial }
    , on_change_ { std::move(on_change) }
{
    set_value(initial);
}

void NumericDragBox::set_value(float v) noexcept
{
    value_ = std::clamp(v, min_value_, max_value_);
}

void NumericDragBox::set_range(float min_v, float max_v) noexcept
{
    min_value_ = std::min(min_v, max_v);
    max_value_ = std::max(min_v, max_v);
    value_     = std::clamp(value_, min_value_, max_value_);
}

void NumericDragBox::reset_to_default()
{
    const float prev = value_;
    set_value(default_value_);
    if (value_ != prev && on_change_)
    {
        on_change_(value_);
    }
}

bool NumericDragBox::tick(const InputState& input)
{
    const bool inside = rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    state_.hovered = inside;
    state_.focused = input.focused;

    bool mutated = false;
    if (inside && input.pointer.left_pressed)
    {
        dragging_      = true;
        state_.pressed = true;
        last_mouse_x_  = input.pointer.mouse_x;
    }

    if (dragging_)
    {
        if (input.pointer.left_down)
        {
            const float dx = input.pointer.mouse_x - last_mouse_x_;
            last_mouse_x_  = input.pointer.mouse_x;
            if (std::abs(dx) > 0.0F)
            {
                const float prev = value_;
                set_value(value_ + dx * drag_speed_);
                if (value_ != prev)
                {
                    mutated = true;
                }
            }
        }
        else
        {
            dragging_      = false;
            state_.pressed = false;
        }
    }

    if (input.focused)
    {
        for (const auto& k : input.keys)
        {
            const float prev = value_;
            if (k.signal == KeySignal::kRight || k.signal == KeySignal::kUp)
            {
                set_value(value_ + drag_speed_);
            }
            else if (k.signal == KeySignal::kLeft || k.signal == KeySignal::kDown)
            {
                set_value(value_ - drag_speed_);
            }
            if (value_ != prev) mutated = true;
        }
    }

    if (mutated && on_change_)
    {
        on_change_(value_);
    }
    return mutated;
}

void NumericDragBox::draw(cd::ui::renderer::DrawBatcher& batcher,
                          cd::ui::font::Font* font,
                          const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    const Color bg = state_.pressed ? theme.surface_press
                   : (state_.hovered ? theme.surface_hover : theme.surface);
    batcher.rounded_rect(rect_.x, rect_.y, rect_.w, rect_.h, 3.0F, to_render_color(bg));

    char buf[32] {};
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value_));
    draw_text_line(batcher, font, buf, rect_.x + 6.0F, rect_.y + 4.0F, theme.text);
}

// ============================================================================
// 2. Vec3Control
// ============================================================================

Vec3Control::Vec3Control()
{
    badges_[0].set_label("X");
    badges_[1].set_label("Y");
    badges_[2].set_label("Z");
}

Vec3Control::Vec3Control(float x, float y, float z,
                         float default_val,
                         ChangeCallback on_change)
    : Vec3Control()
{
    on_change_ = std::move(on_change);
    set_default_values(default_val, default_val, default_val);
    set_values(x, y, z);
}

void Vec3Control::set_rect(Rect r) noexcept
{
    rect_ = r;
    const float gap     = 4.0F;
    const float total_w = std::max(0.0F, r.w - 2.0F * gap);
    const float unit_w  = total_w / 3.0F;
    const float badge_w = std::min(20.0F, unit_w * 0.35F);
    const float drag_w  = std::max(0.0F, unit_w - badge_w);

    for (std::size_t i = 0U; i < 3U; ++i)
    {
        const float ux = r.x + static_cast<float>(i) * (unit_w + gap);
        badges_[i].set_rect(Rect { ux, r.y, badge_w, r.h });
        axes_[i].set_rect(Rect { ux + badge_w, r.y, drag_w, r.h });
    }
}

void Vec3Control::set_values(float x, float y, float z) noexcept
{
    axes_[0].set_value(x);
    axes_[1].set_value(y);
    axes_[2].set_value(z);
}

std::array<float, 3> Vec3Control::values() const noexcept
{
    return { axes_[0].value(), axes_[1].value(), axes_[2].value() };
}

void Vec3Control::set_default_values(float dx, float dy, float dz) noexcept
{
    axes_[0].set_default_value(dx);
    axes_[1].set_default_value(dy);
    axes_[2].set_default_value(dz);
}

void Vec3Control::set_drag_speed(float s) noexcept
{
    for (auto& a : axes_) a.set_drag_speed(s);
}

Rect Vec3Control::badge_rect(std::size_t axis) const noexcept
{
    return axis < 3U ? badges_[axis].rect() : Rect {};
}

Rect Vec3Control::drag_rect(std::size_t axis) const noexcept
{
    return axis < 3U ? axes_[axis].rect() : Rect {};
}

bool Vec3Control::tick(const InputState& input)
{
    bool changed = false;
    for (std::size_t i = 0U; i < 3U; ++i)
    {
        if (badges_[i].tick(input))
        {
            const float prev = axes_[i].value();
            axes_[i].reset_to_default();
            if (axes_[i].value() != prev) changed = true;
        }
        if (axes_[i].tick(input))
        {
            changed = true;
        }
    }
    if (changed && on_change_)
    {
        on_change_(axes_[0].value(), axes_[1].value(), axes_[2].value());
    }
    return changed;
}

void Vec3Control::draw(cd::ui::renderer::DrawBatcher& batcher,
                       cd::ui::font::Font* font,
                       const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    constexpr std::array<Color, 3> kAxisColors {{
        { 220U,  80U,  80U, 255U },  // X = Red
        {  80U, 200U, 100U, 255U },  // Y = Green
        {  80U, 140U, 220U, 255U },  // Z = Blue
    }};
    for (std::size_t i = 0U; i < 3U; ++i)
    {
        const auto& br = badges_[i].rect();
        if (br.is_valid())
        {
            batcher.quad(br.x, br.y, br.w, br.h, to_render_color(kAxisColors[i]));
            draw_text_line(batcher, font, badges_[i].label(), br.x + 4.0F, br.y + 4.0F, theme.text);
        }
        axes_[i].draw(batcher, font, theme);
    }
}

// ============================================================================
// 3. Splitter
// ============================================================================

Splitter::Splitter(bool vertical_split, float initial_ratio, ChangeCallback on_change)
    : vertical_ { vertical_split }
    , on_change_ { std::move(on_change) }
{
    set_ratio(initial_ratio);
}

void Splitter::set_ratio(float r) noexcept
{
    ratio_ = std::clamp(r, min_ratio_, max_ratio_);
}

void Splitter::set_limits(float min_r, float max_r) noexcept
{
    min_ratio_ = std::clamp(std::min(min_r, max_r), 0.05F, 0.95F);
    max_ratio_ = std::clamp(std::max(min_r, max_r), 0.05F, 0.95F);
    ratio_     = std::clamp(ratio_, min_ratio_, max_ratio_);
}

Rect Splitter::first_rect() const noexcept
{
    if (!rect_.is_valid()) return {};
    if (vertical_)
    {
        const float avail = std::max(0.0F, rect_.w - bar_thickness_);
        return { rect_.x, rect_.y, avail * ratio_, rect_.h };
    }
    const float avail = std::max(0.0F, rect_.h - bar_thickness_);
    return { rect_.x, rect_.y, rect_.w, avail * ratio_ };
}

Rect Splitter::bar_rect() const noexcept
{
    if (!rect_.is_valid()) return {};
    const auto f = first_rect();
    if (vertical_)
    {
        return { f.x + f.w, rect_.y, bar_thickness_, rect_.h };
    }
    return { rect_.x, f.y + f.h, rect_.w, bar_thickness_ };
}

Rect Splitter::second_rect() const noexcept
{
    if (!rect_.is_valid()) return {};
    const auto b = bar_rect();
    if (vertical_)
    {
        const float x2 = b.x + b.w;
        return { x2, rect_.y, std::max(0.0F, (rect_.x + rect_.w) - x2), rect_.h };
    }
    const float y2 = b.y + b.h;
    return { rect_.x, y2, rect_.w, std::max(0.0F, (rect_.y + rect_.h) - y2) };
}

bool Splitter::tick(const InputState& input)
{
    const auto br = bar_rect();
    hovered_ = br.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    if (hovered_ && input.pointer.left_pressed)
    {
        dragging_ = true;
    }
    bool changed = false;
    if (dragging_)
    {
        if (input.pointer.left_down)
        {
            const float prev = ratio_;
            if (vertical_ && rect_.w > bar_thickness_)
            {
                const float rel = (input.pointer.mouse_x - rect_.x - bar_thickness_ * 0.5F)
                                / (rect_.w - bar_thickness_);
                set_ratio(rel);
            }
            else if (!vertical_ && rect_.h > bar_thickness_)
            {
                const float rel = (input.pointer.mouse_y - rect_.y - bar_thickness_ * 0.5F)
                                / (rect_.h - bar_thickness_);
                set_ratio(rel);
            }
            if (ratio_ != prev)
            {
                changed = true;
                if (on_change_) on_change_(ratio_);
            }
        }
        else
        {
            dragging_ = false;
        }
    }
    return changed;
}

void Splitter::draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const
{
    const auto br = bar_rect();
    if (!br.is_valid()) return;
    const Color c = (dragging_ || hovered_) ? theme.accent : theme.divider;
    batcher.quad(br.x, br.y, br.w, br.h, to_render_color(c));
}

// ============================================================================
// 4. ScrollArea
// ============================================================================

void ScrollArea::set_content_height(float h) noexcept
{
    content_height_ = std::max(0.0F, h);
    set_scroll_y(scroll_y_);
}

float ScrollArea::max_scroll_y() const noexcept
{
    return std::max(0.0F, content_height_ - rect_.h);
}

void ScrollArea::set_scroll_y(float sy) noexcept
{
    scroll_y_ = std::clamp(sy, 0.0F, max_scroll_y());
}

Rect ScrollArea::scrollbar_track_rect() const noexcept
{
    if (!rect_.is_valid()) return {};
    return { rect_.x + rect_.w - scrollbar_w_, rect_.y, scrollbar_w_, rect_.h };
}

Rect ScrollArea::scrollbar_thumb_rect() const noexcept
{
    const auto tr = scrollbar_track_rect();
    if (!tr.is_valid() || content_height_ <= rect_.h || content_height_ <= 0.0F)
    {
        return tr;
    }
    const float visible_frac = std::clamp(rect_.h / content_height_, 0.1F, 1.0F);
    const float thumb_h      = tr.h * visible_frac;
    const float travel       = std::max(0.0F, tr.h - thumb_h);
    const float scroll_frac  = max_scroll_y() > 0.0F ? (scroll_y_ / max_scroll_y()) : 0.0F;
    return { tr.x, tr.y + travel * scroll_frac, tr.w, thumb_h };
}

bool ScrollArea::tick(const InputState& input)
{
    const auto thumb = scrollbar_thumb_rect();
    const bool in_thumb = thumb.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    if (in_thumb && input.pointer.left_pressed)
    {
        dragging_thumb_ = true;
        drag_anchor_y_  = input.pointer.mouse_y;
        drag_start_sy_  = scroll_y_;
    }
    bool changed = false;
    if (dragging_thumb_)
    {
        if (input.pointer.left_down)
        {
            const auto tr = scrollbar_track_rect();
            const float travel = std::max(1.0F, tr.h - thumb.h);
            const float dy     = input.pointer.mouse_y - drag_anchor_y_;
            const float prev   = scroll_y_;
            set_scroll_y(drag_start_sy_ + (dy / travel) * max_scroll_y());
            if (scroll_y_ != prev) changed = true;
        }
        else
        {
            dragging_thumb_ = false;
        }
    }
    return changed;
}

void ScrollArea::begin_draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    batcher.quad(rect_.x, rect_.y, rect_.w, rect_.h, to_render_color(theme.background));
    cd::ui::renderer::ScissorRect sr {};
    sr.x      = static_cast<std::int32_t>(rect_.x);
    sr.y      = static_cast<std::int32_t>(rect_.y);
    sr.width  = static_cast<std::uint32_t>(rect_.w);
    sr.height = static_cast<std::uint32_t>(rect_.h);
    (void)batcher.push_scissor(sr);
}

void ScrollArea::end_draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    if (content_height_ > rect_.h)
    {
        const auto tr = scrollbar_track_rect();
        const auto th = scrollbar_thumb_rect();
        batcher.quad(tr.x, tr.y, tr.w, tr.h, to_render_color(theme.surface));
        batcher.rounded_rect(th.x, th.y, th.w, th.h, 3.0F,
                             to_render_color(dragging_thumb_ ? theme.accent : theme.surface_hover));
    }
    batcher.pop_scissor();
}

// ============================================================================
// 5. MenuBar & 6. ContextMenu
// ============================================================================

void ContextMenu::open_at(float x, float y, float width, float item_height) noexcept
{
    x_             = x;
    y_             = y;
    width_         = width > 20.0F ? width : 160.0F;
    item_height_   = item_height > 8.0F ? item_height : 24.0F;
    open_          = true;
    hovered_index_ = static_cast<std::size_t>(-1);
}

Rect ContextMenu::popup_rect() const noexcept
{
    if (!open_ || items_.empty()) return {};
    return { x_, y_, width_, static_cast<float>(items_.size()) * item_height_ };
}

bool ContextMenu::tick(const InputState& input)
{
    if (!open_) return false;
    const auto pr = popup_rect();
    hovered_index_ = static_cast<std::size_t>(-1);
    if (pr.contains(input.pointer.mouse_x, input.pointer.mouse_y))
    {
        const auto idx = static_cast<std::size_t>((input.pointer.mouse_y - y_) / item_height_);
        if (idx < items_.size()) hovered_index_ = idx;
    }

    if (input.pointer.left_released)
    {
        if (hovered_index_ < items_.size() && items_[hovered_index_].enabled)
        {
            auto cb = items_[hovered_index_].on_select;
            close();
            if (cb) cb();
            return true;
        }
        if (!pr.contains(input.pointer.mouse_x, input.pointer.mouse_y))
        {
            close();
        }
    }
    return false;
}

void ContextMenu::draw(cd::ui::renderer::DrawBatcher& batcher,
                       cd::ui::font::Font* font,
                       const Theme& theme) const
{
    if (!open_ || items_.empty()) return;
    const auto pr = popup_rect();
    batcher.drop_shadow(pr.x, pr.y, pr.w, pr.h, 8.0F);
    batcher.quad(pr.x, pr.y, pr.w, pr.h, to_render_color(theme.surface));
    for (std::size_t i = 0U; i < items_.size(); ++i)
    {
        const float iy = y_ + static_cast<float>(i) * item_height_;
        if (i == hovered_index_)
        {
            batcher.quad(x_, iy, width_, item_height_, to_render_color(theme.accent));
        }
        draw_text_line(batcher, font, items_[i].label, x_ + 8.0F, iy + 4.0F,
                       items_[i].enabled ? theme.text : theme.text_dim);
    }
}

void MenuBar::add_menu(std::string title, std::vector<MenuAction> items)
{
    menus_.push_back(Menu { std::move(title), std::move(items) });
}

bool MenuBar::tick(const InputState& input)
{
    if (popup_.is_open())
    {
        if (popup_.tick(input))
        {
            active_menu_ = static_cast<std::size_t>(-1);
            return true;
        }
        if (!popup_.is_open())
        {
            active_menu_ = static_cast<std::size_t>(-1);
        }
    }

    if (rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y) && input.pointer.left_pressed)
    {
        const auto idx = static_cast<std::size_t>((input.pointer.mouse_x - rect_.x) / menu_width_);
        if (idx < menus_.size())
        {
            active_menu_ = idx;
            popup_.set_items(menus_[idx].items);
            popup_.open_at(rect_.x + static_cast<float>(idx) * menu_width_,
                           rect_.y + rect_.h);
            return true;
        }
    }
    return false;
}

void MenuBar::draw(cd::ui::renderer::DrawBatcher& batcher,
                   cd::ui::font::Font* font,
                   const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    batcher.quad(rect_.x, rect_.y, rect_.w, rect_.h, to_render_color(theme.surface_subtle));
    for (std::size_t i = 0U; i < menus_.size(); ++i)
    {
        const float mx = rect_.x + static_cast<float>(i) * menu_width_;
        if (i == active_menu_)
        {
            batcher.quad(mx, rect_.y, menu_width_, rect_.h, to_render_color(theme.surface_hover));
        }
        draw_text_line(batcher, font, menus_[i].title, mx + 8.0F, rect_.y + 4.0F, theme.text);
    }
    popup_.draw(batcher, font, theme);
}

// ============================================================================
// 7. ToastQueue & 8. Tooltip
// ============================================================================

void ToastQueue::push(std::string message, ToastSeverity severity, float duration_s)
{
    const float d = duration_s > 0.1F ? duration_s : 3.0F;
    items_.push_back(ToastItem { std::move(message), severity, d, d });
}

void ToastQueue::tick(float dt_s) noexcept
{
    if (dt_s <= 0.0F) return;
    for (auto& item : items_)
    {
        item.remaining_s -= dt_s;
    }
    std::erase_if(items_, [](const ToastItem& t) { return t.remaining_s <= 0.0F; });
}

void ToastQueue::draw(cd::ui::renderer::DrawBatcher& batcher,
                      cd::ui::font::Font* font,
                      const Theme& theme,
                      Rect viewport) const
{
    if (items_.empty() || !viewport.is_valid()) return;
    const float toast_w = 260.0F;
    const float toast_h = 32.0F;
    const float gap     = 8.0F;
    const float x       = viewport.x + viewport.w - toast_w - 16.0F;

    for (std::size_t i = 0U; i < items_.size(); ++i)
    {
        const float y = viewport.y + viewport.h - static_cast<float>(i + 1U) * (toast_h + gap) - 12.0F;
        Color accent = theme.accent;
        switch (items_[i].severity)
        {
            case ToastSeverity::kInfo:    accent = theme.accent;         break;
            case ToastSeverity::kSuccess: accent = theme.accent_success; break;
            case ToastSeverity::kWarning: accent = theme.accent_warning; break;
            case ToastSeverity::kError:   accent = theme.accent_error;   break;
        }
        batcher.drop_shadow(x, y, toast_w, toast_h, 6.0F);
        batcher.rounded_rect(x, y, toast_w, toast_h, 4.0F, to_render_color(theme.surface));
        batcher.quad(x, y, 4.0F, toast_h, to_render_color(accent));
        draw_text_line(batcher, font, items_[i].message, x + 12.0F, y + 8.0F, theme.text);
    }
}

void Tooltip::update(bool target_hovered, float mouse_x, float mouse_y,
                     std::string_view text, float dt_s)
{
    if (!target_hovered || text.empty())
    {
        timer_s_ = 0.0F;
        visible_ = false;
        return;
    }
    text_.assign(text);
    x_ = mouse_x + 12.0F;
    y_ = mouse_y + 16.0F;
    timer_s_ += std::max(0.0F, dt_s);
    visible_ = (timer_s_ >= delay_s_);
}

void Tooltip::draw(cd::ui::renderer::DrawBatcher& batcher,
                   cd::ui::font::Font* font,
                   const Theme& theme) const
{
    if (!visible_ || text_.empty()) return;
    const float w = static_cast<float>(text_.size()) * 8.0F + 16.0F;
    const float h = 24.0F;
    batcher.drop_shadow(x_, y_, w, h, 4.0F);
    batcher.rounded_rect(x_, y_, w, h, 3.0F, to_render_color(theme.surface_subtle));
    draw_text_line(batcher, font, text_, x_ + 8.0F, y_ + 4.0F, theme.text);
}

// ============================================================================
// 9. NodeGraphCanvas
// ============================================================================

std::uint32_t NodeGraphCanvas::add_node(GraphNode node)
{
    if (node.id == 0U)
    {
        node.id = static_cast<std::uint32_t>(nodes_.size() + 1U);
    }
    const std::uint32_t id = node.id;
    nodes_.push_back(std::move(node));
    return id;
}

bool NodeGraphCanvas::add_wire(GraphWire wire)
{
    if (wire.from_node == wire.to_node) return false;
    wires_.push_back(wire);
    return true;
}

bool NodeGraphCanvas::remove_node(std::uint32_t id)
{
    const auto before = nodes_.size();
    std::erase_if(nodes_, [id](const GraphNode& n) { return n.id == id; });
    std::erase_if(wires_, [id](const GraphWire& w) { return w.from_node == id || w.to_node == id; });
    if (selected_node_ == id) selected_node_ = 0xFFFFFFFFu;
    return nodes_.size() != before;
}

void NodeGraphCanvas::set_zoom(float z) noexcept
{
    zoom_ = std::clamp(z, 0.25F, 4.0F);
}

bool NodeGraphCanvas::tick(const InputState& input)
{
    if (!rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y) && !dragging_node_)
    {
        return false;
    }

    const float canvas_mx = (input.pointer.mouse_x - rect_.x - pan_x_) / zoom_;
    const float canvas_my = (input.pointer.mouse_y - rect_.y - pan_y_) / zoom_;

    if (input.pointer.left_pressed)
    {
        selected_node_ = 0xFFFFFFFFu;
        for (auto it = nodes_.rbegin(); it != nodes_.rend(); ++it)
        {
            const Rect nr { it->x, it->y, it->w, it->h };
            if (nr.contains(canvas_mx, canvas_my))
            {
                selected_node_ = it->id;
                dragging_node_ = true;
                drag_offset_x_ = canvas_mx - it->x;
                drag_offset_y_ = canvas_my - it->y;
                return true;
            }
        }
    }

    if (dragging_node_)
    {
        if (input.pointer.left_down)
        {
            for (auto& n : nodes_)
            {
                if (n.id == selected_node_)
                {
                    n.x = canvas_mx - drag_offset_x_;
                    n.y = canvas_my - drag_offset_y_;
                    return true;
                }
            }
        }
        else
        {
            dragging_node_ = false;
        }
    }
    return false;
}

void NodeGraphCanvas::draw(cd::ui::renderer::DrawBatcher& batcher,
                           cd::ui::font::Font* font,
                           const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    batcher.quad(rect_.x, rect_.y, rect_.w, rect_.h, to_render_color(theme.background));

    cd::ui::renderer::ScissorRect sr {};
    sr.x      = static_cast<std::int32_t>(rect_.x);
    sr.y      = static_cast<std::int32_t>(rect_.y);
    sr.width  = static_cast<std::uint32_t>(rect_.w);
    sr.height = static_cast<std::uint32_t>(rect_.h);
    (void)batcher.push_scissor(sr);

    // Draw wires as segmented bezier curves
    for (const auto& w : wires_)
    {
        const GraphNode* fn = nullptr;
        const GraphNode* tn = nullptr;
        for (const auto& n : nodes_)
        {
            if (n.id == w.from_node) fn = &n;
            if (n.id == w.to_node)   tn = &n;
        }
        if (fn == nullptr || tn == nullptr) continue;

        const float x0 = rect_.x + pan_x_ + (fn->x + fn->w) * zoom_;
        const float y0 = rect_.y + pan_y_ + (fn->y + 32.0F + static_cast<float>(w.from_pin) * 16.0F) * zoom_;
        const float x1 = rect_.x + pan_x_ + tn->x * zoom_;
        const float y1 = rect_.y + pan_y_ + (tn->y + 32.0F + static_cast<float>(w.to_pin) * 16.0F) * zoom_;

        constexpr int kSteps = 8;
        for (int s = 0; s < kSteps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(kSteps);
            const float px = std::lerp(x0, x1, t);
            const float py = std::lerp(y0, y1, t);
            batcher.quad(px - 2.0F, py - 2.0F, 4.0F, 4.0F, to_render_color(theme.accent));
        }
    }

    // Draw nodes
    for (const auto& n : nodes_)
    {
        const float nx = rect_.x + pan_x_ + n.x * zoom_;
        const float ny = rect_.y + pan_y_ + n.y * zoom_;
        const float nw = n.w * zoom_;
        const float nh = n.h * zoom_;
        const float hh = std::min(24.0F * zoom_, nh);

        batcher.drop_shadow(nx, ny, nw, nh, 6.0F);
        batcher.rounded_rect(nx, ny, nw, nh, 4.0F, to_render_color(theme.surface));
        batcher.quad(nx, ny, nw, hh, to_render_color(n.header_tint));
        if (n.id == selected_node_)
        {
            batcher.quad(nx, ny, nw, 2.0F, to_render_color(theme.focus_ring));
        }
        draw_text_line(batcher, font, n.title, nx + 6.0F, ny + 4.0F, theme.text);
    }

    batcher.pop_scissor();
}

// ============================================================================
// 10. FlameChart & 11. Sparkline
// ============================================================================

void FlameChart::draw(cd::ui::renderer::DrawBatcher& batcher,
                      cd::ui::font::Font* font,
                      const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    batcher.quad(rect_.x, rect_.y, rect_.w, rect_.h, to_render_color(theme.background));
    const float lane_h = 18.0F;
    for (const auto& bar : bars_)
    {
        const float x0 = rect_.x + (bar.start_ms / budget_ms_) * rect_.w;
        const float bw = std::max(2.0F, (bar.dur_ms / budget_ms_) * rect_.w);
        const float y0 = rect_.y + static_cast<float>(bar.depth) * (lane_h + 2.0F);
        if (y0 + lane_h > rect_.y + rect_.h) continue;
        batcher.quad(x0, y0, bw, lane_h, to_render_color(bar.color));
        draw_text_line(batcher, font, bar.name, x0 + 4.0F, y0 + 2.0F, theme.text);
    }
}

Sparkline::Sparkline(std::size_t capacity)
    : capacity_ { capacity > 1U ? capacity : 2U }
{
}

void Sparkline::push_sample(float v)
{
    if (samples_.size() >= capacity_)
    {
        samples_.erase(samples_.begin());
    }
    samples_.push_back(v);
}

float Sparkline::average() const noexcept
{
    if (samples_.empty()) return 0.0F;
    const float sum = std::accumulate(samples_.begin(), samples_.end(), 0.0F);
    return sum / static_cast<float>(samples_.size());
}

void Sparkline::draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    batcher.quad(rect_.x, rect_.y, rect_.w, rect_.h, to_render_color(theme.surface));
    if (samples_.empty()) return;

    float max_v = 0.001F;
    for (float s : samples_) max_v = std::max(max_v, s);

    const float bar_w = rect_.w / static_cast<float>(capacity_);
    for (std::size_t i = 0U; i < samples_.size(); ++i)
    {
        const float frac = std::clamp(samples_[i] / max_v, 0.05F, 1.0F);
        const float bh   = rect_.h * frac;
        const float bx   = rect_.x + static_cast<float>(i) * bar_w;
        const float by   = rect_.y + (rect_.h - bh);
        batcher.quad(bx, by, std::max(1.0F, bar_w - 1.0F), bh, to_render_color(theme.accent));
    }
}

// ============================================================================
// 12. GaugeBar, 13. Crosshair, 14. Minimap, 15. InventoryGrid
// ============================================================================

void GaugeBar::set_ratio(float r) noexcept
{
    ratio_ = std::clamp(r, 0.0F, 1.0F);
}

void GaugeBar::draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    batcher.rounded_rect(rect_.x, rect_.y, rect_.w, rect_.h, 3.0F, to_render_color(theme.surface));
    const float fw = rect_.w * ratio_;
    if (fw > 0.0F)
    {
        batcher.rounded_rect(rect_.x, rect_.y, fw, rect_.h, 3.0F, to_render_color(fill_color_));
    }
}

void Crosshair::set_spread(float gap_px, float arm_len_px, float thickness_px) noexcept
{
    gap_       = std::max(0.0F, gap_px);
    arm_len_   = std::max(1.0F, arm_len_px);
    thickness_ = std::max(1.0F, thickness_px);
}

void Crosshair::draw(cd::ui::renderer::DrawBatcher& batcher, Color color) const
{
    const auto rc = to_render_color(color);
    const float ht = thickness_ * 0.5F;
    // Left, Right, Top, Bottom arms
    batcher.quad(cx_ - gap_ - arm_len_, cy_ - ht, arm_len_, thickness_, rc);
    batcher.quad(cx_ + gap_,            cy_ - ht, arm_len_, thickness_, rc);
    batcher.quad(cx_ - ht, cy_ - gap_ - arm_len_, thickness_, arm_len_, rc);
    batcher.quad(cx_ - ht, cy_ + gap_,            thickness_, arm_len_, rc);
}

void Minimap::set_player_pose(float world_x, float world_z, float range_m) noexcept
{
    player_x_ = world_x;
    player_z_ = world_z;
    range_m_  = range_m > 1.0F ? range_m : 50.0F;
}

void Minimap::draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    batcher.rounded_rect(rect_.x, rect_.y, rect_.w, rect_.h, 6.0F, to_render_color(theme.surface));

    const float cx = rect_.x + rect_.w * 0.5F;
    const float cy = rect_.y + rect_.h * 0.5F;
    // Player center marker
    batcher.quad(cx - 3.0F, cy - 3.0F, 6.0F, 6.0F, to_render_color(theme.accent_success));

    for (const auto& b : blips_)
    {
        const float nx = std::clamp((b.world_x - player_x_) / range_m_, -1.0F, 1.0F);
        const float nz = std::clamp((b.world_z - player_z_) / range_m_, -1.0F, 1.0F);
        const float px = cx + nx * (rect_.w * 0.45F);
        const float py = cy + nz * (rect_.h * 0.45F);
        const float hs = b.size_px * 0.5F;
        batcher.quad(px - hs, py - hs, b.size_px, b.size_px, to_render_color(b.color));
    }
}

InventoryGrid::InventoryGrid(std::uint32_t cols, std::uint32_t rows)
    : cols_ { cols > 0U ? cols : 1U }
    , rows_ { rows > 0U ? rows : 1U }
    , items_(static_cast<std::size_t>(cols_) * rows_, 0U)
{
}

void InventoryGrid::set_slot_item(std::size_t slot_idx, std::uint32_t item_id)
{
    if (slot_idx < items_.size()) items_[slot_idx] = item_id;
}

std::uint32_t InventoryGrid::slot_item(std::size_t slot_idx) const noexcept
{
    return slot_idx < items_.size() ? items_[slot_idx] : 0U;
}

Rect InventoryGrid::slot_rect(std::size_t slot_idx) const noexcept
{
    if (!rect_.is_valid() || slot_idx >= items_.size()) return {};
    const auto col = static_cast<std::uint32_t>(slot_idx % cols_);
    const auto row = static_cast<std::uint32_t>(slot_idx / cols_);
    const float cw = (rect_.w - gap_ * static_cast<float>(cols_ - 1U)) / static_cast<float>(cols_);
    const float ch = (rect_.h - gap_ * static_cast<float>(rows_ - 1U)) / static_cast<float>(rows_);
    return {
        rect_.x + static_cast<float>(col) * (cw + gap_),
        rect_.y + static_cast<float>(row) * (ch + gap_),
        cw,
        ch
    };
}

bool InventoryGrid::tick(const InputState& input)
{
    if (!rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y)) return false;
    if (input.pointer.left_pressed)
    {
        for (std::size_t i = 0U; i < items_.size(); ++i)
        {
            if (slot_rect(i).contains(input.pointer.mouse_x, input.pointer.mouse_y))
            {
                selected_slot_ = i;
                return true;
            }
        }
    }
    return false;
}

void InventoryGrid::draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const
{
    if (!rect_.is_valid()) return;
    for (std::size_t i = 0U; i < items_.size(); ++i)
    {
        const auto sr = slot_rect(i);
        const Color bg = (i == selected_slot_) ? theme.accent : theme.surface;
        batcher.rounded_rect(sr.x, sr.y, sr.w, sr.h, 3.0F, to_render_color(bg));
        if (items_[i] != 0U)
        {
            batcher.quad(sr.x + 4.0F, sr.y + 4.0F, sr.w - 8.0F, sr.h - 8.0F,
                         to_render_color(theme.accent_warning));
        }
    }
}

}  // namespace cd::ui::widgets
