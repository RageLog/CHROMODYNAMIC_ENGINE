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
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstdint>
#include <memory>
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
    k0,
    k1,
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
};

// ---- Factory --------------------------------------------------------------

/// Create a platform window. Returns kNotImplemented when the engine was
/// built on a platform without a window backend.
[[nodiscard]] cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc);

}  // namespace cd::platform
