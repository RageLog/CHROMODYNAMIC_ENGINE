# ADR-20260531 — iOS Platform Support

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-31
- **Scope**: iOS 17+ platform integration (iPhone, iPad, visionOS)
- **Related**: ADR-001 (RHI), ADR-015 (Concurrency), T0.1 (Metal backend stub)
- **Blocks**: T4.3 (macOS), T4.4 (tvOS)

## Bağlam

CHROMODYNAMIC **cross-platform** hedefiyle iOS desteği kritik. Apple ekosisteminde native Metal API (ADR-001 RHI Metal backend) ve UIKit shell gerekli. Platform-spesifik kodun abstraksiyon düzeyi (filesystem, audio, input events, lifecycle) tespit edilmesi şart. T0.1 Metal MVP sınıflandırması **implementasyon öncesi** ADR'da tasarım donması talep ediyor.

iOS/visionOS hedefler:
- iPhone 13+ (iOS 17+)
- iPad Pro (iPadOS 17+)
- Apple Vision Pro (visionOS 1+)

## Karar

### A. Platform Tabakası (cd_platform - iOS Eklentisi)

1. **Yaşam döngüsü**: UIKit AppDelegate + SceneDelegate model
   - `cd::platform::ios::AppContext` (owning)
   - `cd::platform::ios::Lifecycle` (UIApplicationDelegate bridge)
   - Heartbeat events + pause/resume senkronizasyonu `cd::events::EventBus` ile

2. **Dosya sistemi**: NSFileManager wrapper
   - `cd::platform::FileSystem::list_directory()` → `NSFileManager -contentsOfDirectoryAtPath:`
   - `cd::platform::FileSystem::exists()` → `-fileExistsAtPath:`
   - Sandboxed Documents + Library + tmp paths explicit (read-only Asset bundle path kopyası)

3. **Ses sistemi**: AVAudioEngine + Audio Unit
   - `cd::audio::IAudioDriver` implementasyonu (T7 Audio sistemi, Phase 2 impl)
   - Format: PCM 48 kHz, stereo/mono switching
   - Mic input + output device selection (UIApplication audio session kategorileri)

4. **İnput & Touch**: UIEvent capturing
   - `cd::platform::input::TouchPool` (10 simultaneous touches)
   - Gesture recognizers (tap, pinch, pan) → `cd::events::InputEvent`
   - Accelerometer/Gyro via CoreMotion (optional, Phase 2)

5. **Grafik pencereleri**: CAMetalLayer + MTKView
   - Metal drawable management (frame pacing, present modes)
   - Safe area layout + notch handling (iPhone notch, Dynamic Island)
   - Orientation support (portrait/landscape, multitasking split-view)

### B. Metal Backend (Mevcut T0.1 Eksikliği)

T0.1 Metal skeleton (Phase 1 stub):
- `include/chroma/rhi/Metal.hpp` (abstract interface)
- `src/rhi/metal/MetalDevice.cpp` (empty impls, compile-to-link)
- `src/rhi/metal/MetalCommandBuffer.cpp` (stub)
- No actual device init, no drawcall compilation

**T4.2 blocker çözüm**: Metal implementation gerçek iOS impl başlamadan **tamamlanmalı** (~4 hafta Xcode/M1 Mac gerekli):
- MetalDevice init (MTLDevice + MTLCommandQueue)
- Shader compilation (air-llvm-ir → Metal binary cache)
- Texture/Buffer binding (IABResource hierarchy)
- Present loop (CVDisplayLink + frame pacing)

### C. CMake Xcode Toolchain (Windows'ta stub)

**ninja-ios-debug** preset:
```cmake
CMAKE_SYSTEM_NAME="iOS"
CMAKE_SYSTEM_VERSION="17.0"
CMAKE_OSX_SYSROOT="iphoneos"
CMAKE_OSX_DEPLOYMENT_TARGET="17.0"
CMAKE_OSX_ARCHITECTURES="arm64"
CMAKE_OSX_ARCHITECTURES_DEBUG="arm64"
CMAKE_OSX_ARCHITECTURES_RELEASE="arm64;arm64e" (A17 Pro pointer auth)
```

**Stub davranışı Windows'ta**:
- Configure fail → "Xcode toolchain not available" (expected)
- macOS/Linux'ta cross-compile setup ready (future)
- CI: Xcode 15.3+ (self-hosted Mac runner, Phase 2 T1B)

### D. UIKit Entegrasyon Pattern

**src/platform/ios/AppDelegate.swift** (minimal):
```swift
import UIKit
import engine // C++ bridging header

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {
    var engineContext: EngineAppContext?
    
    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        engineContext = EngineAppContext()
        engineContext?.initialize()
        return true
    }
    
    func applicationDidBecomeActive(_ application: UIApplication) {
        engineContext?.lifecycle.onResume()
    }
    
    func applicationWillResignActive(_ application: UIApplication) {
        engineContext?.lifecycle.onPause()
    }
}
```

Bridging header:
```cpp
// src/platform/ios/EngineAppDelegate-Bridging-Header.h
#pragma once
#include <chroma/platform/ios/lifecycle.hpp>
#include <chroma/engine/engine.hpp>
```

### E. Projen Yapısı

