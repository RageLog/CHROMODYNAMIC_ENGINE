# ADR-20260531 — Android Platform Support

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-31
- **Deciders**: Cemal TATLI
- **Related**: ADR-001 (RHI), ADR-009 (UI), ADR-014 (CI/CD), ADR-006 (Asset Pipeline)

## Bağlam (Context)

CHROMODYNAMIC, cross-platform library-oriented engine olarak masaüstü (Windows, macOS, Linux) başlamıştır. Mobil platform destek (iOS, Android) Phase 2 hedefi. Tüm direktifler:

- **Target**: Android 11+ (API Level 30+) — NDK r25+ C++23 toolchain
- **Graphics**: Vulkan-only mobile strategy (OpenGL ES deprecation) — VK_KHR_android_surface, VK_KHR_dynamic_rendering
- **Input/Window**: GLFW (desktop) → SDL2 (mobile-friendly event loop, clipboard, IME support)
- **Asset I/O**: Runtime asset loading from APK's `assets/` folder via AAssetManager API
- **Entry point**: NativeActivity (single-window, lifecycle callbacks) — no Java/Kotlin boilerplate
- **Build**: NDK CMake toolchain + preset-driven, ninja-android-{debug,release} presets
- **Phase scope**: v1 = stub + golden-path infrastructure; actual v1 android sample = Phase 2

Performance expectations: 60 FPS target on Snapdragon 8 Gen 2, 120 FPS on flagship; motion-to-photon < 20 ms (parity with desktop). Asset streaming during gameplay (glTF, texture atlases).

## Karar (Decision)

### Platform Abstraction

1. **cd::platform** wraps OS window + input → `IWindow`, `OSEvent`. Android is a new `cd_platform_android` backend.
2. **Window creation factory** (`cd::platform::create_window()`) is runtime-patched to dispatch `SDL2` on Android instead of GLFW.
3. **Compiler/build chain**:
   - NDK CMake toolchain file at `CMakeModules/Android.toolchain.cmake`
   - Preset `ninja-android-debug` / `ninja-android-release` inherit from `ninja-base`
   - Cache variable `CD_ANDROID_API_LEVEL` (default 30), `CD_ANDROID_ABI` (default `arm64-v8a`)

### Asset Management

4. **AssetManager wrapper** (`include/cd/platform/android/AssetManagerIO.hpp`):
   - Thin C++ wrapper around Android NDK `AAssetManager` API
   - Asynchronous I/O path: spawn worker thread on asset read, callback on completion
   - Integrates with `cd::asset::Pipeline` via `IAssetLoader` plugin interface
   - Returns `cd::core::Result<std::vector<uint8_t>>` on read

5. **APK assets lifecycle**:
   - `ANativeActivity::assetManager` pointer saved at app init (lifecycle callback)
   - Asset search paths: hardcoded `assets/` root (no subdirectory discovery yet; Phase 2)
   - Shader binaries + textures + glTF models live in `assets/shaders/`, `assets/textures/`, `assets/models/`

### Graphics API

6. **Vulkan-only** on Android:
   - `cd::rhi::Device<BackendTrait::Vulkan>` instantiates `VkInstance` with `VK_KHR_android_surface` extension
   - `SDL2_vulkan.h` queries Vulkan extensions required for surface creation
   - SPIR-V -> GLSL fallback **not implemented** v1 (Vulkan is mandatory)

7. **Display/Swapchain**:
   - `IWindow::native_window_handle()` returns `ANativeWindow*` on Android
   - `cd::rhi::SwapchainDesc` receives `window_handle = (void*)(ANativeWindow*)`, `display_handle = nullptr`
   - Vulkan `vkCreateAndroidSurfaceKHR()` consumes the ANativeWindow pointer

### Input/Event Loop

8. **SDL2 integration** (vs GLFW for Android):
   - SDL2 has native Android Java/C glue (SDL_android.c)
   - Event pump: `SDL_PollEvent()` → `cd::platform::OSEvent` translation layer
   - Touch input (SDL_FINGERDOWN/FINGERMOTION/FINGERUP) → `cd::platform::OSEvent` (kMouseMove, kMouseButtonDown, etc. for now; kTouchXX added Phase 2)
   - Soft keyboard: SDL2 shows/hides IME on focus/unfocus; no explicit handling v1

### Entry Point

9. **NativeActivity + JNI glue**:
   - Application entry: `ANativeActivity_onCreate()` → saved to global static `g_native_activity`
   - Main game loop runs in native thread spawned by glue
   - Lifecycle: `onStart()`, `onResume()`, `onPause()`, `onStop()`, `onDestroy()` routed to `cd::platform::android::ActivityLifecycleListener` callbacks
   - **No Java/Kotlin required** — NDK's built-in `android_native_app_glue.c` handles JNI bootstrap

