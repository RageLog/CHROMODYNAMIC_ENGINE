# engine/foundation/platform — cd::platform
# Sprint S2.1.e — ADR-005 §G + ADR-017 P0
# Sprint S6.1 — IWindow + Win32 backend.

# Mobile backend TUs (Phase 15.F / Wave 171). Both compile-gated by
# their own platform macros (__ANDROID__, TARGET_OS_IPHONE); on
# desktop they produce empty translation units. The Apple TU is .mm
# so Xcode treats it as Objective-C++; CMake handles that via the
# OBJCXX language when CMAKE_SYSTEM_NAME=iOS.
set(_platform_sources
  src/SignalHandler.cpp
  src/Win32Window.cpp
  src/AndroidWindow.cpp
  src/PlatformStub.cpp
)
# Phase 158 (v0.99.93) — Linux X11 backend, opt-in.
option(CD_PLATFORM_XLIB "Build Xlib window backend (Linux)" OFF)
if(CD_PLATFORM_XLIB AND UNIX AND NOT APPLE)
  list(APPEND _platform_sources src/X11Window.cpp)
endif()
# Phase 159 (v0.99.94) — macOS Cocoa backend, opt-in.
option(CD_PLATFORM_COCOA "Build Cocoa window backend (macOS)" OFF)
if(CD_PLATFORM_COCOA AND APPLE)
  list(APPEND _platform_sources src/CocoaWindow.mm)
endif()
if(APPLE)
  list(APPEND _platform_sources src/IosWindow.mm)
endif()

cd_add_library(platform
  SOURCES ${_platform_sources}
  PUBLIC_DEPS cd::core
)

if(WIN32)
  # User32 + GDI are required for CreateWindowEx / DefWindowProc /
  # LoadCursor — Win32Window.cpp pulls them in unconditionally.
  target_link_libraries(cd_platform PRIVATE user32 gdi32)
endif()
if(CD_PLATFORM_XLIB AND UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_compile_definitions(cd_platform PRIVATE CD_PLATFORM_XLIB=1)
  target_link_libraries(cd_platform PRIVATE ${X11_LIBRARIES})
  target_include_directories(cd_platform PRIVATE ${X11_INCLUDE_DIR})
endif()
if(CD_PLATFORM_COCOA AND APPLE)
  target_compile_definitions(cd_platform PRIVATE CD_PLATFORM_COCOA=1)
  target_link_libraries(cd_platform PRIVATE
    "-framework Cocoa"
    "-framework QuartzCore"
  )
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
