#include "Application.hpp" 

#include <iostream>
#include <numeric>
#include <ranges>
#include <vector>

std::atomic<Application*> Application::m_instance;
std::mutex Application::m_mutex;


void Application::run(void)
{
  
  std::vector<int> numbers = { 1, 2, 3 ,4, 5 };
  auto evenNumbers = numbers | std::ranges::views::filter([](int n){ return n % 2 == 0; })
                           | std::ranges::views::transform([](int n) { return n * 2; });
  for (auto &&i : evenNumbers)
  {
    std::cout<< i << "\n";
  }

  std::cout << ENGINE_VERSION << "\n";
  
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
