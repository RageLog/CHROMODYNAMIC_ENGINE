// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/Widgets.cpp
//
// Concrete widget catalog implementation (ADR-20260530 Phase 2.2).
//
// Drawing strategy:
//   * Each widget paints a base rect via `DrawBatcher::quad` (kSolid
//     variant) using a colour sampled from the supplied `Theme`.
//   * Labels / text are emitted as a horizontal run of `glyph` calls
//     looked up from the supplied font. Layout is the simplest possible
//     left-aligned baseline walk -- callers who want centred / right
//     aligned text wrap a widget in a layout node.
//   * Focus ring is drawn as a 1-pixel border (four thin quads) around
//     the rect when `state.focused == true`.
//
// State transitions are pointer-driven by `PointerState` edges
// (`left_pressed` / `left_released`) and supplemented by `KeySignal`
// events for keyboard-equivalent actions (Space / Enter activate the
// focused widget; arrows nudge sliders; Escape closes modals; text
// editing keys mutate `TextInput`).
//
// Per ADR the draw path tolerates a null font: textless widgets paint
// only their background. The tests rely on this -- they never load a
// real TTF, only verify state machines.
// =============================================================================
#include <cd/ui/widgets/Widgets.hpp>

#include <cd/ui/animation/Animation.hpp>
#include <cd/ui/font/Font.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace cd::ui::widgets
{

// ---- Local helpers ---------------------------------------------------------

namespace
{

[[nodiscard]] cd::ui::renderer::Color to_renderer_color(Color c) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, c.a };
}

/// Linear blend between two 8-bit colour channels by t in [0..1].
[[nodiscard]] std::uint8_t blend_channel(std::uint8_t a, std::uint8_t b, float t) noexcept
{
    const auto ta = static_cast<float>(a);
    const auto tb = static_cast<float>(b);
    const float u  = std::clamp(t, 0.0F, 1.0F);
    const float r  = ta + (tb - ta) * u;
    return static_cast<std::uint8_t>(std::clamp(r, 0.0F, 255.0F));
}

/// Linear blend between two `Color` values. `t` is clamped to [0..1] for
/// colour purposes -- overshoot from back / elastic easings only affects
/// scale / elevation, not pixel colour (avoids RGBA wrap-around).
[[nodiscard]] Color blend_color(const Color& a, const Color& b, float t) noexcept
{
    return Color {
        blend_channel(a.r, b.r, t),
        blend_channel(a.g, b.g, t),
        blend_channel(a.b, b.b, t),
        blend_channel(a.a, b.a, t),
    };
}

/// Advance an animation amount toward a boolean target using a Tweener<float>.
/// When the target flips (e.g. hover off -> on), we (re)start the tweener
/// with from=current, to=target_amount, duration = 1.0 / rate. Subsequent
/// frames just tick(dt) and read value(). Once done(), we snap to the target.
///
/// Returns the new amount value, updates `current` in-place, and (re)starts
/// `tween` whenever `target` flips relative to `prev_target`.
void advance_tween(cd::ui::animation::Tweener<float>& tween,
                   float&                              current,
                   bool                                target,
                   bool&                               prev_target,
                   float                               dt_s,
                   const WidgetAnimation&              policy) noexcept
{
    const float target_amount = target ? 1.0F : 0.0F;

    // Restart the tween whenever the target flips. The tweener interpolates
    // from the CURRENT in-flight value (preserving smoothness when the
    // user flicks hover on/off rapidly) toward the new target.
    if (target != prev_target)
    {
        const float rate = target ? policy.speed_up : policy.speed_down;
        cd::ui::animation::Animation<float> anim {};
        anim.from       = current;
        anim.to         = target_amount;
        anim.duration_s = (rate > 0.0F) ? (1.0F / rate) : 0.0001F;
        anim.easing     = policy.easing;
        tween.start(anim);
        prev_target = target;
    }

    if (dt_s > 0.0F)
    {
        tween.tick(dt_s);
        current = tween.value();
    }

    // Snap to the boolean target once the tween completes, so the value
    // is exactly 0.0F or 1.0F at rest (avoids drifting precision dust).
    if (tween.done())
    {
        current = target_amount;
    }
}

