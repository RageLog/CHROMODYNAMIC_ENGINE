# cd::editor::panel::asset_pipeline_status

Four horizontal bar charts — scene / texture / audio / shader — each
showing **pending vs completed** counts in real time. Drives a
`cd::asset::streamer_pool::StreamerPool` snapshot every frame so the
dev sees the streaming queue depth across all four asset domains
without leaving the editor.

**Moment**: a dev watches the pipeline panel during scene-load,
spots the texture streamer at `pending=200` vs `scene=50`, and tunes
priorities before the next PlayTest without touching the terminal.

## API

```cpp
namespace cd::editor::panel::asset_pipeline_status {

struct PipelineSnapshot
{
    uint32_t   scene_pending,    scene_completed;
    uint32_t   texture_pending,  texture_completed;
    uint32_t   audio_pending,    audio_completed;
    uint32_t   shader_pending,   shader_completed;
};

class AssetPipelineStatus
{
public:
    void                          set_snapshot(PipelineSnapshot);
    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌────────────────────────────────────────────┐
  │ Asset Pipeline                             │
  │ ─────────────────────────────────────────  │
  │ Scene    [▓▓▓░░░░░░░] 12 pending /  48 done│
  │ Texture  [▓▓▓▓▓▓▓░░░] 56 pending /  91 done│
  │ Audio    [▓░░░░░░░░░]  3 pending / 122 done│
  │ Shader   [▓▓░░░░░░░░]  8 pending / 219 done│
  └────────────────────────────────────────────┘
```

Each row is `[bar_normalised pending / completed]`. The bar normalises
`pending / (pending + completed)` so the chart visually answers "how
much of this domain is still unfinished?" in a glance. The text
shows the raw counts so the dev can read absolutes.

## Caller wiring

```cpp
StreamerPool pool(/* ... */);
AssetPipelineStatus panel;

per_frame([&] {
    auto p = pool.pending();
    panel.set_snapshot({
        .scene_pending   = p.scene,
        .scene_completed = scene_done_counter,
        /* ... */
    });
    panel.draw(batcher, theme, panel_bounds);
});
```

The `*_completed` counters are caller-maintained — the streamer pool
itself doesn't track lifetime completion (its job is only to dispatch
the next tick's slice). Editors typically run a simple frame-local
counter that increments per `poll_complete`.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::asset_streamer_pool` — `PoolConfig` + `PendingCounts` shape
  (PUBLIC).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
