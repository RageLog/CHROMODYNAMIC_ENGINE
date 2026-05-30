# cd::ui_a11y

**Purpose**: Accessibility baseline for the `cd::ui` retained widget tree. Phase 4.4 of `ADR-20260530-ui-widget-library`.

**Namespace**: `cd::ui::a11y`.

**Headers**: `cd/ui/a11y/A11y.hpp`.

**Dependencies**: `cd::core`, `cd::ui` (for `Widget` / `Rect` / `WidgetId`). Deliberately **not** `cd::ui_theme` -- the WCAG contrast formula is duplicated here so headless / audit-only tooling can pull a11y without dragging the token system in.

## Why a parallel tree?

The accessibility model is stored **separately** from the widget tree:

- The widget tree (`cd::ui::Widget`) stays free of a11y concerns -- no `role`, `label`, `hint` fields leak into core widgets.
- The a11y tree (`cd::ui::a11y::A11yTree`) is an `unordered_map<WidgetId, A11yMeta>` plus an explicit tab order. It does **not** own widgets.
- Bridges (NSAccessibility / UIA / AT-SPI) and snapshot tests can build the a11y model in headless mode without instantiating widgets.
- Retained-mode refresh: the visual tree can be torn down and rebuilt; re-register with the same `WidgetId` to overwrite a meta entry idempotently.

## Primary types

- `cd::ui::a11y::Role` -- semantic widget role, subset of the W3C ARIA roles: `kButton / kLabel / kTextInput / kSlider / kCheckbox / kRadio / kComboBox / kTab / kTabList / kPanel / kMenu / kMenuItem / kDialog / kProgress / kImage / kLink`. Stable `uint8_t` values pinned by tests.
- `cd::ui::a11y::A11yMeta` -- `{ Role role; std::string label; std::string hint; bool focused; }`.
- `cd::ui::a11y::A11yTree` -- the parallel tree:
  - `register_widget(WidgetId, A11yMeta)` -- idempotent overwrite.
  - `unregister_widget(WidgetId)` -- removes meta + tab-order entry; clears focus if it was on the dropped widget.
  - `meta(WidgetId)` -- returns default `A11yMeta` on miss (no `optional` ceremony at the call site).
  - `set_tab_order(span<WidgetId>)` -- explicit tab order; visual / z-order is ignored.
  - `focus_next() / focus_prev()` -- wrap-around Tab / Shift+Tab navigation.
  - `set_focus(optional<WidgetId>)` -- direct focus assignment; flips the per-meta `focused` bit on the previous and new entries.
  - `screen_reader_hint(WidgetId)` -- exposes the hint string for bridge consumers.

## Focus indicator

```cpp
constexpr float kFocusIndicatorOutsetPx = 2.0F;
constexpr cd::ui::Rect focus_indicator_rect(cd::ui::Rect bounds) noexcept;
```

Returns a 2px outset rect on every side, matching WCAG 2.4.11 "non-text contrast" focus-visible guidance ("at least as large as a 2 CSS pixel solid line around the unfocused control"). The renderer draws a stroke into the returned rect.

## Contrast checker

```cpp
constexpr float compute_contrast_ratio(Rgba fg, Rgba bg) noexcept;     // WCAG 2.1, [1..21]
constexpr bool  passes_contrast(Rgba fg, Rgba bg, ThemeVariant) noexcept;
```

- `compute_contrast_ratio` is the WCAG 2.1 formula `(L_lighter + 0.05) / (L_darker + 0.05)` with `L = 0.2126R + 0.7152G + 0.0722B` (Rec. 709). Inputs assumed already-linear.
- `passes_contrast` enforces the per-variant threshold: AA (>=4.5) for `kStandardTheme`, AAA (>=7) for `kHighContrastTheme`.
- Pure white-on-black evaluates to **21:1** -- the canonical AAA maximum.

## Usage

```cpp
#include <cd/ui/a11y/A11y.hpp>

namespace a11y = cd::ui::a11y;

a11y::A11yTree tree;
tree.register_widget(save_button.id(),
                     { a11y::Role::kButton, "Save", "Save scene (Ctrl+S)", false });
tree.register_widget(volume_slider.id(),
                     { a11y::Role::kSlider, "Volume", "Master output, 0-100%", false });

// Explicit tab order -- not visual order.
const std::array<cd::ui::WidgetId, 2> order { save_button.id(), volume_slider.id() };
tree.set_tab_order(order);

// First Tab focuses the first entry.
tree.focus_next();

// Renderer queries the focus ring for whichever widget is currently focused.
if (const auto fid = tree.focus())
{
    const cd::ui::Rect ring = a11y::focus_indicator_rect(widget_lookup(*fid).bounds());
    renderer.draw_focus_ring(ring);
}
```

## Test command

`ctest --preset ninja-debug -R cd_test_a11y --output-on-failure`. 11 cases covering: WCAG formula (white-on-black = 21:1, identity = 1:1, symmetric), AAA enforcement on the high-contrast variant, 2px focus-indicator outset, `A11yMeta` round-trip lookup + idempotent re-registration + unregister, role enum stable values, tab-order independence from visual order with wrap-around in both directions, screen-reader hint exposure, missing-widget default contract, AA threshold on the standard variant, focus-bit tracking via `set_focus`, and `Widget.bounds()` integration.

## Out of scope (Phase 4.5+)

- Live screen-reader bridges (`cd::ui::a11y_bridge_uia`, `_nsa11y`, `_atspi`) -- platform-specific marshalling lives in separate libraries that depend on this one.
- Live region / announcement queue -- the ARIA `aria-live` analogue is a Phase 5 deliverable.
- High-contrast theme palette itself -- defined in `cd::ui_theme` as `kHighContrastTheme()`; this library only enforces the contrast contract.
- Per-state focus styling (focus-visible vs focus-within) -- single boolean for now; deferred until the widget catalog settles on interaction states.
