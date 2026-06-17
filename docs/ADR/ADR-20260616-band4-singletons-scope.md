# ADR-20260616 — ALL-MODULES-TO-100 BAND 4 / 3 Singletons Kapsam Mührü (platform · particle_system · imgui_backend)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 68402ec)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 4 / 3-singletons close-out — seal-the-deferred-by-design + real-untested-branch topup + add-tests-where-host-testable + correct-misleading-banner pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 4 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; bu üçlünün named gap'leri:
    cd::platform "web/android/iOS window backends (kNotImplemented today) or seal
    desktop-first", cd::particle_system "GPU dispatch + force fields (CPU-only by
    scope) or seal scope", cd::imgui_backend "add tests (zero today) + verify the
    thin D3D12 path; seal" §BAND 4 tablo)
  - `docs/PROJECT_COMPLETION_STATUS.md` §1 (cd::platform 65: "Win32Window (489) +
    X11Window (249) real; web/android/iOS are kNotImplemented stubs") + §6
    (cd::particle_system 65: "SoA CPU sim … GPU dispatch + force fields explicitly
    out-of-scope (documented)") + §2 (cd::imgui_backend 60: "real Vulkan ImGui
    backend (impl_vulkan + win32 input) + ProfilerView; D3D12 header thin/unverified;
    **NO tests**")
  - `docs/ADR/ADR-20260616-band3-foundation-scope.md` +
    `docs/ADR/ADR-20260616-band3-world-scope.md` (kardeş band-mühür ADR'ları, aynı
    şablon: impl/topup-the-small-clean-named-gap + seal-the-rest + promote-on-need)
  - rhi Metal device-gated test deseni (`engine/render/rhi/tests/CMakeLists.txt`
    §Metal `#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)` + GTEST_SKIP) —
    imgui_backend'in GPU-render doğrulaması için referans device-gating modeli.
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı + kesin tetikleyici taşır. Bu pass'te değişiklikler YALNIZ
  `engine/foundation/platform/` + `engine/world/particle_system/` +
  `engine/imgui_backend/` + bu `docs/ADR/` dosyası altında. Kardeş band-4 asset
  pass'i (engine/asset/{shader_cache,vfx_authoring,streamer_pool,validator}/ +
  ADR-20260616-band4-asset-scope.md) + samples/ + hello_* + % docs + diğer gruplara
  DOKUNULMADI. hello_engine render yolu etkilenmez (golden byte-identical doğrulandı).

---

## 1. Bağlam

BAND 4 (60–69%) üç "singleton" kütüphaneyi (her grupta tek başına kalan band-4
üyesi: foundation→platform, world→particle_system, tooling→imgui_backend) 100%'e
taşır. Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk
iki terminal durumdan birinde olduğunda: (a) IMPLEMENTED + tested YA DA (b) bir
paragraflık ADR scope-decision ile formally SEALED. Hiçbir üçüncü "TODO/placeholder"
durumu kalamaz.

İnceleme, baseline banner'larının iki noktada gerçeği YANLIŞ temsil ettiğini
ortaya çıkardı ve bu ADR onları düzeltir:

1. **platform "web/android/iOS are kNotImplemented stubs" YANLIŞ.** Disk üzerindeki
   gerçek kaynaklar gösteriyor ki her hedef gerçek, platforma-özgü bir port:
   `Win32Window.cpp` (489) + `X11Window.cpp` (Linux/Xlib, opt-in `CD_PLATFORM_XLIB`)
   masaüstü; `CocoaWindow.mm` (macOS), `src/platform/ios/*.mm` (iOS UIKit +
   CAMetalLayer, `if(IOS)`), `src/platform/android/*` + `AndroidWindow.cpp`
   (NativeActivity, `if(ANDROID)`), `src/platform/web/WebWindow.cpp` (Emscripten,
   `if(EMSCRIPTEN)`). Hepsi kendi SDK macro'su altında derlenir; masaüstü TU'ları
   onları boş bırakır. `PlatformStub.cpp` yalnız HİÇBİR backend seçilmediğinde
   `kNotImplemented` döndürür — bu bir "stub backend" değil, derdest-olmayan-platform
   fallback'idir. Yani gerçek boşluk "stub" değil, "non-desktop port'lar v1 CI'da
   doğrulanmıyor"dur.

2. **imgui_backend "NO tests" YANLIŞ olacak** (bu pass'ten sonra) — gerçek boşluktu,
   bu pass kapatıyor.

---

## 2. Karar

### 2.1 cd::platform — desktop-first-v1 IMPLEMENTED + topup; non-desktop SEALED

**IMPLEMENTED (bu pass):**
- **Banner düzeltmesi**: `README.md` "Win32 today; X11/Wayland queued" satırı
  gerçeği yansıtacak şekilde düzeltildi — Win32 + X11 masaüstü tam ve desteklenen
  v1 yolu; Cocoa/iOS/Android/Web gerçek port'lar, kendi SDK'sı altında promote-on-need.
- **Gerçek-untested-desktop-branch topup**: `X11Window.cpp` + `CocoaWindow.mm`
  içinde `KeyCode::kArrowLeft/Right/Up/Down` adları enum'da YOKTU (enum
  `kLeft/kRight/kUp/kDown` tanımlar) — bu iki masaüstü backend Linux/macOS'ta
  DERLENMEZDİ. Adlar doğru enum üyelerine düzeltildi: artık masaüstü branch'ler
  gerçekten build-edilebilir. (Marathon host Windows-only olduğu için bu hata CI'da
  görünmezdi — tam da "topup a real untested desktop branch" hedefi.)
- **Header-logic testleri** (`tests/test_platform_window_iface.cpp`, 5 case): OS-gated
  Win32 testlerinin izole edemediği header-only `pump_all_windows` aggregate
  branch'leri (skip-null, skip-closed-without-pump, all-closed terminal, empty-span)
  + default `IWindow::set_parent` kontratı + `platform_errors::make` domain/code
  round-trip — saf bir fake `IWindow` ile her host'ta çalışır.

**SEALED (promote-on-need, kesin tetikleyiciyle):**
- **macOS Cocoa backend** (`CocoaWindow.mm`, `CD_PLATFORM_COCOA`): promote tetiği =
  ilk macOS-masaüstü dağıtım hedefi (Metal swapchain + NSWindow event-loop CI
  donanımı). Kod hazır; doğrulama Apple donanımı gerektirir.
- **iOS UIKit backend** (`src/platform/ios/*.mm`, `if(IOS)`): promote tetiği = ilk
  iOS cihaz/simülatör CI + `CMakeLists.txt §if(IOS)`'un referans ettiği
  `IosWindow.mm/IosLifecycle.mm/main_ios.mm` dosyalarının `src/platform/ios/`
  altına eklenmesi (şu an yalnız birleşik `src/IosWindow.mm` mevcut; iOS build path
  bu dosyalar yazılana kadar konfigüre edilemez — Apple-only, masaüstü/Win-CI'yı
  etkilemez).
- **Android NativeActivity backend** (`src/platform/android/*` + `AndroidWindow.cpp`,
  `if(ANDROID)`): promote tetiği = ilk Android NDK build hedefi + `ANativeWindow`
  teslim eden bir NativeActivity host (`create_window` ANW attach'ten önce
  `kNotImplemented`-benzeri create-failed döndürür, by-design).
- **Web/Emscripten backend** (`src/platform/web/WebWindow.cpp`, `if(EMSCRIPTEN)`):
  promote tetiği = ilk WebGPU/Dawn web-demo build + tarayıcı-içi smoke.
- **Wayland**: hâlâ backlog; promote tetiği = X11'in yetmediği bir Linux
  oturum-yöneticisi gereksinimi.

### 2.2 cd::particle_system — CPU-sim-v1 SEALED + topup

**IMPLEMENTED (bu pass):**
- **Gerçek-untested-branch topup** (`tests/test_particle_system.cpp`, +6 case): mevcut
  CPU-sim kodunun test edilmemiş gerçek branch'leri — `dt<=0` no-op guard
  (zero+negative), spawn-accumulator fractional carry-forward (sub-1 ve >1 spawn/tick),
  large-dt çoklu-spawn `while` loop, dead-particle swap-pop compaction'ın
  "do-NOT-increment-i" branch'i (survivor doğrulamasıyla), velocity-spread bounds,
  remove_emitter(unknown/kInvalidEmitter) no-op. Header/src davranışı DEĞİŞMEDİ —
  yalnız test eklendi.

**SEALED (promote-on-need):**
- **GPU dispatch** (compute-shader particle sim): promote tetiği = profil verisinin
  CPU SoA sim'in bir frame-budget bottleneck'i olduğunu gösterdiği bir sahne (10⁵+
  canlı parçacık). Bağımlılık: cd::gpu_particles emit-kernel + indirect-draw
  (BAND 6) gerçek hale geldiğinde oraya bağlanır.
- **Force fields** (gravity/drag/vortex/attractor): promote tetiği = ilk
  authored-VFX gereksinimi; semi-implicit Euler integrasyon yolu (v-first/x-second)
  zaten yapısal olarak doğru, ivme şu an sıfır — alan ekleme noktasal.
- **Max-particle cap**: şu an sistem sınırsız büyür (havuz cap yok). Cap bir
  davranış/API değişikliğidir (drop-policy: reject-newest vs recycle-oldest seçimi
  gerektirir) → IMPL etmek yerine SEALED. Promote tetiği = bellek-bütçeli bir konsol
  hedefi veya bir DoS-koruması gereksinimi; o noktada `EmitterSpec`'e `max_particles`
  + drop-policy enum eklenir.

### 2.3 cd::imgui_backend — Vulkan-backend-v1 SEALED + 'no tests' state DÜZELTİLDİ

**IMPLEMENTED (bu pass):**
- **'NO tests' → tests** (`tests/test_imgui_backend.cpp`, 9 case): canlı Vulkan
  cihazı OLMADAN test edilebilen tüm yüzey:
  - `detail::color_from_name_hash` — saf FNV-1a → ImU32 (determinism, distinctness,
    sabit-alpha + per-channel floor, boş-isim kararlılığı).
  - `profiler_flamegraph` — data/branch logic, HEADLESS bir ImGui context üzerinden
    (ImGui core CPU-only; yalnız `ImGui_ImplVulkan_*` GPU ister, o hiç çağrılmaz):
    empty-samples guard, degenerate-range guard, çok-thread tam-draw yolu.
  - `Context::create` — null-window/device validation branch'i (`kInvalidArgument`,
    herhangi bir Vulkan çağrısından önce döner).
