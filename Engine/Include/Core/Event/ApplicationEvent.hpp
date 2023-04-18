#ifndef __APPLICATIONEVENT_H__
#define __APPLICATIONEVENT_H__

#include "Define.hpp"
#include "./Core/Event/Event.hpp"


namespace CD
{
    class CDAPI ApplicationWindowCloseEvent: public Event
    {
    private:
        public:
        explicit ApplicationWindowCloseEvent() noexcept = default;
        ApplicationWindowCloseEvent(const ApplicationWindowCloseEvent&) = delete;
        ApplicationWindowCloseEvent& operator=(const ApplicationWindowCloseEvent&) = delete;
        ApplicationWindowCloseEvent(ApplicationWindowCloseEvent&&) = default;
        //ApplicationWindowCloseEvent&& operator=(ApplicationWindowCloseEvent&&) = default;
        ~ApplicationWindowCloseEvent() override = default;

        EVENT_TYPE(EventType::Application_Exit)
        EVENT_CATEGORY(CD::EventCategory::App)
        EVENT_PRIORTY(EventPriorty::High)
    };

    class CDAPI ApplicationWindowResizeEvent: public Event
    {
    private:
        std::uint32_t _width, _height;
    public:
        explicit ApplicationWindowResizeEvent(const std::uint32_t & Width ,const std::uint32_t& Height ) noexcept :
        _width(Width),_height(Height){}
        ApplicationWindowResizeEvent(const ApplicationWindowResizeEvent&) = delete;
        ApplicationWindowResizeEvent(ApplicationWindowResizeEvent&&) = default;
        ApplicationWindowResizeEvent& operator=(const ApplicationWindowResizeEvent&) = delete;
        //ApplicationWindowResizeEvent&& operator=(ApplicationWindowResizeEvent&&) = default;
        ~ApplicationWindowResizeEvent() override = default;
                
        EVENT_TYPE(EventType::Application_WindowResize)
        EVENT_CATEGORY(CD::EventCategory::App)
        EVENT_PRIORTY(EventPriorty::High)
        std::pair<std::uint32_t ,std::uint32_t> getSize() const
        {
            return std::make_pair(_width,_height);
        }
    };

    class CDAPI ApplicationTickEvent: Event
    {
    private:
        
    public:
        explicit ApplicationTickEvent() noexcept = default;
        ApplicationTickEvent(const ApplicationTickEvent&) = delete;
        ApplicationTickEvent(ApplicationTickEvent&&) = default;
        ApplicationTickEvent& operator=(const ApplicationTickEvent&) = delete;
        //ApplicationTickEvent&& operator=(ApplicationTickEvent&&) = default;
        ~ApplicationTickEvent() override = default;
        EVENT_TYPE(EventType::Application_Tick)
        EVENT_CATEGORY(CD::EventCategory::App)
        EVENT_PRIORTY(EventPriorty::High)
    };
    class CDAPI ApplicationUpdateEvent: Event
    {
        private:
        
        public:
            explicit ApplicationUpdateEvent() noexcept = default;
            ApplicationUpdateEvent(const ApplicationUpdateEvent&) = delete;
            ApplicationUpdateEvent(ApplicationUpdateEvent&&) = default;
            ApplicationUpdateEvent& operator=(const ApplicationUpdateEvent&) = delete;
            //ApplicationUpdateEvent&& operator=(ApplicationUpdateEvent&&) = default;
            ~ApplicationUpdateEvent() override = default;
            EVENT_TYPE(EventType::Application_Update)
            EVENT_CATEGORY(CD::EventCategory::App)
            EVENT_PRIORTY(EventPriorty::High)
    };

    
      
} // namespace CD



#endif // __APPLICATIONEVENT_H__