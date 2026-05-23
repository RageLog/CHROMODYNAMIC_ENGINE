# Mobile platform integration

CHROMODYNAMIC's mobile story has two prongs:

- **Android** — Vulkan path via the engine's `cd::rhi_vulkan`
  backend (already shipped). What's missing: an Android-side
  `cd::platform::IWindow` implementation backed by
  `ANativeWindow*`, an NDK-based CMake toolchain, and a
  `gradle` wrapper that drives the engine build as a
  pre-step of the APK's nativeBuildSystem.

- **iOS** — Metal path via the engine's `cd::rhi_metal`
  backend (factory shipped Wave 97; concrete implementation
  is the cross-cutting follow-up alongside Android). The
  iOS-side `cd::platform::IWindow` needs to wrap a
  `UIView*` (or `MTKView` via Swift bridge).

Phase 14.H ships the **scaffolding** for these — the CMake
toolchain configuration shape, the platform-window contract,
and the directory layout. The actual mobile builds run on
externally-provided NDK / Xcode toolchains and are not part
of the desktop CI matrix until a runner with those toolchains
is available.

## Build path — Android

```
# Prereqs: Android NDK r26+ installed; ANDROID_NDK_HOME set;
#          AGP-supported gradle project that wraps the engine build.

cmake -B build/android-arm64 \
      -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-29 \
      -DCD_ENABLE_VULKAN=ON \
      -DCD_ENABLE_SAMPLES=OFF \
      -DCD_DISABLE_VCPKG=ON

cmake --build build/android-arm64 --config Release
```

The `cd::platform` library compiles cleanly without an Android
backend; samples that pull `cd::platform::create_window` will
return `kNotImplemented` until the `AndroidWindow.cpp` lands
(its TU is gated by `defined(__ANDROID__)`).

A `gradle`-based wrapper that invokes the above from inside an
Android app's `app/build.gradle` is the natural integration
shape — the engine is consumed as a sub-project, not as a
gradle plugin.

## Build path — iOS

```
# Prereqs: Xcode 15+, command-line tools, iOS SDK >= 17.0.

cmake -B build/ios-arm64 \
      -G Xcode \
      -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
      -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCD_ENABLE_VULKAN=OFF \
      -DCD_ENABLE_SAMPLES=OFF \
      -DCD_DISABLE_VCPKG=ON

cmake --build build/ios-arm64 --config Release
```

Vulkan is off because MoltenVK on iOS is an opt-in vendor; the
default mobile path is `cd::rhi_metal`. The platform window's
iOS implementation is gated by `defined(__APPLE__) &&
TARGET_OS_IPHONE`.

## What lands next

- `mobile/android/` — `AndroidWindow.cpp` + `JniBridge.cpp` +
  example gradle/CMakeLists wrapper, alongside an actual
  Android runtime test (probably a device-loop CI step).
- `mobile/ios/` — `IosWindow.mm` + an Xcode workspace template
  that links the engine static libs.
- Mobile-specific samples: `hello_triangle_android`,
  `hello_triangle_ios`. Both targeting `cd::rhi_native`'s
  cross-API dispatcher so the same sample code drives Vulkan
  on Android and Metal on iOS.

## References

- [docs/ADR/ADR-20260523-wave164-v0.41.0-phase14h-mobile-scaffold.md](../docs/ADR/ADR-20260523-wave164-v0.41.0-phase14h-mobile-scaffold.md)
- engine/foundation/platform/include/cd/platform/Window.hpp —
  the IWindow contract the mobile backends will plug into
- engine/render/rhi_vulkan/ — Android-side renderer
- engine/render/rhi_metal/ — iOS-side renderer (Wave 97 factory
  stub; concrete code is the follow-up wave)
