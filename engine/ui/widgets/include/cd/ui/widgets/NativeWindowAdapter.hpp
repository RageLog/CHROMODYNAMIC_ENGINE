// =============================================================================
// CHROMODYNAMIC -- cd/ui/widgets/NativeWindowAdapter.hpp
//
// Phase 706 — PopoutDock Sprint-2: native OS window creation per detached panel.
//
// Design
// ------
// NativeWindowAdapter bridges the cd::ui::widgets::PopoutDock state machine
// (Sprint-1, Phase 689) and cd::platform's IWindow factory.
//
// When the user tears off a panel, apps/editor calls:
//
//   bool ok = adapter.open(popout_window);
//
// If cd::platform::create_window() succeeds, the panel lives in a real OS
// window that the user can move to a second monitor. If the platform returns
// kNotImplemented (e.g. a platform without a compiled window backend), the
// adapter records a NativeWindowGap and falls back to the Sprint-1
// floating-internal rendering path so no crash or silent loss occurs.
//
// Gap documentation (for future platform completions)
// ---------------------------------------------------
// The only gap between current cd::platform and ideal multi-window support:
//
//   GAP-1: WindowDesc has no x/y position field.
//           Workaround: after creation, NativeWindowAdapter calls
//           platform_set_position() which is a thin HWND-typed helper on Win32.
//           Non-Win32 platforms that later implement IWindow can add
//           set_position(x,y) to IWindow and promote the workaround.
//
//   GAP-2: IWindow has no "pump all owned windows" aggregate.
//           Workaround: callers must iterate managed_windows() and call
//           pump_events() on each IWindow per frame. This is explicit and safe.
//
// These gaps do NOT block functionality on Windows (the primary target).
//
// Thread model: single-threaded UI thread. No locking.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/platform/Window.hpp>
#include <cd/ui/widgets/PopoutDock.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cd::ui::widgets
{

// ---------------------------------------------------------------------------
// NativeWindowGap
// ---------------------------------------------------------------------------

/// Recorded when NativeWindowAdapter cannot create a native OS window.
/// Exposes the reason and which cd::platform API would unblock real
/// multi-window support on that platform.
struct NativeWindowGap
{
    std::string panel_id     {};
    std::string reason       {};   ///< Human-readable: e.g. "kNotImplemented — no platform backend compiled"
    std::string unblock_api  {};   ///< Which API to implement: e.g. "cd::platform::create_window + IWindow"
};

// ---------------------------------------------------------------------------
// ManagedWindow
// ---------------------------------------------------------------------------

/// One native OS window managed by NativeWindowAdapter, corresponding to a
/// single detached panel.
struct ManagedWindow
{
    std::string                      panel_id {};
    std::unique_ptr<cd::platform::IWindow> window   {};   ///< Owning handle; null if stub path.
    bool                             is_stub  { false };  ///< true = no real OS window (platform gap).

    ManagedWindow() = default;

    ManagedWindow(const ManagedWindow&)            = delete;
    ManagedWindow& operator=(const ManagedWindow&) = delete;
    ManagedWindow(ManagedWindow&&)                 = default;
    ManagedWindow& operator=(ManagedWindow&&)      = default;

    /// True if this window has been asked to close (user pressed X on the
    /// native title bar) or was never created (stub).
    [[nodiscard]] bool should_close() const noexcept
    {
        if (is_stub || window == nullptr) { return false; }
        return window->should_close();
    }

    [[nodiscard]] std::uint32_t width()  const noexcept { return (window != nullptr) ? window->width()  : 0U; }
    [[nodiscard]] std::uint32_t height() const noexcept { return (window != nullptr) ? window->height() : 0U; }
};

// ---------------------------------------------------------------------------
// NativeWindowAdapter
// ---------------------------------------------------------------------------

/// Bridges PopoutDock and cd::platform to create real OS windows for each
/// detached panel.
///
/// Typical frame loop (apps/editor):
///
///   // --- Input tick ---
///   for (auto& mw : adapter.managed_windows()) {
///       if (mw.window) {
///           std::vector<cd::platform::OSEvent> ev;
///           mw.window->pump_events(ev);
///           // route ev to the panel's input handling
///       }
///   }
///
///   // --- On user detach gesture ---
///   dock.detach_panel("Inspector", pos, sz);
///   adapter.open(*dock.find("Inspector"));   // creates OS window
///
///   // --- On user reattach gesture ---
///   adapter.close("Inspector");
///   dock.reattach_panel("Inspector");
///
///   // --- Harvest native-close events ---
///   adapter.collect_closed_panels([&](std::string_view id) {
///       dock.reattach_panel(id);
///   });
///
class NativeWindowAdapter
{
public:
    NativeWindowAdapter() = default;

    NativeWindowAdapter(const NativeWindowAdapter&)            = delete;
    NativeWindowAdapter& operator=(const NativeWindowAdapter&) = delete;
    NativeWindowAdapter(NativeWindowAdapter&&)                 = default;
    NativeWindowAdapter& operator=(NativeWindowAdapter&&)      = default;

    // ---- Lifecycle --------------------------------------------------------

    /// Open a native OS window for `pw`. The title is set to pw.panel_id.
    /// Returns true when a real OS window was created.
    /// Returns false (stub path) when cd::platform returns kNotImplemented;
    /// the gap is recorded in gaps() so callers can surface it in logs/UI.
    /// Returns false (no-op) when pw.panel_id is already managed.
    [[nodiscard]] bool open(const PopoutWindow& pw);

    /// Destroy the native OS window for `panel_id` and remove it from the
    /// managed list. No-op if not found.
    void close(std::string_view panel_id);

    // ---- Per-frame --------------------------------------------------------

    /// Pump OS events for every managed (non-stub) window.
    /// `out` receives all events tagged with their origin panel_id.
    struct TaggedEvent
    {
        std::string               panel_id {};
        cd::platform::OSEvent     event    {};
    };

    void pump_all(std::vector<TaggedEvent>& out);

    /// Invoke `callback(panel_id)` for every managed window whose native
    /// OS close button has been pressed. The windows are removed from the
    /// managed list before the callback fires so callers may freely call
    /// dock.reattach_panel() inside.
    template <typename Fn>
    void collect_closed_panels(Fn&& callback)
    {
        // Collect IDs first to avoid iterator invalidation during close().
        std::vector<std::string> to_close;
        for (const auto& mw : managed_)
        {
            if (!mw.is_stub && mw.window != nullptr && mw.window->should_close())
            {
                to_close.push_back(mw.panel_id);
            }
        }
        for (const auto& id : to_close)
        {
            close(id);
            callback(static_cast<std::string_view>(id));
        }
    }

    // ---- Query ------------------------------------------------------------

    /// Read-only view of all currently managed windows (real + stub).
    [[nodiscard]] const std::vector<ManagedWindow>& managed_windows() const noexcept
    {
        return managed_;
    }

    /// True when `panel_id` is currently managed (real or stub).
    [[nodiscard]] bool is_managed(std::string_view panel_id) const noexcept;

    /// Gaps recorded when native window creation was not possible.
    /// Empty when all panels got real OS windows.
    [[nodiscard]] const std::vector<NativeWindowGap>& gaps() const noexcept
    {
        return gaps_;
    }

private:
    [[nodiscard]] ManagedWindow* find_(std::string_view panel_id) noexcept;

    std::vector<ManagedWindow>  managed_ {};
    std::vector<NativeWindowGap> gaps_   {};
};

}  // namespace cd::ui::widgets
