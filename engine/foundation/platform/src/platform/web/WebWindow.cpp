// =============================================================================
// CHROMODYNAMIC — platform/web/WebWindow.cpp
// Phase 773 — HTMLCanvasElement wrapper (Emscripten).
//
// Implements cd::platform::web::WebWindow: event registration, OS-event
// translation, and canvas resize handling.
//
// Compile-guard: the entire TU is elided on non-Emscripten targets via the
// EMSCRIPTEN macro; on desktop CI the WebWindow factory returns kNotImplemented
// so tests stay green without any Emscripten headers on PATH.
// =============================================================================
#include <cd/platform/web/WebWindow.hpp>

#include <cd/core/Log.hpp>

#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

// ---------------------------------------------------------------------------
// Emscripten HTML5 callback trampoline helpers
// ---------------------------------------------------------------------------
namespace
{

/// Retrieve the WebWindow* stored as user_data in Emscripten callbacks.
cd::platform::web::WebWindow* window_from(void* user_data) noexcept
{
    return static_cast<cd::platform::web::WebWindow*>(user_data);
}

// ---- Key translation -------------------------------------------------------

cd::platform::KeyCode translate_key(const EmscriptenKeyboardEvent* ev) noexcept
{
    using K = cd::platform::KeyCode;
    // Map the DOM key code string to cd::platform::KeyCode.
    const char* code = ev->code;
    // Letters
    if (code[0] == 'K' && code[1] == 'e' && code[2] == 'y') {
        char ch = code[3];
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<K>(static_cast<int>(K::kA) + (ch - 'A'));
        }
    }
    // Digits
    if (code[0] == 'D' && code[1] == 'i' && code[2] == 'g' && code[3] == 'i' &&
        code[4] == 't') {
        char ch = code[5];
        if (ch >= '0' && ch <= '9') {
            return static_cast<K>(static_cast<int>(K::k0) + (ch - '0'));
        }
    }
    // Special keys
    if (__builtin_strcmp(code, "Space") == 0)     { return K::kSpace; }
    if (__builtin_strcmp(code, "Enter") == 0)     { return K::kEnter; }
    if (__builtin_strcmp(code, "Escape") == 0)    { return K::kEscape; }
    if (__builtin_strcmp(code, "Tab") == 0)       { return K::kTab; }
    if (__builtin_strcmp(code, "Backspace") == 0) { return K::kBackspace; }
    if (__builtin_strcmp(code, "Delete") == 0)    { return K::kDelete; }
    if (__builtin_strcmp(code, "ShiftLeft") == 0)   { return K::kLShift; }
    if (__builtin_strcmp(code, "ShiftRight") == 0)  { return K::kRShift; }
    if (__builtin_strcmp(code, "ControlLeft") == 0) { return K::kLCtrl; }
    if (__builtin_strcmp(code, "ControlRight") == 0){ return K::kRCtrl; }
    if (__builtin_strcmp(code, "AltLeft") == 0)     { return K::kLAlt; }
    if (__builtin_strcmp(code, "AltRight") == 0)    { return K::kRAlt; }
    if (__builtin_strcmp(code, "ArrowLeft") == 0)   { return K::kLeft; }
    if (__builtin_strcmp(code, "ArrowRight") == 0)  { return K::kRight; }
    if (__builtin_strcmp(code, "ArrowUp") == 0)     { return K::kUp; }
    if (__builtin_strcmp(code, "ArrowDown") == 0)   { return K::kDown; }
    // Function keys F1–F12
    if (code[0] == 'F') {
        int n = 0;
        for (int i = 1; code[i] != '\0'; ++i) {
            if (code[i] >= '0' && code[i] <= '9') {
                n = n * 10 + (code[i] - '0');
            }
        }
        if (n >= 1 && n <= 12) {
            return static_cast<K>(static_cast<int>(K::kF1) + (n - 1));
        }
    }
    return K::kUnknown;
}

// ---- Mouse button translation ----------------------------------------------

cd::platform::MouseButton translate_mouse_button(int button) noexcept
{
    switch (button) {
        case 0: return cd::platform::MouseButton::kLeft;
        case 1: return cd::platform::MouseButton::kMiddle;
        case 2: return cd::platform::MouseButton::kRight;
        case 3: return cd::platform::MouseButton::kX1;
        case 4: return cd::platform::MouseButton::kX2;
        default: return cd::platform::MouseButton::kLeft;
    }
}