void draw_solid_rect(cd::ui::renderer::DrawBatcher& batcher,
                     const Rect& r, Color c)
{
    if (!r.is_valid()) { return; }
    batcher.quad(r.x, r.y, r.w, r.h, to_renderer_color(c));
}

void draw_focus_ring(cd::ui::renderer::DrawBatcher& batcher,
                     const Rect& r, Color c)
{
    if (!r.is_valid()) { return; }
    const float t = 1.0F;
    const cd::ui::renderer::Color rc = to_renderer_color(c);
    // Top, bottom, left, right strips.
    batcher.quad(r.x,           r.y,           r.w, t, rc);
    batcher.quad(r.x,           r.y + r.h - t, r.w, t, rc);
    batcher.quad(r.x,           r.y,           t,   r.h, rc);
    batcher.quad(r.x + r.w - t, r.y,           t,   r.h, rc);
}

/// Pen-walk through `text` and emit one `glyph` per codepoint via the
/// font's atlas UV lookup. Origin = `(x, baseline_y)`. Returns the
/// advanced pen x after the last glyph (for cursor placement).
float draw_text_line(cd::ui::renderer::DrawBatcher& batcher,
                     cd::ui::font::Font* font,
                     std::string_view text,
                     float x, float baseline_y,
                     Color color,
                     std::uint32_t atlas_slot = 0U)
{
    if (font == nullptr) { return x; }
    float pen = x;
    std::uint32_t prev_cp = 0U;
    const cd::ui::renderer::Color rc = to_renderer_color(color);
    for (const char ch : text)
    {
        // Phase 2.2 ASCII / Latin-1 fast path. Wide codepoint decoding
        // is Phase 4 with HarfBuzz; this matches the font atlas which
        // is only rasterized over BMP slices for now.
        const auto cp = static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
        const std::optional<cd::ui::font::GlyphInfo> g = font->glyph_uv(cp);
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
            batcher.glyph(gx, gy, g->width, g->height,
                          atlas_slot, uv, rc);
        }
        pen    += g->advance;
        prev_cp = cp;
    }
    return pen;
}

/// Map a printable signal to its codepoint when appropriate; otherwise
/// return zero.
[[nodiscard]] std::uint32_t printable_codepoint(const KeyInput& key) noexcept
{
    if (key.signal == KeySignal::kCharacter) { return key.codepoint; }
    if (key.signal == KeySignal::kSpace)     { return static_cast<std::uint32_t>(' '); }
    return 0U;
}

