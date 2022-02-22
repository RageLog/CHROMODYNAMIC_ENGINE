#ifndef __WINDOW_H__
#define __WINDOW_H__




#include <string>

#include "Define.hpp"
#include "Core/Errors.hpp"


namespace CD{
    
    struct CDAPI WindowInfo
    {
        unsigned short Width = 800 ,Height = 600;
        unsigned short positionX = 10,positionY = 10;
        std::string WindowTitle = APPLICATION_NAME ;
    };
    
    class CDAPI Window
    {
    private:
    protected:
        WindowInfo _info;
    public:
        constexpr explicit Window(const WindowInfo&& info) noexcept:
        _info(std::move(info)){}
        virtual ~Window() noexcept = 0;
        virtual CD::Errors Create() = 0;
        virtual CD::Errors CallMessageLoop() = 0;
    };
    CD_INLINE Window::~Window() noexcept = default;
}


#endif // __WINDOW_H__