// ---- HTML5 event callbacks -------------------------------------------------

EM_BOOL on_keydown(int /*event_type*/, const EmscriptenKeyboardEvent* ev, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind = cd::platform::OSEventKind::kKeyDown;
    ose.key  = translate_key(ev);
    window_from(user_data)->push_event(ose);
    return EM_FALSE;  // Allow browser default (e.g. F5 reload).
}

EM_BOOL on_keyup(int /*event_type*/, const EmscriptenKeyboardEvent* ev, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind = cd::platform::OSEventKind::kKeyUp;
    ose.key  = translate_key(ev);
    // ESC closes the loop.
    if (ose.key == cd::platform::KeyCode::kEscape) {
        window_from(user_data)->request_close();
    }
    window_from(user_data)->push_event(ose);
    return EM_FALSE;
}

EM_BOOL on_mouse_move(int /*event_type*/, const EmscriptenMouseEvent* ev, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind    = cd::platform::OSEventKind::kMouseMove;
    ose.mouse_x = static_cast<float>(ev->targetX);
    ose.mouse_y = static_cast<float>(ev->targetY);
    window_from(user_data)->push_event(ose);
    return EM_FALSE;
}

EM_BOOL on_mouse_down(int /*event_type*/, const EmscriptenMouseEvent* ev, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind         = cd::platform::OSEventKind::kMouseButtonDown;
    ose.mouse_button = translate_mouse_button(ev->button);
    ose.mouse_x      = static_cast<float>(ev->targetX);
    ose.mouse_y      = static_cast<float>(ev->targetY);
    window_from(user_data)->push_event(ose);
    return EM_FALSE;
}

EM_BOOL on_mouse_up(int /*event_type*/, const EmscriptenMouseEvent* ev, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind         = cd::platform::OSEventKind::kMouseButtonUp;
    ose.mouse_button = translate_mouse_button(ev->button);
    ose.mouse_x      = static_cast<float>(ev->targetX);
    ose.mouse_y      = static_cast<float>(ev->targetY);
    window_from(user_data)->push_event(ose);
    return EM_FALSE;
}

EM_BOOL on_wheel(int /*event_type*/, const EmscriptenWheelEvent* ev, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind  = cd::platform::OSEventKind::kMouseWheel;
    ose.wheel = static_cast<float>(-ev->deltaY);  // positive = scroll forward.
    window_from(user_data)->push_event(ose);
    return EM_TRUE;  // Prevent page scroll while canvas has focus.
}

EM_BOOL on_resize(int /*event_type*/, const EmscriptenUiEvent* /*ev*/, void* user_data)
{
    // Emscripten exposes the canvas size; query it directly.
    auto* win = window_from(user_data);

    double w = 0.0;
    double h = 0.0;
    emscripten_get_element_css_size(win->canvas_selector().c_str(), &w, &h);

    cd::platform::OSEvent ose;
    ose.kind   = cd::platform::OSEventKind::kResize;
    ose.width  = static_cast<std::uint32_t>(w);
    ose.height = static_cast<std::uint32_t>(h);
    win->push_event(ose);
    return EM_FALSE;
}

EM_BOOL on_focus(int /*event_type*/, const EmscriptenFocusEvent* /*ev*/, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind = cd::platform::OSEventKind::kFocusGained;
    window_from(user_data)->push_event(ose);
    return EM_FALSE;
}

EM_BOOL on_blur(int /*event_type*/, const EmscriptenFocusEvent* /*ev*/, void* user_data)
{
    cd::platform::OSEvent ose;
    ose.kind = cd::platform::OSEventKind::kFocusLost;
    window_from(user_data)->push_event(ose);
    return EM_FALSE;
}

}  // namespace

// ---------------------------------------------------------------------------
// WebWindow implementation
// ---------------------------------------------------------------------------

