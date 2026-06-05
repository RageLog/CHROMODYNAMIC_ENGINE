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
# Phase 771 — iOS UIKit + CAMetalLayer backend (Sprint-1 scaffold).
# Compiled only when CMAKE_SYSTEM_NAME=iOS (ninja-ios-* presets).
# On macOS desktop the TARGET_OS_IPHONE=0 guard inside IosWindow.mm
# prevents UIKit inclusion; the if(IOS) split here avoids adding the
# Sprint-1 .mm files that import UIKit headers absent from the macOS SDK.
if(IOS)
  list(APPEND _platform_sources
    src/IosWindow.mm
    src/platform/ios/IosLifecycle.mm
    src/platform/ios/main_ios.mm
  )
elseif(APPLE)
  # macOS: legacy IosWindow.mm stub only (TARGET_OS_IPHONE guard ensures
  # no UIKit symbols are compiled when building for macOS).
  list(APPEND _platform_sources src/IosWindow.mm)
endif()

# Phase 532 (v1.0.0 Android) — NativeActivity + AssetManager wrapper.
# Phase 772 (FINALE-7 W2B F3) — AndroidWindow.hpp + AndroidInputBridge.hpp.
if(ANDROID)
  list(APPEND _platform_sources
    src/platform/android/AssetManagerIO.cpp
    src/platform/android/ActivityLifecycle.cpp
    src/platform/android/main_android.cpp
  )
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

# Phase 771 — iOS UIKit + QuartzCore (CAMetalLayer) framework links.
if(IOS)
  target_link_libraries(cd_platform PRIVATE
    "-framework UIKit"
    "-framework QuartzCore"
    "-framework Metal"
  )
  # Expose iOS-specific headers so consumers can reach
  # <cd/platform/ios/IosWindow.hpp> and <cd/platform/ios/IosLifecycle.hpp>.
  target_include_directories(cd_platform
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_definitions(cd_platform PUBLIC CD_PLATFORM_IOS=1)
endif()

if(ANDROID)
  # Android NDK libraries for asset manager + JNI glue.
  target_link_libraries(cd_platform PRIVATE
    android                # AAssetManager API
    log                    # Android logging
  )
  # Phase 772: AndroidInputBridge.hpp is a header-only bridge that translates
  # AInputEvent* to cd::ui::input wire types.  Including the header from an
  # Android consumer requires cd::ui_input on the include path.  Expose it as
  # PUBLIC so android_demo and any other Android binary that links cd_platform
  # automatically gets the cd::ui_input include path too.
  target_link_libraries(cd_platform PUBLIC cd::ui_input)
  # Expose the android sub-directory via the platform include path so that
  # consumers can reach <cd/platform/android/AndroidWindow.hpp> and
  # <cd/platform/android/AndroidInputBridge.hpp> without extra include dirs.
  target_include_directories(cd_platform
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )
  target_compile_definitions(cd_platform PUBLIC CD_PLATFORM_ANDROID=1)
endif()

# Phase 773 (FINALE-7 W2C Sprint-1) — Web platform (Emscripten + WebGPU)
# Guarded entirely by EMSCRIPTEN so Win11 builds are untouched.
if(EMSCRIPTEN)
  # Web sources: WebWindow + WebInputBridge + main_web entry point.
  target_sources(cd_platform
    PRIVATE
      src/platform/web/WebWindow.cpp
      src/platform/web/WebInputBridge.cpp
  )

  # Expose web headers via the existing cd_platform include path.
  # Consumers: #include <cd/platform/web/WebWindow.hpp>
  target_include_directories(cd_platform
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  )

  # Emscripten compiler flags for the platform library.
  target_compile_options(cd_platform
    PRIVATE
      -fPIC
      $<$<CONFIG:Debug>:-g3>
      $<$<CONFIG:Release>:-O2>
  )

  # Emscripten linker flags applied to the platform library object files.
  # Executable-level flags (-sUSE_WEBGPU, --shell-file, etc.) live in
  # apps/web_demo/CMakeLists.txt so the library stays reusable.
  target_compile_definitions(cd_platform
    PUBLIC
      CD_PLATFORM_WEB=1
  )
endif()

# Legacy subdirectory stub (Phase 533): kept for backward compat, now a no-op
# because all sources have moved to src/platform/web/.
if(CD_PLATFORM_WEB AND CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  add_subdirectory(web)
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
