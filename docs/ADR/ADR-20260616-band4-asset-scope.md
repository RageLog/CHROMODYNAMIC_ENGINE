# ADR-20260616 — ALL-MODULES-TO-100 BAND 4 / ASSET Kapsam Mührü (4 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 68402ec)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 4 asset subset close-out — v1-functional SEAL + promote-on-need trigger + real-untested-branch edge-test-topup)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 4 listesi: asset::shader_cache 65
    "compiler integration (cache-only today) or seal", asset::vfx_authoring 65
    "runtime simulation tie-in", asset::streamer_pool 65 "depends on the 3
    streamers' real decode (Band 6) — re-verify after", asset::validator 60 "deep
    structural/schema parse (header-sniff only) or seal shallow scope";
    §"What 100% MEANS" honest-rule: her boşluk ya (a) IMPLEMENTED+test ya da (b)
    bir paragraflık kapsam-ADR ile SEALED, üçüncü "TODO/placeholder" durumu yok)
  - `docs/PROJECT_COMPLETION_STATUS.md` §2 (asset baseline: shader_cache 65 "real
    binary cache format (CDSC magic/version/timestamps) + serialize/lookup; 7
    gtests; cache-only by design, no compiler integration"; vfx_authoring 65 "JSON
    authoring/round-trip for VFX/particle descriptors; 10 gtests; no runtime
    simulation tie-in"; streamer_pool 65 "priority-weighted round-robin token
    dispatch (Bresenham deficit); 8 gtests; governs streamers whose payloads are
    stubs"; validator 60 "magic-header sniff (PNG/JPG/KTX2/DDS/HDR/GLB/WAV/OGG) →
    Severity issues; 16 gtests; shallow by design (no deep schema parse)")
  - kardeş band-mühür ADR'ları (`ADR-20260616-band4-render-features-scope.md`,
    `ADR-20260616-band3-asset-scope.md`) — aynı şablon: impl-the-clean-gap +
    seal-the-rest-as-vN + promote-on-need + targeted-edge-topup
- **Scope guard**: Bu ADR YALNIZ kapsam-karar + gerçek-test-edilmemiş-dal edge-test
  top-up dokümanıdır. Mühürlenen maddeler "deferred-by-design"dır — her biri
  "ihtiyaç doğunca promote" çıkış kapısı taşır. Dört kütüphane de ÇALIŞAN,
  test-dolu bir v1 fonksiyonel katman taşır; baseline'da "no X" diye listelenen
  her madde bir TÜKETİCİ/KOMŞU-KATMAN sorumluluğudur (compiler entegrasyonu →
  cd::shader; runtime sim → cd::world::particle_system; decode → Band-6 streamer;
  deep schema parse → loader). Bu yüzden dürüst terminal durum: v1-fonksiyoneli
  MÜHÜRLEMEK + gerçek dokunulmamış dalları test etmek. Sadece
  `engine/asset/{shader_cache,vfx_authoring,streamer_pool,validator}/` test dosyaları
  + bu ADR'a DOKUNULDU. 3 streamer (texture/scene/audio, Band 6) DEĞİL, asset
  umbrella / texture_compress / material_authoring DEĞİL, samples/ / hello_* / %
  docs DEĞİL. Chrome golden BYTE-IDENTICAL (fixture #5 cmp baseline).

---

## 1. Bağlam

BAND 4 (60–69%) asset alt-kümesi 4 kütüphaneyi 100%'e taşır: cd::asset::shader_cache
(65), cd::asset::vfx_authoring (65), cd::asset::streamer_pool (65),
cd::asset::validator (60). Dördü de gerçek bir `.cpp` + dokümante ikili/JSON format
+ adanmış gtest binary taşır — hiçbiri kNotImplemented/skeleton DEĞİLDİR. Baseline'da
her birine iliştirilen "no X" notu (no compiler integration / no runtime sim tie-in /
payloads are Band-6 stubs / no deep schema parse), kütüphanenin KENDİ kapsamının
DIŞINDA, bir komşu katmanın veya tüketicinin sorumluluğundadır:

- **shader_cache** depolama katmanıdır (SPIR-V kelimeleri opak payload); GLSL→SPIR-V
  derleme `cd::shader::CachedCompiler`'a aittir. Cache ↔ compiler sınırı kasıtlı.
- **vfx_authoring** yazma/doğrulama/round-trip katmanıdır; runtime parçacık
  simülasyonu `cd::world::particle_system` tüketicisine aittir.
- **streamer_pool** decode-AGNOSTİK bir zamanlayıcıdır (priority-weighted round-robin,
  Bresenham deficit); payload decode'u her bir streamer'ın (Band 6) işidir.
- **validator** sığ-by-design bir magic-header sniff'idir; derin yapısal/şema parse'ı
  loader'ın işidir.

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net) VEYA (b) bir
paragraflık kapsam-ADR ile SEALED (promote-on-need). Bu pass'in bölünme çizgisi:
**v1-fonksiyonel SEAL + gerçek-test-edilmemiş-dal edge-topup**.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::asset::shader_cache — 65 → 100  [SEALED cache-format-v1 + edge-topup]

