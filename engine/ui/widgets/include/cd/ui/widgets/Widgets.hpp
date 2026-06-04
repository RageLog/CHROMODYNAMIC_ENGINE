// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/Widgets.hpp
//
// Phase 2.2 of ADR-20260530-ui-widget-library. Concrete widget catalog
// built on top of:
//   * cd::ui            -- retained-mode widget tree (Widget / Rect / ...)
//   * cd::ui_layout     -- Flex layout solver (Phase 1.0)
//   * cd::ui_font       -- glyph atlas + rasterizer (Phase 1.1)
//   * cd::ui_renderer   -- CPU draw batcher (Phase 1.2)
//   * cd::ui_input      -- hit-test + focus + modal capture (Phase 2.1)
//   * cd::core          -- foundation
//
// Widget catalog (this header):
//   * Button       -- click target with label
//   * TextInput    -- single-line text editor with cursor
//   * Slider       -- horizontal 0..1 value slider
//   * Toggle       -- two-state on/off switch (lozenge style)
//   * Checkbox     -- two-state check (square box style)
//   * Dropdown     -- option list with a selected index
//   * Modal        -- full-bleed dim layer + centred panel (visibility gate)
//
// Each widget:
//   * Owns its rect (set externally by layout via `set_rect`).
//   * Has `tick(input_state)` for pointer + key driven state transitions.
//   * Has `draw(batcher, font, theme)` to emit draw commands.
//   * Exposes a typed `on_change` / `on_click` callback hook.
//
// All widgets are renderer-agnostic at the point of state mutation: a
// callsite that only mutates state (testing / headless tooling) can skip
// the `draw` call entirely. Per ADR contract this lets the test suite
// validate state transitions without spinning up a GPU.
//
// ButtonState is the canonical pointer-focus tri-state used by every
// widget that responds to hover / press / keyboard focus. Sliders and
// dropdowns extend it with their own per-frame indices.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/animation/Animation.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::ui::widgets
{

// ---- Geometry / colour primitives -----------------------------------------

/// Pixel-space axis-aligned rectangle. Top-left origin, w/h positive.
struct Rect
{
    float x { 0.0F };
    float y { 0.0F };
    float w { 0.0F };
    float h { 0.0F };

    [[nodiscard]] constexpr bool contains(float px, float py) const noexcept
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return w > 0.0F && h > 0.0F;
    }
};

/// 8-bit RGBA colour. Matches `cd::ui::renderer::Color` layout. Kept here
/// (not aliased) so this header has zero include-order dependency on the
/// renderer header beyond a forward declaration of the batcher class.
struct Color
{
    std::uint8_t r { 255U };
    std::uint8_t g { 255U };
    std::uint8_t b { 255U };
    std::uint8_t a { 255U };
};

/// Theme palette. Concrete widgets sample these slots when drawing. The
/// frontend (editor / app) supplies a theme via the draw call so multiple
/// widget trees can share one theme instance without cloning.
///
/// Phase 673 — 6 semantic tokens added (warm-dark defaults):
///   surface_subtle   -- slightly lighter than surface; panel backgrounds vs
///                       deeper interior fill (e.g. docked panel header vs body).
///   divider          -- low-alpha line between dock nodes / section separators.
///   text_dim         -- low-contrast text for hints and secondary labels.
///   accent_warning   -- yellow; asset_validator warning badge, toast banners.
///   accent_error     -- red;    asset_validator error badge, critical status.
///   accent_success   -- green;  asset_validator pass badge, build-success toast.
///
/// Back-compat: all pre-phase673 fields remain unchanged (name, type, default).
struct Theme
{
    Color background    { 32U,  32U,  36U,  255U };
    Color surface       { 48U,  48U,  56U,  255U };
    Color surface_hover { 64U,  64U,  72U,  255U };
    Color surface_press { 80U,  80U,  92U,  255U };
    Color accent        { 96U, 160U, 255U,  255U };
    Color accent_hover  {120U, 180U, 255U,  255U };
    Color focus_ring    {255U, 220U, 100U,  255U };
    Color text          {235U, 235U, 240U,  255U };
    Color text_dim      {160U, 160U, 170U,  255U };
    Color dim_overlay   {  0U,   0U,   0U,  160U };

