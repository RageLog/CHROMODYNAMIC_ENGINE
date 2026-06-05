// =============================================================================
// CHROMODYNAMIC — cd/platform/web/WebWindow.hpp
// Phase 773 — Web platform: HTMLCanvasElement wrapper via Emscripten.
//
// WebWindow implements cd::platform::IWindow over an Emscripten canvas.
// The canvas ID defaults to "#canvas" (matching the HTML shell); callers may
// supply a different CSS selector via WebWindowDesc.
//
// Lifecycle:
//   1. Construct WebWindow (registers Emscripten HTML5 event handlers).
//   2. Call pump_events() each frame — translates buffered Emscripten input
//      events into cd::platform::OSEvent and appends them to the output.
//   3. Destroy: handlers are automatically unregistered in the destructor.
//
// Thread-safety: single-threaded; all Emscripten callbacks fire on the main
// browser thread (JS event loop). Do not call from a worker thread.
//
// Dependency: Emscripten-only. Compile-guarded by #ifdef __EMSCRIPTEN__.
// On non-Emscripten targets this header compiles to an empty namespace stub
// so including it from shared headers does not break desktop builds.
// =============================================================================
#pragma once

#include <cd/platform/Window.hpp>

#include <cd/core/Defines.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace cd::platform::web
{

// ---- WebWindow description ------------------------------------------------

struct WebWindowDesc
{
    /// CSS selector for the target canvas element. Default: "#canvas".
    const char* canvas_selector { "#canvas" };
    /// Logical (CSS-pixel) width; the backing framebuffer uses device-pixel-ratio.
    std::uint32_t width { 1280 };
    /// Logical (CSS-pixel) height.
    std::uint32_t height { 720 };
    /// Window title (shown in document title bar if browser supports it).
    const char* title { "CHROMODYNAMIC" };
};

// ---- WebWindow ------------------------------------------------------------

/// IWindow implementation for an Emscripten HTMLCanvasElement.
///
/// On non-Emscripten targets the class compiles as a thin stub that returns
/// kNotImplemented from the factory so desktop CI stays green.
class WebWindow final : public IWindow
{
public:
    /// Factory — preferred construction path. Returns a valid window or an
    /// error (e.g. canvas element not found, not running under Emscripten).
    [[nodiscard]] static cd::core::Result<std::unique_ptr<WebWindow>>
    create(const WebWindowDesc& desc) noexcept;

    ~WebWindow() override;

    WebWindow(const WebWindow&) = delete;
    WebWindow& operator=(const WebWindow&) = delete;
    WebWindow(WebWindow&&) = delete;
    WebWindow& operator=(WebWindow&&) = delete;

    // ---- IWindow interface ------------------------------------------------

    [[nodiscard]] bool pump_events(std::vector<OSEvent>& out) override;

    [[nodiscard]] bool should_close() const noexcept override
    {
        return m_should_close.load(std::memory_order_relaxed);
    }

    void request_close() noexcept override
    {
        m_should_close.store(true, std::memory_order_relaxed);
    }

    /// Web platform: canvas element has no native OS window/display handle.
    /// Returns the canvas CSS selector as a const char* cast to void*.
    /// RHI WebGPU backend retrieves the canvas via emscripten_webgpu_get_device().
    [[nodiscard]] void* native_window_handle() const noexcept override
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        return static_cast<void*>(const_cast<char*>(m_canvas_selector.c_str()));
    }

    [[nodiscard]] void* native_display_handle() const noexcept override
    {
        return nullptr;  // Not applicable on web platform.
    }

    [[nodiscard]] std::uint32_t width() const noexcept override  { return m_width; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return m_height; }

    void set_title(std::string_view title) override;

    // ---- Web-specific API -------------------------------------------------

    /// Returns the CSS selector string supplied at construction time.
    [[nodiscard]] const std::string& canvas_selector() const noexcept
    {
        return m_canvas_selector;
    }

    /// Push a pre-translated OSEvent from an Emscripten callback.
    /// Thread-safe: may be called from the browser event thread.
    void push_event(OSEvent ev) noexcept;

private:
    explicit WebWindow(const WebWindowDesc& desc);

    /// Register all Emscripten HTML5 event handlers.
    void register_handlers() noexcept;

    /// Unregister Emscripten HTML5 event handlers.
    void unregister_handlers() noexcept;

    std::string           m_canvas_selector;
    std::uint32_t         m_width;
    std::uint32_t         m_height;
    std::atomic<bool>     m_should_close { false };

    // Pending event queue — written by Emscripten callbacks, read by pump_events.
    mutable std::mutex    m_event_mutex;
    std::vector<OSEvent>  m_event_queue;
};

}  // namespace cd::platform::web
