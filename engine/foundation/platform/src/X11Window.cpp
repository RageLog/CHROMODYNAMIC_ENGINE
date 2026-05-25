// =============================================================================
// CHROMODYNAMIC — cd/platform/X11Window.cpp
// Phase 158 / v0.99.93 — Linux Xlib window backend (untested).
//
// Compiled only when:
//   * The build is on Linux (__linux__ defined), AND
//   * CD_PLATFORM_XLIB=ON (the CMake gate), AND
//   * libX11 development headers are present.
//
// Marathon scope: the marathon host is Windows-only; this file
// ships as "intended to work on Linux but the marathon couldn't
// run it." Reviewer asistanı on a real Linux box should:
//   1. Install libx11-dev (apt) / libX11-devel (dnf).
//   2. cmake -DCD_PLATFORM_XLIB=ON ...
//   3. Run hello_engine and visually confirm a window opens.
//
// Wayland support stays on the v1.3 backlog — Xlib is the lowest
// common denominator and covers every popular distro's session.
// =============================================================================
#include <cd/platform/Window.hpp>

#if defined(__linux__) && defined(CD_PLATFORM_XLIB)

    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/keysym.h>

    #include <cstdint>
    #include <cstring>
    #include <memory>
    #include <string>

namespace cd::platform
{

namespace
{

[[nodiscard]] KeyCode key_from_keysym(KeySym k) noexcept
{
    if (k >= XK_a && k <= XK_z)
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::kA) +
                                    static_cast<std::uint16_t>(k - XK_a));
    if (k >= XK_A && k <= XK_Z)
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::kA) +
                                    static_cast<std::uint16_t>(k - XK_A));
    if (k >= XK_0 && k <= XK_9)
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::k0) +
                                    static_cast<std::uint16_t>(k - XK_0));
    switch (k)
    {
        case XK_space:        return KeyCode::kSpace;
        case XK_Return:       return KeyCode::kEnter;
        case XK_Escape:       return KeyCode::kEscape;
        case XK_Tab:          return KeyCode::kTab;
        case XK_BackSpace:    return KeyCode::kBackspace;
        case XK_Left:         return KeyCode::kArrowLeft;
        case XK_Right:        return KeyCode::kArrowRight;
        case XK_Up:           return KeyCode::kArrowUp;
        case XK_Down:         return KeyCode::kArrowDown;
        case XK_Control_L:    return KeyCode::kLCtrl;
        case XK_Control_R:    return KeyCode::kRCtrl;
        case XK_Shift_L:      return KeyCode::kLShift;
        case XK_Shift_R:      return KeyCode::kRShift;
        case XK_Alt_L:        return KeyCode::kLAlt;
        case XK_Alt_R:        return KeyCode::kRAlt;
        case XK_F1:           return KeyCode::kF1;
        case XK_F12:          return KeyCode::kF12;
        default:              return KeyCode::kUnknown;
    }
}

class X11Window final : public IWindow
{
public:
    [[nodiscard]] bool create(const WindowDesc& d)
    {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) return false;

        const int screen = DefaultScreen(display_);
        const auto root = RootWindow(display_, screen);
        const auto black = BlackPixel(display_, screen);
        const auto white = WhitePixel(display_, screen);

        window_ = XCreateSimpleWindow(display_, root,
            0, 0,
            d.width > 0 ? d.width : 1,
            d.height > 0 ? d.height : 1,
            1, black, white);
        if (window_ == 0) { XCloseDisplay(display_); display_ = nullptr; return false; }

        // Subscribe to events we care about.
        XSelectInput(display_, window_,
            ExposureMask | KeyPressMask | KeyReleaseMask |
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
            StructureNotifyMask | FocusChangeMask);

