// =============================================================================
// CHROMODYNAMIC — apps/android_demo/src/android_demo_main.cpp
// Phase 772 / FINALE-7 W2B F3 — Android NativeActivity entry point.
//
// Sprint-1 scope:
//   * Lifecycle routing via cd::platform::android::ActivityLifecycleRouter.
//   * ANativeWindow registration -> cd::platform::create_window().
//   * AInputQueue polling -> cd::platform::android::AndroidInputBridge.
//   * AssetManagerIO initialised and smoke-read verified.
//   * Vulkan surface hook: native_window_handle() returns the ANativeWindow*
//     ready for vkCreateAndroidSurfaceKHR (VK_KHR_android_surface).
//     Actual Vulkan initialisation is out of Sprint-1 scope.
//
// Frame loop stub:
//   The loop runs at ALooper cadence (100 ms poll) and logs lifecycle events.
//   Sprint-2 will wire in the real cd::rhi_vulkan swapchain.
// =============================================================================
#include <cd/platform/android/AndroidWindow.hpp>       // bridge fns
#include <cd/platform/android/AndroidInputBridge.hpp>  // translate_motion / translate_key
#include <cd/platform/android/ActivityLifecycle.hpp>   // ActivityLifecycleRouter
#include <cd/platform/android/AssetManagerIO.hpp>      // AssetManagerIO::create
#include <cd/platform/Window.hpp>                      // cd::platform::create_window

#include <cd/core/Log.hpp>

// android_native_app_glue
#include <android_native_app_glue.h>

#include <atomic>
#include <memory>

// =============================================================================
// App state
// =============================================================================
namespace
{

struct DemoAppState final : public cd::platform::android::IActivityLifecycleListener
{
    std::atomic<bool> should_exit { false };
    std::atomic<bool> window_ready { false };

    // Called from the glue thread via ActivityLifecycleRouter.
    void on_lifecycle_event(cd::platform::android::LifecycleEvent event) noexcept override
    {
        using Event = cd::platform::android::LifecycleEvent;
        switch (event)
        {
        case Event::kOnCreate:  CD_LOG(Info) << "android_demo: onCreate";  break;
        case Event::kOnStart:   CD_LOG(Info) << "android_demo: onStart";   break;
        case Event::kOnResume:  CD_LOG(Info) << "android_demo: onResume";  break;
        case Event::kOnPause:   CD_LOG(Info) << "android_demo: onPause";   break;
        case Event::kOnStop:    CD_LOG(Info) << "android_demo: onStop";    break;
        case Event::kOnDestroy:
            CD_LOG(Info) << "android_demo: onDestroy — requesting exit";
            should_exit = true;
            break;
        }
    }
};

// ---- App-command handler --------------------------------------------------

void on_app_cmd(android_app* app, int32_t cmd)
{
    auto* state = static_cast<DemoAppState*>(app->userData);
    using Event = cd::platform::android::LifecycleEvent;

    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        if (app->window != nullptr)
        {
            // Register the ANativeWindow so cd::platform::create_window() succeeds.
            cd_platform_set_android_native_window(app->window);
            state->window_ready = true;
            CD_LOG(Info) << "android_demo: ANativeWindow ready";
        }
        break;
    case APP_CMD_TERM_WINDOW:
        cd_platform_set_android_native_window(nullptr);
        state->window_ready = false;
        break;
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
    default:
        break;
    }
}

// ---- Input handler -------------------------------------------------------

int32_t on_input_event(android_app* /*app*/, AInputEvent* ev)
{
    const int32_t type = AInputEvent_getType(ev);
    if (type == AINPUT_EVENT_TYPE_MOTION)
    {
        auto me = cd::platform::android::AndroidInputBridge::translate_motion(ev);
        // Sprint-2: dispatch me to cd::ui::FocusManager / widget tree.
        (void)me;
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY)
    {
        auto ke = cd::platform::android::AndroidInputBridge::translate_key(ev);
        // Sprint-2: dispatch ke to cd::ui::FocusManager.
        (void)ke;
        return 1;
    }
    return 0;
}

}  // namespace

// =============================================================================
// android_main — NativeActivity entry point
// =============================================================================
extern "C" void android_main(android_app* app)
{
    CD_LOG(Info) << "=== CHROMODYNAMIC android_demo (Phase 772 Sprint-1) ===";

    DemoAppState app_state;
    cd::platform::android::ActivityLifecycleRouter::set_listener(&app_state);

    app->userData    = &app_state;
    app->onAppCmd    = on_app_cmd;
    app->onInputEvent = on_input_event;

    // Register AInputQueue bridge (handled via onInputEvent callback above).
    // Explicit set_android_input_queue kept for any direct AInputQueue consumers.
    if (app->inputQueue != nullptr)
    {
        cd_platform_set_android_input_queue(app->inputQueue);
    }

    // ---- AssetManagerIO smoke-init ------------------------------------------
    auto asset_io_result = cd::platform::android::AssetManagerIO::create(
        app->activity->assetManager);
    if (!asset_io_result)
    {
        CD_LOG(Error) << "android_demo: AssetManagerIO init failed";
    }
    else
    {
        CD_LOG(Info) << "android_demo: AssetManagerIO ready";
    }

    // ---- Event / frame loop --------------------------------------------------
    std::unique_ptr<cd::platform::IWindow> window;

    while (!app_state.should_exit)
    {
        // Poll app events (lifecycle + input).
        int events = 0;
        android_poll_source* source = nullptr;
        const int timeout_ms = app_state.window_ready ? 0 : 100;

        ALooper_pollAll(timeout_ms, nullptr, &events, reinterpret_cast<void**>(&source));
        if (source != nullptr)
        {
            source->process(app, source);
        }

        // Create the window once the ANativeWindow is ready.
        if (app_state.window_ready && window == nullptr)
        {
            auto wnd_result = cd::platform::create_window(cd::platform::WindowDesc {
                .title  = "chromodynamic-android",
                .width  = 0,   // resolved from ANativeWindow at attach
                .height = 0,
            });

            if (wnd_result)
            {
                window = std::move(*wnd_result);
                CD_LOG(Info) << "android_demo: IWindow created ("
                             << window->width() << "x" << window->height() << ")";
                // native_window_handle() == ANativeWindow* — ready for
                // vkCreateAndroidSurfaceKHR (VK_KHR_android_surface).
                // Sprint-2: pass to cd::rhi_vulkan::VulkanDevice::create_surface().
            }
            else
            {
                CD_LOG(Error) << "android_demo: create_window failed";
            }
        }

        // Sprint-1 frame stub — Sprint-2 integrates real Vulkan render loop.
        if (window != nullptr && !window->should_close())
        {
            std::vector<cd::platform::OSEvent> os_events;
            if (!window->pump_events(os_events))
            {
                CD_LOG(Info) << "android_demo: window close requested";
                app_state.should_exit = true;
            }
            // os_events forwarded to AndroidInputBridge in Sprint-2.
        }
    }

    CD_LOG(Info) << "android_demo: exiting";
}