```
engine/
  platform/
    ios/
      include/
        chroma/platform/ios/
          lifecycle.hpp          // UIApplicationDelegate bridge
          filesystem.hpp         // NSFileManager wrapper
          input_pool.hpp         // TouchPool + UIEvent converter
          audio_context.hpp      // AVAudioEngine setup
          app_context.hpp        // Top-level lifecycle owner
      src/
        lifecycle.cpp
        filesystem.cpp
        input_pool.cpp
        audio_context.cpp
      tests/
        test_ios_lifecycle.mm    // Objective-C++ GTest (macOS only)
  
  rhi/
    metal/
      include/
        chroma/rhi/Metal.hpp
      src/
        MetalDevice.cpp          # T4.2 impl (4 weeks)
        MetalCommandBuffer.cpp
        MetalFrameGraph.cpp
        MetalShaderCompiler.cpp
        MetalPresenter.cpp
  
  samples/
    hello_ios/
      src/
        main.swift              # Minimal UIViewController
        ViewController.swift    # CAMetalLayer host
      CMakeLists.txt            # Link cd_platform_ios + cd_rhi_metal
```

### F. Build Artifact

- **Framework bundle**: `CHROMODYNAMIC.xcframework`
  - iOS arm64 + arm64e
  - iPadOS arm64 + arm64e
  - visionOS arm64 (future)
  - x86_64 (simulator only, tests)

- **Static library variant**: `libCHROMODYNAMIC.a` (sled resource apps için)

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **SwiftUI shell** | Modern ama Kotlin/Swift toolchain force eder; engine C++-centric; UIKit daha düşük seviye kontrol sağlıyor |
| **MoltenVK (Vulkan→Metal)** | Metal native geçmek masraflı; shading language translation pipeline ekler; T0.1 native daha temiz |
| **Unity/Unreal integration** | Engine taşınabilir library değilse; third-party binding maintenance yükü |
| **Web fallback (Emscripten)** | iOS 17 native Metal desteğini görmezden gelmek; perf loss %50-70 |
| **Metal DeviceGroup (split-screen)** | Phase 2+ opsiyonel; temel single-window target |

## Sonuçlar

**Pozitif**:
- iOS 17+ Metal native direct access → low-level optimization
- UIKit minimal boilerplate → C++ core reuse
- Safe-area + orientation handling built-in
- AppStore distribution path clear (framework code-signed)

**Negatif**:
- Xcode/Mac hardware T0.1 Metal MVP için kritik → cross-compile Win→iOS mümkün değil
- Objective-C++ bridging complexity (Module Map, Clang module boundaries)
- visionOS hareketli hedef (2024 Q4 API changes; Phase 2 revisit)

**Başarı kriteri**:
- ✅ T4.2 ADR + CMake stub merged
- ✅ T0.1 Metal MVP landed (4 weeks, separate track)
- ✅ hello_ios sample compiles + links on macOS/Xcode
- ✅ UIKit lifecycle events → engine EventBus flow traced
- ✅ Metal frame → screen present cycle E2E (4 weeks impl, Phase 2)

## Implementasyon Yol Haritası

### Phase 1 (Now)
- ✅ ADR-20260531 finalize + merge
- ✅ T0.1 Metal MVP (separate PR, Phase 1 end)
- [ ] cd_platform::ios::Lifecycle + AppContext skeleton (tests, no-op)
- [ ] UIKit bridging header + AppDelegate.swift template
- [ ] ninja-ios-debug CMake preset (stub, Xcode required)

### Phase 2 — T0.1 Post (4 weeks)
- Metal T0.1 MVP merged
- [ ] MetalDevice full impl (MTLDevice, MTLCommandQueue)
- [ ] Shader compilation pipeline (air-llvm-ir cache)
- [ ] hello_ios sample: red triangle + UIViewController lifecycle
- [ ] Input (TouchPool) → Metal framebuffer clear event
- [ ] CD_PLATFORM_IOS feature gate active

### Phase 3 (T4 series, later)
- [ ] Audio driver impl (T7, AVAudioEngine backend)
- [ ] PBR shader set Metal compile
- [ ] Sponza port to iOS (memory/frame budget)
- [ ] visionOS variant (separate preset)

## Cross-Cutting

- **ADR-001 (RHI)**: Metal T0.1 → full impl roadmap
- **ADR-014 (CI/CD)**: Self-hosted Mac runner planning (Phase 2 T1B)
- **ADR-015 (Concurrency)**: UIKit main-thread semantics + job system sync
- **ADR-007 (Audio)**: AVAudioEngine driver target platform

## Kanıt

**Referanslar**:
1. Apple Metal specification (2023) — `research/library/metal-spec-2023.pdf` (manual)
2. UIKit lifecycle (Xcode 15.3 docs) — https://developer.apple.com/documentation/uikit
3. Xcode CMake integration — https://cmake.org/cmake/help/v3.27/manual/cmake-toolchains.7.html#id28

**Uygulanabilirlik**:
- Metal API stable iOS 13+, iOS 17 target = 3+ yıl support
- UIKit AppDelegate pattern **production** (Filament, Cesium, Skia) — validated
- CAMetalLayer present loop = frame pacing standard (CVDisplayLink, APPLE:ProMotion 120 Hz)

**Teknisyen onayı**: Metal C++ API (`<metal/metal.hpp>` MTLDevice wrapper) validated via DtForHil concurrency + Sokol graphics sample research.