        // Hook the WM_DELETE_WINDOW protocol so X sends us a ClientMessage
        // on close rather than killing the connection.
        wm_delete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display_, window_, &wm_delete_, 1);

        std::string title { d.title };
        XStoreName(display_, window_, title.c_str());

        if (d.visible) XMapWindow(display_, window_);
        XFlush(display_);

        width_  = d.width;
        height_ = d.height;
        return true;
    }

    ~X11Window() override
    {
        if (display_ != nullptr)
        {
            if (window_ != 0) XDestroyWindow(display_, window_);
            XCloseDisplay(display_);
        }
    }

    [[nodiscard]] bool pump_events(std::vector<OSEvent>& out) override
    {
        if (display_ == nullptr) return false;
        while (XPending(display_) > 0)
        {
            XEvent ev {};
            XNextEvent(display_, &ev);
            switch (ev.type)
            {
                case ClientMessage:
                {
                    if (static_cast<Atom>(ev.xclient.data.l[0]) == wm_delete_)
                    {
                        close_requested_ = true;
                        out.push_back({ OSEventKind::kClose });
                    }
                    break;
                }
                case ConfigureNotify:
                {
                    const auto& xc = ev.xconfigure;
                    if (static_cast<std::uint32_t>(xc.width)  != width_ ||
                        static_cast<std::uint32_t>(xc.height) != height_)
                    {
                        width_  = static_cast<std::uint32_t>(xc.width);
                        height_ = static_cast<std::uint32_t>(xc.height);
                        OSEvent e { OSEventKind::kResize };
                        e.width = width_; e.height = height_;
                        out.push_back(e);
                    }
                    break;
                }
                case KeyPress:
                case KeyRelease:
                {
                    const KeySym k = XLookupKeysym(&ev.xkey, 0);
                    OSEvent e { ev.type == KeyPress ? OSEventKind::kKeyDown
                                                    : OSEventKind::kKeyUp };
                    e.key = key_from_keysym(k);
                    out.push_back(e);
                    break;
                }
                case ButtonPress:
                case ButtonRelease:
                {
                    OSEvent e { ev.type == ButtonPress ? OSEventKind::kMouseButtonDown
                                                       : OSEventKind::kMouseButtonUp };
                    e.mouse_x = static_cast<float>(ev.xbutton.x);
                    e.mouse_y = static_cast<float>(ev.xbutton.y);
                    e.mouse_button = ev.xbutton.button == Button1 ? MouseButton::kLeft
                                   : ev.xbutton.button == Button2 ? MouseButton::kMiddle
                                   : ev.xbutton.button == Button3 ? MouseButton::kRight
                                                                  : MouseButton::kLeft;
                    if (ev.xbutton.button == Button4 || ev.xbutton.button == Button5)
                    {
                        OSEvent w { OSEventKind::kMouseWheel };
                        w.wheel = ev.xbutton.button == Button4 ? 1.0F : -1.0F;
                        out.push_back(w);
                    }
                    else
                    {
                        out.push_back(e);
                    }
                    break;
                }
                case MotionNotify:
                {
                    OSEvent e { OSEventKind::kMouseMove };
                    e.mouse_x = static_cast<float>(ev.xmotion.x);
                    e.mouse_y = static_cast<float>(ev.xmotion.y);
                    out.push_back(e);
                    break;
                }
                case FocusIn:  out.push_back({ OSEventKind::kFocusGained }); break;
                case FocusOut: out.push_back({ OSEventKind::kFocusLost }); break;
                default: break;
            }
        }
        return !close_requested_;
    }

    void request_close() noexcept override { close_requested_ = true; }

    [[nodiscard]] void* native_window_handle() const noexcept override
    {
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(window_));
    }
    [[nodiscard]] void* native_display_handle() const noexcept override { return display_; }
    [[nodiscard]] std::uint32_t width()  const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return height_; }

    void set_title(std::string_view title) override
    {
        if (display_ != nullptr)
        {
            std::string s { title };
            XStoreName(display_, window_, s.c_str());
        }
    }

private:
    Display* display_ { nullptr };
    Window   window_  { 0 };
    Atom     wm_delete_ { 0 };
    std::uint32_t width_  { 0 };
    std::uint32_t height_ { 0 };
    bool close_requested_ { false };
};

}  // namespace

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc& d)
{
    auto w = std::make_unique<X11Window>();
    if (!w->create(d))
    {
        return std::unexpected(platform_errors::make(
            platform_errors::Code::kCreateFailed,
            "X11Window: XOpenDisplay / XCreateSimpleWindow failed"));
    }
    return w;
}

}  // namespace cd::platform

#endif  // __linux__ && CD_PLATFORM_XLIB
