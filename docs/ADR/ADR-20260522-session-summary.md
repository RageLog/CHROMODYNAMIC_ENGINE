# ADR-20260522 — Session summary (autonomous overnight run)

## Bağlam

Kullanıcı bu oturumda CHROMODYNAMIC engine'in birden çok alt-sistemini
genişletmek istedi. Talimat: "kolaydan zora, temelden kapsamlıya, gece
boyu duraksamadan ilerle". 02:00-18:00 aralığında çalış.

Bu meta-ADR oturumda yapılan tüm değişiklikleri tek noktada belgeler
ve detaylı ADR'lere işaret eder.

## Karar — Tamamlanan iş

### Yeni kütüphaneler (10)

| Lib | Sorumluluk | Test sayısı | Detay ADR |
|---|---|---|---|
| `cd::asset_image` | PNG/JPG/TGA/BMP/HDR via stb_image | 6 | ADR-asset-loader-tier |
| `cd::asset_obj` | Wavefront .obj (hand-rolled) | 9 | ADR-asset-loader-tier |
| `cd::asset_cdmesh` | Cooked binary mesh format | 7 | ADR-asset-loader-tier |
| `cd::asset_gltf` v2 | glTF + node hierarchy + instances | 8 (existing) | ADR-asset-loader-tier |
| `cd::camera` | Camera + OrbitController + Frustum cull | 8 | ADR-profile-camera-headless |
| `cd::profile` | Scope timer + sinks + StatsAggregator | 7 | ADR-profile-camera-headless |
| `cd::shader::CachedCompiler` | SPIR-V on-disk cache | 5 | ADR-shader-pipeline-caches |
| `cd::shader::FileWatcher` | Polling file-watcher (hot-reload base) | 5 | ADR-shader-pipeline-caches |
| `cd::sample_common` | Sample runtime (headless / no-spin) | — | ADR-profile-camera-headless |
| `CDCoverage` (CMake) | gcov/llvm-cov instrumentation | — | ADR-static-analysis-coverage |

### Yeni samples (6) ve tools (1)

* `hello_ecs` — 100k entity particle sim (6.64 M updates/s Debug)
* `hello_scene_graph` — sun/planet/moon nested transforms
* `hello_framegraph` — MVP single-pass framegraph demo
* `hello_gltf` — textured PBR (Cook-Torrance) + instanced multi-mesh
* `hello_obj` — Wavefront viewer
* `cd_cook_mesh` (tool) — offline .obj/.gltf → .cdmesh cooker

### CI hardening

