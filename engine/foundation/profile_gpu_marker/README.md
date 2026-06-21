# cd::profile::gpu_marker

GPU marker recorder. Companion to `cd::profile::cpu_marker_overlay`
(Phase 587) — the GPU half of the CPU/GPU profile-marker pair.

The library both:

1. Records `(name, gpu_start_tick, gpu_end_tick, duration_ms_computed)` tuples
   through a `Recorder` for later overlay drawing, **and**
2. Calls `push_debug_group` / `pop_debug_group` on the supplied
   `ICommandBuffer` so capture tools (RenderDoc / PIX / NSight) see
   nested labelled spans in their GPU timeline.

## Real GPU Timing (BAND5-foundation, A-QUERY subsystem)

When `IDevice::features().timestamp_queries` is true, call `prepare(device,
max_markers)` once before recording. The recorder then:

- Owns a `kTimestamp` `QueryPool` with `2 * max_markers` slots.
- Emits `write_timestamp` at each `begin_marker` and `end_marker` call.
- Reads real nanoseconds via `IDevice::get_query_results` in `resolve()`.

On devices without timestamp support (Null, headless, mobile GLES2) the
recorder transparently falls back to a deterministic 1 GHz monotonic counter
— no API change, no code change from the caller's perspective.

## Public surface

```cpp
namespace cd::profile::gpu_marker {

struct GpuMarkerSample
{
    std::string  name;
    uint64_t     gpu_start_tick;        // ns (real path) or counter tick (stub)
    uint64_t     gpu_end_tick;          // ns (real path) or counter tick (stub)
    double       duration_ms_computed;  // populated by resolve()
};

struct MarkerHandle
{
    uint32_t index;       // ~0U = invalid
    bool valid() const noexcept;
};

class Recorder
{
public:
    // Optional: enable real GPU timestamp path.  Returns true when a
    // kTimestamp QueryPool is active.  Idempotent per same device +
    // capacity; grows the pool when max_markers increases.
    bool prepare(cd::rhi::IDevice& device, uint32_t max_markers);

    // Open a named GPU region.  Emits push_debug_group + write_timestamp
    // (real path) or monotonic tick (stub).  Returns a handle for end_marker.
    [[nodiscard]] MarkerHandle begin_marker(cd::rhi::ICommandBuffer&,
                                            std::string_view name);

    // Close the region.  Emits pop_debug_group + write_timestamp (real path).
    // Calling with an invalid handle is a no-op.
    void end_marker(cd::rhi::ICommandBuffer&, MarkerHandle);

    // Resolve pending samples.  Call after the GPU submit completes
    // (fence / wait_idle).  Additive: does not clear previously resolved
    // samples; use clear() between frames.
    void resolve(cd::rhi::IDevice&);

    [[nodiscard]] std::span<const GpuMarkerSample> samples() const noexcept;

    void clear() noexcept;   // resets resolved + pending + in-flight
};

class Scope    // RAII — begin_marker on construct, end_marker on destruct
{
public:
    Scope(Recorder&, cd::rhi::ICommandBuffer&, std::string_view name);
    ~Scope();
    // Non-copyable, non-movable.
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
nested debug group around the drawcalls.

## Lifecycle

```cpp
// Once, after device creation:
rec.prepare(device, 64);          // 128-slot kTimestamp pool created

// Per frame (inside command recording):
{
    cd::profile::gpu_marker::Scope s(rec, cmd, "GBuffer");
    // … draw calls …
}                                  // end_marker called by ~Scope

// After GPU submit completes (fence/wait_idle):
rec.resolve(device);

// Read results:
for (const auto& sample : rec.samples())
    printf("%s: %.3f ms\n", sample.name.c_str(), sample.duration_ms_computed);

rec.clear();   // discard before next frame
```

## GPU backend hookup (SEALED — promote-on-need)

The live emission into real GPU command buffers via backend-specific debug
marker APIs (Vulkan `VK_EXT_debug_utils` labels, D3D12 PIX `BeginEvent`/
`EndEvent`, Metal `os_signpost`) is separate from the timing path:

- `push_debug_group` / `pop_debug_group` on `ICommandBuffer` carry the label
  down to the real backend; the Vulkan and D3D12 backends implement these
  for RenderDoc / PIX capture-tool visibility.
- The `write_timestamp` / `get_query_results` real timing path requires
  linking a concrete backend (e.g. `cd::rhi_vulkan`) into this foundation
  library, which would violate the `foundation → backend` dependency wall.
  **This is intentionally SEALED as promote-on-need**: an on-GPU profiling
  overlay (e.g. a cd::renderer-level timing pass) should call
  `rec.prepare(device, N)` inside the renderer, which already owns the
  backend and its command buffers. The foundation library owns the pool
  end-to-end once prepare is called; no renderer-side plumbing changes are
  needed beyond calling prepare/resolve at the right lifecycle points.
  See ADR-20260616-band5-foundation-scope.md §profile_gpu_marker and
  §Reddedilen alternatifler for the rationale.

## Dependencies

- `cd::core` — `Defines.hpp`.
- `cd::rhi` — `ICommandBuffer` + `IDevice` forward declarations (PUBLIC).
  The header forward-declares these; consumers must include the rhi headers
  themselves to pass concrete objects.

## See also

- `cd::profile::cpu_marker_overlay` — CPU companion library.
- `cd::profile::frame_graph_timeline` — GPU Gantt chart reader for
  these samples.
