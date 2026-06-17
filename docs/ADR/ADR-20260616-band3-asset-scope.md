# ADR-20260616 — ALL-MODULES-TO-100 BAND 3 / asset+tooling subset Kapsam Mührü (3 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD a71d570)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 3 asset+tooling subset close-out — LAST Band-3 group; seal-the-deferred-by-design + edge-test-deepening + promote-on-need pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 3 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; bu alt-kümenin named
    gap'leri: texture_compress "real BC1 endpoint fit (naive today) + BC3/BC5",
    material_authoring "schema versioning + validation depth", texture_synth
    "(tooling) seal as complete fixture tool" §BAND 3 tablo)
  - `docs/PROJECT_COMPLETION_STATUS.md` §2 (engine/asset+tooling: texture_compress
    70 "partial … BC1 naive endpoints, BC3/BC5 future", material_authoring 70
    "partial … no schema versioning/validation depth", texture_synth 70 "tooling …
    narrow scope")
  - `docs/ADR/ADR-20260616-band3-ui-aisquad-scope.md` +
    `docs/ADR/ADR-20260616-band3-world-scope.md` +
    `docs/ADR/ADR-20260616-band3-foundation-scope.md` (kardeş band-mühür
    ADR'ları, aynı şablon: impl-the-small-clean-named-gap + seal-the-rest +
    promote-on-need; bu ADR aynı bölünme çizgisini bu son Band-3 grubuna uygular)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Bu pass'te değişiklikler YALNIZ
  engine/asset/texture_compress/ + engine/asset/material_authoring/ +
  engine/texture_synth/ test'leri + bu docs/ADR/ dosyası altında. asset
  streamer'lar (texture/scene/audio) + shader_cache + vfx_authoring + validator +
  streamer_pool + runtime (alt-band'ler) + asset umbrella (done) + samples/ +
  hello_* + % docs'a DOKUNULMADI. Bu pass yalnız test-dosyalarına ekleme yaptı
  (header/src davranışı + CMake link surface DEĞİŞMEDİ; yalnız texture_synth test
  CMakeLists'e ikinci test .cpp dosyası aynı target'a eklendi) → public API
  yüzeyi sabit, ABI sabit, hello_engine render yolu etkilenmez.

---

## 1. Bağlam

BAND 3 (70–79%) asset+tooling alt-kümesi — bandın SON grubu — 3 kütüphaneyi
100%'e taşır: cd::asset::texture_compress (803 src; gerçek BC1 encode/decode/PSNR
+ opt-in BC7 via bc7enc_rdo + opt-in ASTC via ARM astc-encoder, feature-flag
gated; BC1 endpoint'leri naive min/max, BC3/BC5 future) + cd::asset::
material_authoring (322 src; AuthoredMaterial JSON round-trip + range
validation + presets; no schema versioning / deep validation) + cd::texture_synth
(header-only Noise + Earth bakers; deterministik procedural fixture üretimi,
hello_engine'in harici asset yokken kullandığı tooling lib).

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu alt-kümenin üç named gap'i de aynı KARAKTERdedir: her biri gerçek,
charter-complete bir v1 KATMAN + onun ÜSTÜNDE bir kalite/algoritma/şema
GENİŞLEMESİ (BC1 PCA endpoint fit + BC3/BC5 formatları, schema versioning + deep
validation, tooling-completeness). Bu genişlemeler doğal "promote-on-need"
adaylarıdır: ya gerçek bir tüketici (mobil/RDO sıkıştırma hedefi, çok-sürümlü
authoring schema migration, yeni fixture tipi) yokken üretmek dead-code/over-
engineering olur, ya da zaten dürüst bir v1 ile karşılanmıştır (BC1 fonksiyonel
+ opt-in real encoder'lar gerçek; texture_synth tooling-charter'ı tam). Dolayısıyla
bu pass'in bölünme çizgisi: **her v1 katmanını SEAL et + gerçekten test-edilmemiş
edge/karar dallarını IMPLEMENTED(test) yap + genişlemeyi precise trigger ile
promote-on-need ayır.** Yapay yüzey-şişirme (don't pad) ya da hedefsiz bir BC1
PCA / şema-migration motorunu bu pass'te kurmak (tüketici yok + scope dışı) RED.

Kümülatif +17 yeni test (5 texture_compress + 5 material_authoring + 7
texture_synth Earth), hepsi edge/contract + anti-flakiness (sleep_for YOK;
deterministik sentetik girdi). Hiçbir public header/src davranışı değişmedi.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::asset::texture_compress (asset/texture_compress) — 70 → 100  [BC1-naive-v1 + opt-in-BC7/ASTC SEALED; IMPLEMENTED(test)]

- **Bağlam**: 803 src; BC1 encode (per-block min/max endpoint picker → RGB565,
  c0>=c1 normalisation, 4-renk palette, 2-bit indices) + BC1 decode (her iki mod:
  c0>c1 4-renk opaque VE c0<=c1 3-renk+transparent) + analyze() (BC1 decode →
  RMSE/ratio) + mip chain (2×2 box downsample) + opt-in BC7 (bc7enc_rdo,
  `CD_TC_HAS_BC7ENC`, FetchContent) + opt-in ASTC 4×4/8×8 (ARM astc-encoder,
  `CD_TC_HAS_ASTCENC`). 14 gtest (BC7/ASTC-gated SKIP-clean). Named gap (roadmap):
  "real BC1 endpoint fit (naive today) + BC3/BC5".
- **Karar**: **BC1-naive-v1 + opt-in-BC7/ASTC tasarımı SEALED**; BC1 PCA endpoint
  fit + BC3/BC5 **promote-on-need**. BC1 yolu FONKSİYONELdir (doğru-şekilli
  8-byte/blok çıktı, hem-encode-hem-decode round-trip, solid blokta lossless,
  ratio 8:1) ve opt-in encoder'lar (BC7 via bc7enc_rdo, ASTC via ARM astcenc)
  GERÇEK, production-grade kütüphanelerdir — "kaliteli endpoint fit" zaten BC7/ASTC
  yolunda mevcuttur. BC1'in naive min/max endpoint'i, opt-in real encoder'lar
  varken, dürüst bir baseline'dır: yüksek-kalite RGB sıkıştırma isteyen tüketici
  BC7'ye geçer. Gerçekten test-edilmemiş 5 BC1/analyze dalı **IMPLEMENTED(test)**
  (14→19 gtest): `T15_Bc3Bc5ReturnNullopt` (encode()'in BC3/BC5 fall-through'u —
  hiç koşulmamıştı; her test BC1/BC7/ASTC hedefliyordu), `T16_AnalyzeRejectsNonBc1Format`
  (analyze()'in `format != kBC1` guard'ı — prior analyze testleri daima BC1
  geçiyordu), `T17_AnalyzeRejectsTruncatedBlob` (`blob.size() < mip0_bytes` guard —
  OOB read koruması, hiç test-edilmemişti), `T18_AnalyzeRmseZeroForSolidWhite`
  (single-colour fast-path siyah-dışı bir renk için de exact reconstruct —
  T10 yalnız siyah 0,0,0'ı kapsıyordu), `T19_DecodeThreeColorTransparentModeBranch`
  (decoder'ın c0<=c1 3-renk+transparent modu — encoder daima c0>=c1 emit ettiği
  için encode() üzerinden ASLA ulaşılamayan dal; el-yapımı c0<c1 blob analyze()
  ile decode edilerek tek doğru yoldan exercise edildi).
- **Gerekçe**: texture_compress'in gerçek değeri, BC1 fonksiyonel baseline +
  opt-in production encoder (BC7/ASTC) mimarisidir. Named gap'in "real BC1 endpoint
  fit" yarısı düşük-leverage'dır: kaliteli endpoint fit isteyen tüketici zaten
  BC7'yi (gerçek RDO encoder) açar; BC1'e PCA endpoint search eklemek, BC7 varken,
  marjinal kalite kazancı için ek kod-yüzeyi ve bakım yükü demektir. "BC3/BC5"
  yarısı ise gerçek bir alpha-DXT5 / normal-map-RGTC tüketicisi belirene kadar
  dead-code'dur (bc7enc_rdo'nun rgbcx.h'i ileride bunları da sağlayabilir —
  Sprint-3 notu kaynakta). honest-rule (b) promote-on-need tam bunun için.
  BC1/analyze edge testleri (BC3/BC5-nullopt / non-BC1-reject / truncated-reject /
  solid-white-lossless / 3-color-decode-mode) küçük+net+deterministik — honest-rule
  (a); özellikle T19 daha önce HİÇBİR yoldan ulaşılamayan decode dalını kilitler
  (BC1 dosya formatı 3-renk modu içeren harici .dds yüklenirse decode doğru
  davranmalı) — artık fail-on-revert.
- **Promote-on-need**: BC1 PCA-based endpoint search (Castaño/ryg-style covariance
  axis fit) + BC3 (DXT5 alpha) + BC5 (RGTC2 normal-map) encoder'ları — gerçek bir
  alpha-block / normal-map BC ihtiyacı + bc7enc_rdo rgbcx.h entegrasyonu + ayrı
  ADR ile gelir; encode/analyze imza yüzeyi (Format enum + EncodeOptions +
  CompressedTexture + CompressionStats) ve BC1 blok byte-layout'u sabit kalır.

### 2.2 cd::asset::material_authoring (asset/material_authoring) — 70 → 100  [authoring-roundtrip-v1 SEALED; IMPLEMENTED(test)]

- **Bağlam**: 322 src; AuthoredMaterial POD (id + base_color + metallic +
  roughness + 3 texture path + AlphaMode + alpha_cutoff) + save_to_json (pretty
  serialize via cd::asset::json) + load_from_json (required-id + optional-field-
  defaults + AlphaMode token map) + validate_authored (range/NaN/MASK-cutoff/
  BLEND-info diagnostics) + AuthoringDefaults (dielectric/metal/cloth presetleri).
  8 gtest. Named gap (roadmap): "schema versioning + validation depth".
- **Karar**: **v1-authoring-roundtrip + range-validation tasarımı SEALED**; schema
  versioning + deep validation **promote-on-need**. Round-trip (save → load,
  alan-bazında eşit) + required-id guard + optional-default fallback + range/NaN/
  MASK/BLEND validation charter-complete'tir: "moment" (designer typo'sunu —
  out-of-range scalar / missing id — pipeline öncesi yakala) zaten sağlanmış.
  Gerçekten test-edilmemiş 5 I/O+validation dalı **IMPLEMENTED(test)** (8→13
  gtest): `MalformedJsonReturnsNullopt` (load_from_json'ın json::load parse-fail
  yolu — her prior load testi geçerli JSON besliyordu), `IdWrongTypeReturnsNullopt`
  (required-id guard'ın `!is_string()` yarısı — T7 yalnız ABSENT id'yi kapsıyordu,
  burada key var ama number), `SaveToUnopenablePathReturnsFalse` (save_to_json'ın
  `!ofs.is_open()` dalı — T2 yalnız success yolunu sürüyordu), `NanScalarIsError`
  (range-check'lerin std::isnan limbi — T4 yalnız finite 1.5/-0.1 kullanıyordu),
  `OutOfRangeBaseColorIsError` (validation check #2 base_color in [0,1] — hiçbir
  prior test bir renk kanalını birim-aralık dışına sürmüyordu).
- **Gerekçe**: material_authoring'in gerçek değeri, asset-tier authoring round-trip
  + erken-yakalama validation'dır ve o charter-complete. Named gap "schema
  versioning + validation depth": (a) schema versioning gerçek bir çok-sürümlü
  migration ihtiyacı belirene kadar erken-optimizasyondur — bugün tek bir v1
  schema var, version-field eklemek (tüketicisi olmadan) ölü-alandır; cd::game::
  save lib'i zaten forward-compat versioning'i kanıtlanmış bir pattern olarak
  taşıyor, ihtiyaç doğunca o pattern buraya promote edilir. (b) "validation depth"
  ise mevcut range/NaN/MASK/BLEND diagnostikleri ile zaten pipeline-readiness
  garantisini sağlar; daha derin yapısal doğrulama (texture-path-exists, glTF-
  cross-ref) asset::validator'ın (ayrı lib, alt-band) işidir, burada katman-karışımı
  olur. honest-rule (b) promote-on-need tam bunun için. I/O+validation edge
  testleri (malformed/wrong-type-id/unopenable-save/NaN/out-of-range-color)
  küçük+net+deterministik — honest-rule (a); load/save/validate invariant'ları
  artık fail-on-revert kilitli.
- **Promote-on-need**: JSON şema `"version"` alanı + version-gated migration
  (cd::game::save forward-compat pattern'i) + deep structural validation (texture-
  path resolution, cross-asset ref check) — gerçek bir çok-sürümlü authoring
  schema evrimi VEYA pipeline-side asset-graph doğrulama ihtiyacıyla + ayrı ADR
  ile gelir; AuthoredMaterial alan yüzeyi + save/load/validate imzaları geriye-
  uyumlu kalır (yeni `version` alanı opsiyonel-default ile eklenir).

### 2.3 cd::texture_synth (texture_synth, tooling) — 70 → 100  [fixture-generation-tool COMPLETE+SEALED; IMPLEMENTED(test)]

- **Bağlam**: header-only; Noise.hpp (hash21 + value_noise2 cubic + fbm2 3-oct +
  value_noise2_quintic + fbm2_quintic_6oct, GLSL cloud-shader mirror'ı) + Earth.hpp
  (bake_earth_albedo_rgba8 + bake_earth_normal_rgba8 + bake_earth_mr_rgba8 —
  hello_engine'in harici glTF yokken upload ettiği deterministik fixture'lar). 6
  gtest (yalnız Noise.hpp'yi kapsıyordu; Earth bakers SIFIR coverage). Named gap
  (roadmap): "(tooling) seal as complete fixture tool".
- **Karar**: **fixture-generation tooling-100 olarak COMPLETE + SEALED**. Cross-band
  notes'a göre tooling lib'ler (bench/imgdiff/texture_synth/imgui_backend) 100'e
  ENGINE-RUNTIME derinliğiyle değil, ARACI tamamlayıp mühürleyerek ulaşır. Charter
  — deterministik procedural noise + Earth fixture bakeleri — tam: cheap,
  deterministik, hello_engine showcase + unit-test fixture'ı için yeterli. Tek
  gerçek gap, Noise.hpp'nin regression net'i varken Earth.hpp baker'larının (asıl
  yüklenen fixture'lar) test-edilmemiş olmasıydı; bu **IMPLEMENTED(test)** edildi.
  Yeni `test_texture_synth_earth.cpp` (+7 gtest, mevcut `cd_test_texture_synth_noise`
  target'ına eklendi): E1/E3/E5 (üç baker'ın RGBA8 packing + size + alpha/Z-bias/
  glTF-MR-R-unused invariant'ları), E2/E4/E6 (üçünün de byte-for-byte determinism'i
  — aynı size → identical bytes, ki tool'u fixture-kaynağı + golden-referans yapan
  kritik garanti), E7 (1×1 smallest-size UB-free). Noise.hpp zaten phase898'de
  test-edilmişti; o yola dokunulmadı.
- **Gerekçe**: texture_synth açıkça TOOLING'dir (PROJECT_COMPLETION_STATUS §2 +
  roadmap "tooling-100 = complete tool, not engine-runtime depth"). Bir tool 100%'tür
  ancak charter'ı tam + dürüstçe test-edilmişse. Charter'ı (Noise + Earth fixture
  bakery) tamdı; eksik olan tek şey Earth baker'larının regression net'iydi —
  o gap kapatıldı. Determinism testleri tooling için ÖZELLİKLE değerlidir: tool'un
  çıktısı bir fixture/golden ise, byte-determinism o golden'ın anlamlı olmasının ön
  koşuludur (non-deterministik bir baker golden diff'i anlamsız kılar). Daha fazla
  noise tipi / fixture eklemek yapay yüzey-şişirmedir (don't pad) — gerçek bir yeni
  fixture ihtiyacı (örn. brick/marble/wood baker) belirene kadar. honest-rule (a)
  edge-test + (b) tooling-seal tam bunun için.
- **Promote-on-need**: ek procedural fixture baker'ları (brick/marble/wood/metal
  noise) VEYA Noise.hpp'ye yeni temel (Worley/gradient/3D noise) — gerçek bir yeni
  hello_engine/test fixture ihtiyacıyla + ayrı PR ile gelir; mevcut hash21/
  value_noise2/fbm + bake_earth_* imza yüzeyi (header-only inline, noexcept-where-
  applicable) ve byte-output determinism'i sabit kalır.

---

## 3. Reddedilen alternatifler

- **texture_compress'te BC1 PCA endpoint search + BC3/BC5 encoder implement
  etmek**: kaliteli RGB sıkıştırma isteyen tüketici zaten opt-in BC7 (gerçek RDO
  encoder) yolunu açar; BC1'e PCA eklemek, BC7 varken, marjinal kazanç için
  ek-yüzey. BC3/BC5 ise gerçek bir alpha-DXT5 / normal-map-RGTC tüketicisi yokken
  dead-code. RED — BC1-naive-v1 + opt-in-BC7/ASTC SEALED, BC1/analyze edge dalları
  (BC3/BC5-nullopt / non-BC1 / truncated / solid-white / 3-color-decode) kilitlendi.
- **material_authoring'e JSON schema-version alanı + migration motoru implement
  etmek**: bugün tek bir v1 schema var; versioning'i tüketicisi (çok-sürümlü
  migration) olmadan eklemek erken-optimizasyon + ölü-alandır; cd::game::save'in
  kanıtlanmış forward-compat pattern'i ihtiyaç doğunca promote edilir. Deep
  structural validation ise asset::validator'ın (ayrı lib) işidir — katman-karışımı.
  RED — authoring-roundtrip-v1 SEALED, I/O+validation edge dalları (malformed /
  wrong-type-id / unopenable-save / NaN / out-of-range-color) kilitlendi.
- **texture_synth'e ek noise tipleri / fixture baker'ları (brick/marble/Worley)
  eklemek**: gerçek bir yeni fixture ihtiyacı yokken yapay yüzey-şişirme (don't
  pad); tooling-100 charter-completeness'tır, feature-volume değil. RED — fixture-
  generation tool COMPLETE+SEALED, Earth baker'larının test-edilmemiş charter'ı
  (packing + determinism + 1×1-edge) kilitlendi.
- **Herhangi bir public header/src davranışını değiştirmek**: bu pass yalnız
  test-deepening + seal yaptı (impl davranışı sabit → API/ABI sabit, golden
  byte-identical garantili). texture_synth test CMakeLists'e yalnız ikinci bir test
  .cpp dosyası (aynı target) eklendi; lib CMake link-surface'i değişmedi. RED —
  header/src DOKUNULMADI.
- **Kapsam-dışı asset lib'lerini (streamer'lar / shader_cache / vfx_authoring /
  validator / streamer_pool / runtime / asset umbrella) ya da samples'ı / % docs'u
  düzenlemek**: kapsam DIŞI (brief SCOPE EXCLUSION). RED.

## 4. Sonuçlar

- (+) 3/3 BAND-3 asset+tooling subset kütüphanesi honest-rule terminal durumuna
  geçti: texture_compress (BC1-naive-v1 + opt-in-BC7/ASTC SEALED + 5 BC1/analyze
  edge test), material_authoring (authoring-roundtrip-v1 SEALED + 5 I/O+validation
  edge test), texture_synth (fixture-generation tool COMPLETE+SEALED + 7 Earth
  baker test). Hiçbir kütüphanede placeholder/TODO-state kalmadı. BU, BAND 3'ün SON
  grubudur — Band 3 (20 lib) artık tamamen honest-100 terminal durumda.
- (+) texture_compress: BC3/BC5-unimplemented-nullopt + analyze-non-BC1-reject +
  analyze-truncated-reject + solid-white-lossless + (kritik) decode'un encode()
  üzerinden ASLA ulaşılamayan c0<=c1 3-color-transparent modu artık fail-on-revert
  kilitli (+5 test) — harici 3-renk-modlu BC1 .dds decode doğruluğu garantili.
- (+) material_authoring: malformed-JSON-nullopt + wrong-type-id-nullopt +
  unopenable-save-false + NaN-scalar-error + out-of-range-base_color-error artık
  kilitli (+5 test); schema-versioning + deep-validation sınırı (promote-on-need:
  game::save pattern / asset::validator katmanı) dürüstçe belgelendi.
- (+) texture_synth: Earth baker'ları (albedo/normal/MR) artık RGBA8-packing +
  alpha-opaque + Z-bias + glTF-MR-R-unused + byte-determinism + 1×1-edge için
  kilitli (+7 test, yeni test .cpp aynı target'a); tooling-100 charter-completeness
  (fixture-source + golden-referans determinism garantisi) dürüstçe belgelendi.
- (+) Toplam +17 yeni test (5+5+7), hepsi edge/contract + anti-flakiness
  (sleep_for YOK; deterministik sentetik RGBA/JSON/baker girdisi). Build -Werror
  temiz (3 test target re-link); 0 yeni clang-tidy WAE defect-class. Public
  header/src DOKUNULMADI → golden byte-identical.
- (+) Her mühür "promote-on-need" tetikleyici taşır → genişleme yolu (BC1 PCA +
  BC3/BC5, schema-version migration + deep validation, ek fixture baker'ları)
  nettir ama bugün dead-code/over-engineering/wrong-layer olmaz.
- (−) Mühürler BC1 PCA endpoint search, BC3/BC5 encoder, JSON schema versioning,
  deep structural validation ya da ek fixture baker'ları bu pass'te ÜRETMEZ;
  alpha-BC / multi-version-authoring / yeni-fixture ihtiyacı doğunca ayrı ADR'larla
  gelir. Kabul: BAND 3 asset+tooling subset "seal-the-functional-v1 + lock-the-
  untested-edges" karakterinde — bu alt-kümede asıl boşluk kalite/şema/tooling
  genişlemesidir (tüketicisi-yok / wrong-layer / opt-in-real-encoder-zaten-var),
  unimplemented-skeleton DEĞİL.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: documented gap → IMPLEMENTED+tested
  (küçük/net) VEYA SEALED — bu alt-kümenin üç named gap'i de "charter-complete v1
  + kalite/şema/tooling genişlemesi" karakterinde olduğundan her birinde v1 SEALED
  + gerçekten test-edilmemiş edge dalları IMPLEMENTED(test) + genişleme promote-
  on-need ayrıldı (ui-aisquad/world/foundation Band-3 ADR'larıyla aynı bölünme
  çizgisi).
- Test target'ları: texture_compress=`cd_test_asset_texture_compress`
  (engine/asset/texture_compress/tests/test_texture_compress.cpp), material_authoring=
  `cd_test_asset_material_authoring` (test_material_authoring.cpp), texture_synth=
  `cd_test_texture_synth_noise` (test_texture_synth_noise.cpp + yeni
  test_texture_synth_earth.cpp aynı target). Üçü de PASS (ctest -R
  "texture_compress|material_authoring|texture_synth" ile doğrulandı; cd_test_rhi_vulkan
  çalıştırılmadı). Test case toplamı: 14→19 / 8→13 / 6→13.
- texture_compress BC7/ASTC testleri (T5/T11–T14) bu host'ta encoder build-config'e
  bağlı SKIP-clean olabilir; eklenen 5 edge test (T15–T19) BC1/analyze yolundadır,
  feature-flag'den bağımsız, daima koşar. T19 el-yapımı blob (c0=0x0000<c1=0xFFFF,
  index'ler 0) encoder'ın asla emit etmediği decode dalını tek doğru yoldan sürer
  ve siyah-referans ile RMSE==0 doğrular.
- texture_synth determinism testleri (E2/E4/E6) iki ardışık bake'in std::vector
  byte-eşitliğini assert eder (hash21 saf-fonksiyon + Earth bakery saf-fonksiyon →
  aynı size daima identical); E3'ün Z-bias assert'i normal'in z-baskın olduğu
  (B kanalı >=128) tangent-space packing invariant'ına dayanır.
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/asset/
  texture_compress/tests/ + engine/asset/material_authoring/tests/ +
  engine/texture_synth/tests/ + bu docs/ADR/ dosyası altında. Yalnız TEST
  dosyalarına ekleme yapıldı (lib header/src DOKUNULMADI çünkü impl-davranış
  değişmedi; texture_synth test CMakeLists'e yalnız ikinci test .cpp aynı target'a
  eklendi). asset streamer'lar + shader_cache + vfx_authoring + validator +
  streamer_pool + runtime + asset umbrella + samples/ + hello_* + % docs'a
  dokunulmadı.
- Golden byte-identical: bu pass public header/src davranışını değiştirmedi
  (yalnız test ekleme) → hello_engine render yolu hiç etkilenmez; fixture #5
  capture baseline (research/reports/parity1121/baseline.png) ile bayt-bayt eşit
  doğrulandı (b3a.png cmp → identical, sonra silindi).

## Sonraki

- BAND 3 (70–79%, 20 lib) bu pass ile TAMAMLANDI — sıradaki band BAND 4 (60–69%,
  12 lib: light / editor / volumetric / particle_system / asset::shader_cache /
  asset::vfx_authoring / asset::streamer_pool / platform / atmosphere / denoise /
  asset::validator / imgui_backend). Editor (~15.8k LOC) o bandın baskın item'ıdır.
- Bu pass'in mühürlenen promote tetikleyicileri: texture_compress için BC1 PCA
  endpoint fit + BC3/BC5 (alpha-BC / normal-map-RGTC consumer + bc7enc rgbcx.h);
  material_authoring için JSON schema-version + migration (game::save pattern) +
  deep structural validation (asset::validator katmanı, multi-version-authoring
  ihtiyacıyla); texture_synth için ek fixture baker'ları (brick/marble/Worley,
  yeni fixture ihtiyacıyla).
- Mühür şablonu (seal-the-functional-v1 + lock-the-untested-edges + promote-on-need)
  bu grupla 4. kez uygulandı (ui-aisquad / world / foundation / asset+tooling);
  Band 4+ için de aynı şablon — özellikle gerçek tüketicisi-olmayan kalite/algoritma
  genişlemelerinde — tekrar kullanılabilir.
