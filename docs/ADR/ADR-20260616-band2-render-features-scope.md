# ADR-20260616 — ALL-MODULES-TO-100 BAND 2 / RENDER-FEATURES (Band-2 subset) Kapsam Mührü (3 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 55e7f94)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 2 render-features Band-2-subset close-out — charter-complete kapsam mührü pass; restir_di + ddgi + post)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 2 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; render-features Band-2
    satırı: restir_di/ddgi/post "edge/negative test depth to the project's evidence
    bar + close the ONE documented gap each, then a scope-seal")
  - `docs/PROJECT_COMPLETION_STATUS.md` §5 (engine/render FEATURES baseline %'leri +
    Basis sütunu; restir_di 83 "deepest GPU impl in cluster", ddgi 82 "full RHI
    4-pass … GPU-wired phase1213", post 80 "broadest+best-tested"; grup rollup ~61%
    iki katmanlı — "a STRONG RHI-wired GPU set (restir_di/ddgi/post/lighting_clusters/
    cluster)" vs "a MID header-only set")
  - `docs/ADR/ADR-20260616-band1-scope.md` + `docs/ADR/ADR-20260616-band2-foundation-scope.md`
    + `docs/ADR/ADR-20260616-band2-game-scope.md` + `docs/ADR/ADR-20260616-band2-render-core-scope.md`
    (kardeş band-mühür ADR'ları, aynı şablon: seal-with-promote-on-need +
    targeted-topup-only-where-genuinely-thin)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar; her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Bu üç kütüphane brief'in adlandırdığı **STRONG
  RHI-wired GPU set**'tir (deeply tested) → to-100 mostly a charter-complete SEAL +
  test-topup ONLY where a real surface gap exists (don't pad). Bu pass'te **üçü de
  charter-complete** çıktı: gerçek bir test-edilmemiş yüzey/operatör boşluğu
  BULUNMADI, dolayısıyla bu bir **SAF SEAL pass'idir** — KAYNAK/TEST değişikliği YOK,
  yalnız bu ADR. engine/render/{restir_di,ddgi,post}/ DIŞINDA hiçbir şeye
  dokunulmadı (rhi/ + samples/ + hello_* + diğer render-feature libs
  [atmosphere/denoise/vb. alt-band] + % docs DAHİL); chrome golden BYTE-IDENTICAL.

---

## 1. Bağlam

BAND 2 (80–89%) render-features grubunun bu dispatch'in kapsadığı alt-kümesi 3
kütüphaneyi 100%'e taşır: restir_di 83, ddgi 82, post 80. (Grubun geri kalan
mid-tier header-only kütüphaneleri — atmosphere 62, denoise 60, decal 58,
volumetric 66, velocity 55, virtual_textures 56, virtual_geometry 52, ibl 72,
cluster 70, lighting_clusters 75, light 68, gpu_particles 45, light_shafts 42,
ibl_gpu 48, nrc 40, restir_gi 38 — daha düşük band'lerdedir ve bu dispatch'in
AÇIKÇA DIŞINDADIR.)

