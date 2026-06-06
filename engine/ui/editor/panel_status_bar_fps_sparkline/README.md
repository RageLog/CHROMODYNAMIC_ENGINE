# cd::editor::status_bar::fps_sparkline

FPS sparkline status-bar widget. A ring buffer of **120 per-frame
times**, rendered as a bottom-anchored thin-quad waveform 200 px
wide. Colour-coded by the rolling average frame time:

| Average                | Colour                    |
|------------------------|---------------------------|
| `< 16.67 ms` (~60 fps) | `accent_success` (green)  |
| `< 33.33 ms` (~30 fps) | `accent_warning` (amber)  |
| `≥ 33.33 ms`           | `accent_error`   (red)    |

**Moment**: FPS history at a glance — a dev spots a hitch in the
sparkline tail without opening the profiler.

## API

```cpp
namespace cd::editor::status_bar::fps_sparkline {

class FpsSparkline
{
public:
    void                          push_frame_time(float ms);
    [[nodiscard]] float           rolling_average_ms() const noexcept;
    [[nodiscard]] std::size_t     sample_count() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
                                                       ┌── 200 px ──┐
  ┌──────────────────────────────────────────────────────────────────┐
  │ … status-bar content …            13.2 ms (60+ fps)  ▁▂▃▂▄▅▆▅▄▃ │
  └──────────────────────────────────────────────────────────────────┘
                                       ▲                  ▲
                                       avg + colour text  120-sample waveform
```

The waveform is **always anchored to the right of the strip** — the
oldest sample is on the left, the most recent on the right (the
canonical sparkline reading direction).

## Ring-buffer semantics

`push_frame_time(ms)` writes into a circular buffer of length 120.
When full, oldest samples are overwritten. Two sparklines on the
same status bar (CPU + GPU, e.g.) each own their own ring; no shared
state.

The 120-sample window matches the `cd::profile::cpu_marker_overlay`
default snapshot size (120-frame rolling window at 60 fps = 2 seconds
of history) so the two visualisations agree on scale.

## Colour band thresholds

The thresholds `16.67 ms` and `33.33 ms` are hard-coded as the
canonical 60-fps and 30-fps boundaries. If a game ships with a
different target (120 Hz hand-held, 144 Hz competitive), the thresholds
can be exposed at the API level — queued under
`L-fps-sparkline-thresholds` and currently not in scope.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
