// =============================================================================
// CHROMODYNAMIC -- cd/ui/a11y/A11y.hpp
//
// Phase 478 / ADR-20260530 Phase 4.4 -- accessibility baseline for the
// cd::ui retained widget tree.
//
// Scope (this header):
//   * Role enum            -- semantic widget role (Button / Slider / ...)
//                             aligned with W3C ARIA roles (subset) so a
//                             future screen-reader bridge can map 1:1.
//   * A11yMeta             -- per-widget metadata: role, label, hint, focused
//   * A11yTree             -- parallel tree mirroring cd::ui::Widget by id,
//                             carrying A11yMeta + explicit tab order.
//   * focus_indicator_rect -- 2px outset of a widget's bounds, used by the
//                             renderer to draw the "focus ring".
//   * compute_contrast_ratio(fg, bg) -- WCAG 2.1 contrast ratio in [1..21].
//   * ThemeVariant         -- kStandardTheme vs k_high_contrast_theme; the
//                             min_required_contrast()/passes_contrast()
//                             helpers enforce AA vs AAA on the variant.
//
// Design notes:
//   * The tree is **parallel** -- it does NOT own widgets. The widget tree
//     stays cd::ui::Widget; the a11y tree stores (parent_id -> children_ids)
//     adjacency plus per-id A11yMeta, plus a tab-order list. This keeps
//     cd::ui free of accessibility concerns and lets headless tooling
//     (a11y auditors, screen-reader bridges, snapshot tests) build the
//     a11y model without instantiating widgets.
//   * Tab order is explicit: register_tab_order(span<WidgetId>) replaces
//     the visual / z-order so visually-decorative reordering (e.g. RTL
//     mirroring) does not break keyboard navigation.
//   * Contrast checker re-derives the formula here rather than depending
//     on cd::ui::theme so the a11y library can live without dragging in
//     the token system (audit-only tooling, no theming).
//
// Dependencies: cd::core, cd::ui (Widget for bounds + WidgetId).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/Widget.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::ui::a11y
{

// ---- Role ------------------------------------------------------------------
//
// Semantic widget role -- mirrors a subset of the W3C ARIA role list
// (https://www.w3.org/TR/wai-aria-1.2/#role_definitions). Names are
// platform-neutral so the same enum drives the Win32 UIA bridge, the
// macOS NSAccessibility bridge, and the AT-SPI Linux bridge that the
// future cd::ui::a11y_bridge libraries will host.
enum class Role : std::uint8_t
{
    kUnknown   = 0,
    kButton    = 1,
    kLabel     = 2,
    kTextInput = 3,
    kSlider    = 4,
    kCheckbox  = 5,
    kRadio     = 6,
    kComboBox  = 7,
    kTab       = 8,
    kTabList   = 9,
    kPanel     = 10,
    kMenu      = 11,
    kMenuItem  = 12,
    kDialog    = 13,
    kProgress  = 14,
    kImage     = 15,
    kLink      = 16,
};

// ---- A11yMeta --------------------------------------------------------------
//
// Per-widget accessibility metadata. `label` is the short, user-visible
// name (e.g. "Save"); `hint` is the longer description a screen reader
// reads after the label (e.g. "Save current scene to disk, Ctrl+S").
// `focused` is the model bit owned by the a11y tree -- the widget
// itself does not carry a focus flag because focus is a tree-global
// invariant (exactly one focused widget at any time).
struct A11yMeta
{
    Role        role    { Role::kUnknown };
    std::string label   {};
    std::string hint    {};
    bool        focused { false };

    friend bool operator==(const A11yMeta&, const A11yMeta&) = default;
};

// ---- Contrast --------------------------------------------------------------
//
// Re-derived here (not via cd::ui::theme) so the a11y library has a
// minimal dependency surface. RGBA in [0..1], assumed already-linear
// (callers wanting sRGB should pre-linearise). Returns Rec. 709
// luminance Y' = 0.2126 R + 0.7152 G + 0.0722 B.
struct Rgba
{
    float r { 0.0F };
    float g { 0.0F };
    float b { 0.0F };
    float a { 1.0F };

    friend bool operator==(Rgba, Rgba) noexcept = default;
};

[[nodiscard]] constexpr float relative_luminance(Rgba c) noexcept
{
    return (0.2126F * c.r) + (0.7152F * c.g) + (0.0722F * c.b);
}

// WCAG 2.1 contrast ratio between two colours: (L_lighter + 0.05) /
// (L_darker + 0.05). Range [1..21]. Pure white-on-black evaluates to
// 21:1; identical colours evaluate to 1:1.
[[nodiscard]] constexpr float compute_contrast_ratio(Rgba fg, Rgba bg) noexcept
{
    const float lf = relative_luminance(fg);
    const float lb = relative_luminance(bg);
    const float lighter = (lf > lb) ? lf : lb;
    const float darker  = (lf > lb) ? lb : lf;
    return (lighter + 0.05F) / (darker + 0.05F);
}

// ---- Theme variant + enforcement ------------------------------------------
//
// kStandardTheme expects WCAG 2.1 AA body text (>=4.5); k_high_contrast_theme
// expects AAA (>=7). Large-text and non-text targets have laxer cut-offs;
// this library focuses on body text since editor / HUD UIs are
// overwhelmingly small text.
enum class ThemeVariant : std::uint8_t
{
    kStandardTheme    = 0,
    k_high_contrast_theme = 1,
};

[[nodiscard]] constexpr float min_required_contrast(ThemeVariant v) noexcept
{
    return (v == ThemeVariant::k_high_contrast_theme) ? 7.0F : 4.5F;
}

[[nodiscard]] constexpr bool passes_contrast(Rgba fg, Rgba bg, ThemeVariant v) noexcept
{
    return compute_contrast_ratio(fg, bg) >= min_required_contrast(v);
}

// ---- Focus indicator -------------------------------------------------------
//
// 2px outset of the widget rect on every side -- matches the WCAG 2.4.11
// "non-text contrast" focus-visible guidance ("at least as large as a
// 2 CSS pixel solid line around the unfocused control"). The renderer
// draws a stroke into the returned rect so the focus ring is visually
// distinct from the widget's own border.
inline constexpr float kFocusIndicatorOutsetPx = 2.0F;

[[nodiscard]] constexpr Rect focus_indicator_rect(Rect bounds) noexcept
{
    return Rect {
        bounds.x - kFocusIndicatorOutsetPx,
        bounds.y - kFocusIndicatorOutsetPx,
        bounds.w + (2.0F * kFocusIndicatorOutsetPx),
        bounds.h + (2.0F * kFocusIndicatorOutsetPx),
    };
}

// ---- A11yTree --------------------------------------------------------------
//
// Parallel accessibility model. Indexed by cd::ui::WidgetId; does not
// own widgets, so the same tree can be torn down and re-registered as
// the visual tree mutates (typical retained-mode refresh).
class A11yTree
{
public:
    A11yTree() = default;
    ~A11yTree() = default;
    A11yTree(const A11yTree&) = delete;
    A11yTree& operator=(const A11yTree&) = delete;
    A11yTree(A11yTree&&) noexcept = default;
    A11yTree& operator=(A11yTree&&) noexcept = default;

    /// Register (or overwrite) the a11y meta for `widget_id`. Idempotent:
    /// re-registering the same id replaces the previous meta. The widget
    /// itself is NOT stored; bounds are looked up by the caller and
    /// passed to focus_indicator_rect() at query time.
    void register_widget(WidgetId widget_id, A11yMeta meta);

    /// Remove a widget from the tree. No-op if `widget_id` is not known.
    void unregister_widget(WidgetId widget_id);

    /// Lookup. Missing ids return a default-constructed A11yMeta (role =
    /// kUnknown, empty label/hint, focused = false) so callers can probe
    /// without first checking has().
    [[nodiscard]] A11yMeta meta(WidgetId widget_id) const;
    [[nodiscard]] bool     has(WidgetId widget_id) const noexcept;

    /// Number of registered widgets. Used by tests + a11y auditors that
    /// want to walk the entire model.
    [[nodiscard]] std::size_t size() const noexcept { return metas_.size(); }

    /// Replace the tab-navigation order. Widgets are visited in the
    /// supplied order regardless of their visual / z-order. Unknown ids
    /// are silently ignored at navigation time so callers can pre-bake a
    /// stable order before all widgets are registered.
    void set_tab_order(std::span<const WidgetId> order);

    /// Read-only access to the current tab order. Empty if no order has
    /// been set.
    [[nodiscard]] std::span<const WidgetId> tab_order() const noexcept
    {
        return { tab_order_.data(), tab_order_.size() };
    }

    /// Set the focused widget. Clears the previous focused widget's bit
    /// (if any). Pass nullopt to clear focus.
    void set_focus(std::optional<WidgetId> widget_id);

    /// Currently focused widget, if any.
    [[nodiscard]] std::optional<WidgetId> focus() const noexcept
    {
        return focused_;
    }

    /// Advance focus to the next entry in tab_order(). Wraps around at
    /// the end. No-op if tab_order() is empty. If no widget is currently
    /// focused, focuses the first entry.
    void focus_next();

    /// Walk to the previous entry in tab_order() (Shift+Tab semantics).
    /// Wraps around at the start.
    void focus_prev();

    /// Screen-reader narration accessor: returns the hint string of the
    /// given widget (or empty if not registered). Bridges to NSAccessibility
    /// / UIA / AT-SPI read from here.
    [[nodiscard]] std::string_view screen_reader_hint(WidgetId widget_id) const;

private:
    std::unordered_map<WidgetId, A11yMeta> metas_ {};
    std::vector<WidgetId>                  tab_order_ {};
    std::optional<WidgetId>                focused_ {};
};

}  // namespace cd::ui::a11y
