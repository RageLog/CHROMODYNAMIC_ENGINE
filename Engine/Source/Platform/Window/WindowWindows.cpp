#include "Platform/Window/WindowWindows.hpp"

namespace CD
{
    WindowWindows::WindowWindows(const WindowInfo&& info) noexcept:
    Window(std::move(info))
    {
    }
    LRESULT WINAPI WindowWindows::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
    {
        switch (msg) 
        {
            case WM_ERASEBKGND:
                return 1;
            case WM_CLOSE:
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case WM_SIZE: 
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYUP:
                return 0;
            case WM_MOUSEMOVE: 
                break;
            case WM_MOUSEWHEEL: 
                break;
            case WM_LBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MBUTTONUP:
            case WM_RBUTTONUP:      
                switch (msg) 
                {
                    case WM_LBUTTONDOWN:
                    case WM_LBUTTONUP:
                        break;
                    case WM_MBUTTONDOWN:
                    case WM_MBUTTONUP:
                        break;
                    case WM_RBUTTONDOWN:
                    case WM_RBUTTONUP:
                        break;
                }
            break;
        };
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    CD::Errors WindowWindows::Create() 
    {
        _wndClass.cbSize = sizeof(_wndClass);
        _wndClass.lpfnWndProc = WndProc;
        _wndClass.hInstance = _instance;
        _wndClass.lpszClassName = CLASS_NAME;
        _wndClass.style = CS_DBLCLKS;
        _wndClass.cbClsExtra = 0;
        _wndClass.cbWndExtra = 0;
        _wndClass.hIcon = _icon;
        _wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        _wndClass.hbrBackground = NULL; 
        if (!RegisterClassEx(&_wndClass))
        {
            return CD::Errors::Failture;
        }
        _hwnd = CreateWindowEx( WS_EX_APPWINDOW,
                                CLASS_NAME,
                                _info.WindowTitle.c_str(),
                                WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME,
                                _info.positionX,
                                _info.positionY,
                                _info.Width, 
                                _info.Height,
                                NULL,
                                NULL,
                                _instance,
                                NULL);
        if (_hwnd == NULL)
        {
            return CD::Errors::Failture;
        }

        ShowWindow(_hwnd,SW_SHOW );

        return CD::Errors::Success;
    }
    CD::Errors WindowWindows::CallMessageLoop()
    {
        MSG message;
        while (PeekMessage(&message, _hwnd, NULL, NULL, PM_REMOVE)) 
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        return CD::Errors::Success;
    }

} // namespace CD