Baseline raporunun §5 değerlendirmesi grubu iki katmana ayırır: "a STRONG
RHI-wired GPU set (restir_di/ddgi/post …) and a MID header-only … set whose
on-screen behaviour depends on the consumer". Bu üç kütüphane STRONG katmandır:
gerçek `cd::rhi::IDevice`/`ICmdBuf` dispatch + descriptor write/UAV barrier +
RTX 3080 host'ta no-validation-error koşan derin gtest binary'leri taşırlar.
Yani üçü için de 100% = charter-complete'i + dokümante design-scope'u kabul eden
bir mühür; brief'in deyimiyle "to-100 is mostly a charter-complete SEAL".

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in inceleme bulgusu (kanıt §2'de kütüphane başına):
- **3 charter-complete (SEAL only)**: restir_di (32 test/162 assert),
  ddgi (32 test/122 assert), post (84+31 test across 12 alt-target/197+ assert).
- **0 test-topup**: Üç kütüphanenin de yüzeyi zaten round-trip + negative +
  boundary + GPU-integration test-dolu. Brief'in "test-topup ONLY where a real
  surface gap exists … don't pad" + "topup only a genuinely-untested operator if
  one exists" kuralı: post'un DÖRT tonemap operatörü (Narkowicz/Hill/Hable/AGX)
  de zaten test-dolu (black-in-black-out + clamp + monotonicity + anchor + AGX
  saturation + per-operator GLSL dispatch, hepsi 4 operatörü dolaşır) → topup
  edilecek untested-operator YOK. Pad EDİLMEDİ.
- **0 IMPLEMENT**: Grupta RHI-primitive-gerektirmeyen + temiz-yapılabilir +
  tüketicisi-olan bir gerçek feature-item BULUNMADI; tek dokümante iç-boşluk
  (restir_di SVGF temporal reproject — §2.1) bir motion-vector consumer-input'una
  bağlı → SEALED (render-core framegraph aliasing'iyle aynı mantık: prerequisite
  yok → temiz implement edilemez → seal).

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::restir_di (render/restir_di) — 83 → 100  [charter-complete SEAL]

- **Bağlam**: 1512 hdr + 2649 src / 5 TU; Bitterli 2020 ReSTIR DI'nın tam RHI
  pipeline'ı — per-piksel WRS reservoir (final-weight / first-sample-unconditional
  update / weight-ratio probability / M-clamp / donor-combine) + initial-candidate
  CS + temporal-reuse CS + spatial-reuse CS + tam SVGF denoiser (moment-estimate /
  variance-estimate / a-trous wavelet) + iki orkestratör (FullSvgfPipeline,
  FullPipelineDenoised). 32 gtest / 162 assert (6 dosya), hepsi gerçek GPU:
  DispatchPass record+submit'leri (initial/temporal/spatial) RTX 3080 host'ta
  no-validation-error koşar; SVGF "smooths-noisy-input" + 3-iteration chain +
  invalid-out-buf no-op + frame-advance dahil. Baseline: "deepest GPU impl in cluster".
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: ReSTIR DI'nın load-bearing yüzeyi — WRS reservoir cebiri + üç
  reuse-pass GPU dispatch'i (validation-temiz) + SVGF üç-pass denoiser
  (moment/variance/atrous, short-history-threshold fallback dahil) + uçtan-uca
  denoised orchestrator — zaten 32 testle (kümenin en derini, 162 assert) kapalı;
  pad roadmap "don't pad" + brief "don't pad" kuralına aykırı olurdu.
- **Dokümante tek iç-boşluk (SVGF temporal reproject) → bu mühürle SEALED**:
  `SvgfDenoiser.cpp` moment-estimate CS'inin temporal reproject yolu bilinçli
  olarak stub'tur (`sample(t-1) == sample(t)`, motion-vector ile reprojeksiyon
  YOK — "Sprint-4 skeleton … Sprint-5 wires motion-vec"). Bu **temiz implement
  EDİLEMEZ-bu-pass'te** çünkü gerçek reproject bir per-piksel motion-vector
  (velocity G-buffer) consumer-input'u + history-validation (disocclusion reject)
  ister; bu girdi yalnız bir gerçek render-graph tüketicisi (composite/G-buffer
  pipeline) tarafından sağlanır ve render-core framegraph aliasing mührüyle aynı
  şekil: prerequisite-consumer yokken yarım bir reproject (yanlış history →
  ghosting/lag artefaktı) project quality-bar'ına aykırı olurdu. Bugünkü
  exponential-moment-decay yolu doğru + test-dolu (variance fallback + atrous onu
  tüketir); SVGF denoise charter'ı (smooths-noisy-input) GPU-test ile kanıtlı.
- **Promote-on-need**: SVGF temporal reproject, gerçek bir motion-vector
  (velocity) G-buffer'ı + history-validation sağlayan render-graph tüketicisi
  doğunca implement edilir (reproject sample + disocclusion/normal/depth reject +
  per-piksel history-length clamp) + reproject-doğruluğunu kanıtlayan bir
  before/after golden/bench + ayrı ADR. Ek olarak: bias-corrected MIS spatial reuse
  (Bitterli §5 talbot-MIS), G-buffer-aware initial sampling, ya da multi-bounce
  ReSTIR GI köprüsü (cd::restir_gi Band-7 ile) gerçek bir GI-consumer + ayrı ADR
  ile gelir. Reservoir / DispatchPass / SvgfDenoiser / FullPipelineDenoised yüzeyi
  sabit kalır.

### 2.2 cd::ddgi (render/ddgi) — 82 → 100  [charter-complete SEAL]