    // ---- Phase 673 semantic extension (warm dark palette) -------------------
    // surface_subtle: one step lighter than surface (48,48,56) so panel
    // background quads read as a raised layer without a border. Sits between
    // surface and surface_hover in the elevation stack.
    Color surface_subtle  { 56U,  58U,  68U,  255U };
    // divider: #38424F at alpha 180 — a barely-visible line that separates
    // dock tiles and section headers without drawing the eye.
    Color divider         { 56U,  66U,  79U,  180U };
    // accent_warning: warm amber (#DCC850) — matches asset_validator Severity::kWarning.
    Color accent_warning  {220U, 200U,  80U,  230U };
    // accent_error: desaturated red (#DC5050) — matches asset_validator Severity::kError.
    Color accent_error    {220U,  80U,  80U,  230U };
    // accent_success: muted green (#50C864) — matches asset_validator pass bucket.
    Color accent_success  { 80U, 200U, 100U,  230U };
};

// ---- Pointer + key input snapshot -----------------------------------------

/// One frame's pointer state, low-level enough that widgets never need to
/// touch raw `cd::ui::input::MouseEvent` themselves. The frontend bridge
/// flattens the event stream into this struct per frame.
struct PointerState
{
    float mouse_x       { 0.0F };
    float mouse_y       { 0.0F };
    bool  left_down     { false };   ///< pointer button is held this frame
    bool  left_pressed  { false };   ///< edge: down THIS frame, up previous frame
    bool  left_released { false };   ///< edge: up   THIS frame, down previous frame
};

/// Symbolic key signal for keyboard-driven widget transitions. The
/// frontend bridge maps platform keycodes to one of these. `kCharacter`
/// is a printable Unicode codepoint (e.g. ASCII letter or BMP). Per
/// ADR contract, IME / dead-key composition is deferred to Phase 4.
enum class KeySignal : std::uint8_t
{
    kNone      = 0,
    kCharacter = 1,
    kBackspace = 2,
    kEnter     = 3,
    kEscape    = 4,
    kLeft      = 5,
    kRight     = 6,
    kUp        = 7,
    kDown      = 8,
    kTab       = 9,
    kHome      = 10,
    kEnd       = 11,
    kDelete    = 12,
    kSpace     = 13,
};

/// One key event flattened for widget consumption. `codepoint` is only
/// meaningful when `signal == kCharacter`.
struct KeyInput
{
    KeySignal     signal    { KeySignal::kNone };
    std::uint32_t codepoint { 0U };
};

/// One frame's input snapshot. Widgets read this in `tick`. Keys are
/// supplied as a span so multiple presses in a frame survive (e.g.
/// rapid typing). The pointer is single because widgets only react to
/// one cursor at a time.
struct InputState
{
    PointerState           pointer  {};
    std::span<const KeyInput> keys   {};
    bool                   focused  { false };  ///< true when widget owns focus
};

// ---- ButtonState (shared interaction tri-state) ---------------------------

/// Shared interaction tri-state for hover / press / focus. Each widget
/// owns one of these and updates it in `tick`. Drawing samples it for
/// the right theme slot.
struct ButtonState
{
    bool hovered { false };
    bool pressed { false };
    bool focused { false };
};

// ---- Animation policy ------------------------------------------------------

/// Per-widget animation policy. `speed_up` / `speed_down` are linear rates
/// in units of "hover_amount per second" (so 10.0F means the value covers
/// the [0..1] interval in ~0.1 s). Easing biases the curve shape applied
/// by the per-state `Tweener<float>`.
struct WidgetAnimation
{
    float                         speed_up   { 10.0F };  ///< rate when target=1
    float                         speed_down { 10.0F };  ///< rate when target=0
    cd::ui::animation::Easing     easing     { cd::ui::animation::Easing::kEaseOutCubic };
};

// ---- Forward decls ---------------------------------------------------------

}  // namespace cd::ui::widgets

namespace cd::ui::renderer
{
class DrawBatcher;
}
namespace cd::ui::font
{
class Font;
}

