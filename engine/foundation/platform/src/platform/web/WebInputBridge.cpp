// =============================================================================
// CHROMODYNAMIC — platform/web/WebInputBridge.cpp
// Phase 773 — Emscripten mouse/keyboard → cd::platform::OSEvent bridge.
//
// WebInputBridge is the aggregation layer that owns the global Emscripten
// event handlers when there is no WebWindow object (e.g. during engine
// boot before the canvas IWindow is ready, or in headless test scenarios).
//
// For normal frame-loop operation the handlers are registered by WebWindow
// directly. This TU provides:
//   - cd::platform::web::WebInputBridge: standalone input capture context
//   - Helper functions to inject synthetic events for testing
//
// Architecture:
//   WebInputBridge → push_event() → WebWindow (via observer pointer)
//   OR
//   WebInputBridge → direct OSEvent buffer (headless / pre-window)
//
// Compile-guard: full implementation only under __EMSCRIPTEN__.
// =============================================================================
#include <cd/platform/web/WebInputBridge.hpp>

#include <cd/core/Log.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace cd::platform::web
{

// ---------------------------------------------------------------------------
// WebInputBridge — implementation
// ---------------------------------------------------------------------------

WebInputBridge::WebInputBridge() = default;

WebInputBridge::~WebInputBridge()
{
    detach();
}

void WebInputBridge::attach(WebWindow* window) noexcept
{
    m_window.store(window, std::memory_order_release);
    CD_LOG(Info) << "[web] WebInputBridge attached to WebWindow";
}

void WebInputBridge::detach() noexcept
{
    m_window.store(nullptr, std::memory_order_release);
}

void WebInputBridge::inject(OSEvent ev) noexcept
{
    WebWindow* win = m_window.load(std::memory_order_acquire);
    if (win != nullptr) {
        win->push_event(ev);
    } else {
        std::lock_guard<std::mutex> lk(m_headless_mutex);
        m_headless_queue.push_back(ev);
    }
}

std::vector<OSEvent> WebInputBridge::drain_headless() noexcept
{
    std::lock_guard<std::mutex> lk(m_headless_mutex);
    std::vector<OSEvent> out;
    out.swap(m_headless_queue);
    return out;
}

// ---------------------------------------------------------------------------
// Synthetic event helpers (used by unit tests and automation scripts)
// ---------------------------------------------------------------------------

void WebInputBridge::simulate_key(KeyCode key, bool down) noexcept
{
    OSEvent ev;
    ev.kind = down ? OSEventKind::kKeyDown : OSEventKind::kKeyUp;
    ev.key  = key;
    inject(ev);
}

void WebInputBridge::simulate_mouse_move(float x, float y) noexcept
{
    OSEvent ev;
    ev.kind    = OSEventKind::kMouseMove;
    ev.mouse_x = x;
    ev.mouse_y = y;
    inject(ev);
}

void WebInputBridge::simulate_mouse_button(MouseButton btn, bool down,
                                            float x, float y) noexcept
{
    OSEvent ev;
    ev.kind         = down ? OSEventKind::kMouseButtonDown : OSEventKind::kMouseButtonUp;
    ev.mouse_button = btn;
    ev.mouse_x      = x;
    ev.mouse_y      = y;
    inject(ev);
}

void WebInputBridge::simulate_scroll(float delta) noexcept
{
    OSEvent ev;
    ev.kind  = OSEventKind::kMouseWheel;
    ev.wheel = delta;
    inject(ev);
}

}  // namespace cd::platform::web