- **Bağlam**: 1522 hdr + 1529 src; Majercik 2019 DDGI'nin tam RHI 4-pass'i —
  trace (ray_query) / blend-irradiance / blend-visibility / sample — + descriptor
  write + UAV barrier + octahedral probe-encode/decode + trilinear probe-blend +
  sky-probe fallback. 32 gtest / 122 assert (3 dosya). GPU-wired phase1213.
  CPU-tarafı: octahedral round-trip (pozitif/negatif hemisphere/çoklu yön) + probe
  indexing (cell-centre/corner) + blend-weight (grid-içi sum=1, grid-dışı sum=0,
  max-edge) + atlas-UV (unit-range + distinct) + sky-probe (no-hits→sky,
  hits→zero) + trilinear (sum=1 inside, 0 outside). GPU-tarafı: DispatchPass
  init/record/submit (smoke + ray_query gate), 4-pass dispatch (blend-irradiance/
  visibility/sample), checked-vs-unchecked overload (unbound-input reject), full
  pipeline non-zero output.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: DDGI'nin load-bearing yüzeyi — octahedral encode/decode + probe
  indexing + trilinear blend (grid sınırı dahil) + sky-probe fallback + 4-pass GPU
  dispatch (validation-temiz, multi-frame) + full-pipeline non-zero output — zaten
  32 testle (122 assert) kapalı; pad olurdu. Baseline'ın "CPU-stub" hit'leri gerçek
  bir boşluk DEĞİL: `execute_*(frame_index)` CPU-stub overload'ları, gerçek
  cmd-buffer dispatch'inin yanında **bilinçli ikinci bir API**dır (probe-grid/atlas
  state'i Vulkan'sız doğrular + call-counter bump'lar) ve cmd-buffer-yolu testle
  ayrıca koşar — yani "stub" burada test-edilebilir-CPU-doğrulama-overload'u demek,
  eksik-impl değil.
