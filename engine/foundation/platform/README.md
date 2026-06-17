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
- `IWindow` is the backend-agnostic interface; the `create_window()` factory hides every per-OS TU. Desktop backends Win32 (`Win32Window.cpp`) and X11 (`X11Window.cpp`, opt-in `-DCD_PLATFORM_XLIB=ON`) are complete and the supported v1 path; macOS Cocoa, iOS UIKit, Android NativeActivity and Web/Emscripten backends are real platform-specific ports compiled only under their own SDK (`CD_PLATFORM_COCOA`, `IOS`, `ANDROID`, `EMSCRIPTEN`) — promote-on-need per ADR-20260616-band4-singletons-scope §platform. Wayland stays on the backlog.
- Used by all engine samples for their main window + input loop (hello_engine drives WASD + right-mouse + scroll via this layer).
- The InputEvent visitor pattern lets samples bind only the events they care about without a giant switch.
