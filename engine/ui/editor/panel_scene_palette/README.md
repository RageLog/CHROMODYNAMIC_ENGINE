# cd::editor::panel::scene_palette

**Live theme-token preview** panel. A grid of labelled colour swatches,
one per semantic token in `cd::ui::widgets::Theme`. Click any swatch
to record its token name (a future Sprint-2 wires this to
copy-to-clipboard so a designer can paste the token name into a
code editor).

**Moment**: a UI designer sees all 15 palette tokens at-a-glance with
actual colour swatches — not memorised hex values. Theme reskins
are validated visually in seconds.

## Tokens displayed

```
  background         surface           surface_subtle
  surface_hover      surface_press     divider
  text               text_dim          dim_overlay
  accent             accent_hover      focus_ring
  accent_warning     accent_error      accent_success
```

15 tokens in 3 columns × 5 rows (default). The token set follows the
`cd::ui::widgets::Theme` schema in lockstep — when the theme adds a
new token the panel picks it up via the swatch enumerator without
panel-side code changes (the registry walks every member of the
`Theme` struct via reflection-style helper macros).

## API

```cpp
namespace cd::editor::panel::scene_palette {

class ScenePalette
{
public:
    void                          set_theme(cd::ui::widgets::Theme);
    [[nodiscard]] std::optional<std::string>
                                  clicked_token() const noexcept;   // last click

    void                          simulate_click(cd::math::Vec2f screen_xy);

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌─────────────────────────────────────────────────────────────┐
  │  Scene Palette                                              │
  ├─────────────────────────────────────────────────────────────┤
  │  ▓▓▓ background          ▓▓▓ surface           ▓▓▓ surface_subtle │
  │  ▓▓▓ surface_hover       ▓▓▓ surface_press     ▓▓▓ divider        │
  │  ▓▓▓ text                ▓▓▓ text_dim          ▓▓▓ dim_overlay    │
  │  ▓▓▓ accent              ▓▓▓ accent_hover      ▓▓▓ focus_ring     │
  │  ▓▓▓ accent_warning      ▓▓▓ accent_error      ▓▓▓ accent_success │
  └─────────────────────────────────────────────────────────────┘
```

Each row contains three labelled swatches. The swatch is painted in
its token colour with a 1-px border in `theme.text_dim` for contrast
against very light or very dark palettes. The token name follows the
swatch — small enough to read but not the focal element.

## Click selection

`simulate_click(screen_xy)` (or, Sprint-2, a real mouse hit-test)
walks the swatch grid and records the hit token name in
`clicked_token()`. The editor exposes this via a copy-to-clipboard
shortcut so the designer's workflow is:

1. Find the colour in the panel.
2. Click the swatch.
3. `Ctrl+C` → token name on clipboard.
4. Paste into the code editor at the call site.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect` (PUBLIC).