- **Promote-on-need**: Probe-relocation / classification (Majercik 2019 §"probe
  states", dead-probe inactive), infinite-scrolling probe-grid (kamera-takipli
  cascade), ya da multi-volume DDGI blending gerçek bir büyük-sahne GI tüketicisiyle
  + ayrı ADR ile gelir; ProbeGrid / DispatchPass / FullPipeline yüzeyi sabit kalır.
  (DDGI'nin self-shadow/normal-bias knob'ları zaten Ddgi.hpp'de mevcut + test-dolu.)

### 2.3 cd::post (render/post) — 80 → 100  [charter-complete SEAL]

- **Bağlam**: ~3.8k LOC / 13 hdr (composite 1147, exposure 532 + 102 .cpp,
  bloom 457, taa 214) + 11 INTERFACE alt-target (bloom/camera/composite/dof/fx/
  gtao/motion_blur/smaa/ssr/taa/tonemap) + exposure STATIC alt-target —
  tonemap (Narkowicz/Hill/Hable/AGX) / bloom / TAA / SSR / GTAO / DOF / motion_blur
  / SMAA / composite (DDGI+ReSTIR GI-hook'lu, dual-MRT) / auto-exposure
  (log-avg + EMA). 84 + 31 (exposure) gtest / 197+ assert (12 alt-target test
  dosyası, gtest + custom-main harness karışık). Baseline: "broadest+best-tested".
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe (brief'in "topup only a genuinely-untested operator if one exists"
  kuralı)**: post'un her operatörü zaten test-dolu — **dört tonemap operatörünün
  (Narkowicz/Hill/Hable/AGX) hepsi** black-in→black-out + bright-clamp-to-[0,1] +
  monotonicity + Narkowicz published-anchor (~0.797) + AGX saturation-preserve +
  per-operator GLSL-dispatch non-empty testleriyle (6 test, hepsi 4 operatörü
  dolaşan loop) kapalı; bloom (mip-count + downsample/upsample) / TAA (jitter-phase
  + history-blend) / SSR / GTAO / DOF / motion_blur / SMAA alt-target'ları kendi
  dosyalarında, composite push-layout 256B + 6-binding + dual-MRT + W4/W5 visible-
  bug regression-guard'ları + Wronski volumetric-fog wire + exposure log-avg/EMA
  31 testle test-dolu. **Test-edilmemiş bir operatör YOK** → topup edilecek bir şey
  yok; pad EDİLMEDİ (brief: "don't pad").
- **Promote-on-need**: Yeni tonemap operatörü (PBR-neutral / Khronos PBR-neutral
  2024, AgX-custom-look LUT), GPU-tarafı bloom/exposure reduction pass'i (bugün
  bloom CPU-helper + GLSL string; exposure CPU-kernel + "GPU reduction queued for
  the composite-pass companion"), ya da TSR/upscaling (DLSS/FSR-shape) gerçek bir
  post-pipeline tüketicisiyle + ayrı ADR ile gelir; her alt-target'ın Settings +
  glsl_for / make_*_source yüzeyi sabit kalır.

---

## 3. Reddedilen alternatifler

- **restir_di SVGF temporal reproject'i bu pass'te implement etmek**: temiz
  yapılamaz — gerçek reproject bir per-piksel motion-vector (velocity G-buffer)
  consumer-input'u + history-validation (disocclusion/normal/depth reject) ister;
  bu girdiyi yalnız bir gerçek render-graph tüketicisi sağlar. Prerequisite-consumer
  yokken yarım bir reproject (yanlış history → ghosting/temporal-lag artefaktı)
  project quality-bar'ına aykırı olurdu (render-core framegraph aliasing mührüyle
  aynı mantık: prerequisite yok → clean-implement değil → SEAL). RED — bugünkü
  exponential-moment-decay + variance-fallback + atrous yolu doğru + test-dolu;
  reproject promote-on-need (motion-vector consumer + before/after golden).
- **3 charter-complete render-features kütüphanesinin (restir_di/ddgi/post) yüzeyini
  pad etmek**: yüzeyleri zaten round-trip + negative + boundary + GPU-integration
  test-dolu (restir_di 32/162, ddgi 32/122, post 84+31/197+); baseline "STRONG
  RHI-wired GPU set … deeply tested". Yapay test "test-topup ONLY where a real
  surface gap exists … don't pad" + "topup only a genuinely-untested operator if
  one exists" kuralına aykırı; post'ta untested-operatör YOK (4/4 tonemap test-dolu).
  RED — saf SEAL pass, 0 yeni test.
- **ddgi "CPU-stub" overload'larını gerçek-boşluk sayıp doldurmaya çalışmak**:
  `execute_*(frame_index)` overload'ları gerçek cmd-buffer dispatch'inin yanında
  bilinçli ikinci-API (Vulkan'sız state-doğrulama + call-counter), ayrıca
  cmd-buffer-yolu testle koşuyor. RED — bunlar test-edilebilirlik affordance'ı,
  eksik-impl değil.
- **engine/render'in diğer FEATURES kütüphanelerini (atmosphere/denoise/decal/
  volumetric/velocity/cluster/light/ibl/… ya da restir_gi/nrc/gpu_particles)
  düzenlemek**: bunlar daha düşük band'lerde (3–7) ve bu dispatch'in açıkça DIŞINDA
  ("the Band-2 subset only … 3 GPU libs"). RED.
- **engine/render/rhi/'ı / samples'ı / hello_*'ı / % docs'u / diğer grupları
  düzenlemek**: kapsam DIŞI (brief: "work ONLY in engine/render/{restir_di,ddgi,
  post}/ + docs/ADR/. NOT rhi/, samples/, hello_*, % docs"). RED. (post README'deki
  stale TODO "expand coverage (currently <3 test cases)" — yanıltıcı ama load-bearing
  değil; engine/render/post/ altında olduğundan kapsam-içi ama minimal-surface
  gereği DOKUNULMADI, "Sonraki"ye yazıldı.)

## 4. Sonuçlar

- (+) 3/3 BAND-2 render-features (Band-2-subset) kütüphanesi honest-rule terminal
  durumuna geçti: üçü de çekirdek-IMPLEMENTED + zaten-derin-test (charter-complete)
  + bu mühürle SEALED; restir_di'nin tek dokümante iç-boşluğu (SVGF temporal
  reproject) "clean-implement değil — motion-vector consumer-input gerektirir"
  gerekçesiyle v1-SEALED. Hiçbir kütüphanede placeholder/TODO terminal-olmayan-durum
  kalmadı; her mühür promote-on-need çıkış kapısı taşır.