/// Return true when any of the supplied keys carries an `activate`
/// signal (Enter or Space). Helper for buttons / toggles / checkboxes.
[[nodiscard]] bool any_activate_key(std::span<const KeyInput> keys) noexcept
{
    for (const KeyInput& k : keys)
    {
        if (k.signal == KeySignal::kEnter || k.signal == KeySignal::kSpace)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

// =========================================================================
// Button
// =========================================================================

Button::Button(std::string label, ClickCallback on_click)
    : label_(std::move(label))
    , on_click_(std::move(on_click))
{
}

bool Button::tick(const InputState& input, float dt_s)
{
    const bool inside = rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    state_.hovered = inside;
    state_.focused = input.focused;

    bool clicked = false;

    // Pointer click cycle: press inside arms; release inside while armed
    // fires; release outside / press outside drops the arm.
    if (input.pointer.left_pressed)
    {
        armed_ = inside;
        state_.pressed = inside;
    }

    if (input.pointer.left_down && armed_)
    {
        state_.pressed = inside;
    }

    if (input.pointer.left_released)
    {
        if (armed_ && inside)
        {
            clicked = true;
        }
        armed_         = false;
        state_.pressed = false;
    }

    // Keyboard activate when focused (Enter or Space).
    if (input.focused && any_activate_key(input.keys))
    {
        clicked = true;
    }

    // Animate hover / press / focus toward their boolean targets via the
    // per-state Tweener<float>. The boolean state_ remains the authoritative
    // input-state; the *_amount_ floats are the visual interpolant draw()
    // consumes for smooth tints and elevation.
    advance_tween(hover_tween_, hover_amount_, state_.hovered, hover_target_, dt_s, animation_);
    advance_tween(press_tween_, press_amount_, state_.pressed, press_target_, dt_s, animation_);
    advance_tween(focus_tween_, focus_amount_, state_.focused, focus_target_, dt_s, animation_);

    if (clicked && on_click_)
    {
        on_click_();
    }
    return clicked;
}

void Button::draw(cd::ui::renderer::DrawBatcher& batcher,
                  cd::ui::font::Font* font,
                  const Theme& theme) const
{
    // Tint = blend(base -> hover, hover_amount) then blend toward press_amount.
    // Press dominates hover when both are active (a pressed button is also
    // hovered by construction in tick()).
    Color bg = blend_color(theme.surface,       theme.surface_hover, hover_amount_);
    bg       = blend_color(bg,                  theme.surface_press, press_amount_);

    // Elevation: slight upward shift on hover, downward on press. The
    // shift is purely visual -- hit-testing in tick() still uses the
    // canonical rect_.
    const float elevation = (hover_amount_ - press_amount_) * 2.0F;
    Rect elevated = rect_;
    elevated.y -= elevation;
    draw_solid_rect(batcher, elevated, bg);

    if (focus_amount_ > 0.0F)
    {
        // Fade the focus ring in with the focus tween rather than a hard
        // edge -- visually softer when keyboard nav arrives.
        Color ring = theme.focus_ring;
        ring.a = static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(ring.a) * focus_amount_,
                       0.0F, 255.0F));
        draw_focus_ring(batcher, elevated, ring);
    }

    if (font != nullptr && !label_.empty())
    {
        // Baseline: roughly 1/3 from the bottom of the rect.
        const float baseline = elevated.y + elevated.h - (elevated.h * 0.30F);
        const float x        = elevated.x + 8.0F;
        (void) draw_text_line(batcher, font, label_, x, baseline, theme.text);
    }
}

// =========================================================================
// TextInput
// =========================================================================

TextInput::TextInput(std::string initial, ChangeCallback on_change)
    : text_(std::move(initial))
    , cursor_(text_.size())
    , on_change_(std::move(on_change))
{
}

void TextInput::set_text(std::string text)
{
    text_   = std::move(text);
    cursor_ = std::min(cursor_, text_.size());
}

void TextInput::set_cursor(std::size_t pos) noexcept
{
    cursor_ = std::min(pos, text_.size());
}

bool TextInput::tick(const InputState& input)
{
    const bool inside = rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    state_.hovered = inside;
    state_.focused = input.focused;

    if (input.pointer.left_pressed)
    {
        state_.pressed = inside;
    }
    if (input.pointer.left_released)
    {
        state_.pressed = false;
    }

    if (!input.focused) { return false; }

    bool mutated = false;

    for (const KeyInput& key : input.keys)
    {
        switch (key.signal)
        {
        case KeySignal::kBackspace:
            if (cursor_ > 0U && !text_.empty())
            {
                text_.erase(text_.begin() +
                            static_cast<std::ptrdiff_t>(cursor_ - 1U));
                --cursor_;
                mutated = true;
            }
            break;

        case KeySignal::kDelete:
            if (cursor_ < text_.size())
            {
                text_.erase(text_.begin() +
                            static_cast<std::ptrdiff_t>(cursor_));
                mutated = true;
            }
            break;

        case KeySignal::kLeft:
            if (cursor_ > 0U) { --cursor_; }
            break;

        case KeySignal::kRight:
            if (cursor_ < text_.size()) { ++cursor_; }
            break;

        case KeySignal::kHome:
            cursor_ = 0U;
            break;

        case KeySignal::kEnd:
            cursor_ = text_.size();
            break;

        case KeySignal::kCharacter:
        case KeySignal::kSpace:
        {
            const std::uint32_t cp = printable_codepoint(key);
            // Phase 2.2 ASCII / Latin-1 fast path: only emit single bytes.
            // Wider codepoints become Phase 4 with UTF-8 encoding.
            if (cp > 0U && cp < 0x100U)
            {
                text_.insert(text_.begin() +
                             static_cast<std::ptrdiff_t>(cursor_),
                             static_cast<char>(cp));
                ++cursor_;
                mutated = true;
            }
            break;
        }

        // The rest are non-editing signals for this widget.
        case KeySignal::kEnter:
        case KeySignal::kEscape:
        case KeySignal::kUp:
        case KeySignal::kDown:
        case KeySignal::kTab:
        case KeySignal::kNone:
        default:
            break;
        }
    }

    if (mutated && on_change_)
    {
        on_change_(std::string_view { text_ });
    }
    return mutated;
}