- **Bağlam**: `ShaderCache.cpp` (254 src) gerçek bir ikili cache dosya formatı taşır:
  8-bayt `CDSC\x00\x01\x00\x00` magic (major=1/minor=0) + little-endian entry-count +
  per-entry `[source_hash][entry_point][stage][spec_const_hash][spirv words][cached_at_ms]`.
  put/get/save_to_disk/load_from_disk/clear/entry_count + FNV-1a `std::hash<ShaderKey>`.
  Cache-only by design: glslang/ICompiler bağımlılığı YOK.
- **Karar (SEAL cache-format-v1; compiler entegrasyonu = promote-on-need)**: Cache
  DEPOLAMA katmanıdır; SPIR-V kelimelerini opak payload olarak tutar. GLSL→SPIR-V
  derleme `cd::shader::CachedCompiler`'a (render/shader tier) aittir — cache, bir
  loader pre-compiled SPIR-V (.pak/.cdtex) aldığında onu glslang fan-out'u olmadan
  diske yazıp okumak için vardır. **Cache ↔ compiler sınırı kasıtlı katman ayrımıdır.**
  Compiler entegrasyonu (cache'i ICompiler decorator'ı olarak sarmak) **promote-on-need**:
  bir tüketici source-to-SPIR-V'yi cache'lemek isterse `cd::shader::CachedCompiler`
  zaten o işi yapar — bu cache asset-tier sidecar'ı olarak ayrı kalır.
- **Edge-topup (gerçek dokunulmamış dal)**: load_from_disk'in reddetme yolları
  test-edilmemişti. +2 gtest:
  - `VersionMismatchRejectedAndDoesNotClobber` — magic'in version baytı bump edilirse
    load reddeder ve önceden yüklü entry'leri EZMEZ (version-mismatch invalidation).
  - `CorruptAndTruncatedHeaderRejected` — garbage magic / entry-count'tan önce biten
    truncated header / var-olmayan path: üçü de `false` döner, kısmi load yok, throw yok.
- **Çıkış durumu**: cache-format-v1 SEALED + edge-topup; 7 → 9 gtest. PASS.

### 2.2 cd::asset::vfx_authoring — 65 → 100  [SEALED authoring-roundtrip-v1 + edge-topup]

- **Bağlam**: `VfxAuthoring.cpp` (351 src) `cd::asset::json` üzerinden gerçek
  JSON authoring/round-trip taşır: `AuthoredVfx` POD (emitter/life/colour/size/
  velocity/texture) ↔ `.vfx.json`; save_to_json/load_from_json/validate_authored +
  4 Source-2-archetype preset (jump_dust/muzzle_flash/fire_smoke/water_splash).
  Required-field (id/emitter_kind) zorlama + optional-field default fallback +
  range/NaN doğrulama.
- **Karar (SEAL authoring-roundtrip-v1; runtime sim tie-in = promote-on-need)**:
  Bu kütüphane YAZMA + DOĞRULAMA katmanıdır; tasarımcının hand-edit ettiği `.vfx.json`'ı
  `AuthoredVfx`'e çevirip pipeline-readiness doğrular. Runtime parçacık simülasyonu
  (AuthoredVfx → canlı emitter spawn) `cd::world::particle_system` TÜKETİCİSİNE aittir —
  authoring katmanı rhi/particle bağımlılığı taşımaz (asset tier'da kalır).
  Runtime sim tie-in **promote-on-need**: hot-reload yolu authoring çıktısını
  particle_system'e bağladığında, o köprü tüketici tarafında kurulur.
- **Edge-topup (gerçek dokunulmamış dal)**: malformed-JSON ve unknown-key yolları
  test-edilmemişti (mevcut T8/T9 yalnız eksik-required-field, T10 yalnız eksik-optional).
  +2 gtest:
  - `T11_LoadMalformedJsonReturnsNullopt` — sonlanmamış obje (parse error) → nullopt,
    exception sızmaz.
  - `T12_LoadUnknownKeysAreIgnored` — bilinmeyen/fazla key'ler (author/editor_revision/
    tags/nested) tolere edilir; bilinen alanlar yüklenir, default'lar bozulmaz.
