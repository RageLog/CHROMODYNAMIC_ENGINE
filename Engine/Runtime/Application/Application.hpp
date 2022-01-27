#ifndef __APPLICATION_H__
#define __APPLICATION_H__

#include<atomic>
#include<mutex>
#include<memory>

#include "Define.hpp"
#include "Core/for_now.hpp"

class CDAPI Application
{
private:
  Application() = default;
  ~Application() = default;
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  static std::atomic<Application*> m_instance;
  static std::mutex m_mutex;
public:
    static Application* getInstance();
    void run(void);
};



#endif // __APPLICATION_H__