void TextInput::draw(cd::ui::renderer::DrawBatcher& batcher,
                     cd::ui::font::Font* font,
                     const Theme& theme) const
{
    Color bg = theme.surface;
    if (state_.focused)      { bg = theme.surface_hover; }
    else if (state_.hovered) { bg = theme.surface_hover; }
    draw_solid_rect(batcher, rect_, bg);

    if (state_.focused)
    {
        draw_focus_ring(batcher, rect_, theme.focus_ring);
    }

    if (font == nullptr)
    {
        return;
    }

    const float baseline = rect_.y + rect_.h - (rect_.h * 0.30F);
    const float x        = rect_.x + 6.0F;
    const float pen_after = draw_text_line(batcher, font, text_, x, baseline, theme.text);

    // Cursor: thin vertical bar drawn after the text up to `cursor_`.
    // We re-walk the text to find the exact x for the caret -- cheap
    // for one-line widgets and avoids storing per-char advances.
    if (state_.focused)
    {
        float caret_x = x;
        std::uint32_t prev_cp = 0U;
        for (std::size_t i = 0U; i < cursor_ && i < text_.size(); ++i)
        {
            const auto cp = static_cast<std::uint32_t>(
                static_cast<unsigned char>(text_[i]));
            const std::optional<cd::ui::font::GlyphInfo> g = font->glyph_uv(cp);
            if (!g.has_value())
            {
                prev_cp = cp;
                continue;
            }
            if (prev_cp != 0U) { caret_x += font->kerning(prev_cp, cp); }
            caret_x += g->advance;
            prev_cp = cp;
        }
        // Clamp so we never draw past the right edge.
        const float right_edge = rect_.x + rect_.w - 1.0F;
        if (caret_x > right_edge) { caret_x = right_edge; }
        const float caret_h = rect_.h * 0.70F;
        const float caret_y = rect_.y + (rect_.h - caret_h) * 0.5F;
        batcher.quad(caret_x, caret_y, 1.0F, caret_h, to_renderer_color(theme.text));
        (void) pen_after;
    }
}

// =========================================================================
// Slider
// =========================================================================

Slider::Slider(float initial, ChangeCallback on_change)
    : value_(std::clamp(initial, 0.0F, 1.0F))
    , on_change_(std::move(on_change))
{
}

void Slider::set_value(float v) noexcept
{
    value_ = std::clamp(v, 0.0F, 1.0F);
}

namespace
{
[[nodiscard]] float slider_value_from_pointer(const Rect& r, float mouse_x) noexcept
{
    if (r.w <= 0.0F) { return 0.0F; }
    const float t = (mouse_x - r.x) / r.w;
    return std::clamp(t, 0.0F, 1.0F);
}
}  // namespace

