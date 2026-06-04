// =============================================================================
// CHROMODYNAMIC -- cd/ui/widgets/NativeWindowAdapter.cpp
//
// Phase 706 — PopoutDock Sprint-2: NativeWindowAdapter implementation.
//
// Multi-window assessment (Win32 / 2026-06-03)
// ---------------------------------------------
// cd::platform::create_window() IS multi-window capable on Win32:
//   - The Win32Window backend handles ERROR_CLASS_ALREADY_EXISTS gracefully
//     (Win32Window.cpp line 164-169), allowing multiple simultaneous windows.
//   - Each call returns an independent unique_ptr<IWindow> with its own HWND,
//     event queue (pending_), and lifecycle.
//   - No singleton guard exists in any platform TU.
//
// GAP-1: WindowDesc lacks x/y position.
//   Workaround: NativeWindowAdapter calls platform_try_set_position() which
//   uses SetWindowPos on Win32 (HWND extracted from native_window_handle()).
//   On other platforms the call is a no-op until IWindow gains set_position().
//
// GAP-2: No aggregate pump_events for multiple windows.
//   Workaround: pump_all() iterates managed_ and calls pump_events on each.
// =============================================================================
#include <cd/ui/widgets/NativeWindowAdapter.hpp>

#include <cd/platform/Window.hpp>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

#include <algorithm>
#include <cstdint>
#include <string>

namespace cd::ui::widgets
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

/// GAP-1 workaround: attempt to move the native window to (x, y).
/// On Win32 this is a one-liner; on other platforms this is a no-op until
/// IWindow::set_position(x, y) is added.
void platform_try_set_position(cd::platform::IWindow& win,
                                std::int32_t x,
                                std::int32_t y) noexcept
{
#if defined(_WIN32)
    auto* hwnd = static_cast<HWND>(win.native_window_handle());
    if (hwnd != nullptr)
    {
        // SWP_NOSIZE   — don't change the size we just created with.
        // SWP_NOZORDER — keep the Z-order.
        // SWP_NOACTIVATE — don't steal focus from the main window.
        ::SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#else
    // GAP-1: other platform backends need IWindow::set_position(x,y) to
    // support precise initial placement. The window still opens (at the
    // OS-chosen default position) — only initial placement is missing.
    (void)win;
    (void)x;
    (void)y;
#endif
}

}  // namespace

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

ManagedWindow* NativeWindowAdapter::find_(std::string_view panel_id) noexcept
{
    auto it = std::ranges::find_if(managed_,
        [panel_id](const ManagedWindow& m) { return m.panel_id == panel_id; });
    return (it != managed_.end()) ? &(*it) : nullptr;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool NativeWindowAdapter::open(const PopoutWindow& pw)
{
    // Idempotent: already managed.
    if (find_(pw.panel_id) != nullptr) { return false; }

    const cd::platform::WindowDesc desc {
        .title    = pw.panel_id,
        .width    = (pw.size[0] > 0.0F)
                        ? static_cast<std::uint32_t>(pw.size[0])
                        : 400U,
        .height   = (pw.size[1] > 0.0F)
                        ? static_cast<std::uint32_t>(pw.size[1])
                        : 300U,
        .resizable = true,
        .visible   = pw.is_active,
    };

    auto result = cd::platform::create_window(desc);

    if (result)
    {
        // GAP-1 workaround: position the window at the requested coordinates.
        platform_try_set_position(*result.value(),
                                  static_cast<std::int32_t>(pw.position[0]),
                                  static_cast<std::int32_t>(pw.position[1]));

        ManagedWindow mw;
        mw.panel_id = std::string { pw.panel_id };
        mw.window   = std::move(result.value());
        mw.is_stub  = false;
        managed_.push_back(std::move(mw));
        return true;
    }

    // Platform returned an error — record the gap and create a stub entry so
    // callers can still track the panel in managed_windows() and fall back
    // to floating-internal rendering.
    const bool is_not_impl =
        (result.error().code == static_cast<std::uint32_t>(
            cd::platform::platform_errors::Code::kNotImplemented));

    NativeWindowGap gap;
    gap.panel_id    = std::string { pw.panel_id };
    gap.reason      = is_not_impl
        ? "kNotImplemented — no platform backend compiled in for this OS"
        : ("kCreateFailed — OS window creation failed: " +
           std::string { result.error().message });
    gap.unblock_api = is_not_impl
        ? "Implement cd::platform::create_window() + IWindow for this platform, "
          "then NativeWindowAdapter::open() will automatically use it."
        : "Investigate OS-level window creation failure (permissions, "
          "display server availability).";
    gaps_.push_back(std::move(gap));

    ManagedWindow stub;
    stub.panel_id = std::string { pw.panel_id };
    stub.window   = nullptr;
    stub.is_stub  = true;
    managed_.push_back(std::move(stub));
    return false;
}

void NativeWindowAdapter::close(std::string_view panel_id)
{
    const auto it = std::ranges::find_if(managed_,
        [panel_id](const ManagedWindow& m) { return m.panel_id == panel_id; });
    if (it == managed_.end()) { return; }

    // Destroy the OS window (unique_ptr dtor calls IWindow dtor -> DestroyWindow).
    managed_.erase(it);

    // Remove any associated gap record too (keeps gaps() accurate).
    const auto git = std::ranges::find_if(gaps_,
        [panel_id](const NativeWindowGap& g) { return g.panel_id == panel_id; });
    if (git != gaps_.end())
    {
        gaps_.erase(git);
    }
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void NativeWindowAdapter::pump_all(std::vector<TaggedEvent>& out)
{
    for (auto& mw : managed_)
    {
        if (mw.is_stub || mw.window == nullptr) { continue; }

        std::vector<cd::platform::OSEvent> raw;
        // pump_events returns false when window is closing; we rely on
        // collect_closed_panels() for lifecycle — just drain events here.
        static_cast<void>(mw.window->pump_events(raw));

        for (auto& ev : raw)
        {
            out.push_back(TaggedEvent { mw.panel_id, ev });
        }
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

bool NativeWindowAdapter::is_managed(std::string_view panel_id) const noexcept
{
    return std::ranges::any_of(managed_,
        [panel_id](const ManagedWindow& m) { return m.panel_id == panel_id; });
}

}  // namespace cd::ui::widgets
