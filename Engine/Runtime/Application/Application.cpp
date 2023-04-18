#include "Application.hpp"

#include <iostream>
#include <numeric>
#include <ranges>
#include <vector>
#include <memory>

#include "Platform/Window/WindowWindows.hpp"

namespace CD {

Application::Application()
{
  resizeEventListener.onEvent([&](Event* e) {
    auto* event = static_cast<CD::ApplicationWindowResizeEvent*>(e);
    CD_IGNORE_UNUSED(event)
  });
  EventManager::getInstance()->registerEvent(resizeEventListener);
  closeEventListener.onEvent([&](Event* e) {
    CD_IGNORE_UNUSED(e)
    _status = ApplicationStatus::Stoped;
  });
  EventManager::getInstance()->registerEvent(closeEventListener);
}
CD::Errors Application::run()
{
  std::cout << ENGINE_VERSION << "\n";
  _status = ApplicationStatus::Running;
  WindowInfo info;
  auto       w = std::make_unique<WindowWindows>(std::move(info));

  w->Create();
  for(; _status == ApplicationStatus::Running;)
  {
    if(w->CallMessageLoop() != CD::Errors::Success)
    {
      _status = ApplicationStatus::Stoped;
    }
  }

  return CD::Errors::Success;
}

Application* Application::getInstance()
{
  Application* instance = m_instance.load(std::memory_order_acquire);
  if(!instance)
  {
    std::lock_guard<std::mutex> m_Lock(m_mutex);
    instance = m_instance.load(std::memory_order_relaxed);
    if(!instance)
    {
      instance = new Application();
      m_instance.store(instance, std::memory_order_release);
    }
  }
  return instance;
}
}  // namespace CD