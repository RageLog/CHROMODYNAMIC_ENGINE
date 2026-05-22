# ADR-20260522 — Wave 3: tooling closure (Doxygen + smoke + KTX2)

## Bağlam

Wave 1 (overnight, ~06:00–11:00): foundation libraries + samples + frustum +
ImGui (ADR-20260522-overnight-final).

Wave 2 (~11:00–11:30): KTX2 minimal reader added (engine/asset_ktx2,
8 tests, ADR-20260522-ktx2-minimal-reader). Closed the asset import gap:
engine-internal `.cdtex` for cook pipeline + industry-standard KTX2 for
external content. cdtex bumped to 45 libs / 47 ctest binaries.

Wave 3 (~11:30–12:00) bu ADR'nin konusu: developer-experience tooling
katmanını tamamladı. Engine artık derliyor, çalışıyor, 47 testi geçiyor
— ama bir reviewer için "her şey çalışıyor mu?" sorusu hâlâ manuel.
Wave 3 bu soruyu tek komuta indiriyor.

## Karar

Üç ortogonal tooling iyileştirmesi:

### 1. Doxygen API dokümantasyonu

* `docs/Doxyfile.in` — CMake-template Doxyfile (PROJECT_NAME / VERSION /
  INPUT / OUTPUT yolu @VAR@ formatında expand edilir).
* `cmake/CDDoxygen.cmake` — `cd_setup_doxygen()` fonksiyonu:
  - `CD_ENABLE_DOXYGEN=OFF` default, opt-in flag
  - `find_package(Doxygen QUIET)` graceful degrade
  - Per-path quoted INPUT (boşluk içeren SOURCE_DIR'i tolere eder)
  - Custom aliases: `\cdlibrary{name}`, `\cdsubsystem{name}`, `\cdrouting`
  - Preprocessor PREDEFINED: `[[nodiscard]]=`, `noexcept=`, `CD_*_API=`
  - Excludes: tests, _deps, _legacy, research/library, Dependencies
* `cd_docs` custom target + `docs` alias.
* `.github/workflows/docs.yml` — push'ta build + master'a deploy to
  GitHub Pages, PR'da artifact-only upload (review için).

**Yerel ölçüm:** Doxygen 1.16.1, 19 MB HTML, 1422 dosya, 5 minor warning
(markdown link resolution, blocking değil).

### 2. Sample smoke harness

* `scripts/run_all_samples.ps1` — PowerShell 5.1 uyumlu, .NET
  ProcessStartInfo doğrudan kullanır (Start-Process'in `-PassThru`
  ExitCode bug'ı atlandı), async stdout/stderr drain (4KB pipe buffer
  deadlock'una karşı), per-sample log dosyası, configurable timeout,
  watchdog kill.
* `scripts/run_all_samples.sh` — bash + `timeout(1)` ile aynı pattern;
  Linux/macOS CI için.
* `cmake/CDSmoke.cmake` — `cd_setup_smoke()` fonksiyonu; `WIN32` ⟹
  PowerShell host, aksi halde bash. `cmake --build . --target smoke`
  ile tüm `hello_*` exe'leri `--headless 3` ile çalıştırır.

**Yerel ölçüm:** 18/18 sample geçti, 14.83 s wall time. hello_imgui
de geçti — viewports/CreateVkSurface fix doğrulanmış oldu.

Sample sınıflandırma (smoke timing):
| Tip | Adet | Tipik süre |
|---|---|---|
| Pure CPU (core, foundation, handle, runtime, ecs, scheduler) | 6 | 20–60 ms |
| GPU + 3-frame headless | 12 | 0.7–1.4 s |

### 3. KTX2 reader (Wave 2 ama bu wave'in 47-test çıktısının bir bileşeni)

ADR-20260522-ktx2-minimal-reader — ayrı dosyada.

## Reddedilen alternatifler

### Smoke harness

* **`ctest` add_test():** Çekici çünkü zaten CTest infrastructure var.
  Ama:
  - Sample target'ları `cd_add_sample` üzerinden ekleniyor; test
    olarak ayrıca kaydetmek per-target hook gerektirir
  - Smoke "test" 14 saniye sürüyor; her ctest invocation'a 14 s eklemek
    pahalı (devs `ctest --preset ninja-debug`'ı sık çalıştırıyor)
  - Headless mode'un başarısızlığı != unit-test başarısızlığı semantikası
* **Pure cmake `add_custom_command(POST_BUILD)`:** Her sample build
  ettikçe çalışırdı — geliştirme sırasında gürültü
* **CI-only Python orchestrator:** Cross-platform iyi ama Python
  dependency eklemek istemiyoruz (CLAUDE.md §6: minimum dep)

### Doxygen

* **mkdocs / sphinx:** C++ public-surface tarama yetenekleri zayıf; her
  yöntem için manuel docstring sürdürmek gerekir
* **GitHub Pages markdown auto:** Sadece ADR'leri render eder, API
  reference yok
* **Doxygen Awesome theme:** İlk pass'ta gereksiz; default style yeterli
  okunabilir, theme'i v2'de ekle

## Sonuçlar

| Metric | Wave 1 sonu | Wave 2 sonu | Wave 3 sonu |
|---|---|---|---|
| Engine libraries | 42 | 45 | **45** |
| Samples | 17 | 18 | **18** |
| Tools | 2 | 2 | **2** |
| ctest binaries | 45 | 47 | **47** |
| Headless smoke-clean samples | 17 | 18 | **18 / 18** |
| ADRs (this date) | 11 | 15 | **17** |
| CI workflows | 1 (ci.yml) | 1 | **2 (+docs.yml)** |
| Custom targets | coverage-* | coverage-* | coverage-*, `cd_docs`, `docs`, `smoke` |

Reviewer flow artık tek komut:
```bash
cmake --preset ci-gcc -DCD_ENABLE_COVERAGE=ON
cmake --build --preset ci-gcc-debug
ctest --preset ci-gcc-debug                           # 47/47
cmake --build --preset ci-gcc-debug --target smoke    # 18/18 headless
cmake --build --preset ci-docs --target cd_docs       # Doxygen HTML
cmake --build --preset ci-gcc-debug --target coverage-report
```

Her satır pass/fail ile gate edilebilir; CI yaml halen bu komutları
çağırıyor.

## Açık sorular

* **Linux GPU CI:** Mesa Lavapipe ile headless Vulkan; ROI golden-image
  diff için yüksek ama bunun için Renderer'a "framebuffer dump"
  primitive'i lazım. v2.
* **Coverage threshold gating:** `coverage-summary` çıktısına min%
  parse koy; PR'da düşüş varsa fail. Şu an "rapor üret + insan baksın"
  modunda.
* **Doxygen warning-as-error:** Şu an 5 minor warning var (md link
  resolution). Cleanup yapıp `WARN_AS_ERROR = YES` çevir.
* **Sample smoke için CI job:** Windows runners Vulkan loader içermez
  (defaultu); ekleyince ICD config zorunlu. Bunu da Lavapipe pass
  ile birlikte v2'de yap.
