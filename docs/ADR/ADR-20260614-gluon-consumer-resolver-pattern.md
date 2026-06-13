# ADR-20260614 — cd::gluon Consumer-Side Resolver Deseni (raster + compute genişlemesi)

- **Status**: Accepted (phase1164-1169 5 consumer migrasyonu kanonikleştirdi; bu ADR deseni normatifleştirir)
- **Date**: 2026-06-14
- **Branch**: dev (HEAD a68fe1f)
- **Deciders**: Cemal TATLI
- **Author**: architect (Material.cpp:160-169 + ddgi/DispatchPass.cpp compile_trace_module/compile_blend_module grep kanıtları + gluon CMakeLists DAG envanteri girdileriyle)
- **Related**:
  - `docs/ADR/ADR-20260612-shader-library-architecture.md` (SL-B: kütüphane mimarisi + §2.2 `IIncludeResolver` seam + §2.3 closure-hash + §2.6 migrasyon) — bu ADR o seam'in TÜKETİCİ-tarafı kullanım sözleşmesini normatifleştirir
  - `docs/ADR/ADR-20260612-shader-library-architecture.md` Addendum (2026-06-13) — material default-resolver köprüsü; ilk consumer örneği, bu ADR onu genelleştirir
  - `docs/ADR/ADR-20260613-gluon-module-manifest.md` (~35 modül manifesti + dalga zinciri) — yeni modüller bu desenle tüketilir
  - `docs/ADR/ADR-20260531-ddgi.md` (ikinci consumer'ın domeni: compute trace/blend yolu)
- **Scope guard**: Bu ADR YALNIZ tüketici-tarafı çözümleyici kullanım sözleşmesidir. gluon modül `.glsl` dosyalarına, `ModuleResolver` implementasyonuna veya consumer `.cpp`'lerine DOKUNMAZ (eşzamanlı başka ajan orada olabilir). Yeni consumer migrasyonu Developer tarafından yapılır; her migrasyon ADR-20260612 §2.5 test kapısı + §2.6 golden-pin disiplinine bağlıdır.

---

## 1. Bağlam

### 1.1 Problem

ADR-20260612 (SL-B) gluon kütüphanesini ve `cd::shader::ICompiler::compile`'a
eklenen opsiyonel `IIncludeResolver*` seam'ini tanımladı. Aynı ADR'nin
2026-06-13 Addendum'u, `cd::material`'ın gömülü GLSL'ini `#include
<cd/gluon/*.glsl>` ile derleyebilmesi için bir **default-resolver köprüsü**
kurdu: `Material::create` GLSL derleme dalında, `desc.include_resolver ==
nullptr` ise fonksiyon-lokal bir `cd::gluon::ModuleResolver` kullanılır.

phase1164-1169'da bu köprü tek bir kütüphaneye özgü bir hack olmaktan
çıktı: **5 consumer migrasyonu** gluon'u kanonik tek-kaynak yaptı ve
KRİTİK MİMARİ KARAR phase1169'da netleşti — desen RASTER yolundan
(`Material::create`, fragment/vertex GLSL) COMPUTE yoluna
(`DispatchPass` dispatch, compute GLSL) genişledi. Artık `#include
<cd/gluon/*.glsl>` ile derleyen HER kütüphane, kütüphane-tipinden
bağımsız aynı 3-adımlı sözleşmeyi izlemek ZORUNDA. Bu sözleşme bugün iki
kütüphanede dosya:satır kanıtıyla yaşıyor; ADR yokluğunda üçüncü
consumer'ı yazan Developer hangi varyantın doğru olduğunu (PUBLIC mi
PRIVATE mi, static resolver mi function-local mi, resolver'ı compile'dan
önce mi sonra mı kurmalı) tahmin etmek zorunda kalır — bu sessiz-drift
ve init-order tuzağı sınıfıdır.

### 1.2 Bugünkü iki consumer (dosya:satır kanıtı — CLAUDE.md §3 evidence-based)

**Consumer 1 — cd::material (RASTER yolu), `Material.cpp:160-169`:**

```cpp
// SL-D wave 3 (ADR-20260612 addendum): a null `desc.include_resolver`
// no longer means "#include is a compile error" — it bridges to the
// embedded cd::gluon module catalogue ... Function-local, NOT static ...
cd::gluon::ModuleResolver default_resolver {};
cd::shader::IIncludeResolver* const include_resolver =
    desc.include_resolver != nullptr ? desc.include_resolver : &default_resolver;
```

`MaterialDesc::include_resolver` tipi `cd::shader::IIncludeResolver*`
kalır; consumer-enjekte resolver varsa ona saygı, yoksa gömülü gluon
kataloğuna fallback. CMake kanıtı: ADR-20260612 Addendum'da `cd::material
→ cd::gluon` **PRIVATE_DEPS** (header gluon tipi adlandırmaz).

**Consumer 2 — cd::ddgi (COMPUTE yolu), `DispatchPass.cpp`:**

`compile_trace_module` (satır 108-112) ve `compile_blend_module` (satır
152-156) iki ayrı compute-derleme fonksiyonu, aynı deseni taşır:

```cpp
// Resolve #include <cd/gluon/*.glsl> against the embedded module catalogue.
// Function-local (NOT static): ModuleResolver is stateless + deterministic
// per ModuleRegistry.hpp, mirroring Material.cpp:167-169.
cd::gluon::ModuleResolver default_resolver {};
cd_desc.include_resolver = &default_resolver;   // compiler->compile ÇAĞRISINDAN ÖNCE
```

CMake kanıtı (`engine/render/ddgi/CMakeLists.txt:21`):
`PRIVATE_DEPS cd::shader cd::gluon`.

### 1.3 DAG kanıtı (cycle/layer kontrolü)

- gluon kendi PUBLIC_DEPS'i YALNIZ `cd::core` + `cd::shader`
  (`gluon/CMakeLists.txt:82-84`); başka hiçbir render kütüphanesini
  tanımaz. GLSL-modül seviyesinde modüller yalnız `math_common.glsl`
  include eder (ADR-20260612 §2.2).
- Karşılıklı `#include` SIFIR: `gluon/src` içinde yalnız `cd::gluon`
  namespace'i var, hiçbir consumer header'ı include edilmez
  (`ModuleRegistry.cpp` grep'i boş — yalnız self-namespace).
- gluon'un bağımlılığı (`cd::core` + `cd::shader`) zaten hem material'ın
  hem ddgi'nin closure'unda — yeni edge eklemez, yalnız mevcut alt-katmanı
  paylaşır. **Cycle yok, layer ihlali yok**: consumer → gluon → {core,
  shader}; gluon hiçbir consumer'ı tanımaz.

### 1.4 İlerideki consumer'lar

ADR-20260613 manifesti ~35 modülü 9 kategoriye yaydı; P0-A dalgası
(`packing`/`hash_noise`/`sampling`/`color_space`/`shadow_filtering`/
`ibl_sampling`) ile P1 BRDF aileleri yeni tüketiciler doğuracak. Bu
tüketiciler raster (material/post-fx fragment), compute (ddgi/restir/
gpu_particles dispatch), ve gelecekte mesh/task/RT-shader derleyen
kütüphaneler olacak. Her biri aynı çözümleyici sözleşmesine bağlanmalı —
aksi halde her consumer kendi ad-hoc köprüsünü icat eder (drift).

## 2. Karar

`#include <cd/gluon/*.glsl>` ile GLSL derleyen HER kütüphane, derleme
yapan HER fonksiyonda aşağıdaki **üç-adımlı kanonik sözleşmeyi** izler.
Sözleşme kütüphane tipinden (raster/compute/mesh/task/RT) BAĞIMSIZDIR;
yalnız "bir `cd::shader::CompileDesc` kurup `compiler->compile` çağıran"
olmak yeter.

### 2.1 (a) CMake: cd::gluon'a PRIVATE bağımlılık

Consumer `CMakeLists.txt`'inde gluon **`PRIVATE_DEPS`** olarak listelenir,
PUBLIC değil:

```cmake
# engine/render/ddgi/CMakeLists.txt:21 (kanonik)
  PRIVATE_DEPS cd::shader cd::gluon
```

Gerekçe: hiçbir consumer header'ı gluon TİPİ adlandırmaz —
`include_resolver` tipi `cd::shader::IIncludeResolver*`'dır (shader zaten
PUBLIC). gluon yalnız `.cpp` derleme dalında görünür. PRIVATE, DAG
görünürlüğünü minimumda tutar; static-lib link transitivity'sini CMake
`$<LINK_ONLY:>` zaten taşır. **Veto**: PUBLIC link gereksiz görünürlük
ve aşağı-akış consumer'ların gluon'a istemeden bağlanmasıdır → iade.

### 2.2 (b) Derleme fonksiyonunda function-local stateless ModuleResolver

Derleme fonksiyonu, `compiler->compile(...)` çağrısından ÖNCE
**fonksiyon-lokal** (NOT static, NOT üye, NOT global) bir
`cd::gluon::ModuleResolver` kurar ve `CompileDesc::include_resolver`'a
bağlar:

```cpp
cd::gluon::ModuleResolver default_resolver {};   // function-local
cd_desc.include_resolver = &default_resolver;    // compile ÇAĞRISINDAN ÖNCE
auto compiled = compiler->compile(cd_desc);      // resolver canlıyken
```

Gerekçe (ADR-20260612 §2.2 + ModuleRegistry.hpp kontratı):
`ModuleResolver` **stateless + deterministic** — çözüm yalnız sanal-yola
bağlı, requester/system-include ayrımını yoksayar, hiçbir mutable durum
tutmaz (`ModuleRegistry.hpp:42-51`: `ModuleResolver() noexcept = default`,
tek `resolve()` override'ı). Bu nedenle per-call inşa **ücretsizdir** ve
`static` GEREKMEZ. CLAUDE.md §7 "library'ler arası global state yasak"
kuralı KORUNUR. Yaşam-süresi (lifetime) invariant'ı: resolver, onu
referanslayan `compile()` çağrısı boyunca scope'ta kalmalı — bu yüzden
fonksiyon-lokal değişken `compile`'dan ÖNCE tanımlanır ve aynı scope'ta
yaşar. **Veto**: `static cd::gluon::ModuleResolver` veya üye/global
resolver → init-order + test-izolasyon riski → iade.

### 2.3 (c) Consumer-enjekte resolver'a saygı (fallback default)

`CompileDesc`/`MaterialDesc` zaten bir consumer-enjekte resolver
taşıyorsa (`desc.include_resolver != nullptr`), ona saygı gösterilir;
yalnız null ise gömülü gluon kataloğuna fallback yapılır:

```cpp
// Material.cpp:168-169 (kanonik — enjeksiyon noktası olan consumer'lar)
cd::shader::IIncludeResolver* const include_resolver =
    desc.include_resolver != nullptr ? desc.include_resolver : &default_resolver;
```

Bir consumer dışarıdan resolver enjeksiyonu KABUL ETMİYORSA (ddgi gibi —
`DispatchPass` resolver'ı dışarı açmaz), `(c)` koşulsuz default-atamaya
indirgenir (`cd_desc.include_resolver = &default_resolver;`). Her iki
biçim de aynı sözleşmenin örneğidir; ayrım yalnız "consumer dışarıya
resolver-enjeksiyon yüzeyi açıyor mu" sorusudur.

**Null semantiği** (ADR-20260612 Addendum'dan miras, normatif): null artık
"include = hata" değil "gömülü gluon kataloğu" demektir. `#include`
içermeyen kaynaklar bayt-aynı derlenir; closure boş kaldığından §2.3
gereği cache anahtarı DEĞİŞMEZ. "include görürse hata" isteyen bir tüketici
her isteği reddeden bir resolver enjekte ederek opt-out etmelidir.

### 2.4 Korunan disiplin kuralları (ADR-20260612'den miras)

- **§2.2 granülarite**: modüller yalnız `math_common.glsl` include eder;
  bu desen modül-içi bağımlılığı değiştirmez, yalnız modül→consumer
  derleme yolunu standartlaştırır.
- **§2.3 closure-hash**: resolver çözdüğü her `(yol, içerik)` çiftini
  CachedCompiler closure-hash'ine besler; consumer bunu manuel yönetmez —
  seam otomatik yapar. Yeni consumer cache doğruluğu için EK İŞ
  gerektirmez.
- **§2.5 test kapısı**: her yeni consumer migrasyonu headless compile-test
  + (tüketici varsa) golden-pin (chrome_probe / ddgi per-feature fixture).
- **§2.6 migrasyon + IBL kuralı**: tüketiciler TEKER TEKER; IBL bake
  YENİDEN ÜRETİLMEZ.
- **`#extension` head placement**: taşınan/derlenen her gömülü string
  head'ine `#extension GL_GOOGLE_include_directive : enable` eklenir
  (bindless-olayı önleyici kuralı — extension direktifi shader head'inde).

## 3. Reddedilen alternatifler

- **(i) gluon'u her consumer'a PUBLIC link + global static resolver**:
  PUBLIC link, gluon tipini header yüzeyine sızdırmadığı halde DAG
  görünürlüğünü kirletir ve aşağı-akış consumer'ları gluon'a transitif
  bağlar (gereksiz). Global `static cd::gluon::ModuleResolver` ise
  CLAUDE.md §7 "library'ler arası global state yasak" ile doğrudan çatışır;
  ayrıca init-order fiyaskosu (TU'lar-arası static başlatma sırası
  tanımsız) ve test izolasyon kaybı (paylaşılan durum → set sırasına
  bağlı sonuç) getirir. ModuleResolver stateless olduğundan global
  yapmanın HİÇBİR kazancı yok — per-call inşa zaten ücretsiz. RED.
- **(ii) preprocessed GLSL'i configure-time'da diske gömüp #include'ı
  tamamen kaçınmak** (her consumer için pre-flattened tek-dosya GLSL
  üretmek): include mekanizmasını tamamen by-pass eder ama (1) X5
  hot-reload kırılır — bir modül düzenlendiğinde flatten-cache bayatlar,
  configure yeniden koşana dek consumer eski GLSL derler (sessiz
  yanlış-render, [feedback_ondisk_shader_golden_staleness] sınıfı); (2)
  variant esnekliği kaybolur — flatten configure-time'da donar, runtime
  define-driven kompozisyon yapılamaz; (3) configure süresi N-consumer ×
  M-modül flatten ile patlar. RED — include seam + closure-hash bu
  problemleri zaten çözüyor.
- **(iii) tek merkezi shader-compile servisi** (tüm consumer'ların
  shader'ını derleyen process-wide bir `cd::shader::ShaderService`):
  KATMAN İHLALİ — alt-katman shader servisi, üst-katman consumer'ların
  (material/ddgi/post-fx) domen-özgü `CompileDesc` kurulumunu (entry
  point, target env, source-name, stage seçimi) bilmek zorunda kalır;
  ddgi'nin `needs_tlas ? kDdgiTraceCS : kDdgiTraceSmokeCS` gibi consumer
  iç-mantığını merkeze taşımak God-service yaratır. Ayrıca global state
  (servis singleton) §7 ihlali ve test-izolasyon kaybı getirir. RED —
  derleme kararı consumer'a ait; gluon yalnız include ÇÖZÜMÜNÜ sağlar.
- **(iv) `MaterialDesc`/`CompileDesc`'ten resolver alanını kaldırıp her
  zaman gömülü gluon'a zorlamak**: consumer-enjekte resolver yeteneğini
  (örn. test-mock, harici proje kendi modül kataloğu) öldürür;
  standalone-product hedefiyle (gluon başka projede tüketilebilir)
  çatışır. RED — fallback'li opsiyonel enjeksiyon (§2.3) doğru esneklik.

## 4. Sonuçlar

- (+) İki kütüphane (raster `cd::material` + compute `cd::ddgi`) bugün
  bayt-aynı sözleşmeyi taşıyor; üçüncü consumer'ı yazan Developer'ın
  tahmin etmesi gereken hiçbir karar kalmadı — CMake PRIVATE, function-local
  stateless resolver, compile-öncesi atama, null→fallback semantiği
  normatif.
- (+) Raster→compute genişlemesi kanıtladı: gluon artık "tüm shader-derleyen
  consumer tiplerine hizmet eden" tek-kaynak. Gelecekte compute/mesh/task/
  RT shader derleyen yeni kütüphaneler aynı 3-adımı izler.
- (+) `kSampleFS` gibi henüz-derlenmeyen gömülü string'ler (consumer
  doğmadığı için bugün compile-path'i olmayan modül-tüketen kaynaklar)
  ilgili consumer doğunca otomatik compile-path kazanır — desen onları
  "ücretsiz" kapsar, ek mimari karar gerektirmez.
- (+) DAG temiz kalır: her yeni edge consumer → gluon (PRIVATE);
  gluon → {core, shader} alt-katmanı paylaşılır; cycle imkânsız (gluon
  hiçbir consumer'ı tanımaz, ModuleRegistry.cpp self-namespace).
- (+) Cache doğruluğu otomatik: resolver closure-hash'i §2.3 seam'inde
  beslediğinden yeni consumer cache-key yönetimi için EK İŞ yapmaz.
- (−) Her derleme fonksiyonunda 2 satırlık boilerplate tekrarı (resolver
  inşası + atama). Kabul: function-local + stateless gereği bu, paylaşılan
  bir helper'a çıkarılamaz (global state'e geri düşmeden) — tekrar,
  global-state'siz doğruluğun bedelidir. İstenirse consumer-içi (gluon
  DIŞI, consumer-lokal) ince bir `make_default_compile_desc` yardımcısı
  yazılabilir; ancak resolver'ın yaşam-süresi çağıran scope'ta kalmalı
  (helper resolver'ı döndüremez — dangling).
- (−) "include görürsem hata ver" isteyen tüketici artık explicit opt-out
  (reddeden resolver enjekte) etmeli. ADR-20260612 Addendum'da dökümante
  edildi; bu ADR onaylar.
- (→) Developer sırası: ADR-20260613 manifest dalgaları (P0-A: packing →
  hash_noise → ...) ile yeni modüller doğdukça, her yeni tüketici
  migrasyonu bu 3-adımlı sözleşmeyi izler + §2.5 headless compile-test +
  §2.6 golden-pin kapısından geçer; commit başına bir consumer.

---

## Varsayımlar

- `cd::gluon::ModuleResolver`'ın stateless + deterministic invariant'ı
  `ModuleRegistry.hpp:38-51` kontratına dayanır (default ctor noexcept,
  tek `resolve()` override, mutable üye yok); bu invariant kırılırsa
  (resolver durum tutmaya başlarsa) function-local kararı yeniden
  değerlendirilmeli — ama o durumda doğru cevap resolver'ı stateless'a
  geri çekmektir, global yapmak değil.
- ddgi `DispatchPass`'in dışarıya resolver-enjeksiyon yüzeyi açmadığı
  varsayımı `DispatchPass.cpp` grep'ine dayanır (yalnız default-atama,
  `!= nullptr` dalı yok); ileride ddgi harici modül-kataloğu desteklerse
  §2.3'ün tam üçlü-operatör biçimine yükseltilir (geriye-uyumlu).
- CachedCompiler closure-hash beslemesinin seam-otomatik olduğu varsayımı
  ADR-20260612 §2.3 kararına dayanır; consumer'ın manuel cache-key
  yönetmediği bu ADR kapsamında verili kabul edilir.

## Sonraki

- Implementasyonu **Developer** yapar: ADR-20260613 P0-A dalgasının her
  yeni modül-tüketicisi (örn. `packing.glsl`'i tüketen ilk compute
  consumer) bu 3-adımlı sözleşmeyi izler.
- Veto noktaları (Developer iadesi): (1) consumer gluon'u PUBLIC linklerse;
  (2) `static`/üye/global `ModuleResolver` kullanılırsa; (3) resolver
  `compiler->compile` çağrısından SONRA atanırsa (dangling/etkisiz);
  (4) merkezi shader-compile servisi önerilirse (katman ihlali);
  (5) IBL bake yeniden üretilirse (§2.6).
- gluon → consumer karşılıklı `#include` ASLA oluşmamalı; her yeni
  consumer eklendiğinde `Grep` ile gluon/src'nin self-namespace kaldığı
  doğrulanır (cycle nöbeti).
