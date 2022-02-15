#include "Application.hpp"

#include <iostream>
#include <numeric>
#include <ranges>
#include <vector>

#include "Platform/Window/WindowWindows.hpp"

namespace CD
{
std::atomic<Application*> Application::m_instance;
std::mutex Application::m_mutex;

Application::Application() noexcept
{
  EventManager::getInstance()->registerEvent(CD::ApplicationWindowResizeEvent::getStaticType(),[&](Event* e){
    auto event = static_cast<CD::ApplicationWindowResizeEvent*>(e);
    std::cout << event->getSize().first << " " << event->getSize().second  << '\n';
    //CD_IGNORE_UNUSED(e)
  });
  EventManager::getInstance()->registerEvent(CD::ApplicationWindowCloseEvent::getStaticType(),[&](Event* e){
    //static_cast<CD::ApplicationWindowCloseEvent*>(e)->getName();
    CD_IGNORE_UNUSED(e)
    _status = ApplicationStatus::Stoped;
  });

}
CD::Errors Application::run(void)
{
  std::cout << ENGINE_VERSION << "\n";
  _status = ApplicationStatus::Running;
  WindowInfo info;
  CD::Window* w = new CD::WindowWindows(std::move(info));

  w->Create();
  for (; _status == ApplicationStatus::Running;)
  {
    if (w->CallMessageLoop() != CD::Errors::Success)
    {
      _status = ApplicationStatus::Stoped;
    }
    
  }

  return CD::Errors::Success;
}

Application* Application::getInstance()
{
  Application* instance = m_instance.load(std::memory_order_acquire);
  if (!instance)
  {
    std::lock_guard<std::mutex> m_Lock(m_mutex);
    instance = m_instance.load(std::memory_order_relaxed);
    if (!instance)
    {
      instance = new Application();
      m_instance.store(instance, std::memory_order_release);
    }
  }
  return instance;
}
}  // namespace CD