# ADR-20260531 — Web Platform (Emscripten + WebGPU)

- **Status**: Accepted (Phase 1 Design)
- **Date**: 2026-05-31
- **Deciders**: Cemal TATLI
- **Related**: ADR-001 (RHI Architecture), ADR-002 (Renderer), T4.Q6 (Web platform scope)

## Bağlam (Context)

CHROMODYNAMIC, library-oriented hibrid 2D+3D engine hedeflenirken, browser dağıtımı (distribution) ve tarayıcı tabanlı editör imkanı açılmaktadır. Hedefler:

- **Browser dağıtımı**: Oyunlar, uygulamalar ve örnekler WebAssembly (WASM) olarak tarayıcıda çalıştırılabilmeli.
- **Editör tarayıcı desteği**: Gelecek versiyonda (Post-MVP) WebAssembly editör envanteri olabilmeli.
- **Library-oriented prensibi korunmalı**: `cd::rhi` ve rendering stack'i WASM context'inde de bağımsız kütüphane olarak tüketilebilmeli.
- **Modern GPU API**: WebGPU (W3C standardında yol alan, Dawn/Chromium, Firefox prototip, Safari geliştirmede) hedef; WebGL 2.0 tercihen opsiyonel fallback.

Mevcut SOTA:
- **Emscripten**: LLVM/Clang toolchain, C++ → WASM, POSIX emulation, sdl2-glue, WebAudio bridge.
- **Dawn / WebGPU**: Google/Khronos tarafından standartlaştırılan GPU API; Chromium, Firefox (Servo), Safari işbirliği; native backend'ler (Vulkan/D3D12/Metal) üzerinde çalıştırılabiliyor.
- **WebGL 2.0 / GLSL ES 3.0**: Eski tarayıcılar, ancak Descriptor Indexing yok; opak uniform buffer yönetimi gerekir.
- **IDBFS**: Emscripten'in IndexedDB tabanlı filesystem emulasyonu; persistent storage.
- **WebAudio API**: Ses için tarayıcı standardı; WASM bridge'i Emscripten + custom bağlantı noktası yoluyla.

Proje sınırlaması:
- T4.Q6 = C (out-of-scope v1); WebGPU 4. backend takvimi Post-MVP Sprint 12+.
- T4.3 (bu görev) = **ADR + CMake preset + platform stub** sadece.

## Karar (Decision)

**Emscripten + Dawn WebGPU + IDBFS + WebAudio bridge** kombinasyonu benimsenir:

### Araçlar ve toolchain

- **Emscripten SDK** (≥3.1.51): LLVM/Clang → WASM cross-compiler; POSIX emulation ve standar C++ kütüphanesi.
- **Emscripten browser.h**: İşletim sistemi tarafından sağlanan `emscripten_set_main_loop()`, `emscripten_fetch()`, browser event hooks.
- **Dawn library** (pre-built veya `FetchContent`): WebGPU C++ binding'leri, `webgpu/webgpu.h` header'ı, WASM üzerinde çalışan D3D12/Metal/Vulkan/OpenGL backend fallback'leri.
- **IDBFS** (Emscripten built-in): `/MOUNT_POINT` olarak tarayıcı IndexedDB'ye mirror; persistent asset cache.
- **WebAudio via JavaScript callback**: Emscripten SDL2 audio sürücüsü veya özel `cd::audio` bridge; tarayıcı WebAudio Context.

### Platform stub implementasyonu

**Hedef**: `engine/foundation/platform/web/` directory ile minimal proof-of-concept:

1. **main_web.cpp**: 
   - `int main()` → `emscripten_set_main_loop(tick_callback, 0, 1)` sarıcı.
   - `emscripten::val` veya direkt JavaScript binding yoluyla ImGui/swapchain invalidation sinyalleri.
   - Minimal frame loop: input polling → tick → render.

2. **index.html template**:
   - Canvas element, WASM module loader.
   - WebGPU feature detection fallback (WebGL 2.0 uyarısı).
   - Minimal styling + asset loading indicator.

3. **CMakeLists.txt** (foundation/platform Web target):
   - Emscripten compiler/linker flag'leri (`-fPIC`, `-sPIC`, `-sWASM_BIGINT`, vb).
   - IDBFS mount point tanımlaması.
   - SDL2 (Emscripten uyumlu) dependency (opsiyonel; minimal I/O yönetimi için).
   - Output: `.wasm` + `.js` bootstrapper.

### CMake preset: ninja-web-debug

