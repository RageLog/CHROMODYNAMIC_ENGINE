// =============================================================================
// CHROMODYNAMIC — platform/android/main_android.cpp
// Phase 532 — Android NativeActivity entry point.
//
// android_main() is the C++ entry point for NDK NativeActivity.
// The NDK provides android_native_app_glue.c which bridges JNI calls
// to this function. Callers must link against libandroid and libc++_shared.
//
// Lifecycle:
//  1. android_native_app_glue spins up a native thread.
//  2. android_main(android_app*) is called in that thread.
//  3. We create the window, init Vulkan, run the frame loop.
//  4. app->onAppCmd callbacks route lifecycle events to ActivityLifecycleRouter.
//  5. On pause/destroy, we halt the frame loop and clean up.
//
// Phase 2: Refactor to use HelloEngine sample app main() directly.
// =============================================================================
#include <cd/platform/android/ActivityLifecycle.hpp>
#include <cd/platform/Window.hpp>

#include <cd/core/Log.hpp>

#include <android/app/android_app.h>
#include <android/native_activity.h>
#include <android/native_window.h>

#include <atomic>
#include <memory>

namespace
{

/// Minimal app state for android_main lifecycle.
/// Phase 2: Move this into cd::sample_framework or HelloEngine.
struct AndroidAppState : public cd::platform::android::IActivityLifecycleListener
{
    std::atomic<bool> should_exit { false };

    void on_lifecycle_event(cd::platform::android::LifecycleEvent event) noexcept override
    {
        using Event = cd::platform::android::LifecycleEvent;
        switch (event)
        {
        case Event::kOnCreate:
            CD_LOG(Info) << "Android: onCreate";
            break;
        case Event::kOnStart:
            CD_LOG(Info) << "Android: onStart";
            break;
        case Event::kOnResume:
            CD_LOG(Info) << "Android: onResume";
            break;
        case Event::kOnPause:
            CD_LOG(Info) << "Android: onPause (suspending frame loop)";
            break;
        case Event::kOnStop:
            CD_LOG(Info) << "Android: onStop";
            break;
        case Event::kOnDestroy:
            CD_LOG(Info) << "Android: onDestroy (requesting exit)";
            should_exit = true;
            break;
        }
    }
};

/// Handle app command (lifecycle) callbacks from android_native_app_glue.
void on_app_cmd(android_app* app, int32_t cmd)
{
    auto* app_state = static_cast<AndroidAppState*>(app->userData);

    using Event = cd::platform::android::LifecycleEvent;
    switch (cmd)
    {
    case APP_CMD_CREATE:
        cd::platform::android::ActivityLifecycleRouter::dispatch(Event::kOnCreate);
        break;
    case APP_CMD_START:
        cd::platform::android::ActivityLifecycleRouter::dispatch(Event::kOnStart);
        break;
    case APP_CMD_RESUME:
        cd::platform::android::ActivityLifecycleRouter::dispatch(Event::kOnResume);
        break;
    case APP_CMD_PAUSE:
        cd::platform::android::ActivityLifecycleRouter::dispatch(Event::kOnPause);
        break;
    case APP_CMD_STOP:
        cd::platform::android::ActivityLifecycleRouter::dispatch(Event::kOnStop);
        break;
    case APP_CMD_DESTROY:
        cd::platform::android::ActivityLifecycleRouter::dispatch(Event::kOnDestroy);
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
        // Phase 2: Route input focus changes
        break;
    default:
        break;
    }
}

}  // anonymous namespace

/// Android entry point. Linked by android_native_app_glue.c.
/// Phase 1: Stub that initializes state, routes lifecycle, and exits.
/// Phase 2: Integrate HelloEngine or cd::sample_framework main loop.
extern "C" void android_main(android_app* app)
{
    CD_LOG(Info) << "=== CHROMODYNAMIC Android NativeActivity (Phase 532) ===";

    // Initialize app state and lifecycle router.
    AndroidAppState app_state;
    cd::platform::android::ActivityLifecycleRouter::set_listener(&app_state);

    app->userData = &app_state;
    app->onAppCmd = on_app_cmd;

    // Phase 532: Request create. Real onCreate is triggered by the system.
    cd::platform::android::ActivityLifecycleRouter::dispatch(
        cd::platform::android::LifecycleEvent::kOnCreate);

    // Phase 1: Stub event loop. Phase 2: Integrate real frame loop.
    while (!app_state.should_exit)
    {
        // Poll app commands (lifecycle callbacks).
        int ident = 0;
        int events = 0;
        android_poll_source* source = nullptr;
        int timeout_ms = 100;

        ident = ALooper_pollAll(timeout_ms, nullptr, &events, (void**)&source);
        if (ident >= 0 && source != nullptr)
        {
            source->process(app, source);
        }

        // Phase 2: Run frame loop here (window creation, Vulkan render, input)
        // For now, just spin and check lifecycle.
    }

    CD_LOG(Info) << "Android: Exiting native activity";
}
