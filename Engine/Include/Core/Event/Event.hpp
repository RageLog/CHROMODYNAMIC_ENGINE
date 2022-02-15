#ifndef __EVENT_H__
#define __EVENT_H__


#include <functional>
#include <concepts>
#include <type_traits>


#include "Define.hpp"
#include "Core/Errors.hpp"
#include "Core/Math/Math.hpp"

namespace CD
{

  using EventCategoryFlags_t = unsigned int;
  enum class EventType: CD::byte
  {
    Application_Exit          = 0x00,
    Application_WindowResize  = 0x01,
    Application_Tick          = 0x02,
    Application_Update        = 0x03,
    Key_Press                 = 0x04,
    Key_Release               = 0x05,
    Mause_Move                = 0x06,
    Mause_ButtonPress         = 0x07,
    Mause_ButtonRelase        = 0x08,
    Mause_Wheel               = 0x09
  };
  enum EventCategory
  {
    None            = 0,
		App             = CD::Mask(0),
		Input           = CD::Mask(1),
		Keyboard        = CD::Mask(2),
		Mouse           = CD::Mask(3),
		MouseButton     = CD::Mask(4)
  };
  enum class EventPriorty: CD::byte
  {
    High = 0x00,
    Medium = 0x01,
    Low = 0x02
    //TODO:implament priorty for event
  };


//! thanks to the cherno for this-> hazel engine
  #define EVENT_TYPE(type) static constexpr EventType getStaticType() { return type; }\
								constexpr virtual EventType getType() const override { return getStaticType(); }\
								constexpr virtual const char* getName() const override { return #type; }

  #define EVENT_CATEGORY(category) constexpr virtual int getCategoryFlags() const override { return category; }

  #define EVENT_PRIORTY(priorty) constexpr virtual EventPriorty getPriorty() const override { return priorty; }
  

  class CDAPI Event
  {
  private:
  protected:
  public:
  
    virtual ~Event() noexcept = 0;
    explicit Event() noexcept = default;
    Event(const Event&) = delete;
    Event(Event&&) = default;
    Event& operator = (const Event&) = delete;
    //Event&& operator = (Event&&) = default;

    constexpr bool isHasCategory(const EventCategory& category) const;
    constexpr virtual int getCategoryFlags() const = 0;
    constexpr virtual EventType getType() const = 0;
    constexpr virtual const char* getName() const = 0;
    constexpr virtual EventPriorty getPriorty() const = 0;
  };
  
  template<typename T>
  concept Event_t = std::is_base_of_v<Event,T>;

  using EventCallback = std::function<void(Event*)>;

} // namespace CD
#endif // __EVENT_H__