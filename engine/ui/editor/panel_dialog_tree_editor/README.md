# cd::editor::panel::dialog_tree_editor

Visualises a `cd::game::dialog_tree::DialogTree` as a **graph**:
colour-coded node rectangles per `NodeKind`, L-shaped edge connectors
parent → child, and a click-to-select highlight.

## Node colour convention

```
  NodeKind     Colour
  ────────     ──────
  kSay         blue
  kChoice      yellow
  kCondition   orange
  kEnd         grey
```

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase666` | Static left-to-right depth-column layout, click-to-select via `simulate_click`. |
| 2 | queued    | Pan / zoom + live hot-reload binding. |

## API

```cpp
namespace cd::editor::panel::dialog_tree_editor {

using NodeId = uint32_t;

class DialogTreeEditor
{
public:
    void                          set_tree(const cd::game::dialog_tree::DialogTree*);
    void                          set_selected(NodeId);
    [[nodiscard]] std::optional<NodeId>
                                  selected() const noexcept;

    // Sprint-1 testing harness; Sprint-2 promotes to live mouse hit.
    void                          simulate_click(cd::math::Vec2f screen_xy);

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Layout

Sprint-1 lays out nodes in **left-to-right depth columns**:

```
  depth 0       depth 1       depth 2       depth 3
  ────────      ────────      ─────────     ────────
  [Say  ]──┐    [Choice]──┐   [Cond]──┐     [End]
           ├──► [Choice]──┤   [Cond]──┘
           │              │
           └──────────────┘
```

Children of a node are stacked vertically; row spacing scales with
the deepest sibling stack. L-shaped connectors mirror the BG3
authoring tool convention so a dialog author can read the flow
top-down or left-right without retraining.

## Click selection

`simulate_click(screen_xy)`:

1. Walk every laid-out node bounding box.
2. First hit (z-order: top-most painted) wins.
3. `set_selected(hit_id)` so the editor can scope a property
   inspector to the chosen node.

Selected nodes paint with `theme.accent` border (1.5× width) plus
their normal `NodeKind` fill colour.

## Sprint-2 live binding

When `set_tree(tree_ptr)` is called with the same pointer **but
modified contents** (hot reload reparses the `.json` and rebuilds
the graph in place), the editor calls a `mark_dirty()` API that
re-lays out and repaints. Pointer identity stability is the
contract; the panel does not copy the graph.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::game_dialog_tree` — `DialogTree`, `Node`, `NodeKind` (PUBLIC).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
