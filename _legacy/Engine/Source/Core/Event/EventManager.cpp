#include <vector>

#include "Core/Event/EventManager.hpp"
namespace CD {
EventManager* EventManager::getInstance()
{
  EventManager* instance = m_instance.load(std::memory_order_acquire);
  if(!instance)
  {
    std::lock_guard<std::mutex> m_Lock(m_mutex);
    instance = m_instance.load(std::memory_order_relaxed);
    if(!instance)
    {
      instance = new EventManager();
      m_instance.store(instance, std::memory_order_release);
    }
  }
  return instance;
}
}  // namespace CD
