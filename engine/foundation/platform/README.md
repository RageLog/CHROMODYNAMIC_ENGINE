# cd::platform

**Purpose**: window + input + clipboard + display info abstraction. Wraps the per-OS window manager (Win32 / X11 / Wayland / Cocoa) behind a uniform Window + InputEvent API so engine code targets all 5 OS targets without per-platform branches.

**Namespace**: `cd::platform`.

**Headers**: `cd/platform/{Window,InputEvent,Clipboard,Display,KeyCodes,MouseCodes}.hpp`.

**Primary types**:
- `cd::platform::Window` -- top-level OS window. `width()`, `height()`, `poll_events()`, `should_close()`.
- `cd::platform::InputEvent` -- discriminated union (key down/up, mouse move/button/wheel, resize, focus lost, close request).
- `cd::platform::KeyCode` / `MouseButton` -- canonical key + button enums (kEscape, kSpace, kLeft, kRight, ...).
- `cd::platform::Clipboard` -- get/set UTF-8 clipboard string.
- `cd::platform::Display` -- enumerate monitors + DPI scale + current refresh rate.

**Test command**: `ctest --preset ninja-debug -R cd_test_platform --output-on-failure`.

**Notes**:
- Header-only public API; per-OS backends in `cd::platform::detail` (Win32 today; X11/Wayland queued).
- Used by all engine samples for their main window + input loop (hello_engine drives WASD + right-mouse + scroll via this layer).
- The InputEvent visitor pattern lets samples bind only the events they care about without a giant switch.