```cmake
{
  "name": "ninja-web-debug",
  "displayName": "Emscripten Web Debug",
  "description": "Cross-compile to WebAssembly with Emscripten toolchain (Debug)",
  "inherits": "ninja-base",
  "cacheVariables": {
    "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/CMakeModules/emscripten.cmake",
    "CMAKE_BUILD_TYPE": "Debug",
    "CD_PLATFORM_WEB": "ON",
    "CD_ENABLE_TESTING": "OFF",
    "CD_ENABLE_SAMPLES": "ON",
    "EMSCRIPTEN_PREFIX": "$env{EMSDK}/upstream/emscripten"
  }
}
```

(Buildable veya pre-configured emscripten.cmake toolchain file gerekir.)

### Mimari sınırlar (Boundaries)

- **RHI backend**: WebGPU (Dawn library), OpenGL ES 3.0 fallback (GLSL ES).
- **Filesystem**: IDBFS mounted `/data/` directory; asset manifest JSON yükleme.
- **Audio**: WebAudio Context bridge (`cd::audio::WebAudioSink` stub).
- **Platform abstraction**: `cd::platform::Window` + `cd::platform::EventPump` Web uyumlu uygulaması; SDL2-Emscripten ve custom JavaScript event handler overlay.
- **Global state**: Shared libraries (`cd::core`, `cd::math`, `cd::rhi::core`) stateless veya explicit context object (global state yasak per CLAUDE.md §7).

### Build artefaktları

- `output.wasm` — WebAssembly modül (yürütülebilir bytecode).
- `output.js` — Emscripten runtime + WASM loader + glue code.
- `index.html` — Browser entry point; Canvas + WebGPU feature check.
- `output.wasm.map` — (Debug) source map.

### Timeline (T4.3 POST referansı, implementasyon v1 takvimi)

- **T4.3 (bu PR)**: ADR + preset stub + `main_web.cpp` skeleton.
- **T2.7 (parallel)**: WebGPU backend RHI (`cd::rhi::webgpu`) completion; test coverage.
- **T4.5 (Sequential T2.7 sonrası)**: Emscripten integration + IDBFS mount + sample app compilation (4-6 hafta estimated).
- **Post-MVP (T4.6+)**: Browser editor, hot-reload, multi-instance Web support.

## Reddedilen Alternatifler

| Alternatif | Sebep |
|---|---|
| **WebGL 2.0 only** | Deprecated, ES 2/3 feature parity eksik; bindless descriptor heap yok; opak uniform buffer yönetimi kısıtlı; modern GPU perf patterns uygulanamaz. WebGPU ileriye yönelik. |
| **wasm-bindgen (Rust-centric)** | CHROMODYNAMIC C++; wasm-bindgen Rust ekosistemini varsayıyor (cargo, Cargo.toml). Emscripten C++ native path daha doğrudan. |
| **Unity / Godot export fallback** | Out-of-scope; CHROMODYNAMIC kendi Web backend'i taşıyacak. |
| **asm.js (legacy)** | WASM modern ve daha hızlı; asm.js 2014 technoloji. |
| **Cheerp (Leaning Technologies)** | Tescilli; Emscripten açık kaynak ve daha geniş toolchain desteği. |
| **Native app shell (React Native, Flutter)** | Out-of-scope; Web platform hedefleri tarayıcı-spesifik. |
| **Single-threaded WASM + explicit async** | SharedArrayBuffer + web worker worker'larının güvenliğe dair çekinceler var; Phase 1 single-threaded yeterli; Phase 2 AsyncIO/job batching ile revisit. |

## Sonuçlar (Consequences)

**Pozitif**:
- Browser dağıtımı tarayıcı başında Day 1 roadmap item'ı; editor v2 imkanı açılır.
- Emscripten mature + geniş C++ toolchain desteği; pthread emulation, SDL2 bridge, IDBFS hazır.
- WebGPU standardında yol alıyor; Chrome, Firefox, Safari kademeli rollout; fallback OpenGL ES for older browsers.
- WASM modüler; asset streaming, dynamic module loading (Phase 2) uygulanabilir.
- Library-oriented prensibi korunur; `cd::rhi::core` WASM context'inde de stateless tüketilebilir.
- Cross-platform build; aynı CMake tree Windows/Linux/macOS/Web'e aynı anda target edebilir.