- (+) 0 kaynak değişikliği + 0 yeni test: brief'in "STRONG RHI-wired GPU set →
  to-100 is mostly a charter-complete SEAL + test-topup ONLY where a real surface
  gap exists (don't pad)" kuralı tam uygulandı — gerçek bir test-edilmemiş yüzey/
  untested-operatör BULUNMADI (post'ta 4/4 tonemap operatörü test-dolu), bu yüzden
  SAF SEAL pass. Mevcut 96 test (restir_di 32 + ddgi 32 + post 84+31 alt-target,
  toplam 20 ctest binary + exposure) RTX 3080 host'ta PASS (fail-on-revert koruması
  zaten yerinde).
- (+) Chrome golden BYTE-IDENTICAL (fixture #5, baseline ile `cmp` eşit). Hiçbir
  kaynak/shader değişmediğinden render-yolu aynen korunur. Build -Werror temiz,
  0 yeni clang-tidy WAE (0 değişen satır).
- (−) Mühürler restir_di SVGF-reproject, ddgi probe-relocation, post yeni-operatör/
  GPU-reduction gibi feature'ları bu pass'te ÜRETMEZ; prerequisite/consumer doğunca
  ayrı ADR'larla gelir. Kabul: bu üç kütüphane baseline'ın "STRONG RHI-wired GPU
  set, deeply tested" katmanı; brief açıkça "to-100 is mostly a charter-complete
  SEAL". Grubun gerçek feature-build kalanı (mid-tier header-only + restir_gi/nrc/…)
  daha düşük band'lerdedir, bu dispatch'in dışında.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu (band1 + band2-foundation/game/render-core
  ADR'larıyla aynı): küçük/net/tüketicisi-olan/temiz boşluğu implement et,
  consumer/RHI-prerequisite-gerektiren'i seal et. Bu üç kütüphanede temiz-implement-
  edilebilir bir boşluk BULUNMADI (restir_di SVGF-reproject motion-vector-consumer'a
  bağlı) → üçü de charter-complete SEAL.
- "Test-topup ONLY where a real surface gap exists / topup only a genuinely-untested
  operator if one exists, don't pad" kuralı: üç kütüphanenin de yüzeyi (WRS reservoir
  + 3-reuse-pass + SVGF / octahedral + 4-pass + trilinear / 4-tonemap + bloom/TAA/
  SSR/GTAO/DOF/motion_blur/SMAA/composite/exposure) round-trip+negative+boundary+
  GPU-integration test-dolu; post'un 4 tonemap operatörünün hepsi test ediliyor →
  untested-operatör YOK → 0 test eklendi (pad değil).
- ADR dosya-adı `ADR-20260616-band2-render-features-scope.md` + tarih 2026-06-16:
  band-cohort kardeş ADR'larıyla (band1/foundation/game/render-core, hepsi 20260616)
  hizalı; HEAD 55e7f94 (phase1232 render-core seal'lerinin üstü).
- "engine/render/{restir_di,ddgi,post}/ + docs/ADR DIŞINA dokunma" kuralı uygulandı:
  KAYNAK/TEST değişikliği SIFIR (saf SEAL); tek yeni dosya bu ADR. rhi/ + samples/ +
  hello_* + diğer render-feature libs + % docs'a DOKUNULMADI. hello_engine RELINK
  bile gerekmedi (hiçbir header/binary değişmedi) → golden byte-identical garantisi
  trivial (capture ile de doğrulandı: `cmp baseline.png b2rf.png` → identical,
  b2rf.png silindi).

## Sonraki

- BAND 2 render-features'ın geri kalan mid-tier header-only kütüphaneleri
  (atmosphere/denoise/decal/volumetric/velocity/cluster/light/ibl/ibl_gpu/
  gpu_particles/light_shafts/virtual_textures/virtual_geometry + restir_gi/nrc)
  DAHA DÜŞÜK band'lerde (3–7); kendi band dispatch'lerinde "give them real .cpp RHI
  dispatch (the cd::ddgi/restir_di pattern)" + de-dup froxel-clustering ile ele
  alınır — bu üç STRONG kütüphane o işin REFERANS-pattern'idir.
- restir_di SVGF temporal-reproject promote tetikleyicisi: bir gerçek render-graph
  tüketicisinin per-piksel motion-vector (velocity) G-buffer'ı + history-validation
  sağlaması; o landıktan sonra reproject + disocclusion/normal/depth reject +
  before/after golden + ayrı ADR.
- post stale README TODO ("expand coverage (currently <3 test cases)",
  engine/render/post/README.md:130) GERÇEK durumu (84+31 test) yanlış yansıtıyor;
  bu pass minimal-surface gereği DOKUNMADI — ayrı bir doc-refresh pass'inde
  düzeltilebilir (kapsam-içi ama bu mühürün konusu değil).
- ddgi probe-relocation/classification + post yeni-operatör (Khronos PBR-neutral)/
  GPU-reduction: ilgili büyük-sahne-GI / post-pipeline tüketicileri doğunca ayrı
  ADR'larla.