10. **C++ main() bootstrap**:
    - `src/platform/android/main_android.cpp` implements `android_main(struct android_app* state)` entry point
    - Initializes SDL2, creates window via `cd::platform::create_window()`
    - Runs engine's `HelloEngine` or sample app main loop
    - Cleanup on ANativeActivity pause/destroy

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **GLFW for Android** | GLFW dropped Android support in v3.3; community forks unsupported. SDL2 is maintained. |
| **OpenGL ES backend** | OpenGL ES deprecated (Khronos EOL path); Vulkan proven on Android 5.0+; drives bindless, ray tracing future work. |
| **Java/Kotlin activity in Java** | `ANativeActivity` sufficient; full Java layer adds build complexity (gradle, NDK-build two-pass). Single-language C++ (via glue) preferred. |
| **Content provider for assets** | `AAssetManager` from APK is simpler; content provider adds permission/IPC overhead. |
| **In-app threading per asset** | ThreadPool pattern requires careful lifecycle (pause/resume may suspend). Phase 2: explicit asset streaming. |
| **OpenXR for VR** | Out-of-scope Phase 1; eye-tracking, controller input, compositor integration add 4+ weeks. Phase 3+. |
| **Multiple NDK toolchain variants** | Single NDK r25+ baseline; fallback to r23 if users need older API level outside Phase 1. |

## Sonuçlar (Consequences)

**Pozitif**:
- Vulkan-only simplifies shader toolchain (SPIR-V canonical, no GL -> GLSL transpile).
- SDL2 proven event loop + input; Wayland/X11/Android backends unified.
- `AAssetManager` integrates cleanly with asset pipeline's `IAssetLoader` plugin.
- NativeActivity avoids Java boilerplate; NDK r25+ cmake support is solid.
- `cd::platform::android` is standalone library; other platforms unaffected.
- Preset-driven build (ninja-android-debug) integrates with Phase 1 design discipline.

**Negatif / Risk**:
- Vulkan-only may alienate Android 4.x devices (< 5% market share 2026); accept it.
- SDL2 dynamic linking (`.so` in APK) adds 2-3 MB binary size; static link possible but complex. Phase 2: evaluate.
- Lifecycle (pause/resume) requires careful shutdown (frame graph flush, asset unload); test burden. Phase 2: stress test suite.
- Cross-compilation on Windows (MSVC host) with NDK requires `CMAKE_SYSTEM_NAME=Android` override; validation needed. Phase 2 CI.
- Touch vs mouse abstraction (both route to OSEvent::kMouseMove for now) may confuse multi-touch gestures; Phase 2: kTouchXX events.

**Replace-Ready (D1 discipline)**:
- `cd::platform::android::AssetManagerIO` wraps NDK libc `<android/asset_manager.h>` only; zero external dependency beyond NDK.
- `cd::platform::android::ActivityLifecycle` routes JNI callbacks; no Kotlin runtime dependency.

## Açık Sorular

| ID | Soru | Karar v1 | Çözüm noktası |
|---|---|---|---|
| Q1 | Tremolo (app-controlled pause) vs automatic suspend on app backgrounding | Auto via NativeActivity::onPause | Phase 2 stress test |
| Q2 | Multi-touch gesture recognition (pinch, rotate) | Not v1; kTouchXX events Phase 2 | ADR-20260630-android-input |
| Q3 | Haptic feedback (vibration API) | Out-of-scope v1; Phase 3 if demanded | Demo with dummy no-op |
| Q4 | App-specific temp dir for cache (shaders, compiled assets) | Use Android `app.cacheDir` via JNI; Phase 2 integration | ADR-20260630-android-storage |
| Q5 | Multiple device orientation (portrait/landscape) | Fixed landscape Phase 1; manifest orientation override; Phase 2: rotation event handling | ADR-20260630-android-lifecycle |
| Q6 | Game pad / XInput controller support | SDL2 joystick subsystem available; no hello_engine demo Phase 1 | Phase 2 sample integration |

## Architektur Detay

### Directory Layout

```
engine/foundation/platform/
├── include/cd/platform/
│   ├── Window.hpp                          (unchanged)
│   ├── OSEvent, IWindow abstraction
│   └── android/
│       └── AssetManagerIO.hpp              (NEW) — AAssetManager wrapper
├── src/platform/
│   ├── android/
│   │   ├── AssetManagerIO.cpp              (NEW)
│   │   ├── ActivityLifecycle.hpp           (NEW)
│   │   ├── ActivityLifecycle.cpp           (NEW)
│   │   └── main_android.cpp                (NEW) — android_main() entry
│   └── ... (Win32, Wayland, Cocoa unchanged)
└── tests/
    └── platform_android_test.cpp           (NEW, stub)
```

### CMakeLists Integration

```cmake
# engine/foundation/platform/CMakeLists.txt
if(ANDROID)
  target_sources(cd_platform PRIVATE
    src/platform/android/AssetManagerIO.cpp
    src/platform/android/ActivityLifecycle.cpp
    src/platform/android/main_android.cpp
  )
  target_link_libraries(cd_platform PRIVATE
    android                                  # NDK libc android.h
    log                                      # Android NDK logging
  )
  # Vulkan surface extension is pulled in by cd::rhi
endif()

# CMakePresets.json
# ninja-android-debug / ninja-android-release
# CMAKE_SYSTEM_NAME=Android
# CMAKE_ANDROID_PLATFORM=android-30
# CMAKE_ANDROID_ABI=arm64-v8a
# CMAKE_ANDROID_NDK=<NDK root, e.g. $HOME/android-ndk-r25>
```

