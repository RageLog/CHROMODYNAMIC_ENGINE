#ifndef __EVENTLISTENER_H__
#define __EVENTLISTENER_H__

#include <memory>
#include <functional>

#include "Define.hpp"
#include "Core/Errors.hpp"
#include "Core/Event/Event.hpp"

namespace CD
{
    struct EventListenerBase{};
    template <Event_t T>
    class EventListener: public EventListenerBase
    {
    private:
        EventCallback callback;
    public:
        CD_INLINE void constexpr onEvent(const EventCallback& eCallback)
        {
            callback = eCallback;
        }
        CD_INLINE void constexpr Update(T&& e) const
        {
            callback(&e);
        }
    };
        
} // namespace CD


#endif // __EVENTLISTENER_H__