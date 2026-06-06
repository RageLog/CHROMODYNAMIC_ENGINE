# cd::editor::panel::pathfinding_viz

**Top-down 2-D navmesh projection + A\* path overlay** panel.
Renders every `cd::ai::pathfinding::NavMesh` triangle as a top-down
quad with outline; the last `PathResult`'s waypoints draw on top
as a thicker accent-coloured L-shaped polyline.

## API

```cpp
namespace cd::editor::panel::pathfinding_viz {

class PathfindingViz
{
public:
    void                          set_navmesh(const cd::ai::pathfinding::NavMesh*);
    void                          set_last_path(cd::ai::pathfinding::PathResult);

    // Sprint-1: testing harness for headless tests of the viz layout.
    // Sprint-2 promotes this to a live "drag-and-query" mode where the
    // caller re-runs find_path() on every drag tick.
    void                          simulate_click_to_test_path(cd::math::Vec2f screen_xy);

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

```
  ┌──────────────────────────────────────────┐
  │   Pathfinding Viz   (top-down)            │
  ├──────────────────────────────────────────┤
  │                                          │
  │      ▲                                   │
  │     /│\           ◄── start              │
  │    / │ \                                 │
  │   /  │  \    last path                   │
  │  /   ●───●──────────────●  ◄── goal      │
  │  \   │                                   │
  │   \  │  /  navmesh triangles             │
  │                                          │
  ├──────────────────────────────────────────┤
  │ Triangles 412 | path ✓ | dist 24.3 m     │
  │ explored 31                              │
  └──────────────────────────────────────────┘
```

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase677` | Static top-down projection + stats strip + simulated click. |
| 2 | queued    | Pan / zoom + live `Pathfinder` re-query on goal drag. |

## Projection

Sprint-1 projects each triangle by **dropping Y**:

```
  screen_xy = world_xz * scale + offset;
```

The transform is fixed (no zoom yet); the panel auto-fits the
navmesh AABB into the panel rectangle at first `draw()` and caches
the scale + offset. Sprint-2 promotes this to a pan-zoom view.

## Sprint-2 live re-query plan

`simulate_click_to_test_path` becomes `on_goal_drag(world_xy)` which:

1. Project the screen drag back to world XZ via the cached
   transform.
2. Call `cd::ai::pathfinding::find_path(*mesh, last_start, world_xy)`.
3. `set_last_path(result)` and the viz repaints next frame.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::ai_pathfinding` — `NavMesh` + `PathResult` (PUBLIC, header use).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