### Build Command

```bash
# Requires NDK r25+ installed (e.g., $ANDROID_NDK_ROOT=/opt/android-ndk-r25)
cmake --preset ninja-android-debug
cmake --build --preset ninja-android-debug
```

### Preset Definition (CMakePresets.json addition)

```json
{
  "name": "ninja-android-base",
  "displayName": "Android NDK (Ninja Multi-Config)",
  "description": "Android 11+ / NDK r25+ / arm64-v8a",
  "inherits": "ninja-base",
  "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" },
  "cacheVariables": {
    "CMAKE_SYSTEM_NAME": "Android",
    "CMAKE_SYSTEM_VERSION": "30",
    "CMAKE_ANDROID_PLATFORM": "android-30",
    "CMAKE_ANDROID_ABI": "arm64-v8a",
    "CMAKE_ANDROID_NDK": "$env{ANDROID_NDK_ROOT}"
  }
}
```

## Implementation Roadmap

**Phase 1 (v0.99.XX → v1.0.0 + Android scaffolding)**:
- Week 1: ADR approval + CMakeModules/Android.toolchain.cmake skeleton + presets
- Week 2: AssetManagerIO header + stub impl + basic unit test
- Week 3: NativeActivity glue + main_android.cpp + HelloEngine android sample
- Week 4: SDL2 integration, window creation + Vulkan surface binding (golden path)
- Week 5: CI provisioning (self-hosted Android device runner or emulator, Phase 2)

**Phase 2 (Android v1 feature complete)**:
- Lifecycle stress test (pause/resume/destroy cycles)
- Touch multi-gesture recognition
- Storage (cache dir, app-specific storage) integration
- App icon, permissions manifest
- Play Store distribution prep

## Cross-Cutting

- **ADR-001 (RHI)**: Vulkan backend consumes Android surface from `IWindow::native_window_handle()`
- **ADR-006 (Asset)**: `cd::asset::Pipeline` loads glTF/shader via `AssetManagerIO` plugin
- **ADR-009 (UI)**: ImGui native Android support (soft keyboard, touch) Phase 2
- **ADR-014 (CI/CD)**: Android self-hosted runner or cloud device farm Phase 2
- **CLAUDE.md § 6**: CMakeModules expanded; cross-compile matrix (Windows→Android NDK via WSL/native, macOS→Apple Silicon) Phase 2

## Kanıt

**SOTA mobile rendering engines**:
- Unreal Engine 5 Android: NDK + Vulkan mandatory, ES2 legacy path dropped 5.2+; https://dev.epicgames.com/documentation/en-us/unreal-engine/android-game-development (acc 2026-05-31)
- Godot Engine 4.x Android: Godot Android plugin system, Vulkan primary; https://docs.godotengine.org/en/stable/tutorials/platforms/android/index.html (acc 2026-05-31)
- SDL2 Android support: Full native event loop + JNI glue, maintained; https://github.com/libsdl-org/SDL/tree/main/src/core/android (acc 2026-05-31)
- Android NDK r25 release: CMake toolchain, C++23 support; https://developer.android.com/ndk/downloads (acc 2026-05-31)
- Khronos VK_KHR_android_surface spec: Surface binding for native ANativeWindow; https://registry.khronos.org/vulkan/specs/1.3/html/vkspec.html#VK_KHR_android_surface (acc 2026-05-31)

**Mobile platform ecosystem 2026**:
- Android market share: ~70% global (gartner.com, 2025-2026 forecast) — primary mobile target
- Vulkan adoption: Vulkan 1.3 on ~85% Android 10+ devices; OpenGL ES deprecated (Khronos EOL 2024)
- NDK adoption: Google actively maintains r25+; r23 support-life extended to 2026

**Engineering blogs / references**:
- Google Filament on Android: Performance profiling + asset management; https://github.com/google/filament/blob/main/docs/content/materials.md (acc 2026-05-31)
- Arm Mali GPU guides: Optimization patterns for Vulkan on Android; https://developer.arm.com/documentation/101897/ (acc 2026-05-31)

Akademik literatür (Demir Kural — şu an null; Phase 2 demand bazında):
- None required Phase 1 (mobile rendering is engineering-driven SOTA; pure research papers on Android-specific architecture are rare)

## Jira / Tracking

**Phase 1 deliverables**:
- T4.1-a: ADR-20260531-android-platform.md ✓ (this doc)
- T4.1-b: CMakeModules/Android.toolchain.cmake stub
- T4.1-c: CMakePresets.json ninja-android-{debug,release}
- T4.1-d: `include/cd/platform/android/AssetManagerIO.hpp` stub
- T4.1-e: `src/platform/android/main_android.cpp` NativeActivity entry
- T4.1-f: Build skeleton (ninja-android-debug, expect LNK error on non-NDK host; OK)
- T4.1-g: Commit hash + tag `phase532-android-platform`
