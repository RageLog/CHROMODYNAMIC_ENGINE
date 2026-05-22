#ifndef __APPLICATION_H__
#define __APPLICATION_H__

#include<atomic>
#include<mutex>
#include<memory>

#include "Define.hpp"
#include "Core/Errors.hpp"

#include "Core/Event/EventManager.hpp"
#include "Core/Event/ApplicationEvent.hpp"





namespace CD
{
  enum class ApplicationStatus: CD::byte
  {
    Running = 0x00,
    Suspended = 0x01,
    Stoped = 0x02
  };
  class Application
  {
  private:
    explicit Application();
    ~Application() = default;

    ApplicationStatus _status = ApplicationStatus::Stoped;
    static inline std::atomic<Application*> m_instance;
    static inline std::mutex m_mutex;
    CD::EventListener<CD::ApplicationWindowCloseEvent> closeEventListener;
    CD::EventListener<CD::ApplicationWindowResizeEvent> resizeEventListener;
  public:
      Application(const Application&) = delete;
      Application& operator=(const Application&) = delete;
      static Application* getInstance();
      CD::Errors run();
  };
}



#endif // __APPLICATION_H__