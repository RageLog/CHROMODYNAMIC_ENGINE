// =============================================================================
// CHROMODYNAMIC — cd/platform/Win32Window.cpp
//
// Win32 backend for IWindow. RegisterClassExW + CreateWindowExW; the
// WindowProc routes messages through a `this`-pointer pinned to
// GWLP_USERDATA so the same proc serves multiple windows in the same
// process.
//
// Compiled only on Windows (`#if defined(_WIN32)`). The non-Win32 stub
// path lives in `PlatformStub.cpp`.
// =============================================================================
#include <cd/platform/Window.hpp>

#if defined(_WIN32)

    // MinGW's headers pre-define NOMINMAX through windres / its prelude; guard
    // the macro so a project-wide -DNOMINMAX (and MinGW's prelude) don't both
    // fire `-Wmacro-redefined`.
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

    #include <cstdint>
    #include <memory>
    #include <string>
    #include <vector>

namespace cd::platform
{

namespace
{

// ---- Window-class refcount (Phase 764 / FINALE-6 W1) -----------------------
//
// Multiple Win32Window instances now coexist in the same process. The class
// only needs to be registered once but must outlive every HWND that uses it
// (UnregisterClassW fails while any window of that class still exists).
// Refcount the registration so the *last* window destructor cleans up.

[[nodiscard]] int& class_refcount() noexcept
{
    static int s_count = 0;
    return s_count;
}

[[nodiscard]] KeyCode key_from_vk(WPARAM vk) noexcept
{
    // Win32 maps letters to ASCII codes and digits the same — quick shortcuts.
    if (vk >= 'A' && vk <= 'Z')
    {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::kA) + static_cast<std::uint16_t>(vk - 'A'));
    }
    if (vk >= '0' && vk <= '9')
    {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::k0) + static_cast<std::uint16_t>(vk - '0'));
    }
    switch (vk)
    {
        case VK_SPACE:
            return KeyCode::kSpace;
        case VK_RETURN:
            return KeyCode::kEnter;
        case VK_ESCAPE:
            return KeyCode::kEscape;
        case VK_TAB:
            return KeyCode::kTab;
        case VK_BACK:
            return KeyCode::kBackspace;
        case VK_DELETE:
            return KeyCode::kDelete;
        case VK_LSHIFT:
            return KeyCode::kLShift;
        case VK_RSHIFT:
            return KeyCode::kRShift;
        case VK_LCONTROL:
            return KeyCode::kLCtrl;
        case VK_RCONTROL:
            return KeyCode::kRCtrl;
        case VK_LMENU:
            return KeyCode::kLAlt;
        case VK_RMENU:
            return KeyCode::kRAlt;
        case VK_LEFT:
            return KeyCode::kLeft;
        case VK_RIGHT:
            return KeyCode::kRight;
        case VK_UP:
            return KeyCode::kUp;
        case VK_DOWN:
            return KeyCode::kDown;
        case VK_F1:
            return KeyCode::kF1;
        case VK_F2:
            return KeyCode::kF2;
        case VK_F3:
            return KeyCode::kF3;
        case VK_F4:
            return KeyCode::kF4;
        case VK_F5:
            return KeyCode::kF5;
        case VK_F6:
            return KeyCode::kF6;
        case VK_F7:
            return KeyCode::kF7;
        case VK_F8:
            return KeyCode::kF8;
        case VK_F9:
            return KeyCode::kF9;
        case VK_F10:
            return KeyCode::kF10;
        case VK_F11:
            return KeyCode::kF11;
        case VK_F12:
            return KeyCode::kF12;
        default:
            return KeyCode::kUnknown;
    }
}

