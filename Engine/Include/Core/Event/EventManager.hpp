#ifndef __EVENTMANAGER_H__
#define __EVENTMANAGER_H__

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>

#include "Define.hpp"
#include "Core/Event/Event.hpp"
#include "Core/Event/EventListener.hpp"



namespace CD
{
    class CDAPI EventManager
    {
    private:
        std::unordered_map<CD::EventType,std::unordered_set<EventListenerBase*>> eventListenerList;
        explicit EventManager() noexcept = default;
        ~EventManager() = default;
        EventManager(const EventManager&) = delete;
        EventManager& operator=(const EventManager&) = delete;
        static std::atomic<EventManager*> m_instance;
        static std::mutex m_mutex;
    public:
        static EventManager* getInstance();
        template<Event_t E>
        auto registerEvent(EventListener<E>& listene) -> void;
        template<Event_t E>
        auto unregisterEvent(EventListener<E>& listener) -> void;
        template<Event_t E>
        auto eventOccur(E&& e) -> void;
    };
    template<Event_t E>
    CD_INLINE auto EventManager::registerEvent(EventListener<E>& listener) -> void
    {
        auto type = E::getStaticType();
        EventManager::eventListenerList[type].insert(static_cast<EventListenerBase*>(&listener));
    }
    template<Event_t E>
    CD_INLINE auto EventManager::unregisterEvent(EventListener<E>& listener) -> void
    {
        auto type = E::getStaticType();
        eventListenerList[type].erase(&listener);
    }
    template<Event_t E>
    CD_INLINE auto EventManager::eventOccur(E&& e) -> void
    {
        for (auto&& listener : EventManager::eventListenerList[e.getType()])
        {
            static_cast<EventListener<E>*>(listener)->Update(std::move(e));
        }
    }

} // namespace CD

#endif // __EVENTMANAGER_H__