namespace cd::ui::widgets
{

// ---- Button ----------------------------------------------------------------

/// Click target with a centred label. Fires `on_click` on release-inside.
class Button
{
public:
    using ClickCallback = std::function<void()>;

    Button() = default;
    explicit Button(std::string label, ClickCallback on_click = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_label(std::string label) { label_ = std::move(label); }
    [[nodiscard]] std::string_view label() const noexcept { return label_; }

    void set_on_click(ClickCallback cb) { on_click_ = std::move(cb); }

    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    void set_animation(WidgetAnimation a) noexcept { animation_ = a; }
    [[nodiscard]] const WidgetAnimation& animation() const noexcept { return animation_; }

    /// Current hover transition amount in [0..1] (0 = idle, 1 = fully hovered).
    /// Driven by tick(input, dt) toward the boolean state_.hovered target.
    [[nodiscard]] float hover_amount() const noexcept { return hover_amount_; }
    /// Current press transition amount in [0..1] (0 = released, 1 = fully pressed).
    [[nodiscard]] float press_amount() const noexcept { return press_amount_; }
    /// Current focus transition amount in [0..1] (0 = blurred, 1 = fully focused).
    [[nodiscard]] float focus_amount() const noexcept { return focus_amount_; }

    /// Returns true on a click-completed-this-frame transition (release
    /// inside the rect after a press inside). Equivalent to `tick(input, 0.0F)`
    /// (no animation advance). Preserved for legacy / instant-snap callers.
    bool tick(const InputState& input) { return tick(input, 0.0F); }

    /// Advance state machine AND animate hover / press / focus tweens by
    /// `dt_s` seconds. Returns true on click-completed-this-frame.
    bool tick(const InputState& input, float dt_s);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect             rect_          {};
    std::string      label_         {};
    ClickCallback    on_click_      {};
    ButtonState      state_         {};
    bool             armed_         { false };  ///< press was inside this rect
    WidgetAnimation  animation_     {};
    cd::ui::animation::Tweener<float> hover_tween_ {};
    cd::ui::animation::Tweener<float> press_tween_ {};
    cd::ui::animation::Tweener<float> focus_tween_ {};
    float            hover_amount_  { 0.0F };
    float            press_amount_  { 0.0F };
    float            focus_amount_  { 0.0F };
    bool             hover_target_  { false };
    bool             press_target_  { false };
    bool             focus_target_  { false };
};

// ---- TextInput -------------------------------------------------------------

/// Single-line text editor with a caret. Phase 2.2 supports ASCII +
/// Latin-1 codepoints (full Unicode editing is Phase 4 with shaping).
class TextInput
{
public:
    using ChangeCallback = std::function<void(std::string_view)>;

    TextInput() = default;
    explicit TextInput(std::string initial, ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_text(std::string text);
    [[nodiscard]] std::string_view text() const noexcept { return text_; }

    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    void set_cursor(std::size_t pos) noexcept;

    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    /// Returns true when the text contents mutated this frame.
    bool tick(const InputState& input);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect           rect_      {};
    std::string    text_      {};
    std::size_t    cursor_    { 0U };
    ChangeCallback on_change_ {};
    ButtonState    state_     {};
};

// ---- Slider ----------------------------------------------------------------

/// Horizontal slider with a normalised 0..1 value. Drag the thumb or
/// click anywhere on the track to set. Keyboard left/right nudges by
/// `step` (default 0.01).
class Slider
{
public:
    using ChangeCallback = std::function<void(float)>;

