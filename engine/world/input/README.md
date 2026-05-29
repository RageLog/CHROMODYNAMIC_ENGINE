# cd::input

**Purpose**: Input handling abstraction for keyboard, mouse, gamepad, and touch. Provides keystroke recognition (single, hold, double-click), axis mapping, drag state tracking, and cursor control. Decouples application logic from OS input events.

**Namespace**: `cd::input`.

**Public Headers**:
- `cd/input/Input.hpp` — main input pump; keyboard, mouse, gamepad, touch state.
- `cd/input/Bindings.hpp` — action binding (key/button → logical action with priority).
- `cd/input/KeyChord.hpp` — multi-key combinations (Ctrl+S, Shift+Click).
- `cd/input/Axis.hpp` — normalized axis input (-1..+1 range) for stick/trigger.
- `cd/input/DoubleClick.hpp` — detects double-click within time window.
- `cd/input/Hold.hpp` — detects key held for minimum duration (long-press).
- `cd/input/MouseDragState.hpp` — drag tracking (origin, current, delta).
- `cd/input/GamepadState.hpp` — unified gamepad state (sticks, triggers, buttons).
- `cd/input/Cursor.hpp` — cursor position, visibility, icon state.

**Primary Types**:
- `Input` — global input state + update methods.
- `Bindings` — action binding table with conflict resolution.
- `KeyChord` — multi-key chord (modifier + main key).
- `MouseDragState` — drag tracking and velocity computation.
- `GamepadState` — normalized gamepad input.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_input
ctest --preset ninja-debug -R input --output-on-failure
```

**Dependencies**: cd::core.

**Notes**:
- No OS/event loop coupling; Input pump is called explicitly each frame.
- Gamepad support includes both XINPUT (Windows) and generic HID paths.
- Drag state includes inertial scrolling support (delta velocity).
- All input is frame-snapped for deterministic replay / networking.
