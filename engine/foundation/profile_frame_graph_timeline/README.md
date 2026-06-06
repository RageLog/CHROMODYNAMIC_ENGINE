# cd::profile::frame_graph_timeline

GPU-side **Gantt-chart timeline** overlay. Each GPU pass that the
renderer issues becomes a coloured bar on a horizontal time axis.
Companion library to `cd::profile::cpu_marker_overlay` (CPU
bar-chart) — the two together produce a Tracy-style CPU+GPU
profile view.

The library separates two concerns:

| Component        | Role                                              |
|------------------|---------------------------------------------------|
| `Timeline`       | Per-frame GPU pass accumulator + double-buffer.   |
| `TimelineOverlay`| Reader-side renderer: horizontal Gantt into `DrawBatcher`. |

`cd::ui_renderer` is **PRIVATE** — `DrawBatcher` appears only inside
`TimelineOverlay::draw()` in the `.cpp`. Consumers include
`FrameGraphTimeline.hpp` and do **not** pull in the full ui renderer
headers.

## Public surface

```cpp
namespace cd::profile::frame_graph_timeline {

struct PassSample
{
    uint64_t    start_ns;            // GPU timestamp converted to ns
    uint64_t    end_ns;
    uint32_t    queue_id;            // 0 = graphics, 1 = compute, 2 = transfer
    uint32_t    colour_rgba;
    const char* name;                // string-literal-only; no copy
};

class Timeline                       // double-buffered for race-free read
{
public:
    void                                begin_frame();
    void                                push(PassSample);
    void                                end_frame();
    [[nodiscard]] std::span<const PassSample> snapshot() const;   // last frame
};

class TimelineOverlay
{
public:
    void                                draw(cd::ui::renderer::DrawBatcher& batcher,
                                             const Timeline&                source,
                                             /* … */);
};

}
```

## Lifecycle

```
  begin_frame()  — swap buffers; the read side now sees the previous
                   frame; the write side starts fresh.
  push(sample)   — call once per GPU pass when its timestamp readback
                   resolves. Caller maps GPU timestamps to ns via the
                   rhi's TimestampQueryPool.
  end_frame()    — finalise this frame's buffer for next-frame readers.
  snapshot()     — overlay reader; returns the buffer that
                   begin_frame just swapped IN, i.e. last completed
                   frame's passes.
```

This double-buffer pattern means the overlay always reads a fully
populated frame even when the GPU is still building the current
frame's timestamp readback chain.

## Queue colouring

```cpp
queue_id  conventional colour      meaning
────────  ─────────────────        ──────────
   0      cyan (graphics)          opaque scene + shadow passes
   1      magenta (compute)        light-cull / GTAO / bloom / clusters
   2      yellow (transfer)        staging buffer copies, BLAS builds
```

The library does not enforce these; `PassSample::colour_rgba` is
free-form. Renderers conventionally use the table above so eyes can
parse a screenful of bars in a glance.

## Thread-safety

* `Timeline::push` is called from the **render thread only** —
  GPU-pass timestamp resolution is single-threaded by RHI design.
* `Timeline::snapshot` is callable from the overlay-drawing thread
  (typically the same render thread); the double-buffer guarantees
  no torn reads.

## See also

* `cd::profile::cpu_marker_overlay` — CPU-side bar-chart overlay.
* `cd::rhi::TimestampQueryPool` — GPU timestamp readback chain.
