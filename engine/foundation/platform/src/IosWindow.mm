// =============================================================================
// CHROMODYNAMIC — cd/platform/IosWindow.mm
// Phase 15.F / Wave 171 — iOS (UIView*) backend stub.
//
// Compile-time scaffold so an iOS Xcode build produces a real
// platform_window TU. Concrete UIView wrapping + CADisplayLink event
// pump are deferred until a CI runner with iOS hardware is available
// (the v1.0 rollback honesty principle applies — unverified mobile
// binaries shipping would re-create the silent-output trap).
// =============================================================================
#include <cd/platform/Window.hpp>

#if defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE

        #include <memory>

namespace cd::platform
{

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc&)
{
    return std::unexpected(platform_errors::make(
        platform_errors::Code::kNotImplemented,
        "iOS: cd::platform::create_window does not own the UIWindow / "
        "UIView lifecycle. Phase 15 scaffold only — the Metal-backed "
        "UIView + CADisplayLink event pump lands in a follow-up wave "
        "alongside a mobile CI runner. Apps that need the engine "
        "today should drive cd::rhi_metal directly with a CAMetalLayer "
        "obtained from a UIView the app already owns."));
}

}  // namespace cd::platform

    #endif  // TARGET_OS_IPHONE
#endif  // __APPLE__
