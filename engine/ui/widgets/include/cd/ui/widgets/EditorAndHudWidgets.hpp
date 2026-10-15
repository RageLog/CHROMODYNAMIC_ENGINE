// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/EditorAndHudWidgets.hpp
//
// Wave 2.4 of the 10-Phase Master Plan (ADR-20260530-ui-widget-library).
// Complete Editor & In-Game HUD widget catalog built on top of:
//   * cd::ui::widgets::Rect / Color / Theme / InputState (Widgets.hpp)
//   * cd::ui::renderer::DrawBatcher
//   * cd::ui::font::Font
//
// Widgets provided:
//   1. NumericDragBox  -- horizontal drag-to-scrub float box with reset
//   2. Vec3Control     -- color-coded X(Red)/Y(Green)/Z(Blue) 3-axis control
//   3. Splitter        -- resizable horizontal/vertical split pane divider
//   4. ScrollArea      -- scissor-clipped vertical scroll container + thumb
//   5. MenuBar         -- horizontal menu bar with expandable dropdown items
//   6. ContextMenu     -- popup action list at cursor position
//   7. ToastQueue      -- transient notification banners with auto-dismiss
//   8. Tooltip         -- hover-delayed floating label box
//   9. NodeGraphCanvas -- zoomable/pannable node & wire graph editor canvas
//  10. FlameChart      -- horizontal pass timing profiler bar chart
//  11. Sparkline       -- rolling scalar history graph (frame-time / FPS)
//  12. GaugeBar        -- in-game HUD progress / health / stamina bar
//  13. Crosshair       -- dynamic spread HUD reticle
//  14. Minimap         -- 2D radar/minimap with player & blip markers
//  15. InventoryGrid   -- N×M slot grid with selection & drag-drop index
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::ui::widgets
{

// ---- 1. NumericDragBox -----------------------------------------------------

class NumericDragBox
{
public:
    using ChangeCallback = std::function<void(float)>;

    NumericDragBox() = default;
    explicit NumericDragBox(float initial,
                            float min_v = -1e6F,
                            float max_v =  1e6F,
                            float speed =  0.1F,
                            ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_value(float v) noexcept;
    [[nodiscard]] float value() const noexcept { return value_; }

    void set_range(float min_v, float max_v) noexcept;
    [[nodiscard]] float min_value() const noexcept { return min_value_; }
    [[nodiscard]] float max_value() const noexcept { return max_value_; }

    void set_drag_speed(float s) noexcept { drag_speed_ = s > 0.0F ? s : 0.01F; }
    [[nodiscard]] float drag_speed() const noexcept { return drag_speed_; }

    void set_default_value(float d) noexcept { default_value_ = d; }
    [[nodiscard]] float default_value() const noexcept { return default_value_; }

    void reset_to_default();
    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    [[nodiscard]] bool is_dragging() const noexcept { return dragging_; }
    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    bool tick(const InputState& input);
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect           rect_          {};
    float          value_         { 0.0F };
    float          min_value_     { -1e6F };
    float          max_value_     {  1e6F };
    float          drag_speed_    {  0.1F };
    float          default_value_ {  0.0F };
    float          last_mouse_x_  {  0.0F };
    bool           dragging_      { false };
    ButtonState    state_         {};
    ChangeCallback on_change_     {};
};

// ---- 2. Vec3Control --------------------------------------------------------

/// Godot/Unreal-style X/Y/Z inspector control with color-coded reset badges
/// (X = Red, Y = Green, Z = Blue). Clicking a badge resets that axis to
/// `default_xyz[axis]`; dragging a numeric box scrubs the axis value.
class Vec3Control
{
public:
    using ChangeCallback = std::function<void(float, float, float)>;

    Vec3Control();
    explicit Vec3Control(float x, float y, float z,
                         float default_val = 0.0F,
                         ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept;
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_values(float x, float y, float z) noexcept;
    [[nodiscard]] std::array<float, 3> values() const noexcept;
    [[nodiscard]] float x() const noexcept { return axes_[0].value(); }
    [[nodiscard]] float y() const noexcept { return axes_[1].value(); }
    [[nodiscard]] float z() const noexcept { return axes_[2].value(); }

    void set_default_values(float dx, float dy, float dz) noexcept;
    void set_drag_speed(float s) noexcept;
    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    [[nodiscard]] Rect badge_rect(std::size_t axis) const noexcept;
    [[nodiscard]] Rect drag_rect(std::size_t axis) const noexcept;

    bool tick(const InputState& input);
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect                        rect_       {};
    std::array<NumericDragBox, 3> axes_     {};
    std::array<Button, 3>       badges_     {};
    ChangeCallback              on_change_  {};
};

// ---- 3. Splitter -----------------------------------------------------------

#ifndef CD_UI_WIDGETS_SPLITTER_DEFINED
#define CD_UI_WIDGETS_SPLITTER_DEFINED
class Splitter
{
public:
    static constexpr float kHandleThickness { 6.0F };  ///< visible bar
    static constexpr float kHitTolerance    { 8.0F };  ///< click target padding

    using ChangeCallback = std::function<void(float)>;

    Splitter() = default;
    explicit Splitter(bool vertical_split,
                      float initial_ratio = 0.5F,
                      ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_ratio(float r) noexcept;
    [[nodiscard]] float ratio() const noexcept { return ratio_; }

    void set_limits(float min_r, float max_r) noexcept;
    void set_bar_thickness(float t) noexcept { bar_thickness_ = t > 1.0F ? t : 1.0F; }
    [[nodiscard]] float bar_thickness() const noexcept { return bar_thickness_; }

    [[nodiscard]] bool is_vertical() const noexcept { return vertical_; }
    [[nodiscard]] bool is_dragging() const noexcept { return dragging_; }

    [[nodiscard]] Rect first_rect() const noexcept;
    [[nodiscard]] Rect second_rect() const noexcept;
    [[nodiscard]] Rect bar_rect() const noexcept;

    bool tick(const InputState& input);
    void draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const;

private:
    Rect           rect_          {};
    bool           vertical_      { true };  ///< true = left | right, false = top / bottom
    float          ratio_         { 0.5F };
    float          min_ratio_     { 0.1F };
    float          max_ratio_     { 0.9F };
    float          bar_thickness_ { 6.0F };
    bool           dragging_      { false };
    bool           hovered_       { false };
    ChangeCallback on_change_     {};
};
#endif // CD_UI_WIDGETS_SPLITTER_DEFINED

// ---- 4. ScrollArea ---------------------------------------------------------

class ScrollArea
{
public:
    ScrollArea() = default;

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_content_height(float h) noexcept;
    [[nodiscard]] float content_height() const noexcept { return content_height_; }

    void set_scroll_y(float sy) noexcept;
    [[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }
    [[nodiscard]] float max_scroll_y() const noexcept;

    void scroll_by(float dy) noexcept { set_scroll_y(scroll_y_ + dy); }

    [[nodiscard]] Rect scrollbar_track_rect() const noexcept;
    [[nodiscard]] Rect scrollbar_thumb_rect() const noexcept;

    bool tick(const InputState& input);

    /// Push the scroll area's scissor rect onto `batcher`.
    void begin_draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const;
    /// Draw the scrollbar track + thumb and pop the scissor rect.
    void end_draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const;

private:
    Rect  rect_           {};
    float content_height_ { 0.0F };
    float scroll_y_       { 0.0F };
    float scrollbar_w_    { 10.0F };
    bool  dragging_thumb_ { false };
    float drag_anchor_y_  { 0.0F };
    float drag_start_sy_  { 0.0F };
};

// ---- 5. MenuBar & 6. ContextMenu -------------------------------------------

struct MenuAction
{
    std::string           label     {};
    std::function<void()> on_select {};
    bool                  enabled   { true };
};

class ContextMenu
{
public:
    ContextMenu() = default;

    void set_items(std::vector<MenuAction> items) { items_ = std::move(items); }
    [[nodiscard]] std::span<const MenuAction> items() const noexcept { return items_; }

    void open_at(float x, float y, float width = 160.0F, float item_height = 24.0F) noexcept;
    void close() noexcept { open_ = false; hovered_index_ = static_cast<std::size_t>(-1); }
    [[nodiscard]] bool is_open() const noexcept { return open_; }
    [[nodiscard]] Rect popup_rect() const noexcept;

    bool tick(const InputState& input);
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    std::vector<MenuAction> items_         {};
    float                   x_             { 0.0F };
    float                   y_             { 0.0F };
    float                   width_         { 160.0F };
    float                   item_height_   { 24.0F };
    bool                    open_          { false };
    std::size_t             hovered_index_ { static_cast<std::size_t>(-1) };
};

class MenuBar
{
public:
    struct Menu
    {
        std::string             title {};
        std::vector<MenuAction> items {};
    };

    MenuBar() = default;

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void add_menu(std::string title, std::vector<MenuAction> items);
    [[nodiscard]] std::size_t menu_count() const noexcept { return menus_.size(); }
    [[nodiscard]] std::size_t active_menu() const noexcept { return active_menu_; }

    bool tick(const InputState& input);
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect              rect_        {};
    std::vector<Menu> menus_       {};
    float             menu_width_  { 72.0F };
    std::size_t       active_menu_ { static_cast<std::size_t>(-1) };
    ContextMenu       popup_       {};
};

// ---- 7. ToastQueue & 8. Tooltip --------------------------------------------

enum class ToastSeverity : std::uint8_t
{
    kInfo    = 0,
    kSuccess = 1,
    kWarning = 2,
    kError   = 3,
};

struct ToastItem
{
    std::string   message       {};
    ToastSeverity severity      { ToastSeverity::kInfo };
    float         remaining_s   { 3.0F };
    float         total_s       { 3.0F };
};

class ToastQueue
{
public:
    ToastQueue() = default;

    void push(std::string message,
              ToastSeverity severity = ToastSeverity::kInfo,
              float duration_s = 3.0F);

    void tick(float dt_s) noexcept;
    [[nodiscard]] std::size_t active_count() const noexcept { return items_.size(); }
    [[nodiscard]] std::span<const ToastItem> items() const noexcept { return items_; }

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme,
              Rect viewport) const;

private:
    std::vector<ToastItem> items_ {};
};

class Tooltip
{
public:
    Tooltip() = default;

    void set_delay(float delay_s) noexcept { delay_s_ = delay_s >= 0.0F ? delay_s : 0.0F; }
    void update(bool target_hovered, float mouse_x, float mouse_y,
                std::string_view text, float dt_s);
    [[nodiscard]] bool is_visible() const noexcept { return visible_; }

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    std::string text_    {};
    float       x_       { 0.0F };
    float       y_       { 0.0F };
    float       timer_s_ { 0.0F };
    float       delay_s_ { 0.35F };
    bool        visible_ { false };
};

// ---- 9. NodeGraphCanvas ----------------------------------------------------

struct GraphNode
{
    std::uint32_t id          { 0U };
    std::string   title       {};
    float         x           { 0.0F };
    float         y           { 0.0F };
    float         w           { 140.0F };
    float         h           { 80.0F };
    std::uint32_t input_pins  { 1U };
    std::uint32_t output_pins { 1U };
    Color         header_tint { 70U, 110U, 180U, 255U };
};

struct GraphWire
{
    std::uint32_t from_node { 0U };
    std::uint32_t from_pin  { 0U };
    std::uint32_t to_node   { 0U };
    std::uint32_t to_pin    { 0U };
};

class NodeGraphCanvas
{
public:
    NodeGraphCanvas() = default;

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    std::uint32_t add_node(GraphNode node);
    bool add_wire(GraphWire wire);
    bool remove_node(std::uint32_t id);

    [[nodiscard]] std::span<const GraphNode> nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::span<const GraphWire> wires() const noexcept { return wires_; }

    void set_pan(float px, float py) noexcept { pan_x_ = px; pan_y_ = py; }
    [[nodiscard]] float pan_x() const noexcept { return pan_x_; }
    [[nodiscard]] float pan_y() const noexcept { return pan_y_; }

    void set_zoom(float z) noexcept;
    [[nodiscard]] float zoom() const noexcept { return zoom_; }

    [[nodiscard]] std::uint32_t selected_node() const noexcept { return selected_node_; }

    bool tick(const InputState& input);
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect                   rect_          {};
    std::vector<GraphNode> nodes_         {};
    std::vector<GraphWire> wires_         {};
    float                  pan_x_         { 0.0F };
    float                  pan_y_         { 0.0F };
    float                  zoom_          { 1.0F };
    std::uint32_t          selected_node_ { 0xFFFFFFFFu };
    bool                   dragging_node_ { false };
    float                  drag_offset_x_ { 0.0F };
    float                  drag_offset_y_ { 0.0F };
};

// ---- 10. FlameChart & 11. Sparkline ----------------------------------------

struct FlameScopeBar
{
    std::string   name     {};
    float         start_ms { 0.0F };
    float         dur_ms   { 0.0F };
    std::uint32_t depth    { 0U };
    Color         color    { 96U, 160U, 255U, 255U };
};

class FlameChart
{
public:
    FlameChart() = default;

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_budget_ms(float budget) noexcept { budget_ms_ = budget > 0.1F ? budget : 16.67F; }
    [[nodiscard]] float budget_ms() const noexcept { return budget_ms_; }

    void set_bars(std::vector<FlameScopeBar> bars) { bars_ = std::move(bars); }
    [[nodiscard]] std::span<const FlameScopeBar> bars() const noexcept { return bars_; }

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect                       rect_      {};
    float                      budget_ms_ { 16.67F };
    std::vector<FlameScopeBar> bars_      {};
};

class Sparkline
{
public:
    explicit Sparkline(std::size_t capacity = 60U);

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void push_sample(float v);
    [[nodiscard]] std::span<const float> samples() const noexcept { return samples_; }
    [[nodiscard]] float latest() const noexcept { return samples_.empty() ? 0.0F : samples_.back(); }
    [[nodiscard]] float average() const noexcept;

    void draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const;

private:
    Rect               rect_     {};
    std::size_t        capacity_ { 60U };
    std::vector<float> samples_  {};
};

// ---- 12. GaugeBar, 13. Crosshair, 14. Minimap, 15. InventoryGrid -----------

class GaugeBar
{
public:
    GaugeBar() = default;
    explicit GaugeBar(float ratio, Color fill_color = { 80U, 200U, 100U, 255U })
        : ratio_ { ratio }, fill_color_ { fill_color }
    {
    }

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_ratio(float r) noexcept;
    [[nodiscard]] float ratio() const noexcept { return ratio_; }

    void set_fill_color(Color c) noexcept { fill_color_ = c; }
    [[nodiscard]] Color fill_color() const noexcept { return fill_color_; }

    void draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const;

private:
    Rect  rect_       {};
    float ratio_      { 1.0F };
    Color fill_color_ { 80U, 200U, 100U, 255U };
};

class Crosshair
{
public:
    Crosshair() = default;

    void set_center(float cx, float cy) noexcept { cx_ = cx; cy_ = cy; }
    void set_spread(float gap_px, float arm_len_px = 8.0F, float thickness_px = 2.0F) noexcept;
    [[nodiscard]] float spread_gap() const noexcept { return gap_; }

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              Color color = { 240U, 240U, 240U, 220U }) const;

private:
    float cx_        { 0.0F };
    float cy_        { 0.0F };
    float gap_       { 4.0F };
    float arm_len_   { 8.0F };
    float thickness_ { 2.0F };
};

struct MinimapBlip
{
    float world_x { 0.0F };
    float world_z { 0.0F };
    Color color   { 220U, 80U, 80U, 255U };
    float size_px { 4.0F };
};

class Minimap
{
public:
    Minimap() = default;

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_player_pose(float world_x, float world_z, float range_m = 50.0F) noexcept;
    void set_blips(std::vector<MinimapBlip> blips) { blips_ = std::move(blips); }
    [[nodiscard]] std::span<const MinimapBlip> blips() const noexcept { return blips_; }

    void draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const;

private:
    Rect                     rect_     {};
    float                    player_x_ { 0.0F };
    float                    player_z_ { 0.0F };
    float                    range_m_  { 50.0F };
    std::vector<MinimapBlip> blips_    {};
};

class InventoryGrid
{
public:
    InventoryGrid(std::uint32_t cols = 4U, std::uint32_t rows = 4U);

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    [[nodiscard]] std::uint32_t columns() const noexcept { return cols_; }
    [[nodiscard]] std::uint32_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t slot_count() const noexcept { return static_cast<std::size_t>(cols_) * rows_; }

    void set_slot_item(std::size_t slot_idx, std::uint32_t item_id);
    [[nodiscard]] std::uint32_t slot_item(std::size_t slot_idx) const noexcept;

    [[nodiscard]] std::size_t selected_slot() const noexcept { return selected_slot_; }
    [[nodiscard]] Rect slot_rect(std::size_t slot_idx) const noexcept;

    bool tick(const InputState& input);
    void draw(cd::ui::renderer::DrawBatcher& batcher, const Theme& theme) const;

private:
    Rect                       rect_          {};
    std::uint32_t              cols_          { 4U };
    std::uint32_t              rows_          { 4U };
    float                      gap_           { 4.0F };
    std::vector<std::uint32_t> items_         {};
    std::size_t                selected_slot_ { static_cast<std::size_t>(-1) };
};

}  // namespace cd::ui::widgets
