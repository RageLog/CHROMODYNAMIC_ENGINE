# cd::profile::cpu_marker_overlay

Tracy-style **CPU marker bar-chart overlay**. The classic
"colour-tagged timeline of named scopes per thread" view, drawn into
`cd::ui::renderer::DrawBatcher` as coloured quads. Companion library
to `cd::profile::frame_graph_timeline` (GPU Gantt chart).

The library separates three concerns:

| Component | Role                                                |
|-----------|-----------------------------------------------------|
| `Collector` | Ring-buffer of `MarkerSample` records (push from any thread). |
| `Scope`     | RAII helper: start on construction, end on destruction. |
| `Overlay`   | Reader-side renderer: emits coloured quads into a `DrawBatcher`. |

`cd::ui_renderer` is a **PRIVATE** dep — `DrawBatcher` is only
mentioned in `Overlay::draw()` in the `.cpp`. Consumers of this
library include `CpuMarkerOverlay.hpp` and do **not** transitively
pull in the full ui renderer headers.

## Public surface

```cpp
namespace cd::profile::cpu_marker_overlay {

struct MarkerSample
{
    uint64_t    start_ns;            // monotonic
    uint64_t    end_ns;
    uint32_t    thread_id;
    uint32_t    colour_rgba;
    const char* name;                // string-literal-only; no copy
};

class Collector
{
public:
    void                                push(MarkerSample);
    [[nodiscard]] std::span<const MarkerSample> snapshot() const;
    void                                clear();
};

class Scope                          // RAII
{
public:
    Scope(Collector& sink, const char* name, uint32_t colour_rgba);
    ~Scope();                        // pushes a MarkerSample on destruction
};

class Overlay
{
public:
    void                                draw(cd::ui::renderer::DrawBatcher& batcher,
                                             const Collector&               source,
                                             /* … */);
};

}
```

## RAII pattern

```cpp
void HotPath::tick()
{
    cd::profile::cpu_marker_overlay::Scope
        _scope(g_collector, "HotPath::tick", 0xFFD37F00 /* orange */);

    // … work …
}   // ~Scope() pushes the (start_ns, end_ns) sample
```

Per-thread `Collector` instances avoid contention. The `Overlay`
reader iterates each one's `snapshot()` separately and arranges
rows top-to-bottom by `thread_id`.

## Thread-safety

* `Collector::push` is callable from any thread; the implementation
  is a lock-free ring write (single-producer if you use one
  collector per thread, MPSC if shared).
* `Collector::snapshot` returns a stable view; the reader holds the
  view across one `Overlay::draw` call. Concurrent producers continue
  to push behind the snapshot — they appear in the next frame.
* `Overlay::draw` is a render-thread call only; the `DrawBatcher`
  it writes into is single-threaded by contract.

## See also

* `cd::profile::frame_graph_timeline` — GPU-side Gantt overlay.
* Tracy Profiler — the visual reference this library reproduces in-game.