- **Çıkış durumu**: authoring-roundtrip-v1 SEALED + edge-topup; 10 → 12 gtest. PASS.

### 2.3 cd::asset::streamer_pool — 65 → 100  [SEALED dispatch-scheduler-v1 + edge-topup]

- **Bağlam**: `StreamerPool.cpp` (175 src) gerçek bir priority-weighted round-robin
  token dispatcher'ı taşır: aktif streamer'lar (attached + pending>0) toplam ağırlığa
  göre tartılır, Bresenham deficit accumulator her tick'te tam `max_concurrent_loads`
  token'ı sürüklenmesiz dağıtır. attach_scene/texture/audio/shader (non-owning) +
  configure (deficit reset) + tick + birleşik stats.
- **Karar (SEAL dispatch-scheduler-v1; streamer DECODE = Band 6)**: Pool'un işi
  ZAMANLAMA'dır ve bu TAMDIR + doğru. Pool decode-AGNOSTİK'tir: scene/texture/audio
  streamer'ların payload decode'u (gerçek glTF/cdtex/WAV) Band-6 işidir; pool sadece
  hangi streamer'a kaç token verileceğine karar verir. Baseline'ın "payloads are
  stubs" notu pool'un değil, alt-streamer'ların durumudur. Band-6 decode'lar
  bağlandığında pool'un dispatch davranışı DEĞİŞMEZ (token sayımı pending_count'a
  bağlı, payload içeriğine değil) → re-verify, re-implement değil. **Pool decode-
  agnostik bir zamanlayıcıdır; decode promote-on-need Band-6'da.**
- **Edge-topup (gerçek dokunulmamış dal)**: zero-weight / single-streamer /
  fairness-no-starvation yolları test-edilmemişti (mevcut T3 yalnız iki-pozitif-ağırlık
  oranı). +3 gtest:
  - `ZeroWeightStreamerNeverScheduled` — priority=0 streamer KASITLI hiç schedule
    edilmez (weight 0 == "dispatch etme") + pozitif-ağırlık streamer normal drain olur
    + total_weight=0 dalında sıfıra-bölme yok.
  - `SingleStreamerGetsAllTokens` — tek aktif pozitif-ağırlık streamer her token'ı alır
    (tek tick'te 4 item drain).
  - `LowPriorityStreamerNotStarved` — scene(12) vs audio(1): 12x düşük ağırlıklı audio,
    Bresenham deficit sayesinde sınırlı tick içinde TAM drain olur (kalıcı açlık yok).
- **Çıkış durumu**: dispatch-scheduler-v1 SEALED + edge-topup; 8 → 11 gtest. PASS.
  Not: Band-6 streamer decode'ları indikten sonra bu pool yeniden DOĞRULANMALI
  (re-verify, davranış değişmeyeceği beklenir).

### 2.4 cd::asset::validator — 60 → 100  [SEALED header-sniff-v1 + edge-topup]

- **Bağlam**: `Validator.cpp` (250 src) magic-header sniff taşır: texture
  (PNG/JPG/KTX2/DDS/HDR), glTF (GLB magic + version word / JSON `{`), audio
  (RIFF/WAVE / OggS) → `Severity{kInfo,kWarning,kError}` issue listesi + konfigüre
  edilebilir `ValidatorPolicy` (max_texture_pixels conservative bound).
- **Karar (SEAL header-sniff-v1; deep structural/schema parse = promote-on-need)**:
  Validator KASITLI sığdır — yükleme-ÖNCESİ ucuz bir "bu blob hiç doğru formatta mı?"
  geçidi. Derin yapısal/şema doğrulama (KTX2 mip chain, glTF accessor/buffer-view
  tutarlılığı, WAV fmt-chunk parse, mesh topology) loader'ın KENDİ parse adımına
  aittir — validator onu duplike etmez, sadece confusing rhi-level hatadan önce
  call-site'ta erken-fail sağlar. Deep parse **promote-on-need**: bir loader
  semantik doğrulama isterse onu kendi parse'ında (zaten blob'u açtığı yerde) yapar.
