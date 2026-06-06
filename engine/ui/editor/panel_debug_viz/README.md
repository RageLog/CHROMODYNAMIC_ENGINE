# cd::editor::debug_viz

Three toggleable ~200×150 px **render-debug thumbnails** the editor
shows in the top-right corner:

| Mode          | Source                                                |
|---------------|-------------------------------------------------------|
| `kDepth`      | Grayscale G-buffer depth (closer = brighter).         |
| `kNormal`     | World-normals as RGB (`xyz * 0.5 + 0.5`).             |
| `kAlphaBucket`| Per-pixel render-bucket colour (green / yellow / red).|

**Moment**: a render dev glances at the three thumbnails, spots that
the curtain is `kAlphaBlend` (red) → framegraph order confirmed →
the bug narrows elsewhere.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase684 / M13 W6A` | Placeholder coloured-gradient tiles. |
| 2 | queued     | Real G-buffer sampling once `cd::render::framegraph` exposes depth / normal / bucket textures. |

The Sprint-1 placeholder lets the editor wire the panel into the
dockspace + toggle UI *before* the rhi-side framegraph plumbing
lands. Toggling modes flips through the placeholder tints; the
panel-side state machine + draw path is locked.

## API

```cpp
namespace cd::editor::debug_viz {

enum class Mode : uint8_t { kDepth, kNormal, kAlphaBucket };

class DebugVizOverlay
{
public:
    void                          set_visible(bool);
    void                          set_mode(Mode);
    [[nodiscard]] bool            is_visible() const noexcept;
    [[nodiscard]] Mode            current_mode() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

The `draw()` ignores the supplied `Rect` and pins the thumbnails
to the top-right corner of the viewport — a deliberate decision so
the editor doesn't have to reserve dock space for a debug-only
surface. If a dev wants a dockable version, they wrap the overlay
in a regular panel; the overlay path is the "always-on glanceable"
flavour.

## Bucket colour convention

```cpp
green   — kOpaque
yellow  — kAlphaTest (cutout vegetation, decals)
red     — kAlphaBlend (curtains, particles)
```

Matches the `cd::render::framegraph::Bucket` enum colour table so the
overlay and the framegraph profiler agree at a glance.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.

`cd::render` / `cd::rhi` are **not** linked yet (Sprint-1 is
placeholder-only). Sprint-2 will add `cd::render::framegraph` as a
PUBLIC dep when the texture handle accessors land.