- **Pre-existing header defect fix**: `ProfilerView.hpp:112` `row * row_height`
  (int*float) test'in strict -Werror seti altında derlenince `-Wimplicit-int-float-conversion`
  patladı — sıfır tüketici/test olduğu için gizli kalmıştı; explicit
  `static_cast<float>(row)` ile düzeltildi (size-math explicit-widening kuralı).

**SEALED (promote-on-need, device-gated — rhi Metal deseniyle aynı):**
- **GPU-render doğrulaması** (`ImGui_ImplVulkan_RenderDrawData` çıktısının piksel
  doğruluğu): device-gated, tıpkı rhi Metal-on-Mac GPU doğrulaması gibi. Vulkan
  ImGui draw yolu zaten renderer + rhi pixel-parity capstone tarafından end-to-end
  egzersiz ediliyor. Promote tetiği = imgui_backend'e adanmış, canlı-cihaz golden
  (ImGui penceresinin offscreen render + FLIP/SSIM diff'i) — RHI golden test
  altyapısı buna hazır olduğunda.
- **D3D12 ImGui backend** (`ImGuiD3D12.hpp` ince/doğrulanmamış + `imgui_impl_dx12.cpp`
  WIN32'de derlenir): promote tetiği = editor/tool'ların D3D12-backed bir IDevice
  üzerinde ImGui çizmesi gereken ilk senaryo. Bağımlılık zaten mevcut (rhi D3D12
  100%); köprü `Context::create`'in `kBackendMismatch` branch'ini D3D12 native
  handle alacak şekilde genişletmeyi gerektirir — bir sprint'lik iş, bugün talep yok.

---

## 3. Reddedilen Alternatifler

- **platform non-desktop backend'leri marathon'da doğrula**: REDDEDİLDİ — macOS/iOS
  Apple donanımı, Android NDK+cihaz, Web tarayıcı+Dawn gerektirir; hiçbiri Win-only
  marathon host'unda yok. Kod hazır + per-platform SDK-gated; doğrulama gerçek
  hedef-CI'ya promote edilir (rhi Metal ile aynı dürüst gating).
- **particle_system'e GPU sim + force fields ekle**: REDDEDİLDİ — bugün talep eden
  bir sahne yok; CPU SoA sim stated scope'u için doğru ve test-edilmiş. Promote
  tetiği net (profil-kanıtlı bottleneck / authored-VFX).
- **particle_system'e max-particle cap ekle**: REDDEDİLDİ bu pass'te — drop-policy
  kararı (reject vs recycle) bir API tasarım seçimidir; "small/clean" değil → SEAL.
- **imgui_backend için yalnız device-gated bir GTEST_SKIP testi yaz**: REDDEDİLDİ —
  host-testable gerçek yüzey VARDI (hash encoder + flamegraph data logic + create
  validation); önce o gerçek coverage eklendi, GPU-render kısmı device-gated mühürlendi.

---

## 4. Sonuçlar

**Olumlu:**
- Üç singleton da honest-rule terminal durumunda: platform = desktop-first-v1 IMPL
  (banner düzeltildi + 2 derlenemeyen masaüstü backend onarıldı + 5 header-logic
  testi) + non-desktop SEALED; particle_system = CPU-sim-v1 SEALED + 6 gerçek-branch
  testi; imgui_backend = 'no tests' DÜZELTİLDİ (9 host-test) + 1 pre-existing header
  defect onarıldı + GPU-render/D3D12 device-gated SEALED.
- İki yanlış banner ("web/android/iOS kNotImplemented stubs", "imgui NO tests")
  düzeltildi → sonraki planlayıcı yanlış-baseline yapmaz.
- Public API + ABI sabit; hello_engine render yolu etkilenmez (golden byte-identical).

**Olumsuz / borç:**
- Non-desktop platform port'ları + Vulkan/D3D12 ImGui GPU-render hâlâ canlı-CI'da
  doğrulanmadı (mühürlü, tetiği net). iOS build path'i referans ettiği
  `src/platform/ios/*.mm` dosyaları henüz disk'te yok — yalnız `if(IOS)` (Apple-only)
  konfigürasyonunu etkiler, masaüstü/Win-CI green.
- particle_system havuz cap'siz (sealed); bellek-bütçeli hedefte promote gerekir.

**Çıkış kriteri (bu pass için karşılandı):** build clean (`ninja-debug`, -Werror,
0 yeni clang-tidy WAE-class) + her libin gtest'i green (cd_test_platform +
cd_test_platform_window_iface + cd_test_platform_multi_window + cd_test_particle_system
+ cd_test_imgui_backend) + golden byte-identical + tag/push/commit YOK.