    Slider() = default;
    explicit Slider(float initial, ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_value(float v) noexcept;
    [[nodiscard]] float value() const noexcept { return value_; }

    void set_step(float s) noexcept { step_ = s > 0.0F ? s : 0.01F; }
    [[nodiscard]] float step() const noexcept { return step_; }

    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    void set_animation(WidgetAnimation a) noexcept { animation_ = a; }
    [[nodiscard]] const WidgetAnimation& animation() const noexcept { return animation_; }

    [[nodiscard]] float hover_amount() const noexcept { return hover_amount_; }
    [[nodiscard]] float press_amount() const noexcept { return press_amount_; }
    [[nodiscard]] float focus_amount() const noexcept { return focus_amount_; }

    /// Returns true when the value mutated this frame. Equivalent to
    /// `tick(input, 0.0F)` (no animation advance).
    bool tick(const InputState& input) { return tick(input, 0.0F); }

    /// Advance state machine AND animate hover / press / focus tweens by
    /// `dt_s` seconds. Returns true when the value mutated this frame.
    bool tick(const InputState& input, float dt_s);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect             rect_          {};
    float            value_         { 0.0F };
    float            step_          { 0.01F };
    ChangeCallback   on_change_     {};
    ButtonState      state_         {};
    bool             dragging_      { false };
    WidgetAnimation  animation_     {};
    cd::ui::animation::Tweener<float> hover_tween_ {};
    cd::ui::animation::Tweener<float> press_tween_ {};
    cd::ui::animation::Tweener<float> focus_tween_ {};
    float            hover_amount_  { 0.0F };
    float            press_amount_  { 0.0F };
    float            focus_amount_  { 0.0F };
    bool             hover_target_  { false };
    bool             press_target_  { false };
    bool             focus_target_  { false };
};

// ---- Toggle (on/off lozenge) ----------------------------------------------

/// Two-state lozenge switch. Click anywhere in the rect flips state.
/// Enter/Space when focused also flips.
class Toggle
{
public:
    using ChangeCallback = std::function<void(bool)>;

    Toggle() = default;
    explicit Toggle(bool initial, ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_value(bool v) noexcept { value_ = v; }
    [[nodiscard]] bool value() const noexcept { return value_; }

    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    void set_animation(WidgetAnimation a) noexcept { animation_ = a; }
    [[nodiscard]] const WidgetAnimation& animation() const noexcept { return animation_; }

    [[nodiscard]] float hover_amount() const noexcept { return hover_amount_; }
    [[nodiscard]] float press_amount() const noexcept { return press_amount_; }
    [[nodiscard]] float focus_amount() const noexcept { return focus_amount_; }

    /// Returns true when the value flipped this frame. Equivalent to
    /// `tick(input, 0.0F)` (no animation advance).
    bool tick(const InputState& input) { return tick(input, 0.0F); }

    /// Advance state machine AND animate hover / press / focus tweens by
    /// `dt_s` seconds. Returns true when the value flipped this frame.
    bool tick(const InputState& input, float dt_s);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect             rect_          {};
    bool             value_         { false };
    ChangeCallback   on_change_     {};
    ButtonState      state_         {};
    bool             armed_         { false };
    WidgetAnimation  animation_     {};
    cd::ui::animation::Tweener<float> hover_tween_ {};
    cd::ui::animation::Tweener<float> press_tween_ {};
    cd::ui::animation::Tweener<float> focus_tween_ {};
    float            hover_amount_  { 0.0F };
    float            press_amount_  { 0.0F };
    float            focus_amount_  { 0.0F };
    bool             hover_target_  { false };
    bool             press_target_  { false };
    bool             focus_target_  { false };
};

// ---- Checkbox (square check) ----------------------------------------------

/// Two-state square check. Visually distinct from Toggle: the rect is
/// treated as `[checkbox box | label]` -- the box is on the left edge,
/// the optional label trails it. Click anywhere in the rect flips.
class Checkbox
{
public:
    using ChangeCallback = std::function<void(bool)>;

