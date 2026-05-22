# CHROMODYNAMIC Engine — Proje Kuralları (Global Standards)

> Bu dosya **otomatik olarak her oturumda** ve **tüm subagent'larda** yüklenir.

## 0. Proje Kimliği

**CHROMODYNAMIC** — cross-platform, cross-API, library-oriented hibrid 2D+3D oyun motoru ve general-purpose rendering/framework. Hedef: state-of-the-art (Unreal RHI, Filament, bgfx, EnTT/Bevy) **aşılacak** mimari kalite. Tüm alt-sistemler bağımsız kütüphane olarak da tüketilebilir olacak.

**Phase 1 = sadece tasarım ve plan** — implementasyon yok. Her büyük karar **araştırma + ADR** ile gerekçelendirilir.

## 1. Modern C++ Kalitesi (Zero Tolerance)

- **Bellek**: Sahiplik için yalnızca `std::unique_ptr` / `std::shared_ptr`. Raw pointer sadece non-owning observer. Hot-path için handle / opaque ID tercih.
- **Const correctness**: State değiştirmeyen üye fonksiyonlar `const`; değişmeyen lokaller `const`.
- **API**: Public dönüşlerde `[[nodiscard]]`. Sanal metodlarda `override` zorunlu. Public header'da `noexcept` doğru kullanım.
- **Modern idiom**: C++23. `static_cast` kullan, C-style cast yasak. Custom makro minimum.
- **Hata yönetimi**: `std::expected` / `std::variant` ile tip sisteminde taşı. `catch(...) {}` yasak. Exception sadece ctor/dtor sınırında ve dökümante edilmiş.
- **Sınır kontrolü**: Güvenlik gereken yerlerde `at()`. Buffer overflow / UB / leak'e karşı savun.
- **Library boundary**: Her library `chroma::<lib>::` namespace; explicit `CHROMA_<LIB>_API` macro export; PIMPL opsiyonel ama önerilen.

## 2. Araştırma-Önce Disiplini (Phase 1 zorunlu)

- **Her** alt-sistem tasarım kararı öncesi: SOTA analizi (Unreal, Filament, bgfx, Sokol, The Forge, Diligent, WebGPU/Dawn, EnTT, Bevy, Flecs, Jolt, vb.) → ADR.
- ADR yolu: `docs/ADR/ADR-YYYYMMDD-konu.md` — Iglberger formatı (Bağlam / Karar / Reddedilen alternatifler / Sonuçlar).
- **Akademik atıf** gerektiğinde Demir Kural geçerlidir: `research/library/MANIFEST.csv` doğrulanmış PDF + `bibliography.bib` BibTeX olmadan atıf yapılamaz. (`academic-researcher` ajanı bunu zorlar.)
- Engineering SOTA (blog, GitHub, cppreference, vendor docs) `researcher` ajanı tarafından toplanır; rapor + link + erişim tarihi formatında.

## 3. Doğruluk (Evidence-Based)

- "Kodu yazdım, doğru olmalı" yasak. Her iddianın **kanıtı** olmalı:
  - Build temiz: `cmake --build --preset ninja-debug`
  - Test geçer: `ctest --preset ninja-debug --output-on-failure`
  - Sembol var: `Grep` ile doğrula
- Build/test fail ederse **root cause**'u bul, üstünü örtme. `--no-verify` veya bypass yasak.

## 4. Cerrahi Düzenleme

- Dosyaları `cat`/`echo`/`Write` ile baştan yazma — `Edit` kullan.
- Sadece ilgili satırları hedefle. API imzası değişiyorsa `Grep` ile etkilenenleri bul ve düzelt.

## 5. Test Disiplini

- Anti-flakiness: `sleep_for` yasak; `condition_variable` / event tabanlı senkron kullan.
- Pattern: `Arrange / Act / Assert`. İzole testler.
- Edge case + negative testing zorunlu.
- Renderer için golden image diff (FLIP/SSIM); ECS/job system için stress + fuzz.

## 6. Build / CMake

- `-Wall -Werror -Wextra -Wshadow -Wnon-virtual-dtor -Wpedantic -Wconversion` standart.
- `PUBLIC` / `PRIVATE` / `INTERFACE` doğru kullan. Layered DAG; cycle yasak.
- ASAN / UBSAN debug preset'lerinde aktif. TSan ayrı preset.
- Compiler matrisi (kararlaştırılınca): MSVC + Clang-cl + Clang + GCC.
- Paket: vcpkg manifest mode (DtForHil pattern); kritik bağımlılıklar opsiyonel submodule.

## 7. Modülarite / Library-Oriented

- Her alt-sistem ayrı kütüphane (`engine/<lib>/`). Public include `include/chroma/<lib>/`.
- Cross-library dependency DAG'ı: foundation → math/memory → concurrency/io → rhi/asset → ecs/scene → engine → editor.
- Library'ler arası global state yasak — explicit context/registry objesi geçilir.
- Library standalone derlenebilir + test edilebilir (kendi gtest binary'si).

## 8. Paralel Çalışma & Subagent Koordinasyonu

- Bağımsız görevler **paralel** subagent çağrılarıyla (tek mesajda birden çok `Agent` çağrısı).
- Orkestratör: `team-lead`. Phase 1 (tasarım) ağırlığı: `researcher`, `academic-researcher`, `architect`, `analyst`, `planner`, `doc-writer`.
- Kalıcı mimari kararlar `docs/ADR/` altında; geçici notlar `research/notes/`.

## 9. Komut Notları (Windows / Bash)

- Shell **bash** (Git for Windows) — `/dev/null` kullan, `NUL` değil; forward slash path.
- Build (kurulduğunda): `cmake --build --preset ninja-debug` veya `ninja-release`.
- Test: `ctest --preset ninja-debug --output-on-failure`.

## 10. Klasör Konvansiyonu

```
.claude/agents/       # Subagent tanımları (33 ajan)
.agent/workflows/     # Legacy workflow notları
docs/ADR/             # Architecture Decision Records
research/
  library/            # İndirilmiş paper'lar + MANIFEST.csv + bibliography.bib
  papers/             # Mimari/grafik/perf KİTAPLARI (gitignored, manuel)
  notes/              # Per-paper özet notları
  reports/            # Audit raporları (citation, methodology, peer-review)
  templates/          # Beamer/slide template'leri
CMakeModules/         # CMake helper'lar (DtForHil'den taşındı + uyarlanmış)
Engine/               # (eski) — yeniden yapılandırılacak
Dependencies/         # vendored / submodule deps
Project/              # örnek uygulama
Tests/                # global test runner (modül başına da test var)
```

## 11. Domain Notu (subagent'lar için)

Bu proje **akademik makale + game engine** hibridi değil; **game engine inşaatı** odaklı. Ancak:
- Mimari kararlar akademik kanıt aramayı hak ediyor (rendering, ECS, concurrency, allocator alanlarında peer-reviewed çalışma var) — Demir Kural geçerli.
- `latex-writer` / `peer-review-simulator` / `slide-builder` ajanları **opsiyonel**: kullanıcı tez/sunum/RFC isterse aktif.
- `safety-integration` mimar görevi: engine'in concurrency yoğun olduğu (job system, render thread, async asset) için **kritik**.
