#ifndef __EVENTMANAGER_H__
#define __EVENTMANAGER_H__

#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>

#include "Define.hpp"
#include "Core/Event/Event.hpp"
#include "Core/Event/EventListener.hpp"



namespace CD
{
   //TODO: implemantation with eventlistener(have to implament with interface);
    class CDAPI EventManager
    {
    private:
        std::unordered_map<CD::EventType,std::vector<EventCallback>> eventList;
        explicit EventManager() noexcept;
        ~EventManager() = default;
        EventManager(const EventManager&) = delete;
        EventManager& operator=(const EventManager&) = delete;
        static std::atomic<EventManager*> m_instance;
        static std::mutex m_mutex;
    public:
        static EventManager* getInstance();
        //template<Event_t E>
        auto registerEvent(CD::EventType e,const EventCallback& callback) -> void; //TODO: this will Refactor
        template<Event_t E>
        auto unregisterEvent(E&& e) -> void; //TODO: this will Refactor 
        template<Event_t E>
        auto eventOccur(E&& e) -> void; //TODO: this will Refactor 
    };
    //template<Event_t E>
    CD_INLINE auto EventManager::registerEvent(CD::EventType e,const EventCallback& callback) -> void
    {
        EventManager::eventList[e].push_back(callback);
    }
    template<Event_t E>
    CD_INLINE auto EventManager::unregisterEvent(E&& e) -> void
    {

    } 
    template<Event_t E>
    CD_INLINE auto EventManager::eventOccur(E&& e) -> void
    {
        for (auto&& callback : EventManager::eventList[e.getType()])
        {
            callback(&e);
        }
    }

} // namespace CD

#endif // __EVENTMANAGER_H__