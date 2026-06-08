// =============================================================================
// CHROMODYNAMIC — cd/platform/Window.hpp
// Phase 6 / Sprint S6.1 — cross-platform OS window + event loop.
//
// `IWindow` is the backend-agnostic surface every platform window
// implementation (Win32, Wayland, Xlib, macOS Cocoa, web canvas) plugs
// into. Engines never include the platform headers directly; the
// `create_window()` factory hides every `<windows.h>` / `<X11/Xlib.h>`
// behind a single TU.
//
// Event model:
//   * `pump_events()` drains the OS message queue and appends one
//     `OSEvent` per discrete user action to the supplied output buffer.
//   * `OSEvent` is a tagged union — keyboard / mouse / window-level
//     (close, resize, focus). It is deliberately a *platform-tier* type
//     that mirrors the cd::input event types: applications convert to
//     `cd::input::InputEvent` when piping into the InputContext, keeping
//     cd::platform's dependency footprint to cd::core only.
//   * The factory returns kNotImplemented when no backend is compiled in
//     for the current OS (e.g. Linux without Wayland or X11 headers).
//
// Lifetime: callers own the unique_ptr; closing happens in the dtor.
//
// FINALE-6 W1 / Phase 764 — multi-window primitive (Sprint-1, Windows-native):
//   * `create_window` may be called multiple times within one process; each
//     IWindow owns its own native handle + event queue.
//   * `IWindow::set_parent(IWindow*)` establishes a popout relationship so
//     the child window floats above the parent (Win32: WS_POPUP + SetParent).
//   * `pump_all_windows` is the aggregate drain that iterates every live
//     window so the host loop only has to call one function per frame.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace cd::platform
{

// ---- Error domain -----------------------------------------------------------

namespace platform_errors
{
inline constexpr std::uint32_t kDomain = 0x0011;

enum class Code : std::uint32_t
{
    kOk = 0,
    kInvalidArgument = 1,
    kCreateFailed = 2,
    kNotImplemented = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace platform_errors

// ---- Keyboard / mouse mirror enums ----------------------------------------
//
// Identical layout to `cd::input::KeyCode` / `cd::input::MouseButton` — the
// duplication keeps the dependency DAG one-way (input depends on nothing
// from platform). Applications converting between the two use a flat
// `static_cast` pair the compiler folds to a no-op.

enum class KeyCode : std::uint16_t
{
    kUnknown = 0,
    kA,
    kB,
    kC,
    kD,
    kE,
    kF,
    kG,
    kH,
    kI,
    kJ,
    kK,
    kL,
    kM,
    kN,
    kO,
    kP,
    kQ,
    kR,
    kS,
    kT,
    kU,
    kV,
    kW,
    kX,
    kY,
    kZ,
    // phase993-misc-confusable-suppress: NOLINT on the digit-key
    // declarations that trigger the rule (k0 vs kO 'oh' letter,
    // k1 vs kI 'eye' letter). This is the canonical keyboard-key
    // enum convention -- renaming to kDigit0 would diverge from
    // every engine SDK in the ecosystem (Win32 VK_0, GLFW
    // GLFW_KEY_0, SDL2 SDLK_0, Unity KeyCode.Alpha0). The
    // suppressions are SPECIFIC rather than blanket-disabling the
    // rule so future identifier collisions in OTHER namespaces are
    // still caught.
    k0,  // NOLINT(misc-confusable-identifiers)  -- vs kO
    k1,  // NOLINT(misc-confusable-identifiers)  -- vs kI
    k2,
    k3,
    k4,
    k5,
    k6,
    k7,
    k8,
    k9,
    kSpace,
    kEnter,
    kEscape,
    kTab,
    kBackspace,
    kDelete,
    kLShift,
    kRShift,
    kLCtrl,
    kRCtrl,
    kLAlt,
    kRAlt,
    kLeft,
    kRight,
    kUp,
    kDown,
    kF1,
    kF2,
    kF3,
    kF4,
    kF5,
    kF6,
    kF7,
    kF8,
    kF9,
    kF10,
    kF11,
    kF12,
    kCount,
};

enum class MouseButton : std::uint8_t
{
    kLeft = 0,
    kRight,
    kMiddle,
    kX1,
    kX2,
    kCount,
};

// ---- OS-level event -------------------------------------------------------

enum class OSEventKind : std::uint8_t
{
    kClose,   ///< User requested window close (X / Alt-F4).
    kResize,  ///< Client area resized; new size in OSEvent::width/height.
    kFocusGained,
    kFocusLost,
    kKeyDown,
    kKeyUp,
    kMouseMove,
    kMouseButtonDown,
    kMouseButtonUp,
    kMouseWheel,
    kTextChar,  ///< Translated character (WM_CHAR / equivalent). Phase 15.E.
};

struct OSEvent
{
    OSEventKind kind { OSEventKind::kClose };
    /// Resize event payload — pixel size of the client area.
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    /// Keyboard payload — valid only for kKeyDown / kKeyUp.
    KeyCode key { KeyCode::kUnknown };
    /// Mouse-button payload — valid only for kMouseButton{Down,Up}.
    MouseButton mouse_button { MouseButton::kLeft };
    /// Mouse coordinates in client-pixel space (top-left = 0,0).
    float mouse_x { 0.0F };
    float mouse_y { 0.0F };
    /// Wheel delta (positive = scroll up / forward).
    float wheel { 0.0F };
    /// Unicode code-point for kTextChar (Win32 WM_CHAR low word,
    /// XLib XLookupString equivalent). ImGui consumes via
    /// AddInputCharacter for InputText/InputFloat widgets.
    std::uint32_t code_point { 0 };
};

// ---- Window description ---------------------------------------------------

struct WindowDesc
{
    std::string_view title { "chromodynamic" };
    std::uint32_t width { 1280 };
    std::uint32_t height { 720 };
    bool resizable { true };
    /// Show the window immediately on creation. Tests may want false so a
    /// hidden window can be hit by Vulkan surface tests without flicker.
    bool visible { true };
};

// ---- Interface ------------------------------------------------------------

class IWindow
{
public:
    IWindow() noexcept = default;
    virtual ~IWindow() = default;
    IWindow(const IWindow&) = delete;
    IWindow& operator=(const IWindow&) = delete;
    IWindow(IWindow&&) = delete;
    IWindow& operator=(IWindow&&) = delete;

    /// Drain the OS message queue. Pending events are appended to `out`.
    /// Returns false when the window has been asked to close — the typical
    /// idiom is `while (window->pump_events(events)) { ... }`.
    [[nodiscard]] virtual bool pump_events(std::vector<OSEvent>& out) = 0;

    [[nodiscard]] virtual bool should_close() const noexcept = 0;
    /// Programmatic close request. Subsequent `pump_events` returns false.
    virtual void request_close() noexcept = 0;

    /// Native handles for swapchain integration. The pair feeds straight
    /// into `cd::rhi::SwapchainDesc::window_handle` / `display_handle`.
    ///   * Win32:   (HWND, HINSTANCE)
    ///   * Wayland: (wl_surface*, wl_display*)
    ///   * Xlib:    (Window, Display*)   — Window cast to (void*) via uintptr_t.
    ///   * Cocoa:   (CAMetalLayer*, nullptr)
    [[nodiscard]] virtual void* native_window_handle() const noexcept = 0;
    [[nodiscard]] virtual void* native_display_handle() const noexcept = 0;

    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;

    virtual void set_title(std::string_view title) = 0;

    /// Establish a parent/child popout relationship between two real OS
    /// windows. Sprint-1 contract (Phase 764 / FINALE-6 W1):
    ///   * Win32: re-styles the child as WS_POPUP and calls SetParent so the
    ///     child floats on top of `parent` and follows its z-order. Passing
    ///     `nullptr` detaches.
    ///   * Other backends: no-op (override available; default ignores).
    /// Returns true when a parent link was actually established. A backend
    /// that does not yet support multi-window parenting returns false.
    [[nodiscard]] virtual bool set_parent(IWindow* parent) noexcept
    {
        (void) parent;
        return false;
    }
};

// ---- Factory --------------------------------------------------------------

/// Create a platform window. Returns kNotImplemented when the engine was
/// built on a platform without a window backend.
///
/// FINALE-6 W1 / Phase 764 — Multiple cd::platform::IWindow instances may
/// coexist in the same process. Each window owns its own native handle and
/// its own event queue; `pump_events` drains only that window's queue.
/// Use `pump_all_windows` to drain every live window in one call.
[[nodiscard]] cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc);

// ---- Multi-window pump ----------------------------------------------------

/// Drain the OS message queue for every window in `windows` and append the
/// resulting events to `out`. Returns false only when every window has
/// signalled close (i.e. the application can exit its main loop). Returns
/// true while at least one window is still alive.
///
/// Sprint-1 (Phase 764) — Windows-native multi-window primitive backing the
/// "Inspector on a second monitor" moment from FINALE-6 W1. Non-Win32
/// backends inherit the same contract via per-window pump_events.
///
/// Implementation is inline because it only touches the IWindow public
/// interface and therefore needs no per-OS translation unit.
[[nodiscard]] inline bool pump_all_windows(std::span<IWindow* const> windows, std::vector<OSEvent>& out)
{
    bool any_alive = false;
    for (auto* w : windows)
    {
        if (w == nullptr)
            continue;
        if (w->should_close())
            continue;
        if (w->pump_events(out))
            any_alive = true;
    }
    return any_alive;
}

}  // namespace cd::platform
