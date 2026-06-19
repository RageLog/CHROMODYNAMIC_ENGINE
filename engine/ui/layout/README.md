# cd::ui_layout

**Purpose**: Pure-C++ Flexbox layout solver for retained-mode UI widgets. Phase 1.0 of ADR-20260530-ui-widget-library — the first concrete brick of the `cd::ui::*` family. Produces final world-space pixel rectangles from a tree of styled nodes; no RHI, no font, no input dependencies.

**Namespace**: `cd::ui::layout`.

**Headers**: `cd/ui/layout/Flex.hpp`.

**Primary types**:
- `cd::ui::layout::FlexStyle` -- per-node style: `direction`, `justify`, `align_items`, `position`, `width`/`height` (or `kAuto`), `min_*`/`max_*`, `flex_grow`/`flex_shrink`/`flex_basis`, `margin`, `padding`, `gap_*`, `intrinsic_*`. Yoga semantic parity for portable callers.
- `cd::ui::layout::FlexTree` -- owns the node store. `create_node(style)` → `NodeId`; `add_child(parent, child)` for tree shape; `solve(root, w, h)` runs the solver; `layout(node) -> Rect` reads computed world rect.
- `cd::ui::layout::Rect` -- final pixel rect in world coordinates (no parent-stack walk needed at render time).

**Phase 1 scope** (this library):
- `direction`: row / row-reverse / column / column-reverse.
- `justify`: flex-start / center / flex-end / space-between / space-around / space-evenly.
- `align_items`: flex-start / center / flex-end / stretch.
- `flex-grow` and `flex-shrink` distribution (Yoga semantics).
- `padding` (4 sides), `gap_main` between siblings.
- Fixed `width` / `height` OR `intrinsic_*` content size hint.
- `min_*` / `max_*` clamps.

**Implemented (Phase 1.1 additions)**:
- `margin` (4 sides per child) offsets main-axis start and cross-axis position.
- `kAbsolute` position: child is placed by `inset_left`/`inset_top`/`inset_right`/`inset_bottom`, excluded from flex flow and grow/shrink.
- Incremental 3-phase decomposition: `compute_main` → `compute_cross` → `position_children` (enables future dirty-flagging without API change).
- `kDuplicate` constraint error detection in `ConstraintSolver::add_constraint`.

**Out of scope (sealed)**:
- `flex-wrap: wrap` — Phase 2; `gap_cross` is a multi-line concept, intentionally ignored in single-line layout.
- `grid` layout — Phase 3.
- BiDi RTL flip — Phase 5.

**Usage**:
```cpp
#include <cd/ui/layout/Flex.hpp>

cd::ui::layout::FlexTree t;
cd::ui::layout::FlexStyle root_s;
root_s.padding = { 10.0F, 10.0F, 10.0F, 10.0F };
auto root = t.create_node(root_s);

cd::ui::layout::FlexStyle btn_s;
btn_s.width = 120.0F;
btn_s.height = 32.0F;
auto btn = t.create_node(btn_s);
t.add_child(root, btn);

t.solve(root, 800.0F, 600.0F);
auto r = t.layout(btn);   // world rect ready to draw
```

**Test command**: `ctest --preset ninja-debug -R cd_test_flex --output-on-failure`. Cross-validates ~14 cases against the Yoga 1.19 reference.

**Notes**:
- Solver runs O(N) over the tree per `solve()`. No heap allocation in the hot path other than `std::vector` for child indices.
- `layout()` returns WORLD-space rects (not local-to-parent) so the renderer iterates without a stack walk.
- `kAuto` is a sentinel `-1.0F` for unpinned dimensions, encoded as a negative float so callers don't need a separate `bool has_*` flag.
- Phase 2 will add `cd::ui::input` (hit-test) which reads these world rects directly.
