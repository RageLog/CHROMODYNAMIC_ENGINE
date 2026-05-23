// =============================================================================
// CHROMODYNAMIC — cd/platform/AndroidWindow.cpp
// Phase 15.F / Wave 171 — Android (ANativeWindow*) backend stub.
//
// Compile-time scaffold so a future Android NDK build produces a real
// platform_window TU instead of falling through to PlatformStub. The
// runtime behaviour (event loop wired to a Looper, ANativeWindow set
// from JNI's onNativeWindowCreated) is the follow-up wave that requires
// a real Android device + a CI runner with the mobile toolchain.
//
// At v0.46.0 (Phase 15 close):
//   * The TU compiles when targeting __ANDROID__ — no link errors.
//   * create_window() returns kNotImplemented with an honest "Android
//     bring-up in progress" message that signals the consumer to plug
//     the JNI surface directly (engine doesn't own the Activity).
//   * The platform integration shape mirrors the documented build path
//     in mobile/README.md.
// =============================================================================
#include <cd/platform/Window.hpp>

#if defined(__ANDROID__)

    #include <memory>

namespace cd::platform
{

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc&)
{
    return std::unexpected(platform_errors::make(
        platform_errors::Code::kNotImplemented,
        "Android: cd::platform::create_window does not own the Activity "
        "/ ANativeWindow lifecycle. Phase 15 scaffold only — the JNI "
        "bridge that wraps the engine entry point lands in a follow-up "
        "wave alongside a mobile CI runner. Apps that need the engine "
        "today should drive the cd::rhi_vulkan path directly with the "
        "ANativeWindow* obtained from android_native_app_glue / GameActivity."));
}

}  // namespace cd::platform

#endif  // __ANDROID__
