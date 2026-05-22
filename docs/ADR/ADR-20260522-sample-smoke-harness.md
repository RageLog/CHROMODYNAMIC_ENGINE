# ADR-20260522 — Sample smoke harness (headless watchdog model)

## Bağlam

CHROMODYNAMIC'in 18 sample'ı var. Bunlardan 12'si GPU-aware (Vulkan
swapchain açar). Unit testler (47 ctest binary) sadece library API'sini
test ediyor; "sample gerçekten boot olabiliyor mu, GPU context'ini ayağa
kaldırıyor mu, 3 frame işleyip temiz çıkıyor mu?" sorusunu cevaplamıyordu.

Bir reviewer asistanı için durum şuydu:
1. `ctest` yeşil = library doğru
2. Sample'lar mı çalışıyor? Manuel olarak `bin/Debug/hello_*.exe`
   tek tek çalıştır, X tuşuna bas, kapat.
3. Bir regression sample'lardan birini kıracak olsa, fark etmesi günler
   sürerdi (sample'lar nightly build matrisinde değil çünkü Lavapipe
   yok).

İhtiyaç: tek komut, tüm sample'ları headless boot et, exit kodlarını topla,
watchdog ile takılan instance'ı öldür.

## Karar

Üç-katmanlı sample smoke harness:

### Katman 1 — Sample tarafı: `cd::sample::Runtime`

(Wave 1'de tanımlandı, bu ADR sadece dokümanter.)

`samples/common/SampleRuntime.hpp`'nin sözleşmesi:
* `--headless N` → N frame sonra `should_continue()` false döner
* `--no-spin` → animasyon dondur (golden-image testleri için)
* Bilinmeyen argv silently tolere et — sample kendi argv'sini parse edebilir

Sample loop pattern:
```cpp
const auto runtime = cd::sample::parse_runtime(argc, argv);
std::uint32_t frame_idx = 0;
while (true) {
    if (!runtime.should_continue(frame_idx))
        window.request_close();
    // ... pump events, render ...
    if (window.should_close()) break;
    ++frame_idx;
}
```

### Katman 2 — Driver: `scripts/run_all_samples.{ps1,sh}`

**PowerShell 5.1 uyumlu** (Windows GHA runner'da pwsh 7 yok; system
PowerShell 5.1 her zaman var):
* `Start-Process -PassThru -RedirectStandardOutput` PS 5.1'de
  `ExitCode` döndürmüyor → **direkt .NET `[System.Diagnostics.Process]::Start`**
* `Process.StandardOutput.ReadToEndAsync()` async drain — 4KB pipe
  buffer deadlock'una karşı koruma
* `Process.WaitForExit(ms)` bounded wait + `Process.Kill()` deadline aşımında
* Per-sample log dosyası (stdout + stderr ayrı)
* Per-sample timeout override map (ör. `hello_textured_cooked` 15 s
  ihtiyacı, ilk çalıştırmada PNG → BC7 → .cdtex cook'u disk'e yazıyor)

**bash** (Linux/macOS CI):
* `timeout --preserve-status -k 2s` → SIGTERM at deadline + SIGKILL 2 s
  sonra
* Exit kodu 124 (SIGTERM) ve 137 (SIGKILL) timeout sınıfı olarak
  ayrıştırılır

### Katman 3 — CMake driver: `cd::CDSmoke`

`cmake/CDSmoke.cmake`:
* `WIN32` ise PowerShell host (pwsh > powershell)
* Aksi halde bash
* `cmake --build . --target smoke` → script'i bu build'in
  `bin/<CONFIG>`'ine yönlendir
* Script exit kodu propagate eder (`USES_TERMINAL` + custom target zaten
  exit-code-propagating)

## Reddedilen alternatifler

### Sample-loop tarafı

* **Environment variable (`CD_HEADLESS_FRAMES=3`):** Argv argümanından
  daha gizli; debug session'ında "exe çalışıyor ama 3 frame'de çıkıyor"
  hayreti. Argv explicit + grep'lenebilir.
* **Compile-time `CD_HEADLESS_BUILD`:** Smoke için ayrı build varyantı
  gerekirdi — matris ikiye katlanır. Runtime flag tek build'i kapsar.
* **`SIGINT` sender:** Loop iç durumda ya `should_close()` ya da `select(2)`
  bekliyor; signal handling Windows'ta zaten ekstradır. Argv flag basit.

### Driver tarafı

* **`ctest add_test()` ile her sample test olarak:** Yukarıdaki ADR
  (wave3-tooling-closure) açıkladı; her ctest run'ına 14 s eklemek
  pahalı; smoke semantikası ≠ unit-test semantikası.
* **`post-build custom_command`:** Geliştirme sırasında her recompile'da
  bütün sample'ları koşar — gürültü; reviewer modu için açık-kapalı bir
  ON/OFF gerekiyor.
* **Python orchestrator:** Cross-platform OK ama dep ekler; PS + bash
  zaten her iki target OS'ta default.
* **`xargs -P N` paralel:** Vulkan instance / GPU memory contention
  garantisi yok. Sıralı çalıştırma deterministik, 15 s total kabul
  edilebilir.

### Timeout / watchdog

* **Sample içi self-kill `std::exit(0)` after N frames:** Çalışırdı
  ama sample'ın "neden çıktım" semantikası bulanır (kullanıcı capable
  olmadığı için mi çıktı?). `request_close()` → normal close path
  daha temiz, RAII destructor'lar düzgün koşar.
* **Pure WaitForExit() infinite:** Hung sample CI'yi durdurur. Watchdog
  zorunlu.

## Sonuçlar

* **Yeni dosyalar**: 3 (CDSmoke.cmake, run_all_samples.ps1, run_all_samples.sh)
* **Yeni CMake target**: `smoke`
* **Reviewer flow**: `cmake --build --target smoke` → 18/18 pass / 14.83 s
* **Regression catch örneği**: hello_imgui'nin önceki segfault'unu
  ben fix ederken, smoke harness olsaydı CI'da otomatik yakalanırdı.

| Sample sınıfı | Adet | Smoke timing |
|---|---|---|
| Pure CPU (hello_core/foundation/handle/runtime/ecs/scheduler) | 6 | 20–60 ms |
| GPU + 3 frame (hello_triangle/mesh/texture/cube/obj/scene_graph/framegraph/cooked/textured_cooked/hot_reload/gltf/imgui) | 12 | 700–1400 ms |

Toplam: 18/18, 14.83 s wall (sequential).

## Açık sorular

* **Lavapipe CI:** Linux runner'da Mesa Lavapipe + `VK_DRIVER_FILES`
  ICD config ile headless Vulkan; bu olunca smoke job'u tüm CI matrisine
  ekle.
* **Golden image diff:** v2 — Renderer'a "framebuffer dump as PNG"
  primitive ekle, smoke harness'a `--dump path.png` flag, FLIP/SSIM ile
  diff. (Şu an headless 3-frame test sadece "boot edebiliyor mu"
  cevabını veriyor.)
* **Smoke target'ın dependency'si:** Şu an `cmake --build --target smoke`
  sample'ları otomatik rebuild etmiyor. `add_dependencies(smoke ${all_hello_targets})`
  eklenmeli — devs build'i unutursa stale binary'yi test eder.
