# ADR-20260613 — cd::gluon SOTA Modül Manifesti (eksiksiz hedef set + inşa dalgaları)

- **Status**: Accepted (kullanıcı yönlendirmesi 2026-06-13: "olabildiğince
  genişlet, olması gereken tüm modüller bulunmalı")
- **Date**: 2026-06-13
- **Branch**: dev (HEAD abf5a96)
- **Deciders**: Cemal TATLI
- **Author**: architect (SL-A researcher raporu + ADR-20260612 SL-B mimari kararı + motor envanter grep'leri girdileriyle)
- **Related**:
  - `docs/ADR/ADR-20260612-shader-library-architecture.md` (SL-B: kütüphane mimarisi, §2.1 tohum set, §2.2 granülarite, §2.3 closure-hash, §2.4 variant, §2.5 test kapısı, §2.6 migrasyon) — bu ADR o kararı GENİŞLETİR, çelişmez
  - `research/notes/shader_library_sota_2026-06-12.md` (SL-A: Unity SRP Core ~80 modül taksonomisi + Filament surface_* + Unreal BRDF.ush)
  - `docs/ADR/ADR-20260612-x4-d3d12-shader-toolchain.md` (X4-A: tek-kaynak GLSL — manifest bu zincirin altında kalır)
- **Scope guard**: Bu ADR YALNIZ tasarım manifestidir. gluon modül `.glsl`
  dosyalarına, `CMakeLists.txt`'e veya `ModuleRegistry`'ye DOKUNMAZ
  (eşzamanlı başka ajan orada). Implementasyon SL-D dalga zincirinde
  Developer tarafından yapılır; her dalga ADR-20260612 §2.5 test kapısına
  ve §2.6 golden-pin disiplinine bağlıdır.

---

## 1. Bağlam

### 1.1 Problem

ADR-20260612 (SL-B) gluon kütüphanesini ve 9 tohum modül adını
(`math_common, color_space, packing, hash_noise, sampling, brdf,
ibl_sampling, shadow_filtering, tonemap`) tanımladı. Bugün diskte yalnız
**8 modül** var ve bunların 4'ü tohum-set DIŞINDADIR (verbatim-lift'ler:
`cotangent_frame, cone_atten, light_atten, ltc_polygon` + folding
`ltc_specular`). Yani SL-B'nin §2.1 listesinden **8/9 modül HENÜZ YOK**
(`color_space, packing, hash_noise, sampling, ibl_sampling,
shadow_filtering` eksik; `brdf` + `tonemap` + `math_common` var).

Kullanıcı hedefi bunun ötesinde: "olması gereken TÜM modüller". SL-B'nin
9-modül tohumu, Unity SRP Core'un ~80 modüllük taksonomisinin yalnız
çekirdek alt-kümesidir. Bu ADR iki ekseni birleştirerek **eksiksiz
normatif manifest** üretir ve her modülü motor-kanıtıyla önceliklendirir
— gluon'u "olması gereken tüm modüller"e taşıyacak inşa dalgalarının
kanıt-tabanlı yol haritası.

### 1.2 SOTA taksonomisi (NORMATİF eksen — SL-A raporundan)

- **Unity SRP Core**: ~80 `.hlsl`, tek-konu-tek-dosya: Common/Macros/
  Packing/Random/Hashes/BSDF/CommonLighting/EntityLighting/
  ImageBasedLighting/AreaLighting/SphericalHarmonics/Color/
  CommonMaterial/NormalSurfaceGradient/ParallaxMapping/SpaceTransforms/
  GeometricTools/Refraction/CommonShadow. En iyi granülarite şablonu;
  lisans kırmızı (yalnız yapı dersi, kod kopyalanamaz).
- **Filament**: surface_brdf / surface_shading_model_{standard,cloth,
  subsurface} / surface_light_{directional,punctual,indirect,reflections}
  / surface_ambient_occlusion/shadowing/fog + common_{math,defines}.
  Apache-2.0 — desen ve fit'ler alınabilir.
- **Unreal**: BRDF.ush / ShadingModels.ush aileleri. Kapalı EULA — yalnız
  şema dersi.

### 1.3 Motor kanıtı (EMPİRİK eksen — grep envanteri, 2026-06-13)

Aşağıdaki çoğaltmalar dosya:satır kanıtıyla doğrulandı (her iddia bir
`Grep`/`#include` çıktısına dayanır — CLAUDE.md §3 evidence-based):

| Çekirdek | Kanıt (dosya:satır) | Adet |
|---|---|---|
| GGX-D / Smith-G / Fresnel-Schlick (GLSL kopya) | `StandardPbrMaterial.hpp:118,128,134,138` + `LitPbrMaterial.hpp:113,121,126,129` | 2 tam set (motor) |
| GGX-D / Smith-G / importance-sample (CPU kopya) | `ibl/PrefilteredSpecular.hpp:45,56` + `ibl/BrdfLut.hpp:50,51` + `material/BrdfLut.hpp:61,71,83` | 2 CPU set |
| Octahedral encode/decode | `ddgi/DispatchPass.cpp:67` + `ddgi/Ddgi.hpp:273,290,392,462` (GLSL `oct_encode`/`oct_decode` + CPU `octahedral_encode/decode`) | ≥4 site |
| Hammersley / radical-inverse / importance-GGX | `ibl/BrdfLut.hpp:51` + `ibl/PrefilteredSpecular.hpp:45,56` + `material/BrdfLut.hpp:61,73,83` | 3 site (CPU); GLSL eşi yok |
| RNG (PCG-tarzı `float rand(inout uint)`) | `restir_di/Reservoir.hpp:194,268,352` + `restir_di/DispatchPass.cpp:140,214` + `restir_gi/GiReservoir.hpp:177,255,325` | ≥8 GLSL kopya |
| Value-noise + hash21 (GLSL) | `post/composite/Composite.hpp:382,395` (`cd_hash21`+value-noise) | 1 GLSL; CPU eşi `texture_synth/Noise.hpp:19`, `math/Noise.hpp:42` |
| fBm / value-noise (CPU) | `texture_synth/Noise.hpp` (fbm2/quintic 6oct) + hello_engine cloud fBm | ≥2 |
| Tonemap operatör switch (GLSL) | `LitPbrMaterial.hpp:254-259` (ACES Narkowicz+gamma) + hello_engine `prim.frag.glsl:21` (4-op switch) + composite + SL-B "34 mention site" | ≥3 GLSL (gluon `tonemap.glsl` bunları HENÜZ tüketmiyor) |
| sRGB/linear + luminance (CPU; GLSL eşi dağınık) | `ui/widgets/ColorPicker.cpp:84,105` + `hdr_display/HdrDisplay.hpp:90` (GLSL `cd_linear_srgb_to_rec2020`) + `light/ColorTemperature.hpp:74` + FLIP/SSIM luminance | ≥5 |
| PCF 3×3 + slope-bias (GLSL) | `prim.frag.glsl:396-428` (`sample_shadow`, textureSize+3×3 loop+slope bias) | 1 (tek tüketici bugün, ama kanonik gerek) |
| Exp/height fog (GLSL) | `prim.frag.glsl:1073-1080` (`1-exp(-dist*density*h_falloff)`) | 1 GLSL + CPU `volumetric/fog/*` |
| LTC area-light (zaten gluon'da) | `ltc_polygon.glsl` + `ltc_specular.glsl` (CPU eşi `brdf/ltc/Ltc.hpp`) | dedupe edildi |
| Sheen/Clearcoat (GLSL hazır, gluon DIŞI) | `brdf/sheen_clearcoat/SheenClearcoat.hpp:71` (`kSheenClearcoatGlsl`) | 1 GLSL string, kütüphanede |
| SSS separable blur + Burley wrap (GLSL hazır, gluon DIŞI) | `brdf/sss/Sss.hpp:84,125` (`kSssSeparableBlurCS`, `kInlineBurleyWrapGlsl`) | 1 CS + 1 inline |
| Henyey-Greenstein faz (CPU; GLSL yok) | `volumetric/fog/test_volumetric_fog_froxel.cpp:28` (`henyey_greenstein`) | CPU-only |
| Cotangent-frame TBN | `cotangent_frame.glsl` (prim.frag'tan verbatim) | dedupe edildi |
| Parallax/POM, normal-blend (reorient/whiteout/UDN) | grep BOŞ | 0 (no consumer) |
| Blue-noise / dither / IGN debanding | grep BOŞ | 0 (no consumer) |
| Depth/projection reconstruct (linearizeDepth/world-from-depth) | grep BOŞ | 0 (no consumer — SSR/GTAO inline tutuyor) |

**Sentez**: P0 dedupe-acil çekirdeği motorca KANITLI çoğaltılan
`packing(oct)` + `hash_noise(rand/pcg ×8)` + `sampling(Hammersley ×3)` +
`color_space(sRGB/lum ×5)` + `shadow_filtering(PCF)` + `ibl_sampling` +
`brdf` genişletme'dir. P1 SOTA-çekirdek motorda GLSL-string olarak HAZIR
ama gluon DIŞINDA duran `sheen_clearcoat` + `sss` (taşınmalı) ve
matematiksel olarak gerekli `oren_nayar/anisotropy/diffuse-spec`
uzantılarıdır. P2 spekülatif (motor HENÜZ kullanmıyor): `parallax`,
`normal_blend`, `dither`, `depth_reconstruct`, `fog/scattering`,
`refraction`, `transforms`.

## 2. Karar

### 2.1 Eksiksiz modül manifesti

Her satır gelecekteki bir gluon `.glsl` modülüdür. **Bağımlılık sütunu
ADR-20260612 §2.2'yi korur**: modüller YALNIZ `math_common.glsl` include
edebilir; modüller-arası diğer bağımlılık YASAK (kompozisyonu tüketen
shader yapar). Tek istisna `ltc_specular → ltc_polygon` folding'idir
(zaten kabul edilmiş, tip-yakın çift). **Öncelik**: P0 = motorca kanıtlı
çoğaltma (acil dedupe); P1 = SOTA-çekirdek (motor kullanıyor ya da
matematiksel olarak temel BRDF tamamlama); P2 = ileri/spekülatif
(motorda tüketici YOK — "no consumer yet" işaretli).

#### (a) math / util

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `math_common` *(VAR)* | PI ailesi, saturate/saturate3, pow5, max3, ONB (Duff) | temel — tüm modüllerin tabanı | P0 | — (kök) |
| `constants_ext` | TAU/INV_TAU, GOLDEN_ANGLE, FLT_MAX/MIN epsilon aileleri, deg↔rad | dağınık literaller (6.28318530 ×3: `ltc_polygon.glsl:42`, `ltc_specular.glsl:62`, sheen GLSL) | P1 | math_common |
| `color_space` | linear↔sRGB (exact + fast pow2.2), luminance (Rec.709/601), RGB↔YCoCg, RGB↔HSV, linear_srgb→Rec.2020 | `ColorPicker.cpp:84,105`, `HdrDisplay.hpp:90`, `ColorTemperature.hpp:74`, FLIP/SSIM lum ×≥5 | **P0** | math_common |
| `packing` | octahedral encode/decode (Cigolle 2014), RGBM/RGBE, unorm/snorm pack, normal→uint, depth pack | `ddgi/Ddgi.hpp:273,290,392,462`, `ddgi/DispatchPass.cpp:67` ×≥4 | **P0** | math_common |
| `depth_reconstruct` | linearizeDepth, view/world-pos-from-depth, NDC↔view, reverse-Z helpers | grep BOŞ (SSR/GTAO inline) | P2 *(no consumer yet)* | math_common |
| `transforms` | quaternion mul/rotate, TBN compose, screen↔NDC, dir-from-cubeface | grep BOŞ | P2 *(no consumer yet)* | math_common |

#### (b) sampling / noise

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `hash_noise` | PCG (`pcg`/`pcg3d`), hash11/21/33, value-noise, perlin, fBm, curl, worley | `restir_di/Reservoir.hpp:194,268,352`+`DispatchPass.cpp:140,214`+`restir_gi/GiReservoir.hpp:177,255,325` (rand ×8), `composite/Composite.hpp:382` (hash21+value-noise), hello_engine cloud fBm | **P0** | math_common |
| `sampling` | Hammersley, radical-inverse (van der Corput), cosine-hemisphere, importance-sample-GGX, uniform-sphere/disk | `ibl/BrdfLut.hpp:51`, `ibl/PrefilteredSpecular.hpp:45,56`, `material/BrdfLut.hpp:61,73,83` (CPU ×3 — GLSL eşi henüz yok) | **P0** | math_common |
| `dither` | ordered/Bayer matrix, interleaved-gradient-noise (IGN), blue-noise sample, debanding | grep BOŞ | P2 *(no consumer yet)* | math_common |

#### (c) BRDF / shading

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `brdf` *(VAR)* | Fresnel-Schlick(+f90,+roughness), GGX-D, Smith-V (height-correlated + fast), Lambert/Burley diffuse, specular-GGX | `StandardPbrMaterial.hpp:118,128,134,138`, `LitPbrMaterial.hpp:113,121,126,129` (2 tam GLSL set) | **P0** *(consumer migrasyonu bekliyor)* | math_common |
| `brdf_diffuse_ext` | Oren-Nayar (Qualitative + full), Disney-retro varyant, energy-comp diffuse | grep BOŞ (motor Lambert/Burley'de) | P1 | math_common |
| `brdf_anisotropy` | anizotropik GGX-D (Burley aspect), anisotropic Smith-V, tangent-bitangent half-vector | grep BOŞ | P1 | math_common |
| `brdf_sheen` | Charlie-D (Estevez 2017), Neubelt-V, sheen energy-comp | `brdf/sheen_clearcoat/SheenClearcoat.hpp:71` (`kSheenClearcoatGlsl` HAZIR, gluon DIŞI) | **P1** *(string → modül taşıma)* | math_common |
| `brdf_clearcoat` | clearcoat D*V (Filament 0.045-min), clearcoat-Fresnel, base-layer atten | `brdf/sheen_clearcoat/SheenClearcoat.hpp:81,96` (`clearcoat_dv` + inline lobe) | **P1** *(string → modül taşıma)* | math_common |
| `brdf_sss` | Burley diffusion profile, wrap-diffusion inline, pre-integrated curvature LUT eval | `brdf/sss/Sss.hpp:84,125` (`kSssSeparableBlurCS`, `kInlineBurleyWrapGlsl` HAZIR, gluon DIŞI) | **P1** *(string → modül taşıma)* | math_common |
| `brdf_cloth` | inverted-GGX/Ashikhmin cloth-D, cloth-V, fuzz term | grep BOŞ | P2 *(no consumer yet)* | math_common |
| `refraction` | Snell refract dir, thin-film/transmittance, IOR→F0 | grep BOŞ | P2 *(no consumer yet)* | math_common |

#### (d) IBL

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `ibl_sampling` | split-sum prefilter ağırlığı (Karis), DFG/env-BRDF LUT eval, roughness→mip, prefilter-mip-bias | `ibl/PrefilteredSpecular.hpp:45,56`, `ibl/BrdfLut.hpp:50,141`, `material/BrdfLut.hpp:83` (CPU bake — GLSL runtime-eval eşi yok) | **P0** *(GLSL runtime yolu için; bake YENİDEN ÜRETİLMEZ — §2.6 kuralı)* | math_common |
| `spherical_harmonics` | SH9 diffuse irradiance eval, SH proje yardımcıları, L1/L2 rotate | `scene/LightProbe.hpp` SH; `ddgi/Ddgi.hpp` irradiance atlas (oct, SH değil) | P1 | math_common |

#### (e) lighting

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `light_atten` *(VAR)* | Frostbite windowed inverse-square distance atten | `prim.frag.glsl` + `LitPbrMaterial.hpp:134` (Addendum dedupe) | P0 | — *(self-contained)* |
| `cone_atten` *(VAR)* | spot cone falloff (t² smoothstep-eşi) | `LitPbrMaterial.hpp` (verbatim) | P1 *(prim.frag smoothstep varyantıyla birleştirme user-signed)* | — |
| `ltc_polygon` *(VAR)* | LTC Lambert form-factor (atan2 edge integral) | prim.frag verbatim; CPU `brdf/ltc/Ltc.hpp` | P0 | math_common (şu an —) |
| `ltc_specular` *(VAR)* | LTC inverse-matrix, sparse M-transform, GGX form-factor | `StandardPbrMaterial.hpp` LTC; CPU eşi | P1 | **ltc_polygon** *(kabul edilmiş folding istisnası)* |
| `light_evaluators` | punctual/directional/spot radiance birleşik eval, NoL clamp, light-vector helper | `prim.frag.glsl:587-602` + LitPbr/StandardPbr dağınık | P1 | math_common |

#### (f) shadow

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `shadow_filtering` | PCF 3×3/5×5/Poisson-disk, slope-scaled + normal bias, cascade-select, depth-compare | `prim.frag.glsl:396-428` (`sample_shadow` 3×3 + slope-bias) | **P0** | math_common |
| `shadow_vsm_esm` | VSM Chebyshev, ESM exp-warp, moment pack/unpack, light-bleed reduce | grep BOŞ (motor PCF'te) | P2 *(no consumer yet)* | math_common |

#### (g) surface

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `cotangent_frame` *(VAR)* | derivatif-tabanlı TBN (Mikkelsen 2010, degenerate-UV guard) | prim.frag verbatim | P0 | — *(self-contained)* |
| `normal_mapping` | TBN apply, normal-blend (whiteout/UDN/RNM/reorient), detail-normal | grep BOŞ (prim.frag inline TBN apply) | P2 *(no consumer yet)* | math_common |
| `parallax` | parallax-offset, steep parallax, POM (parallax-occlusion + soft-shadow) | grep BOŞ | P2 *(no consumer yet)* | math_common |

#### (h) post / grading

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `tonemap` *(VAR)* | Reinhard(+ext), ACES Narkowicz, ACES Hill-fitted, Uchimura/GT | `LitPbrMaterial.hpp:254`, `prim.frag.glsl:21` (4-op), composite — SL-B "34 site" | **P0** *(consumer migrasyonu bekliyor)* | math_common |
| `tonemap_ext` | AgX (full), Hable/Uncharted2, PBR-neutral (Khronos), exposure helper | `prim.frag.glsl:21` fx_params op=3 "AGX" placeholder; Hable op=2 | P1 | math_common |
| `color_grading` | lift-gamma-gain, white-balance (temp/tint), saturation/contrast, channel-mixer, LUT-3D sample | `StandardPbrMaterial.hpp:539` (saturation pull-away + gamma) | P1 | math_common |

#### (i) fog / atmosphere helpers

| Modül | İçereceği fonksiyonlar | Motor-kopya-kanıtı | Öncelik | Bağımlılık |
|---|---|---|---|---|
| `fog` | exponential fog, height/exp-height fog, fog-blend | `prim.frag.glsl:1073-1080` (`1-exp(-dist*density*h_falloff)`) + CPU `volumetric/fog/*` | P1 | math_common |
| `scattering` | Henyey-Greenstein faz, Rayleigh/Mie faz, Schlick-HG yaklaşımı, transmittance | CPU `volumetric/fog/...:henyey_greenstein` (GLSL eşi yok) | P2 *(no consumer yet — CPU-only)* | math_common |

### 2.2 Mevcut 8 modülün manifeste göre durumu

| Modül | Manifest kategori | Durum | Eksik fonksiyon (manifest hedefe göre) |
|---|---|---|---|
| `math_common` | (a) | ✅ uyumlu kök | `constants_ext`'e bölünebilecek TAU/golden-angle yok (modül-içi kalabilir) |
| `brdf` | (c) | ✅ çekirdek tam | Oren-Nayar / anisotropy / sheen / clearcoat AYRI P1 modüllere (granülarite §2.2 — bu modüle TIKMA) |
| `tonemap` | (h) | ✅ 4-op var | AgX-full / Hable / Khronos-neutral → `tonemap_ext` (P1) |
| `cotangent_frame` | (g) | ✅ self-contained | — (normal-blend/parallax AYRI P2 modüller) |
| `light_atten` | (e) | ✅ self-contained | — |
| `cone_atten` | (e) | ⚠️ LitPbr-only | prim.frag smoothstep varyantı birleştirilmedi (user-signed visual phase — §2.6) |
| `ltc_polygon` | (e) | ✅ Lambert fit | production 64×64 LUT-keyed fit yok (analitik fit kabul) |
| `ltc_specular` | (e) | ✅ GGX uzantı | ltc_polygon folding — manifestte tek kabul edilmiş çift-include |

**Boşluk**: SL-B §2.1 tohum setinden 6 modül HÂLÂ EKSİK ve hepsi P0:
`color_space`, `packing`, `hash_noise`, `sampling`, `ibl_sampling`,
`shadow_filtering`. Bunlar dalga-1'in çekirdeğidir.

### 2.3 İnşa dalgaları (kanıt-tabanlı sıralama)

Sıralama §1.3 envanterindeki çoğaltma yoğunluğuna göredir (çok kopya =
acil dedupe = önce). Her modül için: ADR-20260612 §2.5(1) headless
compile-test ZORUNLU; tüketici varsa §2.5(3) golden-pin (chrome_probe +
per-feature fixture, BAYT-AYNI).

**Dalga P0-A — eksik tohum çekirdeği (en yüksek çoğaltma):**
1. `packing` — octahedral (ddgi ×4+). Tüketici-migrasyon notu: ddgi
   GLSL `oct_encode/decode` → `#include <cd/gluon/packing.glsl>`; CPU
   `octahedral_encode/decode` AYRI kalır (C++ yüzeyi, gluon GLSL değil).
2. `hash_noise` — rand/pcg (restir ×8 + composite). Migrasyon: restir
   `float rand(inout uint)` kopyalarını PCG kanonik forma topla — golden
   ReSTIR fixture'ı ile pin (RNG dizisi değişebilir; user-signed kontrol).
3. `sampling` — Hammersley/importance-GGX. Migrasyon: yalnız YENİ GLSL
   runtime tüketiciler; CPU `ibl/material BrdfLut` C++ kalır (bake YENİDEN
   ÜRETİLMEZ — §2.6 IBL kuralı).
4. `color_space` — sRGB/luminance (×5). Migrasyon: ColorPicker/HdrDisplay
   GLSL'ini topla; CPU C++ helper'ları (ColorPicker.cpp) ayrı kalır.
5. `shadow_filtering` — PCF 3×3 + slope-bias. Migrasyon: prim.frag
   `sample_shadow` → modül include; golden = mevcut shadow fixture'ları.
6. `ibl_sampling` — split-sum/DFG runtime eval. Migrasyon: yalnız runtime
   GLSL yolu; bake C++ DOKUNULMAZ.

**Dalga P0-B — mevcut modüllerin consumer dedupe'u (modül VAR, tüketici hâlâ kopya):**
7. `brdf` tüketici migrasyonu: `StandardPbrMaterial.hpp` + `LitPbrMaterial.hpp`
   gömülü `D_GGX/G_Smith/F_Schlick` → `#include <cd/gluon/brdf.glsl>`
   (Addendum default-resolver köprüsü kullanılır; golden = chrome_probe).
8. `tonemap` tüketici migrasyonu: prim.frag 4-op switch + LitPbr ACES →
   `#include <cd/gluon/tonemap.glsl>` (golden = composite/per-feature).

**Dalga P1 — SOTA-çekirdek (motor GLSL-string'i HAZIR ya da matematiksel temel tamamlama):**
9. `brdf_sheen` ← `brdf/sheen_clearcoat/SheenClearcoat.hpp:kSheenClearcoatGlsl`
   (string → modül; CPU yüzey C++ kalır).
10. `brdf_clearcoat` ← aynı header `clearcoat_dv` + inline lobe.
11. `brdf_sss` ← `brdf/sss/Sss.hpp:kSssSeparableBlurCS` + inline-wrap.
12. `brdf_diffuse_ext` (Oren-Nayar) + `brdf_anisotropy` — motor henüz
    Lambert/Burley + izotropik; SOTA tamamlama, no-consumer ama BRDF
    çekirdeğin matematiksel zorunlu uzantısı (P1, P2 değil — material-graph
    rework'ünün hedef tüketicisi var: SheenClearcoat header v1.7 notu).
13. `tonemap_ext` (AgX-full/Hable/Khronos) — prim.frag op=3 "AGX"
    placeholder'ın gerçek hedefi.
14. `color_grading` (lift-gamma-gain/white-balance) — StandardPbr
    saturation/gamma'nın hedefi.
15. `spherical_harmonics`, `light_evaluators`, `cone_atten` birleştirme
    (user-signed), `fog`, `constants_ext`.

**Dalga P2 — ileri / spekülatif (motor tüketici YOK — "no consumer yet"):**
16. `parallax`, `normal_mapping`, `dither`, `depth_reconstruct`,
    `transforms`, `shadow_vsm_esm`, `brdf_cloth`, `refraction`,
    `scattering`. Hepsi P2: yalnız headless compile-test (golden YOK,
    çünkü tüketici yok). Bu modüller "olması gereken tüm modüller"
    hedefinin SOTA-tamlık katmanıdır; SPEKÜLATİF işaretli — eklendiğinde
    bir demo/tüketici ile birlikte gelmeli (review reddi: tüketicisiz
    P2 modül + "TODO: someday" = ölü kod).

### 2.4 Korunan disiplin kuralları (ADR-20260612'den miras)

- **§2.2 granülarite**: TÜM yeni modüller yalnız `math_common.glsl`
  include eder. Veto: bir modül başka bir modülü include ederse
  (ltc_specular→ltc_polygon istisnası DIŞINDA) → Developer'a iade.
  BRDF aileleri (sheen/clearcoat/sss/oren-nayar/anisotropy) AYRI
  dosyalar — `brdf.glsl`'e tıkıştırma God-module ihlali olur.
- **§2.4 variant**: domain başına ≤8 boyut; geçersiz kombinasyon
  derlenmiş varianta dönüşmez (constexpr filter).
- **§2.5 test kapısı**: her modül headless compile-all'a girer;
  tüketici migrasyonu golden-pinned.
- **§2.6 migrasyon + IBL kuralı**: tüketiciler TEKER TEKER; **IBL bake
  YENİDEN ÜRETİLMEZ** — `ibl_sampling`/`sampling` yalnız runtime + yeni
  tüketiciler. Yeni shader politikası (SL-E): yeni çekirdek fonksiyon
  gömülü-string'e eklenmez, modüle gider.
- **CMakeLists pattern**: yeni modül = `CD_GLUON_MODULES` listesine ekleme
  (configure-time embed + sorted katalog). Manifest CMakeLists'e
  DOKUNMAZ — Developer her dalgada ilgili modül adını ekler.

## 3. Reddedilen alternatifler

- **Tek dev `brdf.glsl`'e tüm BRDF ailelerini koymak** (sheen + clearcoat
  + sss + oren-nayar + anisotropy + cloth): God-module; §2.2 granülarite
  ihlali; closure'ları şişirir, variant matrisini patlatır. RED — her
  aile ayrı modül.
- **Tüm ~80 Unity modülünü 1:1 kopyalamak**: (1) Unity lisansı kırmızı
  (kod kopyalanamaz, yalnız yapı dersi); (2) motorun kullanmadığı ~40
  modül ölü kod olur. RED — taksonomi NORMATİF referans, kopya değil;
  motor-kanıtı önceliklendirir.
- **Spekülatif P2 modülleri şimdi yazmak** (parallax/dither/vsm vb.):
  tüketicisiz modül = bakım borcu + bit-rot. RED — P2 modüller bir
  demo/tüketici ile birlikte doğar (CLAUDE.md "her feature'ın bir anı
  olmalı" kültürüyle uyumlu).
- **CPU helper'ları (BrdfLut.hpp, octahedral_encode, ColorPicker)
  gluon GLSL modülüne taşımak**: bunlar C++ yüzeyi (host-side bake/UI);
  gluon GLSL modülleri GPU-side. Karıştırma katman ihlali. RED — CPU
  kopyalar ayrı bir konsolidasyon konusu (gluon kapsamı dışı).
- **`scattering`/`fog` GLSL'ini hemen yazmak**: Henyey-Greenstein
  bugün CPU-only (`volumetric` testi); GLSL tüketici yok. P2 — runtime
  volumetric GLSL yolu açıldığında.

## 4. Sonuçlar

- (+) gluon'un "olması gereken tüm modüller" hedefi 9 kategoriye yayılmış
  **~35 modüllük** eksiksiz manifeste bağlandı; her modül kategorize +
  önceliklendirilmiş + bağımlılığı netleşmiş.
- (+) Motor-kanıtı P0/P1/P2'yi objektif belirledi: P0 = grep'le doğrulanmış
  çoğaltma (oct ×4, rand ×8, sRGB ×5, GGX/Fresnel 2 set, PCF, tonemap ×3+);
  spekülatif modüller (parallax/dither/vsm/cloth/refraction/scattering)
  "no consumer yet" ile işaretlendi → ölü-kod riski yok.
- (+) İnşa dalgaları çoğaltma yoğunluğuna göre sıralı: P0-A (eksik tohum)
  → P0-B (mevcut brdf/tonemap consumer dedupe) → P1 (sheen/clearcoat/sss
  string→modül + oren-nayar/anisotropy/AgX/grading) → P2 (spekülatif).
- (+) SL-B §2.1 tohum setiyle %100 uyumlu (6 eksik tohum modülü P0-A
  dalgasının çekirdeği); §2.2/§2.4/§2.5/§2.6 disiplinleri korundu.
- (−) Manifest BÜYÜK (~35 modül); tamamı tek marathon'da inşa edilemez —
  dalga zinciri çok-faza yayılır. Kabul: P0 (8 modül) ilk; P1/P2 talebe +
  tüketici-doğuşuna bağlı.
- (−) BRDF aileleri ayrı modül kararı dosya sayısını artırır (brdf +
  brdf_diffuse_ext + brdf_anisotropy + brdf_sheen + brdf_clearcoat +
  brdf_sss + brdf_cloth = 7 dosya). Kabul: granülarite §2.2 + Slang
  `module` 1:1 sınırı bunu zorunlu kılar; God-module alternatifi daha kötü.
- (→) Developer sırası: P0-A dalga 1-6 (packing→hash_noise→sampling→
  color_space→shadow_filtering→ibl_sampling), her biri headless compile-test
  + (varsa) golden; sonra P0-B brdf/tonemap consumer migrasyonu (Addendum
  default-resolver köprüsü). P1/P2 talebe bağlı, her P2 bir tüketiciyle.

---

## Varsayımlar

- CPU-side kopyalar (`material/BrdfLut.hpp`, `ibl/*`, `ColorPicker.cpp`,
  `octahedral_encode`) gluon GLSL modül kapsamı DIŞIDIR (host-side); ayrı
  bir C++ konsolidasyon konusu — bu manifest yalnız GPU-side GLSL modülleri
  hedefler.
- `brdf/sheen_clearcoat` + `brdf/sss` kütüphanelerindeki GLSL string'leri
  (`kSheenClearcoatGlsl`, `kSssSeparableBlurCS`) gluon'a TAŞINIR; kaynak
  kütüphanenin C++ yüzeyi (CPU `charlie_d`, `burley_diffusion_profile`)
  kalır — yalnız GLSL gluon'a göç eder (tek-kaynak GPU-side).
- Henyey-Greenstein bugün CPU-only varsayımı `volumetric/fog` grep'ine
  dayanır; runtime volumetric GLSL yolu açılırsa `scattering` P2→P1 olur.
- Module-registration mekanizması (`CD_GLUON_MODULES` configure-time embed
  + sorted katalog) okundu (`CMakeLists.txt:7-52`); manifest ona DOKUNMAZ,
  Developer her dalgada modül adını ekler.

## Sonraki

- Implementasyonu **Developer** yapar: P0-A dalga 1 (`packing.glsl`) ilk
  modül; her dalga ADR-20260612 §2.5 headless compile-test + §2.6
  golden-pin kapısından geçer; commit başına bir modül/tüketici.
- Veto noktaları (Developer iadesi): (1) bir P1/P2 modülü math_common
  dışında modül include ederse; (2) BRDF ailesi `brdf.glsl`'e tıkıştırılırsa
  (God-module); (3) tüketicisiz bir P2 modül "TODO someday" ile eklenirse;
  (4) IBL bake yeniden üretilirse.
- cone_atten birleştirme + ltc production-LUT fit + brdf-aileleri'nin
  material-graph tüketicisi user-signed visual phase'e bağlı (§2.6).