bool Slider::tick(const InputState& input, float dt_s)
{
    const bool inside = rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    state_.hovered = inside || dragging_;
    state_.focused = input.focused;

    bool mutated = false;
    const float prev = value_;

    if (input.pointer.left_pressed && inside)
    {
        dragging_      = true;
        state_.pressed = true;
        value_ = slider_value_from_pointer(rect_, input.pointer.mouse_x);
    }
    else if (input.pointer.left_down && dragging_)
    {
        value_ = slider_value_from_pointer(rect_, input.pointer.mouse_x);
    }
    if (input.pointer.left_released)
    {
        dragging_      = false;
        state_.pressed = false;
    }

    // Keyboard nudge while focused.
    if (input.focused)
    {
        for (const KeyInput& key : input.keys)
        {
            if (key.signal == KeySignal::kLeft)
            {
                value_ = std::clamp(value_ - step_, 0.0F, 1.0F);
            }
            else if (key.signal == KeySignal::kRight)
            {
                value_ = std::clamp(value_ + step_, 0.0F, 1.0F);
            }
            else if (key.signal == KeySignal::kHome)
            {
                value_ = 0.0F;
            }
            else if (key.signal == KeySignal::kEnd)
            {
                value_ = 1.0F;
            }
        }
    }

    if (value_ != prev)
    {
        mutated = true;
        if (on_change_) { on_change_(value_); }
    }

    advance_tween(hover_tween_, hover_amount_, state_.hovered, hover_target_, dt_s, animation_);
    advance_tween(press_tween_, press_amount_, state_.pressed, press_target_, dt_s, animation_);
    advance_tween(focus_tween_, focus_amount_, state_.focused, focus_target_, dt_s, animation_);

    return mutated;
}

void Slider::draw(cd::ui::renderer::DrawBatcher& batcher,
                  cd::ui::font::Font* font,
                  const Theme& theme) const
{
    (void) font;
    // Track.
    const float track_h = std::max(2.0F, rect_.h * 0.25F);
    const float track_y = rect_.y + (rect_.h - track_h) * 0.5F;
    const Rect  track   { rect_.x, track_y, rect_.w, track_h };
    draw_solid_rect(batcher, track, theme.surface);

    // Fill from left edge to thumb.
    const float fill_w = rect_.w * value_;
    const Rect  fill   { rect_.x, track_y, fill_w, track_h };
    draw_solid_rect(batcher, fill, theme.accent);

    // Thumb. Slightly grows with hover_amount (scale 1.0 -> 1.15) and
    // shrinks on press (-0.05). Tint blends hover_amount toward
    // accent_hover and press_amount toward surface_press.
    const float thumb_scale = 1.0F + (hover_amount_ * 0.15F) - (press_amount_ * 0.05F);
    const float thumb_w_base = std::max(6.0F, rect_.h * 0.5F);
    const float thumb_w = thumb_w_base * thumb_scale;
    const float thumb_h = rect_.h * thumb_scale;
    const float thumb_x = rect_.x + fill_w - (thumb_w * 0.5F);
    const float thumb_y = rect_.y + (rect_.h - thumb_h) * 0.5F;
    const Rect  thumb   { thumb_x, thumb_y, thumb_w, thumb_h };
    Color thumb_col = blend_color(theme.surface_hover, theme.accent_hover,  hover_amount_);
    thumb_col       = blend_color(thumb_col,           theme.surface_press, press_amount_);
    draw_solid_rect(batcher, thumb, thumb_col);

    if (focus_amount_ > 0.0F)
    {
        Color ring = theme.focus_ring;
        ring.a = static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(ring.a) * focus_amount_,
                       0.0F, 255.0F));
        draw_focus_ring(batcher, rect_, ring);
    }
}

// =========================================================================
// Toggle
// =========================================================================

Toggle::Toggle(bool initial, ChangeCallback on_change)
    : value_(initial)
    , on_change_(std::move(on_change))
{
}

bool Toggle::tick(const InputState& input, float dt_s)
{
    const bool inside = rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    state_.hovered = inside;
    state_.focused = input.focused;

    bool flipped = false;

    if (input.pointer.left_pressed)
    {
        armed_         = inside;
        state_.pressed = inside;
    }
    if (input.pointer.left_released)
    {
        if (armed_ && inside)
        {
            value_  = !value_;
            flipped = true;
        }
        armed_         = false;
        state_.pressed = false;
    }

    if (input.focused && any_activate_key(input.keys))
    {
        value_  = !value_;
        flipped = true;
    }

    advance_tween(hover_tween_, hover_amount_, state_.hovered, hover_target_, dt_s, animation_);
    advance_tween(press_tween_, press_amount_, state_.pressed, press_target_, dt_s, animation_);
    advance_tween(focus_tween_, focus_amount_, state_.focused, focus_target_, dt_s, animation_);

    if (flipped && on_change_)
    {
        on_change_(value_);
    }
    return flipped;
}

