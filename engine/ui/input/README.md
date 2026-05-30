# cd::ui_input

**Purpose**: Pure-C++ input plumbing for retained-mode UI widgets. Phase 2.1 of ADR-20260530-ui-widget-library -- hit-test walk, focus chain, tab / shift-tab cycling, and modal capture. No RHI, no font, no platform window dep; the frontend bridge (cd::events / editor / app) translates raw input into `MouseEvent` / `KeyEvent` and feeds them in.

**Namespace**: `cd::ui::input`.

**Headers**: `cd/ui/input/Input.hpp`.

**Primary types**:
- `cd::ui::input::WidgetId` -- opaque 32-bit handle owned by the widget tree. `kInvalidWidget = 0xFFFFFFFFu` sentinel (matches `cd::ui::layout::NodeId` convention).
- `cd::ui::input::MouseEvent { x, y, button, action }` and `cd::ui::input::KeyEvent { keycode, action, modifiers }` -- POD wire types fed by the frontend.
- `cd::ui::input::HitRect { id, x, y, width, height }` -- one entry in the caller-supplied hit-test list.
- `cd::ui::input::HitTester` -- stateless helper. `hit_test(rect_list, x, y) -> WidgetId` walks the list back-to-front (last-wins on overlap) and returns the topmost match or `kInvalidWidget`.
- `cd::ui::input::FocusManager` -- owns the focus chain + modal stack. `register_widget(id)` / `set_chain(span)` populate the chain; `focus(id)` / `blur()` / `next()` / `prev()` mutate state; `focus_chain()` returns a read-only view (`FocusChain = std::span<const WidgetId>`).
- Modal capture API: `push_modal(id)` / `register_modal_subchain(span)` / `pop_modal()` -- while a modal is on top, cycling is restricted to its sub-chain (or to the modal id alone) and `is_in_active_chain(id)` returns false for any widget underneath.

**Phase 2.1 scope**:
- Pointer + keyboard surface (POD events; gesture / gamepad / IME deferred).
- Topmost-rect hit-test, O(N) per query.
- Tab / shift-tab focus cycling with wrap-around.
- Modal capture stack with prior-focus restoration on pop.
- Nested modals (each push captures the prior focus snapshot).

**Out of Phase 2.1** (Phase 2.2+):
- Drag / double-click / long-press gesture recognizers.
- Gamepad focus navigation (D-pad / left-stick mapping).
- Text-input compose / IME.
- Bridge to `cd::events` (this lib stays input-source-agnostic).
- Quad-tree / R-tree acceleration for >10K rects (linear walk is fine for Phase 2 widget counts).

**Usage**:
```cpp
#include <cd/ui/input/Input.hpp>

namespace ui = cd::ui::input;

// Frontend builds the hit-test list each frame from the laid-out widget rects.
std::vector<ui::HitRect> rects;
rects.push_back({ ui::WidgetId{1}, 0.0F, 0.0F, 200.0F, 60.0F });  // back panel
rects.push_back({ ui::WidgetId{2}, 8.0F, 8.0F,  80.0F, 24.0F });  // button on top

const ui::WidgetId under = ui::HitTester::hit_test(rects, mouse_x, mouse_y);
if (under.is_valid()) { /* dispatch click */ }

// FocusManager owns the tab chain.
ui::FocusManager fm;
fm.register_widget(ui::WidgetId{1});
fm.register_widget(ui::WidgetId{2});
fm.register_widget(ui::WidgetId{3});

// Tab key handler:
fm.next();              // -> WidgetId{1} (or wraps from current focus)
fm.prev();              // shift-tab

// Modal dialog: capture cycling to a sub-chain.
fm.push_modal(ui::WidgetId{99});
const std::array<ui::WidgetId, 3> sub { ui::WidgetId{99}, ui::WidgetId{101}, ui::WidgetId{102} };
fm.register_modal_subchain(sub);
// ... tab stays inside the modal ...
fm.pop_modal();         // prior focus restored
```

**Test command**: `ctest --preset ninja-debug -R cd_test_ui_input --output-on-failure`. Covers 11 cases: hit-test topmost / no-hit / invalid-id-and-zero-area, focus next / prev cycling and wrap, tab-from-middle, empty-chain safety, `set_chain` stale-focus drop, modal-capture-blocks-underneath, modal-pop-restores, nested modals, and MouseEvent / KeyEvent POD smoke.

**Notes**:
- Hit-test is intentionally stateless: callers re-emit the rect list each frame. Cheap (a rect is 5 floats + an id) and avoids a stale-cache class of bugs.
- The frontend dispatcher applies the modal-aware filter for pointer events by either (a) clipping the rect list to the modal sub-tree before calling `HitTester::hit_test` or (b) post-filtering the resulting id with `FocusManager::is_in_active_chain`.
- `push_modal` snapshots the prior focus; `pop_modal` restores it iff the prior id is still in the active chain (otherwise focus drops to `kInvalidWidget`). This is the spec from ADR Section 2.4 -- nested modals stack cleanly.
- `WidgetId` mirrors the sentinel-as-invalid convention used by `cd::ui::layout::NodeId`; a future cd::ui widget tree binding will likely typedef them together.
- `FocusManager` is NOT thread-safe. The cd::ui dispatcher is single-threaded by ADR contract (one input-dispatch pass per frame on the UI thread).
