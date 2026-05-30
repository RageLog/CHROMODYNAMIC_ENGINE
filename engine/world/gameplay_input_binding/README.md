# cd::gameplay_input_binding

## Purpose
Action-mapping (input remapping) layer one tier above raw `cd::input` polling.
Gameplay code asks "is action `move_forward` pressed?" instead of "is W
down?". Bindings live in data (config file, settings UI, key-rebind dialog)
so swapping device or rebinding mid-game is a data change, not a code change.

## Namespace
`cd::gameplay::input_binding`

## Public headers
- `include/cd/gameplay/input_binding/InputBinding.hpp` — `ActionKind` enum,
  `InputBinding` struct, `RawInputSnapshot`, `ActionMap` class.

## Primary types
- `ActionKind` — `kButton`, `kAxis`, `kVector2` discriminator (informational
  hint; the resolver inspects the binding's device + encoded code instead).
- `InputBinding` — `{ action_name, device, scancode_or_button }`. `device` is
  a free-form short string (`"keyboard"`, `"gamepad"`, `"mouse"`,
  `"gamepad_axis"`, ...). The interpretation of `scancode_or_button` is
  device-side (cast from `cd::input::KeyCode`, `GamepadButton`, etc.).
- `RawInputSnapshot` — POD frame fed into `ActionMap::update()`. Two parallel
  maps: `buttons[device]` set of pressed codes, `axes[device]` per-id float.
- `ActionMap` — bind / unbind / rebind / on_action / update / query.

## Semantics
| Operation                      | Effect                                                     |
|-------------------------------|------------------------------------------------------------|
| `bind(action, binding)`       | Append unique `(device, code)`; returns false on duplicate |
| `unbind(action, binding)`     | Remove the matching `(device, code)` entry                 |
| `unbind_all(action)`          | Drop every binding under `action` (callbacks kept)         |
| `rebind(action, binding)`     | `unbind_all` + `bind` (settings-UI primitive)              |
| `on_action(action, cb)`       | Register rising-edge callback                              |
| `clear_callbacks(action)`     | Drop callbacks, keep bindings                              |
| `update(snapshot)`            | Resolve every binding; fire rising-edge callbacks once     |
| `is_action_pressed(action)`   | Current held state (axis: `\|v\| >= 0.5`)                  |
| `axis_value(action)`          | Signed scalar; 1.0 if pressed button, 0.0 if released      |
| `clear()`                     | Wipe everything                                            |

### Edge-trigger semantics
Callbacks fire **once per rising edge**, not continuously while held — matches
Unity / Unreal / Godot behaviour and is what gameplay code (`jump_on_press`)
actually wants. Use `is_action_pressed()` for while-held polling (sprint,
crouch, fire-held).

### Axis encoding
`scancode_or_button` for axis-style devices encodes `(axis_id, invert)` via
`encode_axis(axis_id, invert)`. Decode with `axis_index(encoded)` /
`axis_invert(encoded)`. Press threshold for axis -> button bridge is `0.5`,
matching Unity's `InputActionType.Button` default.

### Multi-device
The same action can bind to keyboard AND gamepad simultaneously; either source
satisfies "pressed". This is the standard "control schemes" pattern from
Unity's Input System and Unreal's Enhanced Input.

## Usage example
```cpp
#include <cd/gameplay/input_binding/InputBinding.hpp>
#include <cd/input/Input.hpp>

cd::gameplay::input_binding::ActionMap input;

// Bindings — usually loaded from a config file.
input.bind_button("jump",   "keyboard", static_cast<int>(cd::input::KeyCode::kSpace));
input.bind_button("jump",   "gamepad",  static_cast<int>(cd::input::GamepadButton::kA));
input.bind_axis  ("move_x", "gamepad_axis", /*axis_id=*/0);

input.on_action("jump", [](const std::string&) {
    player.try_jump();
});

while (running) {
    cd::gameplay::input_binding::RawInputSnapshot snap;
    // Translate cd::input::InputState into the device-agnostic POD.
    for (int k = 0; k < /*KeyCode::kCount*/ 0; ++k) {
        if (input_state.is_key_down(static_cast<cd::input::KeyCode>(k)))
            snap.press("keyboard", k);
    }
    snap.set_axis("gamepad_axis", 0, pad_state.left_stick_x);

    input.update(snap);

    if (input.is_action_pressed("crouch"))
        player.crouch();
    player.move_x(input.axis_value("move_x"));
}
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_gameplay_input_binding
ctest --preset ninja-debug -R gameplay_input_binding --output-on-failure
```

## Dependencies
- `cd::core` — `Defines.hpp` (header-level dependency only).
- `cd::input` is intentionally **not** a public dependency: callers translate
  raw input into `RawInputSnapshot` at the call site so this library stays
  reusable in headless tests, replay tapes, and alternate backends.

## References
- Unreal Engine `UInputComponent::BindAction` + Enhanced Input `IA_*` assets.
- Unity `InputActionMap` + composite bindings + control schemes.
- Godot `InputMap` (`add_action`, `action_add_event`).
- Bevy `bevy_input::Input<KeyCode>` + Leafwing input manager crate.

## Notes
- Single TU (`src/InputBinding.cpp`); header-only would force every consumer
  to recompile on internal tweaks.
- All public operations are `noexcept` where the spec allows
  (`is_action_pressed`, `axis_value`, `binding_count`, `clear`); allocating
  operations (`bind`, `on_action`, `update`) are not.
- Not thread-safe by design: drive from the same thread that owns the input
  pump (engine main loop).
