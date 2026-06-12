# SOTA: State-of-the-Art Shader Library Tasarımı — Araştırma Raporu

> researcher subagent çıktısı, 2026-06-12. Tüm web kaynakları erişim
> tarihi 2026-06-12. SL-A düğümü (ROADMAP_PHASE_2 §9.2).

## Yapıldı

- 7 başlık araştırıldı (Slang, Unity SRP Core, Unreal, Filament,
  bgfx/Godot/TSL/WGSL-WESL, glslang include + spirv-link + HLSL 2021,
  variant yönetimi).
- Yerel kod tabanı doğrulandı: `engine/render/shader`
  (`ICompiler::compile(source, stage, entry, defines)` — **includer
  bağlı değil**, kaynak string bazlı; `SlangCompilerStub.cpp` seam
  olarak mevcut), `docs/ADR/ADR-20260612-x4-d3d12-shader-toolchain.md`
  (X4-A: tek-kaynak GLSL → SPIR-V → SPIRV-Cross → DXC) ile uyum
  kontrol edildi.

## 1. Slang (Khronos)

| Eksen | Bulgu |
|---|---|
| Modül granülaritesi | Dil-seviyesi modüller: `module X;` + `implementing X` + `__include` (preprocessor durumu yaymaz, idempotent, döngü serbest). Görünürlük: `public`/`internal`/`private`. |
| Include/import | `import` modül API'sini açar; precompiled IR modüller (`.slang-module`) offline derlenip runtime'da linklenir → SPIR-V/DXIL. |
| Variant | Preprocessor yerine **generics + interfaces**; specialization link-time'da. Permutation patlamasına en prensipli çözüm. |
| Hedefler | SPIR-V (doğrudan), HLSL/DXIL, GLSL, MSL, WGSL, CUDA, CPU. Autodiff dahili. |
| Lisans | Apache-2.0 with LLVM Exception — yeşil. Khronos (Kas 2024'te NVIDIA katkısı); v2026.10.2 (2 Haziran 2026), 584 release. |
| Adaptasyon | Yüksek-orta: GLSL korpusu yeniden yazılır; `ICompiler` seam + `SlangCompilerStub` hazır; X4-A GLSL-master kararıyla çatışır (Phase-3 ölçekli revizyon). |

Kaynaklar: shader-slang.org • Khronos Slang Initiative duyurusu •
github.com/shader-slang/slang • Slang modules user guide.

## 2. Unity SRP Core ShaderLibrary

- ~80+ `.hlsl` modülü, **tek-konu-tek-dosya**: Common/Macros/Packing/
  Random/Hashes/BSDF/CommonLighting/EntityLighting/ImageBasedLighting/
  AreaLighting/SphericalHarmonics/Color/CommonMaterial/
  NormalSurfaceGradient/ParallaxMapping/SpaceTransforms/GeometricTools/
  Refraction/CommonShadow + `API/` (platform abstraction) + `Sampling/`
  + `Shadow/`. Hedef modül listemizle birebir örtüşen **en iyi
  granülarite şablonu**.
- Include disiplini: kütüphane header'ları `Common.hlsl`'i **include
  etmez** — tüketen shader eder. BSDF: direct lighting `/PI`'li, IBL
  PI'siz çiftler.
- Variant: `multi_compile` (hepsi derlenir) vs `shader_feature`
  (kullanılmayan strip edilir); `IPreprocessShaders.OnProcessShader`
  build-time stripping callback'i.
- Lisans: **Unity Companion License — kod kopyalanamaz; sadece yapı
  dersi.** Kırmızı.

Kaynaklar: Unity-Technologies/Graphics ShaderLibrary • BSDF.hlsl • SRP
Core built-in shader methods • SRP Core LICENSE • shader variant
stripping docs • Unity variant optimization blog.

## 3. Unreal Engine

- `.ush` header (BRDF.ush, ShadingModels.ush) / `.usf` derlenen gövde;
  `/Engine/...` sanal dosya sistemi; plugin'ler kendi yolunu mount
  eder. `#include` + `#pragma once`; `/Engine/Public/Platform.ush`
  zorunlu — platform abstraction tek noktada.
- Variant: **en güçlü typed-permutation**: C++
  `TShaderPermutationDomain<FDimA, ...>` (BOOL/INT/ENUM boyutlar),
  `ShouldCompilePermutation` statik kapısı. Permutation patlaması (MJP
  serisi) bilinen maliyet.
- ShaderConductor **arşivlendi/defunct** (Eyl 2023); güncel yol DXC
  (SPIR-V backend) + SPIRV-Cross — X4-A ADR'imizle aynı sonuç.
- Lisans: kapalı EULA — yalnız desen dersi. Kırmızı.

Kaynaklar: TShaderPermutationDomain docs • Epic KB Shader Permutations
• Hoffman UE4 Permutations • MJP The Shader Permutation Problem •
Shaders-in-Plugins docs • microsoft/ShaderConductor • DXC issue #5762.

## 4. Filament

- `shaders/src/` işlev-başına modül: surface_brdf.fs,
  surface_shading_model_{standard,cloth,subsurface}.fs,
  surface_light_{directional,punctual,indirect,reflections}.fs,
  surface_ambient_occlusion/shadowing/fog.fs +
  common_{math,defines,getters}.glsl + post_process_*. Katmanlar:
  filabridge / filamat (runtime material+shader üretimi, **bağımsız
  tüketilebilir kütüphane**) / filaflat / matc (offline compiler).
  "Shader kütüphanesi ayrı ürün" hedefinin birebir emsali.
- Variant: **küratörlü 8-bit bitmask** (DIR/DYN/SRE/SKN/DEP/FOG-PCK/
  S2D-VSM/STE), merkezi geçerlilik filtreleri + stage-aware maskeleme
  (`filterVariantVertex/Fragment`) → 256 teorik → ~96 aktif;
  `matc --variant-filter` ile app-level eleme. **Variant disiplini
  SOTA'sı.**
- CI: tüm materyaller matc ile tüm backend'lere build-time derlenir =
  her commit'te variant×backend derleme kapısı.
- Lisans: **Apache-2.0** — yeşil; kod ve desen alınabilir.

Kaynaklar: google/filament • Filament Materials docs • matc man •
Variant.h • shaders/src • discussion #5410.

## 5. bgfx / Godot 4 / Three.js TSL / WGSL-WESL

- **bgfx shaderc**: GLSL-benzeri dialect + varying.def.sc. Ders:
  **dialect icat etmek uzun vadede kısıt borcu üretir** — anti-pattern.
  BSD-2.
- **Godot 4**: `.gdshaderinc` + `#include "res://..."`; döngü tespiti,
  derinlik limiti 25, aynı dosyaya ikinci include otomatik yoksayılır
  (gömülü include-guard) — includer davranış referansı. MIT.
- **Three.js TSL**: JS node-graph → NodeBuilder →
  WGSL/GLSLNodeBuilder. Ders: builder/IR katmanı hedef dilden bağımsız
  tutulursa yeni backend = yeni builder; CSE/dead-code derleyiciye
  bırakılabilir. MIT.
- **WGSL/Tint**: WGSL'de modül sistemi yok; topluluk **WESL** süperseti
  (`import`, build-time'da vanilla WGSL'e indirgenir) ile çözüyor.
  Ders: ekosistem "dil + harici linker/bundler" modeline yakınsıyor —
  "GLSL + includer + kendi variant katmanı" yaklaşımının meşruiyet
  kanıtı.

## 6. glslang include / spirv-link / HLSL 2021

- **glslang**: `#extension GL_GOOGLE_include_directive : enable` +
  `TShader::preprocess/parse`'a `Includer` callback.
  `DirStackFileIncluder` StandAlone örnek kodu — projeler kopyalayıp
  özelleştirir. `GlslangCompiler.cpp` şu an includer bağlamıyor →
  adaptasyon = `ICompiler`'a `IIncludeResolver` seam'i + **cache
  anahtarına include-closure hash'i** (yoksa include değişikliği
  cache'i patlatmaz — doğruluk riski).
- **spirv-link**: `Linkage` capability **Vulkan execution
  environment'ta geçersiz** — Vulkan'a verilecek modül tam linklenmiş
  olmalı; spirv-link en fazla build-time aracı. Runtime kompozisyon
  kaynak/IR seviyesinde (Slang'in değer önerisi tam bu).
- **HLSL 2021**: templates + operator overloading + short-circuit;
  gerçek modül/interface-generics yok (SM5 interface fiilen ölü) —
  HLSL-master modülerlik sorununu çözmezdi.

## 7. Variant Yönetimi — Sentez

| Sistem | Model | Patlama kontrolü |
|---|---|---|
| Unreal | C++ typed permutation domain | ShouldCompilePermutation statik kapı |
| Unity | String keyword | build-time stripping + usage taraması |
| Filament | Küratörlü 8-bit bitmask + merkezi geçerlilik + stage-aware | 256→~96; --variant-filter |
| Slang | Generics/interfaces (tip sistemi) | link-time specialization |

Ders: keyword-string (Unity) en zayıf; **typed domain (Unreal) +
küratörlü bitmask + merkezi geçerlilik filtresi (Filament)** birleşimi
C++23 constexpr/concepts ile ikisinden de iyi yapılabilir
(derleme-zamanı geçersiz-kombinasyon reddi).

## CHROMODYNAMIC Mimari Opsiyonları

### Opsiyon A — cd::shader_lib: GLSL include-modülleri + glslang includer + C++23 typed variant (ÖNERİLEN)

- `engine/render/shader_lib/`: `shaders/modules/` altında Unity
  granülaritesinde GLSL modülleri (brdf, tonemap, noise,
  shadow_filtering, ibl_sampling, color_space, sampling, packing,
  math_common). Disiplin: Unity "header'lar Common'ı include etmez" +
  Godot "idempotent include + döngü reddi + derinlik limiti" +
  Filament "stage-aware ayrım".
- `cd::shader::ICompiler`'a `IIncludeResolver` seam'i
  (DirStackFileIncluder deseni + `cd://shader_lib/...` sanal yolu);
  X5 cache anahtarına include-closure içerik hash'i.
- Variant: Filament-tarzı küratörlü bitmask + Unreal-tarzı typed
  domain, C++23 constexpr permutation domain (geçersiz kombinasyon =
  static_assert / std::expected); define-set deterministik
  serileştirme → cache key.
- X4-A ADR'siyle tam uyumlu (tek-kaynak GLSL korunur); risk en düşük.

### Opsiyon B — Slang adopsiyonu (Phase-3 stratejik aday)

GLSL korpusunun yeniden yazımı + X4-A revizyonu gerektirir; Khronos +
Apache-2.0 + haftalık release temposu riski düşürdü ama bugün taze
D3D12 toolchain kararıyla çatışır. Phase-3'te yeniden değerlendir.

### Opsiyon C — Hibrit (RET)

Çift toolchain bakımı (iki cache epoch'u, iki hata yüzeyi)
kütüphane-ürünleştirme hedefini bulanıklaştırır. Yalnız "Slang-hazır
modül sınırları" fikri A'ya taşındı.

### Puanlama (0-10)

| Eksen | A | B: Slang | C: Hibrit |
|---|---|---|---|
| Performance (compile+runtime) | 8 | 9 | 7 |
| Memory / variant footprint | 8 | 9 | 7 |
| Maintainability | 8 | 9 | 4 |
| Lisans uyumu | 10 | 10 | 10 |
| Cross-API uyum | 8 | 9 | 8 |
| Adaptasyon maliyeti (düşük=iyi) | 9 | 4 | 5 |
| Ekosistem riski | 9 | 8 | 6 |
| **Toplam** | **60/70** | 58/70 | 47/70 |

## Öneri + Confidence

- Önerilen: **Opsiyon A** — GLSL include-modülleri (Unity SRP Core
  granülaritesi + Filament variant disiplini) + glslang
  DirStackFileIncluder-deseni includer + C++23 typed permutation
  domain; modül sınırları Slang-uyumlu çizilir (B'ye geçiş kapısı
  açık).
- Confidence: %85 — X4-A uyumu, düşük adaptasyon maliyeti, lisans
  temizliği kanıtlı; %15 belirsizlik Slang'in 12-18 ayda fiili standart
  olma hızında.
- Riskler: (1) X5 cache anahtarı include-closure'ı hash'lemezse
  bayat-cache doğruluk hatası — includer'la BİRLİKTE çözülmeli;
  (2) variant domain küratörlü tutulmazsa permutation patlaması;
  (3) spirv-link Vulkan'da runtime kompozisyon İÇİN KULLANILAMAZ
  (Linkage capability yok) — kompozisyon kaynak seviyesinde.

## Varsayımlar

- "Yapı normal kütüphane yapımıza yakın" = `engine/<lib>/` +
  `cd::<lib>::` + standalone test; shader_lib hem on-disk GLSL hem C++
  registry/variant yüzeyi taşır.
- X5 cache'in include-closure hash'lemediği sınırlı kod okumasına
  dayalı — implementasyon öncesi derin doğrulama şart.

## Sonraki

- ADR: ADR-20260612-shader-library-architecture.md (Opsiyon A +
  Slang-hazırlık kısıtları + variant domain kuralları).
- X5 cache anahtarına include-closure hash tasarımı (includer seam ile
  aynı fazda).
- Yan bulgu: Slang autodiff — differentiable rendering / NRC araştırma
  hattı için not.

## Kaynaklar (tümü erişim 2026-06-12)

shader-slang.org • khronos.org/news/press/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia •
github.com/shader-slang/slang • shader-slang.org/slang/user-guide/modules.html •
github.com/Unity-Technologies/Graphics (ShaderLibrary + BSDF.hlsl) •
docs.unity3d.com SRP Core built-in shader methods + LICENSE + variant stripping •
unity.com/blog shader-variants-optimization •
docs.unrealengine.com TShaderPermutationDomain • dev.epicgames.com KB shader permutations •
medium.com/@lordned UE4 Part 5 • therealmjp.github.io shader-permutations-part1 •
github.com/microsoft/ShaderConductor • github.com/microsoft/DirectXShaderCompiler/issues/5762 •
github.com/google/filament (+ Materials docs + Variant.h + shaders/src + discussion 5410) •
manpages.ubuntu.com matc.1 • bkaradzic.github.io/bgfx/tools.html •
docs.godotengine.org shader_preprocessor + ShaderInclude •
github.com/mrdoob/three.js wiki TSL + threejs.org/docs/TSL.html •
github.com/wgsl-tooling-wg/wesl-spec • wesl-lang.dev/spec/Imports • w3.org/TR/WGSL •
github.com/KhronosGroup/glslang issues 37/249 + PR 46 • forestsharp.com/glslang-cpp •
github.com/KhronosGroup/SPIRV-Tools linker.hpp • khronos.org/registry/spir-v SPIRV spec •
discourse.llvm.org spirv-link RFC • devblogs.microsoft.com Announcing HLSL 2021 •
github.com/microsoft/DirectXShaderCompiler wiki HLSL-2021
