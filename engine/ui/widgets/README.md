# cd::ui_widgets

**Purpose**: Concrete widget catalog for the retained-mode UI. Phase 2.2 of ADR-20260530-ui-widget-library -- Button, TextInput, Slider, Toggle, Checkbox, Dropdown, Modal. Each widget owns a `Rect`, exposes `tick(input_state)` for state transitions, and `draw(batcher, font, theme)` for command emission. Renderer- and font-agnostic at the state-machine layer (null font is a supported draw path).

**Namespace**: `cd::ui::widgets`.

**Headers**: `cd/ui/widgets/Widgets.hpp`.

**Dependencies**: `cd::core`, `cd::ui` (retained-mode tree), `cd::ui_layout`, `cd::ui_font`, `cd::ui_renderer`, `cd::ui_input`.

## Widget catalog

| Widget      | State                              | Activate                                | Notes |
|-------------|------------------------------------|-----------------------------------------|-------|
| `Button`    | `ButtonState { hovered, pressed, focused }` | release-inside after press-inside; Enter / Space when focused | `on_click()` callback |
| `TextInput` | `ButtonState` + `cursor` (chars)   | character / Backspace / Delete / Arrows / Home / End | ASCII / Latin-1 fast path; full Unicode shaping is Phase 4 |
| `Slider`    | `ButtonState` + normalised `value [0..1]` + `step` | click-on-track / drag / Left-Right / Home-End | `on_change(float)` |
| `Toggle`    | `ButtonState` + `value` (bool)     | release-inside after press-inside; Enter / Space when focused | lozenge style |
| `Checkbox`  | `ButtonState` + `value` (bool) + `label` | release-inside after press-inside; Enter / Space when focused | square box on the left, label trails |
| `Dropdown`  | `ButtonState` + `selected` (idx) + `expanded` + `hovered_option` | header click toggles; option click commits + collapses; Up/Down move selection; Enter/Space toggles | should be paired with `cd::ui::input::FocusManager::push_modal` when expanded |
| `Modal`     | `ButtonState` + `visible` + content rect | Escape (focused) closes; dim-layer click (outside content) closes | full-bleed dim overlay + centred panel |

## Public API surface

```cpp
namespace cd::ui::widgets
{
struct Rect       { float x, y, w, h; };
struct Color      { uint8_t r, g, b, a; };
struct Theme      { Color background, surface, surface_hover, surface_press,
                          accent, accent_hover, focus_ring,
                          text, text_dim, dim_overlay; };
struct PointerState { float mouse_x, mouse_y; bool left_down, left_pressed, left_released; };
enum class KeySignal { kNone, kCharacter, kBackspace, kEnter, kEscape,
                       kLeft, kRight, kUp, kDown, kTab, kHome, kEnd, kDelete, kSpace };
struct KeyInput    { KeySignal signal; uint32_t codepoint; };
struct InputState  { PointerState pointer; std::span<const KeyInput> keys; bool focused; };
struct ButtonState { bool hovered, pressed, focused; };
}
```

Each widget mirrors the same triplet:
- `void set_rect(Rect)`, `const Rect& rect() const`.
- `bool tick(const InputState&)` -- returns `true` on the meaningful transition for that widget type (click, value-change, flip, selection-change, close).
- `void draw(DrawBatcher&, Font*, const Theme&) const` -- emits draw commands via the `cd::ui_renderer` batcher.

## Theme

The default `Theme` is a dark palette tuned for the editor (background `#202024`, surface `#303038`, accent `#60A0FF`). Replace any slot by constructing a `Theme {}` and overriding fields; widgets sample slots per draw call so swapping themes between widget trees is free.

## Drawing

Widgets emit:
1. A background rect (one `DrawBatcher::quad` call) coloured by interaction state.
2. A focus ring (four 1-pixel quads) when `state.focused == true`.
3. Optional glyph runs (`DrawBatcher::glyph`) for labels / option text -- skipped when the font is null.
4. Widget-specific decoration (slider thumb, toggle knob, checkbox check fill, expanded dropdown options, modal dim overlay).