void Toggle::draw(cd::ui::renderer::DrawBatcher& batcher,
                  cd::ui::font::Font* font,
                  const Theme& theme) const
{
    (void) font;
    // Lozenge background. Blend hover tint over the value-driven base.
    Color base = value_ ? theme.accent : theme.surface;
    base = blend_color(base, value_ ? theme.accent_hover : theme.surface_hover,
                       hover_amount_);
    base = blend_color(base, theme.surface_press, press_amount_ * 0.5F);
    draw_solid_rect(batcher, rect_, base);

    // Knob -- circle approximated by a square shrunk to ~80% of the
    // height; renderer doesn't have a primitive circle yet. Knob grows
    // slightly on hover and shrinks on press.
    const float knob_scale = 1.0F + (hover_amount_ * 0.10F) - (press_amount_ * 0.08F);
    const float knob_size  = rect_.h * 0.8F * knob_scale;
    const float knob_y     = rect_.y + (rect_.h - knob_size) * 0.5F;
    const float knob_x     = value_
        ? (rect_.x + rect_.w - knob_size - (rect_.h - knob_size) * 0.5F)
        : (rect_.x + (rect_.h - knob_size) * 0.5F);
    const Rect knob { knob_x, knob_y, knob_size, knob_size };
    Color knob_col = blend_color(theme.text,        theme.text_dim,      hover_amount_);
    knob_col       = blend_color(knob_col,          theme.surface_press, press_amount_);
    draw_solid_rect(batcher, knob, knob_col);

    if (focus_amount_ > 0.0F)
    {
        Color ring = theme.focus_ring;
        ring.a = static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(ring.a) * focus_amount_,
                       0.0F, 255.0F));
        draw_focus_ring(batcher, rect_, ring);
    }
}

// =========================================================================
// Checkbox
// =========================================================================

Checkbox::Checkbox(bool initial, std::string label, ChangeCallback on_change)
    : value_(initial)
    , label_(std::move(label))
    , on_change_(std::move(on_change))
{
}

bool Checkbox::tick(const InputState& input, float dt_s)
{
    const bool inside = rect_.contains(input.pointer.mouse_x, input.pointer.mouse_y);
    state_.hovered = inside;
    state_.focused = input.focused;

    bool flipped = false;

    if (input.pointer.left_pressed)
    {
        armed_         = inside;
        state_.pressed = inside;
    }
    if (input.pointer.left_released)
    {
        if (armed_ && inside)
        {
            value_  = !value_;
            flipped = true;
        }
        armed_         = false;
        state_.pressed = false;
    }

    if (input.focused && any_activate_key(input.keys))
    {
        value_  = !value_;
        flipped = true;
    }

    advance_tween(hover_tween_, hover_amount_, state_.hovered, hover_target_, dt_s, animation_);
    advance_tween(press_tween_, press_amount_, state_.pressed, press_target_, dt_s, animation_);
    advance_tween(focus_tween_, focus_amount_, state_.focused, focus_target_, dt_s, animation_);

    if (flipped && on_change_)
    {
        on_change_(value_);
    }
    return flipped;
}