[[nodiscard]] std::wstring widen(std::string_view s)
{
    if (s.empty())
        return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

class Win32Window final : public IWindow
{
public:
    Win32Window() noexcept = default;

    ~Win32Window() override
    {
        if (hwnd_ != nullptr)
        {
            // Detach the back-pointer so an in-flight WM_DESTROY can't reach us.
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        if (owns_class_ref_)
        {
            int& refs = class_refcount();
            if (refs > 0)
                --refs;
            // Only the last live window unregisters the shared class.
            if (refs == 0 && instance_ != nullptr)
            {
                UnregisterClassW(kClassName, instance_);
            }
            owns_class_ref_ = false;
        }
    }

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    Win32Window(Win32Window&&) = delete;
    Win32Window& operator=(Win32Window&&) = delete;

    [[nodiscard]] bool create(const WindowDesc& desc)
    {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = &Win32Window::wndproc_thunk;
        wc.hInstance = instance_;
        // IDC_ARROW expands via MAKEINTRESOURCE which is char* by default — we
        // need the W variant for the unicode-only LoadCursorW.
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
        wc.lpszClassName = kClassName;
        if (RegisterClassExW(&wc) == 0)
        {
            // Class may already be registered from a prior window in this
            // process — accept the existing one rather than failing. Phase
            // 764 still tracks an owns_class_ref_ slot on the new window so
            // the class refcount is balanced 1:1 with live Win32Window's.
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;
        }
        // Every successfully-constructed Win32Window participates in the
        // class refcount; the *last* one's destructor calls UnregisterClassW.
        ++class_refcount();
        owns_class_ref_ = true;

        const DWORD style =
            desc.resizable ? (WS_OVERLAPPEDWINDOW) : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);

        // Adjust client size to the requested width/height (the OS frame
        // would otherwise eat into our render area).
        RECT r { 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
        AdjustWindowRect(&r, style, FALSE);

        const std::wstring title = widen(desc.title);
        hwnd_ = CreateWindowExW(
            0,
            kClassName,
            title.c_str(),
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            r.right - r.left,
            r.bottom - r.top,
            nullptr,
            nullptr,
            instance_,
            nullptr
        );
        if (hwnd_ == nullptr)
            return false;
        // Stash `this` so the thunk can dispatch back into the instance.
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        width_ = desc.width;
        height_ = desc.height;
        if (desc.visible)
        {
            ShowWindow(hwnd_, SW_SHOWNORMAL);
            UpdateWindow(hwnd_);
        }
        return true;
    }

    // ---- IWindow --------------------------------------------------------

    [[nodiscard]] bool pump_events(std::vector<OSEvent>& out) override
    {
        MSG msg {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // After dispatch the WndProc has appended to `pending_`; move them.
        if (!pending_.empty())
        {
            out.insert(out.end(), pending_.begin(), pending_.end());
            pending_.clear();
        }
        return !should_close_;
    }

    [[nodiscard]] bool should_close() const noexcept override
    {
        return should_close_;
    }

    void request_close() noexcept override
    {
        should_close_ = true;
    }

    [[nodiscard]] void* native_window_handle() const noexcept override
    {
        return static_cast<void*>(hwnd_);
    }

    [[nodiscard]] void* native_display_handle() const noexcept override
    {
        return static_cast<void*>(instance_);
    }

    [[nodiscard]] std::uint32_t width() const noexcept override
    {
        return width_;
    }

    [[nodiscard]] std::uint32_t height() const noexcept override
    {
        return height_;
    }

    void set_title(std::string_view title) override
    {
        if (hwnd_ != nullptr)
        {
            const auto w = widen(title);
            SetWindowTextW(hwnd_, w.c_str());
        }
    }

    [[nodiscard]] bool set_parent(IWindow* parent) noexcept override
    {
        if (hwnd_ == nullptr)
            return false;

        if (parent == nullptr)
        {
            // Detach: restore overlapped style + clear parent.
            LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
            style &= ~static_cast<LONG_PTR>(WS_POPUP);
            style |= static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW);
            SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
            SetParent(hwnd_, nullptr);
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            return true;
        }

        auto* parent_hwnd = static_cast<HWND>(parent->native_window_handle());
        if (parent_hwnd == nullptr)
            return false;

        // Re-style as a popup that follows the parent's z-order. Must
        // SWP_FRAMECHANGED to commit the new style without resizing.
        LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        style &= ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW);
        style |= static_cast<LONG_PTR>(WS_POPUP | WS_CAPTION | WS_SYSMENU);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
        SetParent(hwnd_, parent_hwnd);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        return true;
    }

private:
    static LRESULT CALLBACK wndproc_thunk(HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self == nullptr)
            return DefWindowProcW(h, msg, w, l);
        return self->wndproc(h, msg, w, l);
    }

    LRESULT wndproc(HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        switch (msg)
        {
            case WM_CLOSE:
            {
                should_close_ = true;
                OSEvent e {};
                e.kind = OSEventKind::kClose;
                pending_.push_back(e);
                return 0;
            }
            case WM_SIZE:
            {
                width_ = static_cast<std::uint32_t>(LOWORD(l));
                height_ = static_cast<std::uint32_t>(HIWORD(l));
                OSEvent e {};
                e.kind = OSEventKind::kResize;
                e.width = width_;
                e.height = height_;
                pending_.push_back(e);
                return 0;
            }
            case WM_SETFOCUS:
            {
                OSEvent e {};
                e.kind = OSEventKind::kFocusGained;
                pending_.push_back(e);
                return 0;
            }
            case WM_KILLFOCUS:
            {
                OSEvent e {};
                e.kind = OSEventKind::kFocusLost;
                pending_.push_back(e);
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            {
                OSEvent e {};
                e.kind = OSEventKind::kKeyDown;
                e.key = key_from_vk(w);
                pending_.push_back(e);
                return 0;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP:
            {
                OSEvent e {};
                e.kind = OSEventKind::kKeyUp;
                e.key = key_from_vk(w);
                pending_.push_back(e);
                return 0;
            }
            case WM_CHAR:
            {
                // wParam carries the Unicode code point (we use the
                // wide window class, so Win32 calls WM_CHAR not
                // WM_UNICHAR). Skip control characters except for the
                // ones ImGui expects (tab=9, newline=10, return=13).
                const std::uint32_t cp = static_cast<std::uint32_t>(w);
                if (cp == 0 || (cp < 32 && cp != 9 && cp != 10 && cp != 13))
                    return 0;
                OSEvent e {};
                e.kind = OSEventKind::kTextChar;
                e.code_point = cp;
                pending_.push_back(e);
                return 0;
            }
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            {
                OSEvent e {};
                e.kind = OSEventKind::kMouseButtonDown;
                e.mouse_button = (msg == WM_LBUTTONDOWN)   ? MouseButton::kLeft
                                 : (msg == WM_RBUTTONDOWN) ? MouseButton::kRight
                                                           : MouseButton::kMiddle;
                e.mouse_x = static_cast<float>(GET_X_LPARAM(l));
                e.mouse_y = static_cast<float>(GET_Y_LPARAM(l));
                pending_.push_back(e);
                return 0;
            }
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            {
                OSEvent e {};
                e.kind = OSEventKind::kMouseButtonUp;
                e.mouse_button = (msg == WM_LBUTTONUP)   ? MouseButton::kLeft
                                 : (msg == WM_RBUTTONUP) ? MouseButton::kRight
                                                         : MouseButton::kMiddle;
                e.mouse_x = static_cast<float>(GET_X_LPARAM(l));
                e.mouse_y = static_cast<float>(GET_Y_LPARAM(l));
                pending_.push_back(e);
                return 0;
            }
            case WM_MOUSEMOVE:
            {
                OSEvent e {};
                e.kind = OSEventKind::kMouseMove;
                e.mouse_x = static_cast<float>(GET_X_LPARAM(l));
                e.mouse_y = static_cast<float>(GET_Y_LPARAM(l));
                pending_.push_back(e);
                return 0;
            }
            case WM_MOUSEWHEEL:
            {
                OSEvent e {};
                e.kind = OSEventKind::kMouseWheel;
                e.wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(w)) / static_cast<float>(WHEEL_DELTA);
                pending_.push_back(e);
                return 0;
            }
            default:
                return DefWindowProcW(h, msg, w, l);
        }
    }

    static constexpr const wchar_t* kClassName = L"ChromodynamicWindow";

    HINSTANCE instance_ { nullptr };
    HWND hwnd_ { nullptr };
    bool owns_class_ref_ { false };
    bool should_close_ { false };
    std::uint32_t width_ { 0 };
    std::uint32_t height_ { 0 };
    std::vector<OSEvent> pending_ {};
};

}  // namespace

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
    {
        return std::unexpected(
            platform_errors::make(
                platform_errors::Code::kInvalidArgument,
                "create_window: width and height must be > 0"
            )
        );
    }
    auto w = std::make_unique<Win32Window>();
    if (!w->create(desc))
    {
        return std::unexpected(
            platform_errors::make(platform_errors::Code::kCreateFailed, "create_window: CreateWindowExW failed")
        );
    }
    return std::unique_ptr<IWindow> { std::move(w) };
}

}  // namespace cd::platform

#endif  // _WIN32
