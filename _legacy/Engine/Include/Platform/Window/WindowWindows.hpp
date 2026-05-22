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
        HWND _hwnd = nullptr;
        WNDCLASSEX _wndClass = {};
        HINSTANCE _instance = static_cast<HINSTANCE>(GetModuleHandle(nullptr));
        HICON _icon = LoadIcon(_instance, IDI_APPLICATION);
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        static inline WindowWindows* s_window = nullptr; 
        LRESULT _mProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    public:
        explicit WindowWindows(WindowInfo&& info);
         CD::Errors Create()  final;
         CD::Errors CallMessageLoop()  final;
        ~WindowWindows() override = default;
    };
  
    
   
   
    
    
    
    
    
} // namespace CD




#endif // __WINDOWWINDOWS_H__