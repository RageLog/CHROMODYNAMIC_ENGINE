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
    explicit Application() noexcept;
    ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    ApplicationStatus _status = ApplicationStatus::Stoped;
    static std::atomic<Application*> m_instance;
    static std::mutex m_mutex;
  private:
  public:
      static Application* getInstance();
      CD::Errors run(void);
  };
}



#endif // __APPLICATION_H__