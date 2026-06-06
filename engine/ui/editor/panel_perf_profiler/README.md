# cd::editor::panel::perf_profiler

Deep frame-by-frame **performance profiler** panel. Three-section
in-editor view that surfaces a 60-frame rolling bar chart, a CPU + GPU
drill-down for the selected frame, and a stats strip across the
history window.

**Moment**: a perf-sensitive dev captures 60 frames, sees a spike at
frame 23, drills into it, finds the `TLAS rebuild` GPU pass at 12 ms —
root cause spotted in seconds without leaving the editor.

## Three-section layout

```
  ┌────────────────────────────────────────────────────────────┐
  │ A. 60-frame rolling bar chart                              │
  │    ▒▒▒░░░▒▒▓▓░░▓▓▓▓█▒░▒▒▒░░░▒▓▒░░▒░░▒▓▓▒░░▒▒░▒▓▒░░▒░▒▒░▒  │
  │    green = under budget, red = over budget                 │
  ├────────────────────────────────────────────────────────────┤
  │ B. Drill-down for selected frame                           │
  │    CPU markers (thread × scope timeline)                   │
  │    ──────                                                  │
  │    GPU pass records (queue × pass timeline)                │
  ├────────────────────────────────────────────────────────────┤
  │ C. Stats strip:  avg 12.4 ms  min 8.1 ms                  │
  │                  max 18.7 ms  p99 17.3 ms                 │
  └────────────────────────────────────────────────────────────┘
```

## API

```cpp
namespace cd::editor::panel::perf_profiler {

struct FrameSample
{
    uint32_t   frame_index;
    float      total_ms;
    /* CPU + GPU sample arrays attached out-of-band via setters */
};

class PerfProfiler
{
public:
    void                          push_frame(FrameSample);
    void                          set_budget_ms(float);          // bar chart colour gate
    void                          set_history_size(std::size_t); // default 60

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&);

    // Selection persists across draws.
    void                          select_frame(uint32_t frame_index);
    [[nodiscard]] std::optional<uint32_t> selected_frame() const noexcept;
};

}
```

## CPU / GPU drill-down sources

The drill-down section reads from two companion libraries:

* `cd::profile::cpu_marker_overlay` — CPU `MarkerSample`s grouped by
  thread.
* `cd::profile::frame_graph_timeline` — GPU `PassSample`s grouped by
  queue.

These are weak refs — the profiler panel **does not own** the
collectors; the editor wires them through accessor lambdas passed to
`draw()`. This avoids a hard dependency from this panel onto the GPU
queue plumbing.

## Stats strip

The strip computes:

* `avg`: arithmetic mean of `total_ms` over the history window.
* `min` / `max`: extrema of `total_ms`.
* `p99`: 99th-percentile via `std::nth_element` on a fixed copy each
  draw (history size 60 → ~7 comparisons; negligible).

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.

`cd::profile::cpu_marker_overlay` / `cd::profile::frame_graph_timeline`
are **not** direct deps — the panel takes their snapshot data through
generic `std::span` accessors at `draw()` time.
