#ifndef __EVENTLISTENER_H__
#define __EVENTLISTENER_H__

#include <memory>
#include <functional>

#include "Define.hpp"
#include "Core/Errors.hpp"
#include "Core/Event/Event.hpp"

namespace CD
{
    class EventListenerBase
    {
    private:
        EventCallback callback;
    };
    

    
    template <Event_t T>
    class EventListener: public EventListenerBase
    {
    private:
        T _mEvent();
    public:
        explicit EventListener() noexcept = default;
        EventListener(const EventListener&) noexcept = default;
        EventListener& operator= (const EventListener&) noexcept = default;
        EventListener(EventListener&& ) noexcept = default;
        ~EventListener() noexcept = default;
        CD_INLINE void onEvent(EventCallback& eCallback)
        {
            EventListenerBase::callback = eCallback;
        }
        CD_INLINE void Update(T&& e) const
        {
            _mEvent = std::move(e) 
            EventListenerBase::callback(&e);
        }
    };
        
} // namespace CD


#endif // __EVENTLISTENER_H__