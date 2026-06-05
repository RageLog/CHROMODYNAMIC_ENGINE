# =============================================================================
# apps/android_demo — CHROMODYNAMIC Android Demo (Phase 772 / FINALE-7 W2B F3)
#
# Sprint-1 scope:
#   * ANativeWindow + NativeActivity entry point via cd::platform::android.
#   * AAssetManager I/O (cd::platform::android::AssetManagerIO).
#   * Touch / key bridge (cd::platform::android::AndroidInputBridge).
#   * Vulkan surface wiring via VK_KHR_android_surface (stub; NDK host needed).
#
# This CMakeLists.txt is ONLY processed when CMAKE_SYSTEM_NAME=Android
# (i.e. the NDK toolchain is active).  On Win11 / Linux desktop it is
# guarded by the if(ANDROID) block in apps/CMakeLists.txt so the host
# build is completely untouched.
#
# Build instructions (NDK host):
#   cmake -B build-android \
#       -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#       -DANDROID_ABI=arm64-v8a \
#       -DANDROID_PLATFORM=android-26 \
#       -DANDROID_STL=c++_shared \
#       -G Ninja
#   cmake --build build-android
#
# Output: libandroid_demo.so — loaded by the NativeActivity in the APK.
#
# APK packaging (user-owned):
#   Use gradle / AGP to wrap the .so into an APK with the AndroidManifest.xml
#   from this directory.  The CHROMODYNAMIC project does not ship a Gradle
#   wrapper; device CI is user-owned as per Sprint-1 scope.
# =============================================================================

if(NOT ANDROID)
  message(FATAL_ERROR "apps/android_demo must only be configured for Android targets.")
endif()

# ---- Sources -----------------------------------------------------------------

set(_android_demo_sources
  src/android_demo_main.cpp
)

# ---- Shared library (NativeActivity .so) -------------------------------------

add_library(android_demo SHARED ${_android_demo_sources})
set_target_properties(android_demo PROPERTIES
  OUTPUT_NAME  "android_demo"
  FOLDER       "apps"
)

# android_native_app_glue provides android_main glue code.
# The NDK ships it as a static library; we link it here.
# CMake FindAndroid / FetchContent not needed — the NDK toolchain exposes it.
find_library(log-lib log REQUIRED)
find_library(android-lib android REQUIRED)

target_link_libraries(android_demo
  PRIVATE
    cd::platform          # AndroidWindow + AndroidInputBridge + AssetManagerIO
    cd::core              # Result / ErrorCode / Log
    ${android-lib}        # android_native_app_glue / ALooper / ANativeWindow
    ${log-lib}            # __android_log_print
)

# android_native_app_glue source — shipped with NDK; include via ANDROID_NDK.
if(DEFINED ANDROID_NDK)
  target_sources(android_demo
    PRIVATE
      "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
  )
  target_include_directories(android_demo
    PRIVATE
      "${ANDROID_NDK}/sources/android/native_app_glue"
  )
else()
  message(WARNING "ANDROID_NDK not set — android_native_app_glue not found. "
                  "Set -DANDROID_NDK=<path> when configuring.")
endif()

target_include_directories(android_demo
  PRIVATE
    src
)

# C++23 standard — matches engine-wide policy (CLAUDE.md §1).
target_compile_features(android_demo PRIVATE cxx_std_23)

# Warn flags consistent with engine policy (CLAUDE.md §6).
target_compile_options(android_demo
  PRIVATE
    -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wpedantic
    $<$<CONFIG:Debug>:-g3 -O0>
    $<$<CONFIG:Release>:-O2>
)
