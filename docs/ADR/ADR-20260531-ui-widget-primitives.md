# ADR-20260531 UI Widget Primitives

**Status:** Accepted  
**Date:** 2026-05-31  
**Phases:** 515–521  
**Commits:** e9bc721 (DockSpace, ColorPicker), e9f4862 (CurveEditor), b45674e (ConstraintSolver), fa1ccbf (GestureRecognizer)

---

## Bağlam

Phases 515–521 shipped five UI widget primitive libraries under `engine/cd_ui_*/`. The motivating question was whether each primitive deserves its own CMake target and library boundary, or whether they should be collapsed into a single monolithic `cd_ui` library.

Three forces drove the decision toward separation:

**DAG clarity.** The dependency graph across these five subsystems is non-trivial. `ConstraintSolver` is consumed by both `DockSpace` (layout pass) and by the editor's property panel independently; `GestureRecognizer` feeds into `DockSpace` (tab drag, splitter drag) and into a future touch-first mobile UI layer. A monolith would force every consumer to pay for all transitive headers and link units, collapsing a clean DAG into a ball of mud.

**Build cost.** Each library has a narrow, well-defined header surface. Keeping them separate allows incremental recompilation to remain surgical: a change to the OKLab curve in `ColorPicker` does not invalidate `CurveEditor` or `DockSpace` object files. With a unified library and unity builds disabled, any header touch would stale the full library.

**Reuse outside the editor.** `CurveEditor` is already consumed by the animation timeline (keyframe tangent editing) and the audio DSP envelope tool — neither of which needs `DockSpace`. `GestureRecognizer` is a standalone input-to-intent classifier usable in any ImGui-hosted or RHI-hosted surface. Granular library boundaries allow these consumers to link only what they need, keeping their own link-time and binary size under control. This is a direct requirement of the modularity principle: every subsystem must be consumable as a standalone library by an external tool.

---

## Karar

### DockSpace (`cd_ui_dockspace`) — commit e9bc721, 12 tests

`DockSpace` implements a tree-of-leaves layout model where each node is either a split container (horizontal/vertical, with a draggable splitter) or a leaf pane holding one or more tabbed views. The design follows the Sublime Text / VS Code split-editor model rather than the ImGui `DockBuilder` API, because the ImGui model conflates layout state with render-frame execution and makes serialization a post-hoc retrofit.

Key decisions:
- Layout state is a pure data tree (`DockNode` with `std::variant<SplitNode, LeafNode>`); the render step is a separate traversal that maps the tree to pixel rectangles.
- Splitter interaction uses `ConstraintSolver` to enforce minimum pane sizes rather than ad-hoc clamping, giving consistent resize behaviour with nested splits.
- Tab-merge (drag a tab from one leaf onto another) is modelled as a tree mutation operation, preserving the invariant that the tree is always valid before and after any user gesture.
- Layout serializes to/from JSON via `nlohmann::json`; the schema is versioned (`"dockspace_schema_version": 1`) so future additions can migrate gracefully.
- Test coverage (12 tests): split/unsplit round-trip, splitter min-size enforcement, tab-merge validity, JSON serialize-deserialize identity, nested 3-level split rectangle math.

### ColorPicker (`cd_ui_colorpicker`) — commit e9bc721, 13 tests

`ColorPicker` exposes three complementary color-selection surfaces in a single widget:

1. **HSV wheel + value strip** — the canonical hue ring with a triangular saturation-value selector, matching the mental model most artists already hold.
2. **OKLab cube-root LMS picker** — a perceptually uniform sRGB-bounded slice navigator. The mathematical basis follows Björn Ottosson's 2020 derivation of the OKLab color space, which maps RGB through a cone-response matrix, applies a cube-root non-linearity on each LMS channel, then rotates into a perceptually decorrelated L-a-b space. This makes lightness sliders behave linearly as perceived by a human observer, eliminating the hue shift visible in HSL/HSV lightness edits.

> **[unverified] Ottosson 2020** — Björn Ottosson, "A perceptual color space for image processing," 2020, https://bottosson.github.io/posts/oklab/. PDF + BibTeX entry pending; `research/library/MANIFEST.csv` does not yet contain a verified entry. Academic-researcher pass queued before this citation may be treated as verified under the Demir Kural.

3. **Hex input + ring FIFO palette** — a 16-slot ring buffer of recently used colors, persisted per session. Clicking a swatch sets the current color; shift-click overwrites the slot.

