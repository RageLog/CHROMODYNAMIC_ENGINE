// =============================================================================
// CHROMODYNAMIC — cd/platform/android/ActivityLifecycle.hpp
// Phase 532 — Android NativeActivity lifecycle callback routing.
//
// Wraps JNI ANativeActivity callbacks (onCreate, onStart, onResume, onPause,
// onStop, onDestroy) and routes them to a C++ listener interface.
//
// Integration point: main_android.cpp uses this to gate frame loop and
// resource lifecycle (asset manager init, vulkan device suspend/resume).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

// Forward declare Android types.
struct ANativeActivity;

namespace cd::platform::android
{

/// Lifecycle events from ANativeActivity.
enum class LifecycleEvent : std::uint8_t
{
    kOnCreate,
    kOnStart,
    kOnResume,
    kOnPause,
    kOnStop,
    kOnDestroy,
};

/// Abstract listener for ANativeActivity lifecycle callbacks.
/// Applications derive and override to handle app pause/resume/destroy.
class IActivityLifecycleListener
{
public:
    IActivityLifecycleListener() noexcept = default;
    virtual ~IActivityLifecycleListener() = default;
    IActivityLifecycleListener(const IActivityLifecycleListener&) = delete;
    IActivityLifecycleListener& operator=(const IActivityLifecycleListener&) = delete;
    IActivityLifecycleListener(IActivityLifecycleListener&&) = delete;
    IActivityLifecycleListener& operator=(IActivityLifecycleListener&&) = delete;

    virtual void on_lifecycle_event(LifecycleEvent event) noexcept = 0;
};

/// Global lifecycle singleton. Phase 2: refactor to context-based routing.
class ActivityLifecycleRouter
{
public:
    /// Set the global listener. Typically called from android_main() startup.
    static void set_listener(IActivityLifecycleListener* listener) noexcept;

    /// Get the global listener (may be nullptr if not set).
    [[nodiscard]] static IActivityLifecycleListener* get_listener() noexcept;

    /// Dispatch a lifecycle event to the registered listener (if any).
    static void dispatch(LifecycleEvent event) noexcept;

private:
    static inline IActivityLifecycleListener* g_listener = nullptr;
};

}  // namespace cd::platform::android
