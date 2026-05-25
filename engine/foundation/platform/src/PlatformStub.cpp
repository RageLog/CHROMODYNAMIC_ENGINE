// =============================================================================
// CHROMODYNAMIC — cd/platform/PlatformStub.cpp
//
// Fallback for platforms without a window backend compiled in. Returns
// kNotImplemented from create_window so callers can branch. Compiled
// only when no other backend TU is selected (CMake guard).
// =============================================================================
#include <cd/platform/Window.hpp>

#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(CD_PLATFORM_WAYLAND) && !defined(CD_PLATFORM_XLIB) && !defined(CD_PLATFORM_COCOA)

    #include <memory>

namespace cd::platform
{

cd::core::Result<std::unique_ptr<IWindow>> create_window(const WindowDesc&)
{
    return std::unexpected(
        platform_errors::make(
            platform_errors::Code::kNotImplemented,
            "create_window: no platform backend compiled in (Win32/Wayland/Xlib/macOS)"
        )
    );
}

}  // namespace cd::platform

#endif