Internal representation is always linear-light sRGB `vec4f`; conversions to/from HSV and OKLab are done at widget boundary only. Test coverage (13 tests): HSV↔RGB round-trip accuracy (< 1 ULP float32), OKLab↔sRGB identity at corner colours, hex parse edge cases (3-digit, 6-digit, 8-digit with alpha), ring palette eviction order, swatch overwrite.

### CurveEditor (`cd_ui_curveeditor`) — commit e9f4862, 10 tests

`CurveEditor` provides interactive editing of piecewise cubic-Hermite curves, used by the animation timeline (keyframe tangents) and the audio DSP envelope tool. The mathematical primitive is the cubic Hermite spline segment, evaluated as:

```
p(t) = h00(t)·p0 + h10(t)·m0 + h01(t)·p1 + h11(t)·m1
```

where `h00..h11` are the standard Hermite basis polynomials and `m0`, `m1` are tangent vectors.

Four tangent modes are supported:
- **Catmull-Rom auto** — tangents computed from neighbouring key positions using the Catmull-Rom formula; no manual tangent handles exposed.
- **Linear** — tangents set so the segment degenerates to linear interpolation; piecewise-linear animation curves without overshoots.
- **Stepped** — output holds the value of `p0` for `t ∈ [0, 1)` then jumps to `p1`; used for discrete state changes (e.g., visibility toggles).
- **Free** — fully manual tangent handles, with optional weighted tangent lengths (independent in/out).

Mixed modes per key are supported; the mode is stored per-key, per-side (in-tangent mode may differ from out-tangent mode).

**Tweener integration:** `cd_ui_curveeditor` exports a `TweenerBinding` interface that `cd_anim::Tweener` consumes to drive curve evaluation at runtime. This keeps the editor widget as the single source of truth for curve data while allowing the runtime animation system to evaluate it without depending on UI headers.

Test coverage (10 tests): Catmull-Rom endpoint tangent degenerate case, Hermite C1 continuity at knot, stepped mode hold value, linear mode no overshoot, free tangent round-trip serialize, Tweener binding eval at t=0.0/0.5/1.0, mixed-mode per-key correctness.

### ConstraintSolver (`cd_ui_constraintsolver`) — commit b45674e, 9 tests

`ConstraintSolver` implements an incremental linear constraint solver suitable for UI layout, following the Cassowary algorithm in spirit.

> **[unverified] Badros, Borning, Stuckey 2001** — G. J. Badros, A. Borning, P. J. Stuckey, "The Cassowary Linear Arithmetic Constraint Solving Algorithm," *ACM Transactions on Programming Languages and Systems (TOPLAS)*, 23(4), 2001, pp. 526–544. PDF + BibTeX entry pending; `research/library/MANIFEST.csv` does not yet contain a verified entry. Academic-researcher pass queued before this citation may be treated as verified under the Demir Kural.

The solver operates on a set of variables and linear constraints of the form `a₁x₁ + a₂x₂ + … ≥ c` with associated strength levels (required, strong, medium, weak). The incremental nature means adding or removing a constraint is O(m) amortized rather than re-solving from scratch, which is essential for interactive layout where constraints change on every frame during a drag.

Key design choices:
- Variables and constraints are identified by opaque integer handles (no raw pointers into solver internals).
- The solver exposes a `solve() -> bool` step that returns whether all required constraints are satisfied; layout code checks this and may fall back to proportional allocation if required constraints conflict.
- `DockSpace` uses `ConstraintSolver` to enforce minimum pane widths/heights during splitter drag; the property panel editor uses it to align label columns across variable-length property names.
- No vendored dependency: the solver is implemented from scratch in ~600 lines of C++23 using `std::expected` for error propagation.

Test coverage (9 tests): single required constraint satisfied, conflicting required constraints detected, strength priority (strong beats weak), incremental add/remove constraint, min-size enforcement under nested splits, property-column alignment with 3 rows.

### GestureRecognizer (`cd_ui_gesturerecognizer`) — commit fa1ccbf, 9 tests

`GestureRecognizer` classifies raw pointer/touch/stylus input events into 9 named gesture kinds via a per-gesture state machine. The architecture avoids a monolithic recognizer that collapses all gestures into one switch statement; instead each gesture kind is a separate state machine object, and a `GestureArena` adjudicates conflicts (only one recognizer wins per pointer sequence).