**Negatif / Risk**:
- Emscripten build time yavaş (LLVM IR → WASM compilation + linker); CI pipeline'ı taze optimizasyon gerektirebilir.
- IDBFS filesystem işlem performansı disk SSD'sinden daha yavaş; asset preload manifest + aggressive caching gerekli.
- WebGPU standardı halen Draft (Candidate Recommendation aşamasında); spec churn riski Post-MVP'de API değişiklikleri.
- Thread support (SharedArrayBuffer) tarayıcı security policy'si sebebiyle restricted; Phase 1 single-threaded, job queue async batch → Web worker offload Phase 2.
- Safari WebGPU desteği geç (2024 tech preview); fallback OpenGL ES / WebGL2 Safari older SIP policy'si sebebiyle test yükü.

**Replace-Ready (D1 disiplini)**:
- **Emscripten**: BSD licensed, CMake integration, GNU autotools. Fork/patch riski düşük; vendor lock-in yok.
- **Dawn**: Khronos + Google, WebGPU specification reference implementation. Upstream changes 3-6 ay window'da visible; dependency update düzenli.
- **IDBFS**: Emscripten built-in. Alternative: IndexedDB.js (custom wrapper) ~ SPAs trend'i. Swap'ı mümkün ama ikisinciyi çıkarmak gerekir.

## Açık Sorular

| ID | Soru | Karar | Çözüm noktası |
|---|---|---|---|
| Q1 | Emscripten→CMake integration (toolchain file location) | Vendored CMakeModules/emscripten.cmake veya EMSDK=$env{EMSDK} convention | T4.3 preset merge'de finalize |
| Q2 | WebGPU vs OpenGL ES 3.0 fallback ranking | WebGPU primary, OpenGL ES Feature::Optional detection at runtime | T2.7 RHI backend takvimi |
| Q3 | IDBFS quota + cleanup policy | 50 MB default, user-controlled; manifest + etag cache validation | T4.5 sample app design review |
| Q4 | Audio threading (AudioContext main thread restriction) | Single-threaded WebAudio, Phase 2 worker pool + MessagePort bridge | T4.5 spec |
| Q5 | Networking (fetch API vs XHR vs WebSocket) | Emscripten SDL_net wrapper veya custom fetch()-wrapper `cd::io::WebFetcher` | Phase 2 streamer design |

## Cross-Cutting

- **ADR-001 (RHI)**: WebGPU "4. backend" mentioned; spec T4.Q6 out-of-scope v1 ama ADR-001 Q4 updated.
- **ADR-002 (Renderer)**: Frame graph serialization (Web editor → server compile pipeline) Phase 2; stub'ı compile-time static shader.
- **ADR-005 (Foundation)**: Platform abstraction layer Web uyumlu (`cd::platform::Window` Emscripten glue, `cd::platform::SignalHandler` SIGINT web context'inde stub).
- **T4.5 (Post-MVP takvimi)**: Detailed Emscripten integration spec + sample app (hello_webgpu sample).
- **T2.7 (Concurrent)**: WebGPU backend implementation RHI; T4.3 stub'a precedes T4.5 integration.

## Kanıt

- Emscripten documentation: https://emscripten.org/ (acc 2026-05-31)
- WebGPU W3C specification: https://www.w3.org/TR/webgpu/ (acc 2026-05-31)
- Dawn project: https://dawn.googlesource.com/dawn (acc 2026-05-31)
- Emscripten + CMake: https://emscripten.org/docs/compiling/Building-Projects.html (acc 2026-05-31)
- Safari WebGPU progress: https://webkit.org/status/#specification-webgpu (acc 2026-05-31)
- Khronos WebGPU group: https://www.khronos.org/webgpu/ (acc 2026-05-31)

Akademik literatür (Demir Kural uygulanmaz; engineering SOTA'dan veri aktar):

- Dunne & Celes (2015) "Lua in Games: A Survey", IEEE Transactions on Games, vol. 7, no. 3 — **NOT APPLICABLE** (WASM binary format vs scripting language; diferent scope).
- Haas et al. (2017) "Bringing the Web Up to Speed with WebAssembly", PLDI 2017, DOI: 10.1145/3062341.3062363 — **FOUNDATIONAL** (WASM spec definition; referenced for architecture rationale only, no direct code dependency).

## Notlar

- **Phase 1 disiplini**: Bu ADR Design-stage kapıdır; implementasyon T2.7+T4.5 referansında. Kod yazılmayacak (T4.3 stub ve CMakeLists.txt setting'i hariç).
- **Toolchain file agnosticism**: `CMAKE_TOOLCHAIN_FILE` environment variable override edilebilecek şekilde flexible bırakılacak (CD-provided vendored file veya user EMSDK override).
- **Backwards compatibility**: Web build'i optional; default desktop build'ler (Vulkan, D3D12, OpenGL) unaffected.
