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
batch.add_sphere(centre, radius, segs, color);   // 3 great circles
batch.add_arrow(from, to, color);                // shaft + 4-wing head
batch.add_grid(centre, ax_a, ax_b, n, step, c);  // square editor grid
batch.add_grid_rect(c, ax_a, ax_b, na, nb, s, col); // N×M ground grid
batch.add_axes(origin, right, up, fwd, length);  // R/G/B axis gizmo

upload(batch.vertices());                        // consumer-owned VB
cmd.draw(batch.vertex_count(), 1, 0, 0);         // kLineList pipeline
```

`pack_color` / `unpack_color` convert a `Vec4f` RGBA to/from a packed
`0xAABBGGRR` `uint32` (bgfx `Color` convention) for callers that keep a
compact per-shape colour palette.

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

The GPU half lives in `cd::debug_draw` (phase 1057): kLineList
pipeline + frames-in-flight-safe vertex-buffer growth + the
once-per-pass flush. `samples/engine/hello_engine` consumes the pair
for all of its 3D overlays — decal OBB wireframe, frustum-cull cell
wireframes, bezier curve + control polygon, camera-basis arrows,
area-light outlines, CSM boundary rings, VT atlas grid, meshlet
cluster view, ECS entity cloud and the seven pure-line plots.

## Tests

`tests/test_debug_line.cpp` — 61 cases covering vertex counts,
endpoint placement, colour propagation, AABB normalisation, OBB
rotation, frustum NDC recovery (both depth conventions), circle
plane/radius invariants, sphere radius invariants, arrow topology
(+ degenerate no-op), grid extents/plane confinement and degenerate
inputs.

Phase1253 additions (genuine-100% pass): colour pack/unpack round-trip,
byte-order, clamp and known-word decode; `add_axes` 3-arm R/G/B gizmo
(shared origin, zero-length, rotated-frame, colour-coded); `add_grid_rect`
N×M independent dimensions, 0×0 / 1×1 / negative-clamp / centred-span;
capacity growth across 5000 pushes and no-stale-vertex after a large
fill then clear.

Phase1252 additions (100% depth pass): empty-batch span, single-line
full-RGBA layout, AABB zero-extent point and flat-rectangle, OBB
zero-half-extents, frustum singular matrix (w=0 guard), circle 0/neg/2
segment clamping and exact-3 equilateral, capacity-retention multi-cycle,
cross 6-vertex coordinate check, sphere exact vertex count, zero-radius,
and per-axis plane confinement, polyline exactly-2-points, arrow ±Y shaft
fallback and head_frac boundary clamping, grid negative half_lines clamp
and spacing extents, full-RGBA on AABB and circle, line_count equals
vertex_count/2 invariant.