void Checkbox::draw(cd::ui::renderer::DrawBatcher& batcher,
                    cd::ui::font::Font* font,
                    const Theme& theme) const
{
    // Box is a square sized by the rect height. Elevation lifts on hover,
    // settles on press -- same convention as Button.
    const float elevation = (hover_amount_ - press_amount_) * 1.5F;
    const float box_size  = rect_.h;
    const Rect  box       { rect_.x, rect_.y - elevation, box_size, box_size };
    Color box_col = blend_color(theme.surface,       theme.surface_hover, hover_amount_);
    box_col       = blend_color(box_col,             theme.surface_press, press_amount_);
    draw_solid_rect(batcher, box, box_col);

    if (value_)
    {
        // Inner fill (the visual "check") -- inset by 25% on each side.
        // Pad shrinks slightly with hover_amount so the check "grows" on hover.
        const float pad = box_size * (0.25F - hover_amount_ * 0.03F);
        const Rect  check { box.x + pad, box.y + pad,
                            box_size - 2.0F * pad, box_size - 2.0F * pad };
        draw_solid_rect(batcher, check, theme.accent);
    }

    // Label trails the box.
    if (font != nullptr && !label_.empty())
    {
        const float baseline = box.y + rect_.h - (rect_.h * 0.30F);
        const float x        = rect_.x + box_size + 6.0F;
        (void) draw_text_line(batcher, font, label_, x, baseline, theme.text);
    }

    if (focus_amount_ > 0.0F)
    {
        Color ring = theme.focus_ring;
        ring.a = static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(ring.a) * focus_amount_,
                       0.0F, 255.0F));
        draw_focus_ring(batcher, rect_, ring);
    }
}

// =========================================================================
// Dropdown
// =========================================================================

Dropdown::Dropdown(std::vector<std::string> options,
                   std::size_t selected,
                   ChangeCallback on_change)
    : options_(std::move(options))
    , selected_(options_.empty() ? 0U : std::min(selected, options_.size() - 1U))
    , on_change_(std::move(on_change))
{
}

void Dropdown::set_options(std::vector<std::string> opts)
{
    options_ = std::move(opts);
    if (options_.empty()) { selected_ = 0U; }
    else if (selected_ >= options_.size())
    {
        selected_ = options_.size() - 1U;
    }
}

void Dropdown::set_selected(std::size_t idx) noexcept
{
    if (options_.empty()) { selected_ = 0U; return; }
    selected_ = std::min(idx, options_.size() - 1U);
}

bool Dropdown::tick(const InputState& input)
{
    const bool inside_header = rect_.contains(
        input.pointer.mouse_x, input.pointer.mouse_y);
    state_.hovered = inside_header;
    state_.focused = input.focused;

    // Compute the expanded option list rect (anchored under the header).
    const float list_y = rect_.y + rect_.h;
    const float list_h = option_height_ * static_cast<float>(options_.size());
    const Rect  list   { rect_.x, list_y, rect_.w, list_h };

    // Reset the hover index every frame.
    hovered_option_ = static_cast<std::size_t>(-1);

    bool mutated = false;
    const std::size_t prev = selected_;

    if (expanded_ && list.is_valid() &&
        list.contains(input.pointer.mouse_x, input.pointer.mouse_y))
    {
        const float local_y = input.pointer.mouse_y - list_y;
        const auto idx = static_cast<std::size_t>(local_y / option_height_);
        if (idx < options_.size())
        {
            hovered_option_ = idx;
        }
    }

    if (input.pointer.left_pressed)
    {
        state_.pressed = inside_header;
    }

    if (input.pointer.left_released)
    {
        state_.pressed = false;

        if (expanded_)
        {
            // Releasing on a list option commits it; releasing on the
            // header collapses without changing the selection;
            // releasing outside also collapses.
            if (hovered_option_ != static_cast<std::size_t>(-1))
            {
                selected_ = hovered_option_;
                expanded_ = false;
            }
            else if (inside_header)
            {
                expanded_ = false;
            }
            else
            {
                expanded_ = false;
            }
        }
        else if (inside_header)
        {
            expanded_ = true;
        }
    }

    // Keyboard while focused: Up/Down moves selection without expanding;
    // Enter / Space toggles expansion; Escape collapses.
    if (input.focused && !options_.empty())
    {
        for (const KeyInput& key : input.keys)
        {
            if (key.signal == KeySignal::kUp && selected_ > 0U)
            {
                --selected_;
            }
            else if (key.signal == KeySignal::kDown &&
                     selected_ + 1U < options_.size())
            {
                ++selected_;
            }
            else if (key.signal == KeySignal::kEnter ||
                     key.signal == KeySignal::kSpace)
            {
                expanded_ = !expanded_;
            }
            else if (key.signal == KeySignal::kEscape)
            {
                expanded_ = false;
            }
        }
    }

    if (selected_ != prev)
    {
        mutated = true;
        if (on_change_) { on_change_(selected_); }
    }
    return mutated;
}

