#ifndef __WINDOWWINDOWS_H__
#define __WINDOWWINDOWS_H__


#include <Windows.h>
#include <windowsx.h>

#include "Define.hpp"
#include "Platform/Window/Window.hpp"

#include "Core/Event/Event.hpp"
#include "Core/Event/ApplicationEvent.hpp"
#include "Core/Event/EventManager.hpp"


namespace CD
{
    class CDAPI WindowWindows final: public Window
    {
    private:
        LPCSTR CLASS_NAME = "cdEngineWindowClassName";
        HWND _hwnd;
        WNDCLASSEX _wndClass = {};
        HINSTANCE _instance = static_cast<HINSTANCE>(GetModuleHandle(0));
        HICON _icon = LoadIcon(_instance, IDI_APPLICATION);
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
        static WindowWindows* s_window; 
        LRESULT _mProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    public:
        explicit WindowWindows(const WindowInfo&& info) noexcept;
        virtual CD::Errors Create() override final;
        virtual CD::Errors CallMessageLoop() override final;
        ~WindowWindows() = default;
    };
  
    
   
   
    
    
    
    
    
} // namespace CD




#endif // __WINDOWWINDOWS_H__