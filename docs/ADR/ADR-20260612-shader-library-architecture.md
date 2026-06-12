# ADR-20260612 — SL-B: cd::gluon — SOTA Shader Modül Kütüphanesi Mimarisi

- **Status**: Accepted (kullanıcı yönlendirmesi 2026-06-12: "çok gelişmiş
  state-of-the-art seviyesi bir shader library" — öncelikli plan kalemi)
- **Date**: 2026-06-12
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: orchestrator (SL-A researcher raporu + X4-A architect ADR'si girdileriyle)
- **Related**:
  - `research/notes/shader_library_sota_2026-06-12.md` (SL-A SOTA raporu; puanlama 60/70)
  - `docs/ADR/ADR-20260612-x4-d3d12-shader-toolchain.md` (X4-A: tek-kaynak GLSL — bu ADR o kararın ÜZERİNE inşa eder, çelişmez)
  - `docs/ADR/ADR-20260608-x5-shader-on-disk-hot-reload.md` (CachedCompiler + FileWatcher — cache-key genişlemesi burada tanımlanır)
  - `docs/ROADMAP_PHASE_2.md` §9 (SL milestone DAG'ı)
- **Scope guard**: Bu ADR karar + yüzey kontratı tanımlar. SL-C/SL-D
  implementasyonu bu kontratlara bağlıdır; paralel API açmaz, mevcut
  `cd::shader::ICompiler` yüzeyini `Edit` ile genişletir.

---

## 1. Bağlam

### 1.1 Problem

Shader kodu bugün ÜÇ yerde yaşıyor ve çekirdek parçaları çoğaltılmış
durumda (phase1128 envanteri):

- 76 dosyada gömülü GLSL string'leri (`engine/render/*` + samples);
- 7 on-disk GLSL dosyası (hello_engine X5 yolu + cluster_assign.comp);
- Çoğaltılmış çekirdekler: **Fresnel ×9 dosya, GGX-D ×5, Smith-G ×6,
  fBm ×6, octahedral encode ×8, hash/PCG ×8, Hammersley/importance ×5;
  tonemap operatörleri 34 sitede**.

Her kopya sessiz-drift tehlikesidir: W8 düzeltme dalgaları defalarca
bir kopyayı düzeltip diğerini kaçırdı. Kullanıcı hedefi: yeniden
kullanılabilir, motor kütüphanesi gibi tüketilebilen ve **bağımsız ürün
olarak da verilebilen** bir shader kütüphanesi.

### 1.2 SOTA özeti (SL-A raporundan)

- **Unity SRP Core**: tek-konu-tek-dosya ~80 modül granülaritesi (en iyi
  şablon); "header'lar Common'ı include etmez" disiplini. Lisans kırmızı
  — yalnız yapı dersi.
- **Filament**: filamat = bağımsız tüketilebilir shader kütüphanesi
  emsali; **küratörlü 8-bit variant bitmask + merkezi geçerlilik
  filtresi + stage-aware maskeleme** (256→~96). Apache-2.0.
- **Unreal**: C++ typed permutation domain (`TShaderPermutationDomain`)
  + `ShouldCompilePermutation` statik kapısı; `/Engine/` sanal yolu.
- **Slang**: dil-seviyesi modüller + generics; bugün adopte etmek taze
  X4-A GLSL-master kararıyla çatışır → Phase-3 adayı.
- **Godot**: includer davranış referansı (idempotent, döngü reddi,
  derinlik limiti 25).
- **spirv-link**: `Linkage` capability Vulkan'da GEÇERSİZ → runtime
  SPIR-V modül kompozisyonu yok; kompozisyon KAYNAK seviyesinde.

## 2. Karar

**Opsiyon A**: `engine/render/gluon/` altında `cd::gluon`
kütüphanesi — GLSL include-modülleri + glslang includer seam'i + C++23
typed variant domain. Modül sınırları Slang `module` birimleriyle 1:1
örtüşecek şekilde çizilir (Phase-3 Slang kapısı açık kalır).

### 2.1 Kütüphane yerleşimi (standart `engine/<lib>` konvansiyonu)

```text
engine/render/gluon/
  include/cd/gluon/
    ModuleRegistry.hpp     // modül kataloğu + sanal-yol -> içerik çözümü
    IncludeResolver.hpp    // cd::shader::IIncludeResolver implementasyonu
    VariantDomain.hpp      // C++23 typed permutation domain + curator filter
  shaders/modules/         // GLSL modülleri (kütüphanenin ASIL ürünü)
    math_common.glsl       //  sabitler (PI ailesi), saturate/pow5, ONB (Duff/Frisvad)
    color_space.glsl       //  linear<->sRGB, luminance, YCoCg
    packing.glsl           //  octahedral encode/decode, RGBM, unorm pack
    hash_noise.glsl        //  PCG/hash ailesi, value/perlin, fBm, curl
    sampling.glsl          //  Hammersley, cosine/GGX importance, radical inverse
    brdf.glsl              //  Fresnel-Schlick, GGX D, Smith G (height-correlated), Disney diffuse
    ibl_sampling.glsl      //  split-sum prefilter/irradiance yardımcıları (Karis)
    shadow_filtering.glsl  //  PCF aileleri, bias yardımcıları
    tonemap.glsl           //  ACES (fitted), Reinhard(+ext), Uchimura, AgX yaklaşımı
  src/                     // registry implementasyonu (modülleri embed eden tablo)
  tests/                   // headless compile-all-permutations gtest
  CMakeLists.txt
```

### 2.2 Include mekanizması ve disiplin kuralları

1. GLSL tarafı `#extension GL_GOOGLE_include_directive : enable` +
   `#include <cd/gluon/brdf.glsl>` sanal yolları kullanır
   (Unreal `/Engine/` deseninin bizdeki karşılığı).
2. `cd::shader::ICompiler::compile`'a **opsiyonel** `IIncludeResolver*`
   parametresi eklenir (default `nullptr` = bugünkü davranış —
   mevcut hiçbir çağıran kırılmaz). glslang backend'i resolver'ı
   `glslang::TShader::Includer`'a köprüler (DirStackFileIncluder deseni).
3. Includer davranışı (Godot referansı): **idempotent** (aynı modül
   ikinci kez include edilirse boş döner — ayrıca her modülde
   `#ifndef CD_GLUON_<NAME>_GLSL` koruması bulunur), **döngü reddi**
   (hata), **derinlik limiti 16**.
4. Granülarite disiplini (Unity kuralı uyarlaması): modüller yalnız
   `math_common.glsl`'e include bağımlılığı kurabilir; modüller-arası
   diğer bağımlılıklar YASAK — kompozisyonu TÜKETEN shader yapar. Bu,
   closure'ları küçük ve öngörülebilir tutar ve Slang `module`
   sınırlarıyla örtüşür.
5. BSDF konvansiyonu (Unity'den ders): direct-lighting fonksiyonları
   `/PI` İÇERİR ve `_pi` soneki taşır; IBL varyantları PI'siz.

### 2.3 Cache doğruluğu — include-closure hash (ZORUNLU, SL-C kapısı)

X5 `CachedCompiler` anahtarı bugün `source ⊕ stage ⊕ lang ⊕ target ⊕
entry ⊕ debug`. Includer eklendiğinde anahtar **include closure'unu**
da içermek ZORUNDADIR, yoksa modül düzenlemesi bayat `.spv` döndürür
(sessiz yanlış-render). Karar:

- Resolver, çözdüğü her `(sanal-yol, içerik)` çiftini kaydeder;
- closure hash = FNV-1a üzerinden, SANAL-YOL'a göre SIRALANMIŞ
  `(yol_hash ⊕ içerik_hash)` dizisinin katlanması (deterministik,
  include sırasından bağımsız);
- anahtar: `eski_anahtar ⊕ closure_hash`; closure boşsa (resolver yok
  veya include yok) anahtar DEĞİŞMEZ → mevcut cache epoch'u korunur.
- X5 hot-reload watcher'ı modül dosyalarını da izler; modül değişimi
  o modülü closure'unda taşıyan TÜM root'ları invalidate eder
  (resolver'ın kaydettiği ters-bağımlılık haritası üzerinden).

### 2.4 Variant yönetimi — typed domain + küratörlü filtre

Unreal typed-domain + Filament küratörlü-geçerlilik sentezi, C++23:

```cpp
// VariantDomain.hpp yüzey eskizi (NTTP string boyut adları):
using PbrDomain = cd::gluon::VariantDomain<
    cd::gluon::BoolDim<"DIR_LIGHT">,
    cd::gluon::BoolDim<"SHADOW_RECV">,
    cd::gluon::EnumDim<"TONEMAP", 4>>;

// Filament-tarzı merkezi geçerlilik (constexpr; geçersiz kombinasyon
// derlenmiş varianta hiç dönüşmez):
//   "shadow receiver isen directional light şart"
constexpr auto kPbrFilter = [](auto v) {
    return !v.template get<"SHADOW_RECV"> || v.template get<"DIR_LIGHT">;
};

// Deterministik define serileştirme -> CachedCompiler defines + key:
auto defines = PbrDomain::to_defines(variant);  // alfabetik sıra GARANTİ
```

Kurallar: domain başına ≤ 8 boyut (Filament dersi: küratörlü küçük
küme); `for_each_valid(filter, fn)` tüm geçerli kombinasyonları
ÜRETEBİLİR (headless derleme testinin temeli); geçersiz istek
`std::expected` hatası döner.

### 2.5 Test kapısı (SL-D ile birlikte zorunlu)

1. **Headless compile-all**: her modül, minimal sarmalayıcı shader
   içinde glslang ile derlenir (GPU gerekmez; Null/CI dostu).
2. **Permutation smoke**: temsilî domain'lerin tüm GEÇERLİ
   kombinasyonları derlenir (Filament matc build-time kapısının bizdeki
   karşılığı).
3. **Golden pin**: her tüketici migrasyonu chrome_probe + ilgili
   per-feature golden fixture'larla BAYT-AYNI doğrulanır (Run 31
   extraction disiplininin aynısı).

### 2.6 Migrasyon politikası (SL-D)

- Dalga 1 sırası (çoğaltma yoğunluğuna göre): `brdf.glsl` →
  `hash_noise.glsl` → `sampling.glsl`+`packing.glsl` → `tonemap.glsl`
  → `color_space.glsl` → `shadow_filtering.glsl` → `ibl_sampling.glsl`.
- Tüketiciler TEKER TEKER taşınır; her taşıma kendi golden kanıtıyla
  commit'lenir. IBL bake çıktıları için marathon kuralı geçerli:
  **IBL bake'i yeniden ÜRETME** — `ibl_sampling.glsl` yalnız yeni
  tüketiciler + runtime yolları için.
- Yeni shader politikası (SL-E): her yeni shader modüller + ince entry
  point olarak doğar; gömülü-string'e yeni çekirdek fonksiyon eklemek
  review'da reddedilir.

## 3. Reddedilen alternatifler

- **Slang adopsiyonu (şimdi)**: GLSL korpusunun yeniden yazımı + taze
  X4-A kararının revizyonu; Phase-3'te yeniden değerlendirilecek
  (modül sınırları Slang-uyumlu çizilerek kapı açık tutuluyor).
- **Hibrit (yeni=Slang, eski=GLSL)**: çift toolchain/cache/hata yüzeyi;
  kütüphane-ürün netliğini bozar.
- **bgfx-tarzı kendi dialect'imiz**: dialect icadı uzun vadeli kısıt
  borcu (uniform/varying kısıtları emsali); reddedildi.
- **spirv-link ile runtime kompozisyon**: `Linkage` capability
  Vulkan'da geçersiz; teknik olarak imkânsız.
- **String-keyword variant (Unity multi_compile)**: tip güvenliği yok,
  stripping sonradan-takılan bant; typed domain baştan doğru.

## 4. Sonuçlar

- (+) Çoğaltılmış 9×Fresnel/8×oct/6×fBm çekirdekleri tek kaynağa iner;
  sessiz-drift sınıfı kapanır.
- (+) Kütüphane bağımsız ürün: `shaders/modules/` + C++ yüzeyi başka
  projede tek başına tüketilebilir (filamat emsali).
- (+) X4-A zinciriyle (GLSL→SPIR-V→HLSL→DXIL) ve X5 hot-reload'la
  bütünleşik; D3D12/Metal yolu için ek iş üretmez.
- (+) Variant disiplini derleme-zamanında (constexpr filter) —
  permutation patlamasına yapısal fren.
- (−) Includer + closure-hash CachedCompiler'a dokunur: cache epoch'u
  resolver KULLANILMAYAN yollar için korunur, kullanılan yollar için
  ilk derlemede soğuk başlar (kabul edildi).
- (−) GLSL preprocessor tabanlı kompozisyon Slang generics kadar
  ifadeli değil; karmaşık specialization ihtiyacı doğarsa Phase-3
  Slang değerlendirmesi öne çekilir.
- (→) SL-C implementasyon sırası: (1) IIncludeResolver seam +
  closure-hash [BLOCKING], (2) kütüphane iskeleti + math_common +
  brdf + tonemap tohum modülleri, (3) headless compile testi,
  (4) VariantDomain, (5) ilk tüketici migrasyonu (golden-pinned).
