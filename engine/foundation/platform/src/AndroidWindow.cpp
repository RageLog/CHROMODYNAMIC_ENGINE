// =============================================================================
// CHROMODYNAMIC — cd/platform/AndroidWindow.cpp
// Phase 160 / v0.99.95 — Android NativeActivity window backend (untested).
//
// Wraps an externally-provided `ANativeWindow*` (obtained from
// android_native_app_glue or GameActivity's onNativeWindowCreated)
// into the cd::platform::IWindow interface so the engine doesn't
// need a separate Android code path. The Activity lifecycle stays
// on the Java/Kotlin side; we just consume the native surface +
// pump input events.
//
// Compile-time gating: only when __ANDROID__ is defined.
//
// Marathon scope: this file is written without an Android device or
// NDK build host. The shape mirrors the recommended Google Android
// game-loop pattern (game-activity, swappy) and matches what
// Filament/UE5/Unity ship on Android. Reviewer with an NDK build
// should be able to plug an `ANativeWindow*` and `AInputQueue*`
// into the helpers below and have a working window.
// =============================================================================
#include <cd/platform/Window.hpp>

#if defined(__ANDROID__)

    #include <android/input.h>
    #include <android/native_window.h>
    #include <android/log.h>

    #include <atomic>
    #include <cstdint>
    #include <memory>
    #include <string>

namespace cd::platform
{

namespace
{

// External hook: the host activity sets these pointers from its
// onNativeWindowCreated / onInputQueueCreated callbacks. The engine
// can then call attach_android_native_window(handle) at boot.
std::atomic<ANativeWindow*> g_pending_window { nullptr };
std::atomic<AInputQueue*>   g_pending_input  { nullptr };

[[nodiscard]] KeyCode key_from_aml(int32_t k) noexcept
{
    switch (k)
    {
        case AKEYCODE_ESCAPE:    return KeyCode::kEscape;
        case AKEYCODE_ENTER:     return KeyCode::kEnter;
        case AKEYCODE_SPACE:     return KeyCode::kSpace;
        case AKEYCODE_TAB:       return KeyCode::kTab;
        case AKEYCODE_DEL:       return KeyCode::kBackspace;
        case AKEYCODE_DPAD_LEFT: return KeyCode::kArrowLeft;
        case AKEYCODE_DPAD_RIGHT:return KeyCode::kArrowRight;
        case AKEYCODE_DPAD_UP:   return KeyCode::kArrowUp;
        case AKEYCODE_DPAD_DOWN: return KeyCode::kArrowDown;
        default:                 return KeyCode::kUnknown;
    }
}

class AndroidWindow final : public IWindow
{
public:
    [[nodiscard]] bool attach(ANativeWindow* anw, const WindowDesc& d)
    {
        if (anw == nullptr) return false;
        anw_ = anw;
        ANativeWindow_acquire(anw_);
        width_  = static_cast<std::uint32_t>(ANativeWindow_getWidth(anw_));
        height_ = static_cast<std::uint32_t>(ANativeWindow_getHeight(anw_));
        if (width_ == 0)  width_  = d.width;
        if (height_ == 0) height_ = d.height;
        return true;
    }

    ~AndroidWindow() override
    {
        if (anw_ != nullptr) ANativeWindow_release(anw_);
    }

    [[nodiscard]] bool pump_events(std::vector<OSEvent>& out) override
    {
        // Pull input events from the activity's queue if one was attached.
        AInputQueue* iq = g_pending_input.load(std::memory_order_acquire);
        if (iq != nullptr)
        {
            AInputEvent* ev = nullptr;
            while (AInputQueue_getEvent(iq, &ev) >= 0)
            {
                if (AInputQueue_preDispatchEvent(iq, ev) != 0) continue;
                const auto type = AInputEvent_getType(ev);
                if (type == AINPUT_EVENT_TYPE_KEY)
                {
                    OSEvent e {};
                    e.kind = AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN
                                 ? OSEventKind::kKeyDown
                                 : OSEventKind::kKeyUp;
                    e.key = key_from_aml(AKeyEvent_getKeyCode(ev));
                    out.push_back(e);
                }
                else if (type == AINPUT_EVENT_TYPE_MOTION)
                {
                    OSEvent e { OSEventKind::kMouseMove };
                    e.mouse_x = AMotionEvent_getX(ev, 0);
                    e.mouse_y = AMotionEvent_getY(ev, 0);
                    out.push_back(e);
                }
                AInputQueue_finishEvent(iq, ev, /*handled=*/1);
            }
        }
        return !close_requested_;
    }

    void request_close() noexcept override { close_requested_ = true; }

    [[nodiscard]] void* native_window_handle() const noexcept override
    {
        return anw_;
    }
    [[nodiscard]] void* native_display_handle() const noexcept override { return nullptr; }
    [[nodiscard]] std::uint32_t width()  const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return height_; }

    void set_title(std::string_view) override {}  // Android title is the Activity label

private:
    ANativeWindow* anw_ { nullptr };
    std::uint32_t width_  { 0 };
    std::uint32_t height_ { 0 };
    bool close_requested_ { false };
};

}  // namespace

/// Host activity bridge — set from JNI onNativeWindowCreated.
extern "C" void cd_platform_set_android_native_window(ANativeWindow* anw) noexcept
{
    g_pending_window.store(anw, std::memory_order_release);
}
extern "C" void cd_platform_set_android_input_queue(AInputQueue* iq) noexcept
{
    g_pending_input.store(iq, std::memory_order_release);
}

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc& d)
{
    ANativeWindow* anw = g_pending_window.load(std::memory_order_acquire);
    if (anw == nullptr)
    {
        return std::unexpected(platform_errors::make(
            platform_errors::Code::kCreateFailed,
            "Android create_window: no ANativeWindow attached yet — "
            "host activity must call cd_platform_set_android_native_window "
            "before cd::platform::create_window() returns a valid handle."));
    }
    auto w = std::make_unique<AndroidWindow>();
    if (!w->attach(anw, d))
    {
        return std::unexpected(platform_errors::make(
            platform_errors::Code::kCreateFailed,
            "AndroidWindow: attach failed"));
    }
    return w;
}

}  // namespace cd::platform

#endif  // __ANDROID__
