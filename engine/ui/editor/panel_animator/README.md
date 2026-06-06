# cd::editor::panel::animator

Animation clip browser + timeline scrubber panel. Lists the registered
`ClipId` entries with the active clip highlighted, draws a normalised
playback-time scrubber bar, and shows play/pause + loop toggle
indicator strips. Renders into `cd::ui::renderer::DrawBatcher`.

This is a **view-only** panel — it surfaces playback state. Causing
state changes (play/pause/seek) is the caller's responsibility through
whatever animation backend feeds the panel.

## API

```cpp
namespace cd::editor::panel::animator {

using ClipId = std::uint32_t;

class Animator
{
public:
    void                          register_clip(ClipId, std::string display_name);
    void                          set_clip(ClipId);              // mark active
    void                          set_time(float seconds);
    void                          set_playing(bool);
    void                          set_looping(bool);

    [[nodiscard]] std::size_t     clip_count() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

`draw()` partitions the supplied `Rect` into a left clip-list panel
and a right scrubber+state strip:

```
  ┌──────────────────┬──────────────────────────────────────┐
  │ Clips            │  ► / ⏸ + Loop  (state strip)         │
  │ ─────────────    │ ─────────────────────────────────── │
  │ idle             │  [|------●--------------]  (scrubber)│
  │ walk    ◄ active │  t = 1.23 s / 4.05 s                │
  │ run              │                                      │
  │ jump             │                                      │
  └──────────────────┴──────────────────────────────────────┘
```

The active clip row is highlighted with `theme.accent`; non-active
rows use `theme.text_dim`. The scrubber playhead is a vertical bar
at `time / duration` along the strip.

## Lifecycle pattern

Typical wiring at the editor / play-mode boundary:

```cpp
Animator panel;
for (auto& clip : skeleton.clips)
    panel.register_clip(clip.id, clip.name);

per_frame([&] {
    panel.set_clip(player.current_clip);
    panel.set_time(player.current_time);
    panel.set_playing(player.is_playing());
    panel.set_looping(player.is_looping());

    panel.draw(batcher, theme, panel_bounds);
});
```

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.

`cd::game::anim_graph` is **NOT** linked — the panel reads only
generic `ClipId`-keyed state. Wire whichever animation backend
feeds it from the caller.