Per ADR, the draw path tolerates a null font so headless tooling (and this library's tests) can drive widgets without rasterizing any TTF. The renderer's golden-image suite covers pixel-perfect output separately.

## Usage

```cpp
#include <cd/ui/widgets/Widgets.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

namespace w = cd::ui::widgets;
namespace r = cd::ui::renderer;

// One-time setup.
w::Button save { "Save", []{ /* commit */ } };
save.set_rect({ 16.0F, 16.0F, 120.0F, 32.0F });

w::Slider master { 0.8F, [](float v){ /* set volume */ } };
master.set_rect({ 16.0F, 60.0F, 240.0F, 24.0F });

w::Theme theme {};

// Per-frame.
r::DrawBatcher batcher;
batcher.begin_frame();

w::InputState in;
in.pointer = pointer_for_this_frame();
in.keys    = keys_for_this_frame();
in.focused = focus_owner == save_id;  // from cd::ui_input::FocusManager
save.tick(in);
master.tick(in);

save.draw(batcher,  font_atlas, theme);
master.draw(batcher, font_atlas, theme);

// Submit `batcher.vertices()` / `indices()` / `commands()` to cd::ui_renderer_rhi.
```

## Phase 2.2 scope

- Concrete widget set: `Button`, `TextInput`, `Slider`, `Toggle`, `Checkbox`, `Dropdown`, `Modal`.
- Pointer + key state-machine driven by `InputState`.
- Per-widget callbacks (`on_click` / `on_change` / `on_close`) fired on the canonical transition only.
- Headless draw smoke (null font) for CPU-side validation.

## Out of Phase 2.2 (Phase 3+)

- Drag-and-drop targets, multi-line text, list/tree views.
- Custom shaders / nine-patch / gradient widgets (Phase 1.2 batcher already supports the variants; this catalogue only emits `kSolid` + `kGlyph`).
- Animated transitions between states (couples with `cd::ui_animation`).
- Tab cycling between widgets (lives in `cd::ui_input::FocusManager` -- the frontend orchestrates which widget owns focus this frame).

## Tests

`ctest --preset ninja-debug -R cd_test_ui_widgets --output-on-failure`. 21 cases:
- Button: click-on-release-inside / drag-outside-cancels / Enter activates focused / draw-without-font smoke.
- TextInput: character insert / backspace at start no-op / arrow + Home + End + unfocused-ignore / Delete at end no-op.
- Slider: click positions value / drag updates while down / Left-Right nudge.
- Toggle: click flips / Space + Enter flip when focused / unfocused ignored.
- Checkbox: click flips / outside-click cancel / `set_value` does not fire callback.
- Dropdown: header click toggles expansion / option click commits + collapses / arrow keys move selection with saturation.
- Modal: Escape closes / outside-click closes / inside-content click stays open / hidden modal ignores Escape.
- Cross-widget: all seven widgets emit draw commands without a font without asserts.

## Notes

- `Button::armed_`, `Toggle::armed_`, `Checkbox::armed_` track the "press happened inside this rect" flag so a drag-outside-then-release does not fire the callback (matches the platform-conventional press / release semantics).
- `Slider::dragging_` tracks the captured-cursor state -- once a press happens inside the track, subsequent moves update the value regardless of whether the cursor is still inside the rect.
- `Dropdown::expanded_` is owned by the widget but the frontend is responsible for routing modal capture (`cd::ui_input::FocusManager::push_modal(dropdown_id)`) so cycling stays inside the expanded list. The widget itself only restricts pointer behaviour, not focus.
- `Modal::visible_` is settable from outside (e.g. a "Show preferences..." button calls `modal.set_visible(true)`). The modal closes itself on Escape / dim-click and fires `on_close` once per close event.
- Widget callbacks (`on_click` / `on_change` / `on_close`) are stored in `std::function<...>`; the empty-function check inside `tick` skips invocation when no callback was set.
- Per ADR contract, widgets do NOT own focus -- they read `InputState::focused` per tick. The frontend (editor / app) consults `cd::ui_input::FocusManager` and stamps the bit on the matching widget's `InputState` before calling `tick`.
