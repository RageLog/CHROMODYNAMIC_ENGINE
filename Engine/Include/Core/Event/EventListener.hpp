#ifndef __EVENTLISTENER_H__
#define __EVENTLISTENER_H__

#include <memory>
#include <functional>

#include "Define.hpp"
#include "Core/Errors.hpp"
#include "Core/Event/Event.hpp"

namespace CD
{
    struct EventListenerBase
    {
        size_t listenerId = 0;
    };
    

    
    template <Event_t T>
    class EventListener: public EventListenerBase
    {
    private:
        EventCallback callback;
    public:
        explicit EventListener()
        {
            static size_t id = 0;
            EventListenerBase::listenerId = id;
            ++id;
        }
        CD_INLINE void onEvent(const EventCallback& eCallback)
        {
            callback = eCallback;
        }
        CD_INLINE void Update(T&& e) const
        {
            callback(&e);
        }
    };
        
} // namespace CD


#endif // __EVENTLISTENER_H__