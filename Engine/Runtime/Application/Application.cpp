#include "Application.hpp"

#include <iostream>
#include <numeric>
#include <ranges>
#include <vector>

std::atomic<Application*> Application::m_instance;
std::mutex Application::m_mutex;

template <typename T>
requires std::integral<T> || std::floating_point<T>
constexpr double Average(std::vector<T> const& vec)
{
  const double sum = std::accumulate(vec.begin(), vec.end(), 0.0);
  return sum / static_cast<double>(vec.size());
}

void Application::run(void)
{
  std::cout << "hello chromo " << Average<double>({1.0, 1.0, 1.1}) << '\n';
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
