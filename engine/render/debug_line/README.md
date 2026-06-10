# cd::debug_line — Debug Line Batch Builder

CPU-side line-batch accumulator for 3D viewport debug visualisation.
Turns debug shapes into a flat `kLineList` vertex stream that any
consumer can upload and draw with a 2-attribute pipeline.

## Why it exists

Marathon Runs 27-29 shipped 19 debug-viz demos in `hello_engine` on a
"sphere-at-position" proxy template because the engine had no line
path. The debug-viz research survey
(`docs/RESEARCH_3D_VIEWPORT_DEBUG_VIZ.md` §12 Tier-2) identifies a
dedicated line renderer — bgfx `DebugDrawEncoder`, Bevy `Gizmos` are
the SOTA equivalents — as the canonical upgrade. `cd::debug_line` is
the RHI-independent half of that feature; the GPU half (pipeline +
vertex buffer + flush) lives with the consumer.

## What it provides

```cpp
#include <cd/debug_line/DebugLine.hpp>

cd::debug_line::LineBatch batch;
batch.clear();                                   // per frame
batch.add_line(a, b, {1, 0, 0, 1});
batch.add_aabb(bmin, bmax, color);               // 12 edges
batch.add_obb(centre, right, up, fwd, half, c);  // 12 edges, oriented
batch.add_frustum(inv_view_proj, color);         // 12 edges, Vulkan z=[0,1]
batch.add_circle(centre, axis, radius, segs, c); // >= 3 segments
batch.add_polyline(points, color);               // N-1 segments
batch.add_cross(centre, half_size, color);       // 3-axis marker

upload(batch.vertices());                        // consumer-owned VB
cmd.draw(batch.vertex_count(), 1, 0, 0);         // kLineList pipeline
```

`LineVertex` = `Vec3f position` + `Vec4f color` (28 bytes, tightly
packed). Colour rides per vertex so one batch mixes shapes of any
colour in a single draw.

## Design rules

- **No RHI dependency.** The library only depends on `cd::core` +
  `cd::math`, builds as an INTERFACE target and is unit-tested
  without a GPU. The consumer owns the `kLineList` PSO and the
  host-visible vertex buffer.
- **Append + clear lifecycle.** `clear()` retains capacity, so a
  steady-state frame does zero allocations.
- **Defensive inputs.** AABB corners are normalised per component
  (swapped min/max is not an error); circle segments clamp to 3 and
  a zero axis falls back to +Y; sub-2-point polylines are no-ops.

## Reference consumer

`samples/engine/hello_engine` (phase 1031): `MaterialBundle.line`
(kLineList topology, depth test ON / depth write OFF, neutral
G-buffer fragment outputs) + a lazily-grown `kCpuToGpu` vertex
buffer, flushed once at the end of the HDR pass. Used by the decal
OBB wireframe, frustum-cull cell wireframes, bezier curve + control
polygon, camera-basis arms, area-light outlines and CSM boundary
rings (phase 1032).

## Tests

`tests/test_debug_line.cpp` — 15 cases covering vertex counts,
endpoint placement, colour propagation, AABB normalisation, OBB
rotation, frustum NDC recovery (both depth conventions), circle
plane/radius invariants and degenerate inputs.
