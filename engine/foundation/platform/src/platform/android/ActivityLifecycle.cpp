// =============================================================================
// CHROMODYNAMIC — platform/android/ActivityLifecycle.cpp
// =============================================================================
#include <cd/platform/android/ActivityLifecycle.hpp>

namespace cd::platform::android
{

void ActivityLifecycleRouter::set_listener(IActivityLifecycleListener* listener) noexcept
{
    g_listener = listener;
}

IActivityLifecycleListener* ActivityLifecycleRouter::get_listener() noexcept
{
    return g_listener;
}

void ActivityLifecycleRouter::dispatch(LifecycleEvent event) noexcept
{
    if (g_listener != nullptr)
    {
        g_listener->on_lifecycle_event(event);
    }
}

}  // namespace cd::platform::android
