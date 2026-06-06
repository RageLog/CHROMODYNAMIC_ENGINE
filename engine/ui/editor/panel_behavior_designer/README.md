# cd::editor::panel::behavior_designer

Behavior-tree designer panel. Walks a live `cd::game::ai_bt::BehaviorTree`
top-down, lays the nodes out automatically, and draws each node in a
colour matching its kind. Click-to-select highlights the chosen node
so a property editor can follow up.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase556-558` | Placeholder grid + 3 demo nodes (no live tree). |
| 2 | `phase740`     | Bind a real `BehaviorTree`, recursive auto-layout, edge routing. |

## API

```cpp
namespace cd::editor::panel::behavior_designer {

using BehaviorNodeId = uint32_t;

class BehaviorDesigner
{
public:
    void                          set_tree(const cd::game::ai_bt::BehaviorTree*);
    void                          set_selected(BehaviorNodeId);
    [[nodiscard]] std::optional<BehaviorNodeId>
                                  selected() const noexcept;

    void                          draw(cd::ui::renderer::DrawBatcher&,
                                       const cd::ui::widgets::Theme&,
                                       const cd::ui::widgets::Rect&) const;
};

}
```

## Node colour convention

```
  NodeKind            Colour
  ────────            ──────
  kSelector           orange
  kSequence           cyan
  kParallel           magenta
  kDecorator          yellow
  kLeaf               white
```

The selector / sequence / parallel triad uses colour-complementary
hues so a designer can scan a screenful of nodes by kind at a glance.
Decorators (yellow) read as "modifier" and leaves (white) as
"action / condition terminals".

## Auto-layout

`measure_subtree_(node)` recursively walks the tree:

* Width = sum of children widths + sibling spacing.
* Height = layer count × layer spacing.
* Node x-centroid = mean of children x-centroids.

Edges are drawn **orthogonal** parent → child (the L-shape that AI
behavior-tree authoring tools converge on):

```
  ┌──────────┐
  │ Selector │
  └──────────┘
       │
       ├──────────┬──────────┐
       │          │          │
  ┌──────┐  ┌─────────┐  ┌─────┐
  │ Cond │  │ Seq     │  │ ... │
  └──────┘  └─────────┘  └─────┘
```

## Live-tree contract

`set_tree(tree_ptr)` accepts a pointer to a `BehaviorTree` owned by
the gameplay tier. The panel **does not** copy or clone the tree —
mutations on the gameplay side surface on the next `draw()`. Pointer
ownership lifetime is the caller's responsibility; `set_tree(nullptr)`
clears the binding for safety during play-mode transitions.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `cd::game_ai_bt` — `BehaviorTree`, `Node` hierarchy, `NodeKind`,
  `node_kind()` (PUBLIC).
* `cd::ui_renderer` — `DrawBatcher` (PUBLIC).
* `cd::ui_widgets` — `Theme` + `Rect`.
