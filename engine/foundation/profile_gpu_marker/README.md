# cd::profile::gpu_marker

GPU marker recorder. Companion to `cd::profile::cpu_marker_overlay`
(Phase 587) — the GPU half of the CPU/GPU profile-marker pair.

The library both:

1. Records `(name, start_ticks, end_ticks)` triples through a
   `Recorder` for later overlay drawing, **and**
2. Calls `push_debug_group` / `pop_debug_group` on the supplied
   `ICommandBuffer` so capture tools (RenderDoc / PIX / NSight) see
   nested labelled spans in their GPU timeline.

The library is a **stub** today — it uses a monotonic counter in
place of real GPU timestamps until `ICommandBuffer::write_timestamp()`
lands. The recorded `duration_ms` is therefore approximate; the
debug-group side-effects are real and useful right now.

## Public surface

```cpp
namespace cd::profile::gpu_marker {

struct GpuMarkerSample
{
    std::string  name;
    uint64_t     start_ticks;
    uint64_t     end_ticks;
    float        duration_ms;     // computed at resolve()
};

class Recorder
{
public:
    void                                begin_marker(cd::rhi::ICommandBuffer&,
                                                     const char* name);
    void                                end_marker(cd::rhi::ICommandBuffer&);
    void                                resolve(cd::rhi::IDevice&);

    [[nodiscard]] std::span<const GpuMarkerSample>  samples() const;
    void                                clear();
};

class Scope                          // RAII
{
public:
    Scope(Recorder&, cd::rhi::ICommandBuffer&, const char* name);
    ~Scope();
};

}
```

## RAII pattern

```cpp
void Renderer::draw_shadow_pass(rhi::ICommandBuffer& cmd)
{
    cd::profile::gpu_marker::Scope _s(g_recorder, cmd, "ShadowPass");

    // … record draw calls …
}   // ~Scope() calls Recorder::end_marker (and pop_debug_group via the recorder)
```

A capture in RenderDoc / PIX / NSight will show **`ShadowPass`** as a
nested debug group around the drawcalls, even before the timestamp
query path lands.

## Sprint-2 plan

When `cd::rhi::ICommandBuffer::write_timestamp(QueryPoolHandle, idx)`
exists:

* `begin_marker` writes a timestamp to slot `2 * marker_id`.
* `end_marker` writes a timestamp to slot `2 * marker_id + 1`.
* `resolve(IDevice&)` reads back the timestamp pool, converts ticks
  to nanoseconds via the device's `timestamp_period`, and populates
  `GpuMarkerSample::duration_ms`.

The Recorder API surface above is **forward-compatible** with that
plan — only the `.cpp` changes.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::rhi` — `ICommandBuffer` + `IDevice` forward declarations
  (PUBLIC). The header forward-declares these; consumers must include
  the rhi headers themselves to pass concrete objects.

## See also

* `cd::profile::cpu_marker_overlay` — CPU companion library.
* `cd::profile::frame_graph_timeline` — GPU Gantt chart reader for
  these samples.