The 9 gesture kinds:
1. **Tap** — press and release within distance threshold and time threshold.
2. **DoubleTap** — two tap gestures within a double-tap interval.
3. **LongPress** — press held beyond a configurable duration without movement.
4. **Pan** — unidirectional or free drag exceeding a distance slop.
5. **Swipe** — fast pan that ends with velocity above a threshold; direction classified into N/S/E/W/NE/NW/SE/SW.
6. **Pinch** — two-pointer scale gesture; provides a scale delta per frame.
7. **Rotate** — two-pointer rotation gesture; provides an angle delta per frame.
8. **TwoFingerPan** — two-pointer translation; distinct from single-pointer pan for scroll semantics.
9. **ForceTap** — stylus pressure exceeds a threshold (maps to right-click equivalent on pressure-sensitive surfaces).

State machines are driven by `InputEvent` structs (pointer ID, position, pressure, timestamp) that are backend-agnostic; the ImGui backend adapter translates `ImGuiIO` mouse/touch state into `InputEvent` before feeding the arena. This keeps `cd_ui_gesturerecognizer` free of any ImGui or platform dependency.

Test coverage (9 tests): one per gesture kind — each test drives the state machine through the happy path and verifies the emitted gesture event fields.

---

## Reddedilen Alternatifler

**ImGui built-in widgets (DockSpace, ColorPicker, etc.):** ImGui provides `ImGui::DockSpace`, `ImGui::ColorPicker4`, and limited drag-handle primitives out of the box. These were rejected for three reasons: (1) they are not library-oriented — they cannot be consumed independently of the full ImGui context; (2) their internal state is stored in ImGui's global `GImGui` context, making serialization and multi-viewport isolation difficult; (3) the ColorPicker has no OKLab mode and the DockSpace has no programmatic JSON serialization API. ImGui remains the *host surface* (rendering, input routing) but is not the implementation of these primitives.

**Yoga / Stretch (flexbox layout engines):** Both Yoga (Meta) and Stretch (Bevy-era) implement CSS Flexbox layout. Flexbox was rejected for `ConstraintSolver` and `DockSpace` because Flexbox is a one-shot layout pass — it does not support incremental constraint updates during interactive drag. The Cassowary-style incremental solver is a better fit for a layout engine that must resolve constraints 60+ times per second during resize interactions. Additionally, Yoga is a C library with a C++ wrapper that would introduce a vendored dependency not aligned with the project's no-vendored-deps-in-widget-primitives policy.

**Platform touch APIs (UIKit / Android GestureDetector / Win32 WM_GESTURE):** Native platform gesture APIs were rejected for `GestureRecognizer` because they are platform-specific and would require per-platform reimplementation. The engine targets Windows, Linux, macOS, iOS, Android, and Web (Emscripten); a single backend-agnostic state-machine approach written in C++23 is more maintainable and testable than five platform bindings.

---

## Sonuçlar

**Test count:** The five libraries contribute a combined +63 tests (12 + 13 + 10 + 9 + 9) to the test suite.

**Editor integration:** The editor application (`apps/editor`) links `cd_ui_dockspace` and `cd_ui_constraintsolver` as its primary layout primitives. `cd_ui_colorpicker` is linked by the material editor panel. `cd_ui_curveeditor` is linked by the animation timeline and the audio DSP envelope tool. `cd_ui_gesturerecognizer` is linked by the editor's viewport interaction layer.

**No vendored dependencies:** All five libraries are implemented in pure C++23 with no vendored third-party code. `ConstraintSolver` implements the Cassowary algorithm from scratch. `ColorPicker`'s OKLab implementation is derived from the public mathematical formulation [unverified — see Ottosson citation above].

**DAG position:** The five libraries sit at the `ui-primitives` tier in the dependency DAG:

```
foundation → math → events → [cd_ui_gesturerecognizer, cd_ui_constraintsolver]
                                         ↓                       ↓
                              cd_ui_curveeditor         cd_ui_colorpicker
                                         ↓                       ↓
                                    cd_ui_dockspace (aggregates all four above)
                                         ↓
                                    apps/editor
```

**Citation backlog:** Two academic citations used in this ADR are flagged [unverified]:
- Ottosson 2020 (OKLab) — `academic-researcher` pass queued; add `MANIFEST.csv` entry + PDF + BibTeX before citing as verified.
- Badros, Borning & Stuckey 2001 (Cassowary TOPLAS) — same requirement.

Until both entries appear in `research/library/MANIFEST.csv` with `demir_kural_status = VERIFIED`, these citations must carry the `[unverified]` prefix in all documents.
