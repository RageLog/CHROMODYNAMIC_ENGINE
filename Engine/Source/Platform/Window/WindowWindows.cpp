#include "Platform/Window/WindowWindows.hpp"

namespace CD {
WindowWindows::WindowWindows(WindowInfo&& info)
    : Window(std::move(info))
{
  s_window = this;
}
LRESULT WindowWindows::_mProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch(msg)
  {
    case WM_ERASEBKGND:
      return 1;
    case WM_CLOSE: {
      EventManager::getInstance()->eventOccur(CD::ApplicationWindowCloseEvent());
      DestroyWindow(hWnd);
    }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE: {
      RECT r;
      GetClientRect(hWnd, &r);
      std::uint32_t width  = static_cast<std::uint32_t>(r.right - r.left);
      std::uint32_t height = static_cast<std::uint32_t>(r.bottom - r.top);
      EventManager::getInstance()->eventOccur(CD::ApplicationWindowResizeEvent(width, height));
    }
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
      switch(msg)
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
      default:
          break;
      }
      break;
    default:
      break;
  };
  return DefWindowProc(hWnd, msg, wParam, lParam);
}
LRESULT WINAPI WindowWindows::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  return s_window->_mProc(hWnd, msg, wParam, lParam);
}
CD::Errors WindowWindows::Create()
{
  _wndClass.cbSize        = sizeof(_wndClass);
  _wndClass.lpfnWndProc   = WndProc;
  _wndClass.hInstance     = _instance;
  _wndClass.lpszClassName = CLASS_NAME;
  _wndClass.style         = CS_DBLCLKS;
  _wndClass.cbClsExtra    = 0;
  _wndClass.cbWndExtra    = 0;
  _wndClass.hIcon         = _icon;
  _wndClass.hCursor       = LoadCursor(nullptr, IDC_ARROW);
  _wndClass.hbrBackground = nullptr;
  if(!RegisterClassEx(&_wndClass))
  {
    return CD::Errors::Failture;
  }
  _hwnd = CreateWindowEx(WS_EX_APPWINDOW, CLASS_NAME, _info.WindowTitle.c_str(),
                         WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME,
                         _info.positionX, _info.positionY, _info.Width, _info.Height, nullptr, nullptr, _instance, nullptr);
  if(_hwnd == nullptr)
  {
    return CD::Errors::Failture;
  }

  ShowWindow(_hwnd, SW_SHOW);

  return CD::Errors::Success;
}
CD::Errors WindowWindows::CallMessageLoop()
{
  MSG message;
  while(PeekMessage(&message, _hwnd, 0, 0, PM_REMOVE))
  {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }
  return CD::Errors::Success;
}


}  // namespace CD