    Checkbox() = default;
    explicit Checkbox(bool initial,
                      std::string label = {},
                      ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_value(bool v) noexcept { value_ = v; }
    [[nodiscard]] bool value() const noexcept { return value_; }

    void set_label(std::string label) { label_ = std::move(label); }
    [[nodiscard]] std::string_view label() const noexcept { return label_; }

    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    void set_animation(WidgetAnimation a) noexcept { animation_ = a; }
    [[nodiscard]] const WidgetAnimation& animation() const noexcept { return animation_; }

    [[nodiscard]] float hover_amount() const noexcept { return hover_amount_; }
    [[nodiscard]] float press_amount() const noexcept { return press_amount_; }
    [[nodiscard]] float focus_amount() const noexcept { return focus_amount_; }

    /// Equivalent to `tick(input, 0.0F)` -- no animation advance.
    bool tick(const InputState& input) { return tick(input, 0.0F); }

    /// Advance state machine AND animate hover / press / focus tweens by
    /// `dt_s` seconds.
    bool tick(const InputState& input, float dt_s);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect             rect_          {};
    bool             value_         { false };
    std::string      label_         {};
    ChangeCallback   on_change_     {};
    ButtonState      state_         {};
    bool             armed_         { false };
    WidgetAnimation  animation_     {};
    cd::ui::animation::Tweener<float> hover_tween_ {};
    cd::ui::animation::Tweener<float> press_tween_ {};
    cd::ui::animation::Tweener<float> focus_tween_ {};
    float            hover_amount_  { 0.0F };
    float            press_amount_  { 0.0F };
    float            focus_amount_  { 0.0F };
    bool             hover_target_  { false };
    bool             press_target_  { false };
    bool             focus_target_  { false };
};

// ---- Dropdown --------------------------------------------------------------

/// A header bar with the currently-selected option. Click the bar to
/// expand a vertical option list; click an option to commit + collapse.
/// While expanded, the dropdown is logically a modal (focus stays
/// inside it -- the frontend should push the dropdown id onto the
/// modal stack).
class Dropdown
{
public:
    using ChangeCallback = std::function<void(std::size_t /*selected*/)>;

    Dropdown() = default;
    explicit Dropdown(std::vector<std::string> options,
                      std::size_t selected = 0U,
                      ChangeCallback on_change = {});

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    void set_options(std::vector<std::string> opts);
    [[nodiscard]] std::span<const std::string> options() const noexcept
    {
        return std::span<const std::string>(options_.data(), options_.size());
    }

    void set_selected(std::size_t idx) noexcept;
    [[nodiscard]] std::size_t selected() const noexcept { return selected_; }

    [[nodiscard]] bool expanded() const noexcept { return expanded_; }
    void set_expanded(bool v) noexcept { expanded_ = v; }

    void set_option_height(float h) noexcept { option_height_ = h > 0.0F ? h : 1.0F; }
    [[nodiscard]] float option_height() const noexcept { return option_height_; }

    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    /// Returns the index of the option under the pointer this frame
    /// when expanded (SIZE_MAX = none / collapsed).
    [[nodiscard]] std::size_t hovered_option() const noexcept { return hovered_option_; }

    bool tick(const InputState& input);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect                     rect_           {};
    std::vector<std::string> options_        {};
    std::size_t              selected_       { 0U };
    bool                     expanded_       { false };
    float                    option_height_  { 24.0F };
    ChangeCallback           on_change_      {};
    ButtonState              state_          {};
    std::size_t              hovered_option_ { static_cast<std::size_t>(-1) };
};

// ---- Modal -----------------------------------------------------------------

/// Visibility-gated full-bleed container. When `visible == true`, draw
/// emits the dim overlay over the whole framebuffer and the centred
/// content rect (which the caller supplies via `set_content_rect` and
/// populates with child widgets). Escape closes the modal when focused.
class Modal
{
public:
    using CloseCallback = std::function<void()>;

    Modal() = default;
    explicit Modal(bool visible, CloseCallback on_close = {});

    /// Set the size of the full-bleed dim layer (typically framebuffer
    /// dimensions). The content rect is positioned within this.
    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    /// Set the centred content panel rect. Caller-owned; the modal
    /// just draws the panel background + clips child widgets.
    void set_content_rect(Rect r) noexcept { content_rect_ = r; }
    [[nodiscard]] const Rect& content_rect() const noexcept { return content_rect_; }

    void set_visible(bool v) noexcept { visible_ = v; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    void set_on_close(CloseCallback cb) { on_close_ = std::move(cb); }

    [[nodiscard]] const ButtonState& state() const noexcept { return state_; }

    /// Returns true when the modal closed this frame (Escape pressed
    /// while visible + focused, or an outside-click hit the dim layer).
    bool tick(const InputState& input);

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    Rect          rect_         {};
    Rect          content_rect_ {};
    bool          visible_      { false };
    CloseCallback on_close_     {};
    ButtonState   state_        {};
};

}  // namespace cd::ui::widgets
