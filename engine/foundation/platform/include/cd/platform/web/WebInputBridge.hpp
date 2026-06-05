// =============================================================================
// CHROMODYNAMIC — cd/platform/web/WebInputBridge.hpp
// Phase 773 — Emscripten input event aggregation bridge.
//
// WebInputBridge provides a standalone input capture context that forwards
// Emscripten mouse/keyboard events to an attached WebWindow, or buffers them
// in a headless queue when no window is attached (useful during boot and
// in offline tests).
//
// Typical usage in web_demo main_web.cpp:
//   auto bridge = std::make_unique<cd::platform::web::WebInputBridge>();
//   bridge->attach(web_window.get());
//   // ... frame loop ...
//   bridge->detach();  // or let destructor handle it.
//
// Test usage:
//   WebInputBridge bridge;
//   bridge.simulate_key(KeyCode::kA, /*down=*/true);
//   auto events = bridge.drain_headless();
//   EXPECT_EQ(events[0].kind, OSEventKind::kKeyDown);
// =============================================================================
#pragma once

#include <cd/platform/Window.hpp>
#include <cd/platform/web/WebWindow.hpp>

#include <atomic>
#include <mutex>
#include <vector>

namespace cd::platform::web
{

/// Standalone input aggregation bridge.
///
/// Thread-safety: inject() is thread-safe (protected by mutex / atomic).
/// attach()/detach() should be called from the main thread.
class WebInputBridge
{
public:
    WebInputBridge();
    ~WebInputBridge();

    WebInputBridge(const WebInputBridge&) = delete;
    WebInputBridge& operator=(const WebInputBridge&) = delete;
    WebInputBridge(WebInputBridge&&) = delete;
    WebInputBridge& operator=(WebInputBridge&&) = delete;

    // ---- Attachment --------------------------------------------------------

    /// Attach to a WebWindow. Subsequent inject() calls forward events to it.
    void attach(WebWindow* window) noexcept;

    /// Detach from the current WebWindow. Events are queued headlessly.
    void detach() noexcept;

    // ---- Event injection ---------------------------------------------------

    /// Inject a pre-built OSEvent. Forwards to attached window or headless queue.
    void inject(OSEvent ev) noexcept;

    /// Drain and return all events accumulated in headless mode (no window).
    [[nodiscard]] std::vector<OSEvent> drain_headless() noexcept;

    // ---- Simulation helpers (tests / automation) ---------------------------

    void simulate_key(KeyCode key, bool down) noexcept;
    void simulate_mouse_move(float x, float y) noexcept;
    void simulate_mouse_button(MouseButton btn, bool down, float x, float y) noexcept;
    void simulate_scroll(float delta) noexcept;

private:
    std::atomic<WebWindow*> m_window { nullptr };

    mutable std::mutex    m_headless_mutex;
    std::vector<OSEvent>  m_headless_queue;
};

}  // namespace cd::platform::web
