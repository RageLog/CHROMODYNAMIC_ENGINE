#include "Application.hpp" 

#include <iostream>
#include <numeric>
#include <ranges>
#include <vector>

namespace CD
{
  std::atomic<Application*> Application::m_instance;
  std::mutex Application::m_mutex;


  CD::Errors Application::run(void)
  {
    std::cout << ENGINE_VERSION << "\n";


    return CD::Errors::Seccess;
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
}