- **Edge-topup (gerçek dokunulmamış dal)**: truncated-header (magic var ama header
  tam değil) yolları test-edilmemişti (mevcut T1 yalnız boş, T3 yalnız tamamen-yanlış
  magic). +2 gtest:
  - `TruncatedGlbHeaderIsError` — glTF magic var ama <12 bayt → "GLB too small" kError.
  - `TruncatedWavAndTinyBlobAreErrors` — RIFF var ama <12 bayt (WAVE tag'ine yer yok)
    → kError; 1-baytlık unknown-magic blob (texture+audio yolu) → out-of-bounds
    okuma OLMADAN kError.
- **Çıkış durumu**: header-sniff-v1 SEALED + edge-topup; 16 → 18 gtest. PASS.

---

## 3. Reddedilen Alternatifler

- **Compiler entegrasyonunu shader_cache'e implement etmek**: Reddedildi —
  `cd::shader::CachedCompiler` zaten source-to-SPIR-V cache decorator'ıdır; asset-tier
  cache'i glslang'a bağlamak katman DAG'ını kırar (asset → shader bağımlılığı) ve
  iki yerde aynı sorumluluğu duplike eder. Sınır kasıtlı.
- **vfx_authoring'e runtime sim eklemek**: Reddedildi — particle simülasyonu rhi/world
  tier'ında (`cd::world::particle_system`); authoring kütüphanesini oraya bağlamak
  asset-tier saflığını (no rhi dep) bozar. Tie-in tüketici tarafında.
- **streamer_pool'u Band-6 decode'a kadar BLOCKED bırakmak**: Reddedildi — pool'un işi
  (zamanlama) decode'dan bağımsız ve TAM; decode'a bağlamak yanlış katmanı 100%'ten
  saymaktır. Pool şimdi mühürlenir, decode indiğinde re-VERIFY edilir.
- **validator'a deep schema parse eklemek**: Reddedildi — bu loader'ın parse adımını
  duplike eder; validator'ın değeri ucuz erken-geçit olmasıdır. Deep parse loader'a ait.
- **Hiç test eklememek (saf SEAL)**: Reddedildi — honest-rule "fail-on-revert" adanmış
  test ister; dördünde de gerçek test-edilmemiş reddetme/edge dalları vardı → topup
  hem coverage'ı hem de mühürlenen davranışın regresyon-kilidini sağlar.

---

## 4. Sonuçlar

- **Olumlu**: Dört asset kütüphanesi de honest-rule terminal durumuna ulaştı
  (v1-fonksiyonel SEALED + gerçek dal test-kilitli). +9 yeni gtest (shader_cache +2,
  vfx_authoring +2, streamer_pool +3, validator +2): 41 → 50 case. Katman sınırları
  (cache↔compiler, authoring↔sim, scheduler↔decode, sniff↔deep-parse) dokümante edildi.
- **Olumsuz / borç**: Dört "promote-on-need" madde açık kaldı — her biri bir TÜKETİCİ
  veya komşu-katman iş bağlandığında tetiklenir; bunlar bu kütüphanelerin değil, o
  entegrasyonların kapsamıdır. streamer_pool Band-6 decode sonrası re-verify gerektirir.
- **Nötr**: README banner'ları çalışan path'leri doğru tanıtıyordu (yanlış "stub"
  banner'ı yok) → banner düzeltmesi gerekmedi.

---

## 5. Doğrulama (kanıt)

- **Build**: `cmake --build --preset ninja-debug --target cd_test_asset_shader_cache
  cd_test_asset_vfx_authoring cd_test_asset_streamer_pool cd_test_asset_validator` →
  TEMİZ (-Werror, 0 yeni clang-tidy WAE class; 8/9 compile+link, 0 hata/uyarı).
- **Test**: `ctest --preset ninja-debug -R "shader_cache|vfx_authoring|streamer_pool|
  validator" --output-on-failure` → 4/4 PASS (0.30 s). Per-binary case: shader_cache 9,
  streamer_pool 11, validator 18, vfx_authoring 12. 9 yeni case ayrıca tek tek
  çalıştırılıp OK.
- **Golden**: hello_engine fixture #5 → `b4a.png`, `cmp` baseline → **BYTE-IDENTICAL**;
  b4a.png silindi.
- **Kapsam**: Yalnız `engine/asset/{shader_cache,vfx_authoring,streamer_pool,validator}/
  tests/*.cpp` + bu ADR dokunuldu. 3 streamer / umbrella / samples / hello_* / % docs
  DOKUNULMADI.