namespace cd::platform::web
{

WebWindow::WebWindow(const WebWindowDesc& desc)
    : m_canvas_selector(desc.canvas_selector)
    , m_width(desc.width)
    , m_height(desc.height)
{
}

WebWindow::~WebWindow()
{
    unregister_handlers();
}

// static
cd::core::Result<std::unique_ptr<WebWindow>>
WebWindow::create(const WebWindowDesc& desc) noexcept
{
    auto win = std::unique_ptr<WebWindow>(new WebWindow(desc));
    win->register_handlers();

    // Sync initial canvas dimensions.
    double css_w = 0.0;
    double css_h = 0.0;
    emscripten_get_element_css_size(win->m_canvas_selector.c_str(), &css_w, &css_h);
    if (css_w > 0.0) { win->m_width  = static_cast<std::uint32_t>(css_w); }
    if (css_h > 0.0) { win->m_height = static_cast<std::uint32_t>(css_h); }

    CD_LOG(Info) << "[web] WebWindow created: canvas='" << win->m_canvas_selector
                 << "' size=" << win->m_width << 'x' << win->m_height;

    return win;
}

void WebWindow::register_handlers() noexcept
{
    const char* sel = m_canvas_selector.c_str();
    void* ud = static_cast<void*>(this);

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, ud, EM_TRUE, on_keydown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,   ud, EM_TRUE, on_keyup);
    emscripten_set_mousemove_callback(sel,  ud, EM_TRUE, on_mouse_move);
    emscripten_set_mousedown_callback(sel,  ud, EM_TRUE, on_mouse_down);
    emscripten_set_mouseup_callback(sel,    ud, EM_TRUE, on_mouse_up);
    emscripten_set_wheel_callback(sel,      ud, EM_TRUE, on_wheel);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, ud, EM_TRUE, on_resize);
    emscripten_set_focus_callback(sel,      ud, EM_TRUE, on_focus);
    emscripten_set_blur_callback(sel,       ud, EM_TRUE, on_blur);
}

void WebWindow::unregister_handlers() noexcept
{
    // Pass nullptr callback to Emscripten to clear the handler slot.
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_FALSE, nullptr);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT,   nullptr, EM_FALSE, nullptr);
    const char* sel = m_canvas_selector.c_str();
    emscripten_set_mousemove_callback(sel, nullptr, EM_FALSE, nullptr);
    emscripten_set_mousedown_callback(sel, nullptr, EM_FALSE, nullptr);
    emscripten_set_mouseup_callback(sel,   nullptr, EM_FALSE, nullptr);
    emscripten_set_wheel_callback(sel,     nullptr, EM_FALSE, nullptr);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
    emscripten_set_focus_callback(sel, nullptr, EM_FALSE, nullptr);
    emscripten_set_blur_callback(sel,  nullptr, EM_FALSE, nullptr);
}

bool WebWindow::pump_events(std::vector<OSEvent>& out)
{
    if (m_should_close.load(std::memory_order_relaxed)) {
        return false;
    }
    std::lock_guard<std::mutex> lk(m_event_mutex);
    for (const auto& ev : m_event_queue) {
        if (ev.kind == OSEventKind::kResize) {
            m_width  = ev.width;
            m_height = ev.height;
        }
        out.push_back(ev);
    }
    m_event_queue.clear();
    return true;
}

void WebWindow::set_title(std::string_view title)
{
    // Update the browser document title.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    EM_ASM({ document.title = UTF8ToString($0); }, title.data());
}

void WebWindow::push_event(OSEvent ev) noexcept
{
    std::lock_guard<std::mutex> lk(m_event_mutex);
    m_event_queue.push_back(ev);
}

}  // namespace cd::platform::web

#else  // !__EMSCRIPTEN__

// ---------------------------------------------------------------------------
// Desktop stub — keep the TU non-empty so the static library links cleanly.
// ---------------------------------------------------------------------------
namespace cd::platform::web
{

cd::core::Result<std::unique_ptr<WebWindow>>
WebWindow::create(const WebWindowDesc& /*desc*/) noexcept
{
    return std::unexpected(
        platform_errors::make(platform_errors::Code::kNotImplemented,
                              "WebWindow: not an Emscripten build"));
}

WebWindow::WebWindow(const WebWindowDesc& desc)
    : m_canvas_selector(desc.canvas_selector)
    , m_width(desc.width)
    , m_height(desc.height)
{}

WebWindow::~WebWindow() = default;

void WebWindow::register_handlers() noexcept {}
void WebWindow::unregister_handlers() noexcept {}
bool WebWindow::pump_events(std::vector<OSEvent>& /*out*/) { return false; }
void WebWindow::set_title(std::string_view /*title*/) {}
void WebWindow::push_event(OSEvent /*ev*/) noexcept {}

}  // namespace cd::platform::web

#endif  // __EMSCRIPTEN__