void Dropdown::draw(cd::ui::renderer::DrawBatcher& batcher,
                    cd::ui::font::Font* font,
                    const Theme& theme) const
{
    // Header bar.
    Color hdr = theme.surface;
    if (state_.pressed)      { hdr = theme.surface_press; }
    else if (state_.hovered) { hdr = theme.surface_hover; }
    draw_solid_rect(batcher, rect_, hdr);

    if (state_.focused)
    {
        draw_focus_ring(batcher, rect_, theme.focus_ring);
    }

    if (font != nullptr && selected_ < options_.size())
    {
        const float baseline = rect_.y + rect_.h - (rect_.h * 0.30F);
        const float x        = rect_.x + 8.0F;
        (void) draw_text_line(batcher, font, options_[selected_], x, baseline, theme.text);
    }

    if (!expanded_) { return; }

    // Expanded option list.
    const float list_y = rect_.y + rect_.h;
    for (std::size_t i = 0U; i < options_.size(); ++i)
    {
        const float opt_y = list_y + option_height_ * static_cast<float>(i);
        const Rect  opt   { rect_.x, opt_y, rect_.w, option_height_ };
        Color opt_bg = theme.surface;
        if (i == hovered_option_)  { opt_bg = theme.surface_hover; }
        if (i == selected_)        { opt_bg = theme.accent; }
        draw_solid_rect(batcher, opt, opt_bg);

        if (font != nullptr)
        {
            const float baseline = opt_y + option_height_ - (option_height_ * 0.30F);
            const float x        = rect_.x + 8.0F;
            (void) draw_text_line(batcher, font, options_[i], x, baseline, theme.text);
        }
    }
}

// =========================================================================
// Modal
// =========================================================================

Modal::Modal(bool visible, CloseCallback on_close)
    : visible_(visible)
    , on_close_(std::move(on_close))
{
}

bool Modal::tick(const InputState& input)
{
    if (!visible_)
    {
        state_ = {};
        return false;
    }

    const bool inside_content = content_rect_.contains(
        input.pointer.mouse_x, input.pointer.mouse_y);
    const bool inside_dim = rect_.contains(
        input.pointer.mouse_x, input.pointer.mouse_y);

    state_.hovered = inside_content;
    state_.focused = input.focused;
    if (input.pointer.left_pressed)
    {
        state_.pressed = inside_content;
    }
    if (input.pointer.left_released)
    {
        state_.pressed = false;
    }

    bool closed = false;

    // Escape while focused closes.
    if (input.focused)
    {
        for (const KeyInput& key : input.keys)
        {
            if (key.signal == KeySignal::kEscape)
            {
                closed = true;
                break;
            }
        }
    }

    // Click on the dim layer (outside the content panel) also closes.
    if (!closed && input.pointer.left_released &&
        inside_dim && !inside_content)
    {
        closed = true;
    }

    if (closed)
    {
        visible_ = false;
        if (on_close_) { on_close_(); }
    }
    return closed;
}

void Modal::draw(cd::ui::renderer::DrawBatcher& batcher,
                 cd::ui::font::Font* font,
                 const Theme& theme) const
{
    (void) font;
    if (!visible_) { return; }
    // Full-bleed dim layer.
    draw_solid_rect(batcher, rect_, theme.dim_overlay);
    // Content panel.
    draw_solid_rect(batcher, content_rect_, theme.surface);
    if (state_.focused)
    {
        draw_focus_ring(batcher, content_rect_, theme.focus_ring);
    }
}

}  // namespace cd::ui::widgets