* `vcpkg` placeholder SHA bug giderildi; `CD_DISABLE_VCPKG=ON` her job'a
  enjekte edildi (FetchContent zaten her deps'i çözüyor).
* `clang-tidy` PR-gating job eklendi (ADR-static-analysis-coverage).
* `coverage` job eklendi (gcov + gcovr + HTML/Cobertura upload).
* `.clang-tidy` + `.clang-format` zaten vardı; tidy CI'a wire edildi.

### Engine fixes (production)

* `cd::rhi_vulkan::VulkanDevice` artık `VkPipelineCache` persist ediyor
  (`.shader_cache/pipeline_cache.bin`, 14 KB).
* MSVC `/EHsc` flag eklendi (CDWarnings) — Ninja-driven cl.exe path için
  zorunlu.
* `cmake_minimum_required` proje-içi 4 dosyada birden 3.28'e
  unification + `CMAKE_POLICY_VERSION_MINIMUM 3.15` floor — CMake 4.x
  compat. (ADR-cmake-version-strategy)
* `.clangd.in` template + `Clangd.cmake` per-preset driver/std-flag/build-
  dir generation. (ADR-clangd-auto-generation)
* `Vulkan-Module` (vulkan_headers v2 C++23 module target) `EXCLUDE_FROM_ALL`
  — clang-cl Ninja can't scan modules.

### Bug fixes (user-reported)

* hello_scene_graph: cube vertex 0 had color (0,0,0) → black-corner
  interpolation artefact. Tüm vertex'leri (1,1,1)'e set, tint
  push-constant'tan geliyor.
* hello_gltf: fallback scene `instances` listesi boştu → siyah ekran.
  `make_fallback_scene()` artık identity-matrix instance ekliyor.

### Tri-compiler bug-fixes (4 compilers green)

* MinGW GCC: `std::atoi` MinGW libc'de yalnızca `::` namespace'inde;
  SampleRuntime.hpp `<cstdlib>` ekleyip `::atoi` kullanıyor.
* MinGW GCC: `cd::camera::Frustum.hpp` unscoped enum + underlying-type
  GCC parser'ı tarafından elaborated-type-specifier olarak yorumlandı;
  `enum class` + integer index pattern'e geçildi (+`<cstdint>` include).
* MinGW GCC: `-Wuseless-cast` ObjLoader hash mixer'ında; implicit
  conversion'a güveniyoruz (64-bit only target).
* MinGW GCC: `-Wunused-but-set-variable` ECS Query::each'te fold
  expression detection bug; `[[maybe_unused]]` eklendi.

### Documentation

* 9 yeni ADR (bu meta-ADR dahil):
  * ADR-20260522-asset-loader-tier
  * ADR-20260522-clangd-auto-generation
  * ADR-20260522-cmake-version-strategy
  * ADR-20260522-framegraph-design (post-hoc validation)
  * ADR-20260522-ecs-storage-validation (post-hoc validation)
  * ADR-20260522-shader-pipeline-caches
  * ADR-20260522-profile-camera-headless
  * ADR-20260522-static-analysis-coverage
  * ADR-20260522-cook-tool-pipeline
  * ADR-20260522-session-summary (bu dosya)
* 2 SOTA research raporu (researcher subagent ile paralel oluşturuldu):
  * `research/notes/framegraph_sota.md` — Frostbite/Granite/UE RDG/AMD
    RPS/bgfx/The Forge/Sokol karşılaştırması, cd::framegraph design
    önerileri
  * `research/notes/ecs_sota.md` — EnTT/Flecs/Bevy/Unity DOTS/Unreal
    Mass karşılaştırması, cd::ecs design validation

## Sonuçlar

* Engine boyutu: 32 → 42 library CMakeLists, 8 → 14 sample, +1 tool.
* Test sayısı: 36 → 44 ctest entry (gerçek bireysel test sayısı ~85).
* CI matrix: 4 Windows + 2 Linux + 1 macOS + 2 sanitizer + 1 coverage
  + 1 tidy = **11 paralel job**.
* `.shader_cache/` (gitignored) artık SPIR-V + VkPipelineCache persistasyonu
  yapıyor — cold-start sonrası launch süresi belirgin azalma.
* Headless mode tüm 5 GPU sample için CI smoke-test imkanı.

## Reddedilen / deferred

* **W10.2 cd::editor_ui (ImGui Vulkan):** Bir sonraki oturum. Gerekli:
  cd::platform Win32Window'a WM_ raw event forwarding eklemek (ImGui
  `imgui_impl_win32` o şekilde çalışıyor) + ImGui FetchContent + Vulkan
  backend wiring + render pass entegrasyonu.
* **W14.3 KTX2 / BC7 runtime reader:** libktx 30+ MB dep; daha hafif
  bc7enc + custom KTX2 minimal parser tercih edilebilir.
* **W12.2 Render thread:** safety-integration audit önce gerekli.
  cd::concurrency mevcut ama render-thread spesifik design ADR yok.
* **W8.3 BC7 offline compression:** cooker tool'a bc7enc integration
  eklenecek. Şu an cooker yalnızca geometri.

## Açık sorular

* Yarın session'da öncelik sırası: ImGui editor mi, KTX2 mi, render thread
  mi? Kullanıcı geri bildirimine bağlı.
