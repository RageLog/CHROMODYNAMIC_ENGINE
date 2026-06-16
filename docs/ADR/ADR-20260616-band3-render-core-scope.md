# ADR-20260616 — ALL-MODULES-TO-100 BAND 3 / RENDER-CORE Kapsam Mührü (3 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD eddb7b2)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 3 render-core subset close-out — seal-charter-complete + edge-test-topup pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 3 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; render umbrella "deepen
    Renderer aggregation vs header surface; tests", async_submit "frame-fence
    integration (deliberately minimal today) or seal", mesh_shader "cone/spatial
    clusterizer optimization (greedy today); GPU dispatch already in rhi" — §BAND 3
    tablo)
  - `docs/PROJECT_COMPLETION_STATUS.md` §4 (engine/render CORE baseline %'leri +
    Basis sütunu: render umbrella 78 "real aggregation, modest src depth vs header
    surface", async_submit 78 "deliberately minimal (no frame-fence integration)",
    mesh_shader 70 "real greedy clusterizer … cone/spatial opt future, GPU dispatch
    in rhi")
  - `docs/ADR/ADR-20260616-band3-foundation-scope.md` + `docs/ADR/ADR-20260616-band2-render-core-scope.md`
    (kardeş band-mühür ADR'ları, aynı şablon: impl-the-clean-gap + seal-the-rest +
    promote-on-need + targeted-topup)
  - `docs/ADR/ADR-20260522-wave20-render-thread-deferral` (async_submit'in
    "real render thread" pre-condition'larını dökümante eden mevcut deferral kaydı;
    bu ADR onu band-100 mührüyle terminal duruma getirir)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Üç kütüphane de baseline'da "real aggregation /
  deliberately-minimal / real greedy" — yani charter-complete; bu pass'in named
  gap'leri (render umbrella src-depth, async_submit frame-fence, mesh_shader
  cone-spatial-opt) TEMİZ-yapılamaz-bu-pass'te ya da out-of-scope-prerequisite
  taşıyan **design-scope** kararlarıydı, hepsi SEALED + her birinde gerçek bir
  test-edilmemiş aggregation/edge branch'ine hedefli test eklendi (render +14,
  async_submit +4, mesh_shader +7). engine/render/{include,src} + async_submit +
  mesh_shader + bu ADR DIŞINA DOKUNULMADI; rhi/ + diğer render lib'leri + samples/ +
  hello_* + % docs DEĞİL. Chrome golden BYTE-IDENTICAL.

---

## 1. Bağlam

BAND 3 (70–79%) render-core alt-kümesi 3 kütüphaneyi 100%'e taşır: cd::render
(umbrella, 78 — Renderer.cpp acquire/submit/present orkestrasyonu + DrawBucket/
SortKey/PostProcessChain/PlanarShadow header yüzeyi, 42 test) + cd::async_submit
(78 — thread+cv tek-slot ve N-slot render-thread primitive'i, 10 test, bilinçli
minimal: frame-fence entegrasyonu yok) + cd::mesh_shader (70 — greedy meshlet
clusterizer + Ritter bounds_sphere + cone_axis_cutoff alanı, 6 test, cone/spatial
optimizasyon gelecekte, GPU dispatch rhi'da).

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi: üç named gap de "temiz-implement-edilemez-bu-pass'te"
ya da "out-of-scope-prerequisite" karakterindeydi → üçü de SEALED. Ama baseline'ın
"real aggregation / modest src depth vs header surface" (render umbrella) ve "real
greedy clusterizer" (mesh_shader) gözlemi gerçek bir test-coverage boşluğuna işaret
ediyordu: agregasyon header'larının (SortKey blend/pass significance, DrawBucket
emit_all auto-sort replay, PostProcessChain empty/no-match/clear) ve clusterizer'ın
(degenerate/trailing-partial/vertex-dedup/cap-boundary-flush) load-bearing dalları
mevcut testlerce hiç dokunulmamıştı. Bu pass o gerçek-boşlukları hedefledi (pad
YOK — yalnız test-edilmemiş davranış); süs/charter-tekrarı test eklenmedi.

mesh_shader cone-spatial-opt ve async_submit frame-fence'i NEDEN seal:
- **mesh_shader cone-spatial-opt**: gerçek cone fit (per-meshlet normal koni +
  apex + cutoff) ve spatial-locality reordering bir geometri-optimizasyon iş
  paketidir; greedy v1 DOĞRU (cap'lere saygılı, deterministik, bounds doğru). GPU
  task-shader cone-cull dispatch'i zaten frozen Band-0 rhi/'da (GLSL skeleton'da
  `cone_axis_cutoff` okunuyor). Cone fit'i eklemek correctness için gerekli DEĞİL
  (koni sentinel'i sıfır → cull no-op, güvenli); bir perf-optimizasyonu →
  promote-on-need.
- **async_submit frame-fence**: frame-fence senkronizasyonu bir *renderer-
  integration* concern'ü (brief'in birebir ifadesi). cd::render::Renderer zaten
  kendi `frames_in_flight` fence ring'ini tutuyor (Renderer.cpp PerFrame.fence);
  AsyncSubmit/AsyncSubmitN bilinçli olarak fence-agnostic job-pipelining
  primitive'leridir (SPSC ring + cv). Fence'i bu primitive'lere itmek katman
  ihlali olurdu (primitive renderer'ı bilmemeli). → v1 minimal-primitive SEALED.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::render (umbrella) — 78 → 100  [SEALED charter-complete + edge-test-topup]

- **Bağlam**: Renderer.cpp 371 (create/begin_frame/end_frame/submit_draws/
  recreate_swapchain/wait_idle — acquire→submit→present + implicit
  UNDEFINED→COLOR→PRESENT barrier'lar + frames_in_flight fence ring + move/release
  semantics) + 12 header (DrawBucket/SortKey/PostProcessChain/PlanarShadow/Tonemap/
  MeshStats/ClearColorPreset/DrawBatchKey/TextLayoutMetrics/MeshUpload/TextureUpload).
  42→56 gtest (14'ü Vulkan-ICD-gated, ICD yokken SKIP).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi; Renderer'ın gerçek
  src-depth boşluğu (acquire/submit/present/recreate/move) gerçek bir Vulkan ICD
  ister ve mevcut 8 Vulkan-integration testiyle (round-trip/double-begin/3-frame-
  ring/recreate/submit-draws-replay) kapalı — derinleştirme bir *consumer*'a
  (gerçek render-thread pipeline) bağlı, bu pass'te uydurulamaz. **SEALED**: charter
  tam. Bu pass yalnız gerçekten test-edilmemiş header-aggregation dallarına +14
  test ekledi:
  - **SortKey** (+4): `layer_of` her dört layer için round-trip (önceden yalnız
    opaque<ui eşitsizliği vardı); blend-group significance (bits 57..56 — material
    id'nin ÜSTÜNDE, pass'in ALTINDA: önceki testler bu sıralamayı hiç assert
    etmemişti, gerçek bit-layout'u kilitler); user_lo en-az-anlamlı tie-break
    (üstteki hiçbir alanı bozmaz); transparent back-to-front pre-flipped-depth
    contract'ı (kütüphanenin dökümante painter's-algorithm sözleşmesi).
  - **DrawBucket** (+4): `emit_all` auto-sort-then-replay (önceden YALNIZ Vulkan
    SubmitDraws yolundan kapsanıyordu — ICD-gated; şimdi `NullCommandBuffer` ile
    her host'ta deterministik); emit_all-does-not-clear → reuse; empty-bucket
    no-op; reserve-logical-size-invariant.
  - **PostProcessChain** (+6): all-disabled empty view; empty-chain empty view;
    set_enabled-on-missing-name no-op; clear; remove-on-empty false; params
    round-trip + defaulted-zero overload.
- **Gerekçe**: Renderer src-depth derinleştirmesi gerçek bir render-thread
  *consumer*'ı olmadan over-engineering olur (begin/submit/present zaten doğru +
  Vulkan test-dolu). Eklenen 14 test gerçek boşluktu: agregasyon-header'larının
  load-bearing dalları (sort-key field significance, bucket replay, chain
  enable/clear/no-match) doğrudan iddia edilmemişti; SortKey blend-significance
  testi yazılırken bit-layout'un (blend>material) doğru yönü de pinlendi (testin
  ilk hâli ters varsaymıştı, gerçek layout'a göre düzeltildi — bu tam da
  "test-the-real-contract" değeridir). DrawBucket emit_all artık NullCommandBuffer
  ile GPU'suz kapalı (önceden tek replay testi Vulkan-skip'liydi).
- **Promote-on-need**: Gerçek render-thread pipeline (AsyncSubmitN + Renderer
  frame-fence çift-yönlü entegrasyonu), GPU-driven multi-pass scheduling, ya da
  bindless draw-stream (DrawItem std::function → kompakt PSO/material-index struct,
  baseline'ın "AAA-scale" notu) gerçek bir yüksek-draw-count sahne tüketicisiyle +
  ayrı ADR ile gelir; Renderer/DrawBucket/SortKey/PostProcessChain yüzeyi sabit
  kalır.

### 2.2 cd::async_submit (render/async_submit) — 78 → 100  [SEALED minimal-primitive-v1 + edge-test-topup]

- **Bağlam**: header-only (AsyncSubmit 157 — tek-slot thread+cv producer/consumer;
  AsyncSubmitN 159 — N-slot bounded ring SPSC pipeline). enqueue/wait_idle/
  is_busy/enqueue_count/completion_count + drain→stop→join destructor. 10→14 gtest
  (anti-flakiness: cv-tabanlı, busy-wait yalnız test-introspection'da).
- **Karar (DECIDE: frame-fence hook vs seal — brief §async_submit)**: frame-fence
  entegrasyonu **SEALED (minimal-primitive-v1)**. Frame-fence bir renderer-
  integration concern'üdür (brief'in birebir ifadesi); cd::render::Renderer zaten
  kendi `frames_in_flight` fence ring'ini tutuyor (§2.1). AsyncSubmit/AsyncSubmitN
  bilinçli olarak fence-agnostic job-pipelining primitive'leridir — fence'i bu
  katmana itmek primitive'i renderer'a bağlardı (katman ihlali). Bu pass yalnız
  gerçek test-edilmemiş constructor + ring-wrap dallarına +4 test ekledi:
  - **AsyncSubmitN.ZeroCapacityDefaultsToTwo**: `capacity == 0 ? 2 : capacity`
    ctor dalı (default ctor + explicit 0 — önceki her test explicit capacity
    geçiyordu, bu dal hiç koşmamıştı).
  - **AsyncSubmitN.CapacityOneSerializesAndWrapsRing**: capacity==1 dejenere ring'i
    `head_ = (head_+1) % 1` wrap'ını her job'da tetikler; 20 job strict-FIFO +
    slot-kaybı-yok.
  - **AsyncSubmitN.WaitIdleOnFreshQueueReturnsImmediately**: size_==0 && !running_
    predicate'i zaten-tatmin (bloklamaz) dalı.
  - **AsyncSubmit.FreshSubmitterHasZeroCounters**: idle fresh submitter sayaç/
    is_busy/wait_idle-no-op invariant'ı.
- **Gerekçe**: Brief "OR add a small clean frame-fence hook if testable" diyor —
  ama temiz bir frame-fence hook'u Renderer'ın fence ring'ine bağlanmayı gerektirir
  (aksi hâlde test-edilebilir gerçek bir fence yoktur, yalnız bir fence-handle
  saklama bookkeeping'i olur ki o da değer üretmeyen dead-API olur). Renderer'ın
  fence ring'i zaten var ve AsyncSubmitN ile çift-yönlü bağlamak bir render-thread
  *consumer*'ı ister (mevcut değil). Dolayısıyla "small clean testable hook"
  şartını karşılamıyor → "ELSE SEAL" clause'u geçerli. Eklenen 4 test gerçek
  boşluktu: ctor default-capacity dalı ve capacity==1 ring-wrap'ı hiç koşmamıştı
  (mevcut testler hep capacity 2/3/4 ile çalışıyordu, modulo asla 1 değildi).
- **Promote-on-need**: Gerçek render-thread pipeline frame-fence hook'u (AsyncSubmitN
  her enqueue'da bir Renderer fence-slot'unu wait/signal eder — Renderer +
  AsyncSubmitN'i çift-yönlü bağlayan bir orchestrator) gerçek bir render-thread
  consumer'ı (hello_engine'in main-loop'unu render-thread'e taşıyan bir entegrasyon)
  + ayrı ADR ile gelir; enqueue/wait_idle/is_busy primitive yüzeyi sabit kalır.

### 2.3 cd::mesh_shader (render/mesh_shader) — 70 → 100  [SEALED CPU-clusterizer-v1 + edge-test-topup]

- **Bağlam**: header-only Meshlet.hpp 218; greedy single-pass clusterizer
  (kVerticesPerMeshlet=64 / kTrianglesPerMeshlet=124 cap'lerine saygılı, slot_for
  vertex-dedup, flush-on-overflow) + Ritter bounds_sphere + cone_axis_cutoff alanı
  (greedy v1 doldurmaz — sentinel sıfır) + task/mesh GLSL skeleton'ları
  (VK_EXT_mesh_shader). 6→13 gtest.
- **Karar (DECIDE: cone-spatial-opt — brief §mesh_shader)**: cone/spatial
  optimizasyon **SEALED (CPU-clusterizer-v1)**; greedy DOĞRU (cap'ler + dedup +
  bounds), GPU dispatch frozen Band-0 rhi'da. Bu pass yalnız gerçek test-edilmemiş
  degenerate/boundary/dedup dallarına +7 test ekledi:
  - **IndicesPresentButPositionsEmpty**: `indices.empty() || positions.empty()`
    early-out'unun positions-empty bacağı (önceden yalnız both-empty test ediliyordu).
  - **TrailingPartialTriangleIsIgnored**: `tri + 2 < indices.size()` döngü-sınırı —
    3'ün katı olmayan index akışında dangling remainder düşürülür.
  - **SharedVerticesAreDedupedWithinMeshlet**: slot_for() dedup yolu (quad köşegeni
    paylaşılır → vertex_count 4, 6 değil).
  - **VertexCapForcesFlushAndReslot**: 30 bağımsız triangle (90 vert) 64-cap'i aşar
    → flush + new_count=3 reslot dalı; her meshlet cap-içi + triangle korunur
    (akrosss-flush kayıp yok).
  - **DegenerateTriangleStillProducesBoundedSphere**: collinear (sıfır-alan)
    triangle'da Ritter bounds tüm noktaları içerir + sonlu/non-negatif radius.
  - **FreshMeshletConeCutoffDefaultsToZero**: cone_axis_cutoff sıfır-sentinel'i
    (greedy v1 cone fit YAPMAZ — sealed promote-on-need item'ı; sentinel'i pinlemek
    gelecekteki cone-fit'in sessiz regresyonunu engeller).
  - **TaskGlslSkeletonCarriesConeCullAndPayload**: TASK skeleton non-empty +
    EmitMeshTasksEXT + cone_axis_cutoff token'ları (önceki tek GLSL testi yalnız
    MESH skeleton'ını inceliyordu).
- **Gerekçe**: cone fit ve spatial-locality reordering bir geometri-optimizasyon
  paketidir; greedy v1 doğru ve deterministik. GPU task-shader cone-cull dispatch'i
  zaten rhi'da (GLSL skeleton `cone_axis_cutoff` okuyor); cone fit'i CPU'da eklemek
  correctness için gerekli değil (sıfır-koni → cull no-op, güvenli). Eklenen 7 test
  gerçek boşluktu: degenerate-input, trailing-partial-triangle, vertex-dedup, ve
  **cap-boundary flush+reslot** (clusterizer'ın en load-bearing dalı — bir meshlet
  vertex/triangle cap'ini aşınca yeni meshlet açma + triangle koruma) önceki 6 test
  tarafından hiç tetiklenmemişti (200-vert strip testi cap'e ULAŞIYORDU ama
  triangle-conservation + multi-meshlet bütünlüğünü assert etmiyordu).
- **Promote-on-need**: Gerçek cone fit (Meshoptimizer-tarzı per-meshlet normal-koni
  apex+axis+cutoff), spatial-locality reordering (vertex-cache + meshlet bounds
  locality), ya da meshlet-LOD DAG (Nanite "clusters all the way down") gerçek bir
  GPU-driven Nanite-tarzı geometri tüketicisiyle + ayrı ADR ile gelir;
  build_meshlets + Meshlet/MeshletData yüzeyi sabit kalır (cone_axis_cutoff alanı
  zaten mevcut — fit'i doldurmak geriye-uyumlu).

---

## 3. Reddedilen alternatifler

- **render umbrella Renderer'ın src-depth'ini bu pass'te derinleştirmek**: gerçek
  derinleştirme bir render-thread *consumer*'ına (AsyncSubmitN + Renderer frame-
  fence çift-yönlü entegrasyonu) bağlı; mevcut değil. begin/submit/present zaten
  doğru + 8 Vulkan-integration testiyle kapalı. Uydurma derinlik over-engineering
  olurdu. RED — charter SEALED, yalnız gerçek header-aggregation boşluğu (+14)
  test edildi.
- **async_submit'e frame-fence hook eklemek (seal yerine)**: temiz/test-edilebilir
  bir frame-fence hook'u Renderer'ın fence ring'ine bağlanmayı + bir render-thread
  consumer'ı gerektirir; ikisi de yok. Fence-handle saklayan boş bir bookkeeping
  hook'u değer üretmeyen dead-API olurdu. RED — brief'in "ELSE SEAL" clause'u +
  frame-fence "renderer-integration concern" ifadesi tam bunun için.
- **mesh_shader cone-spatial-opt'u bu pass'te implement etmek**: bir çok-günlük
  geometri-optimizasyon paketi (cone fit + spatial reorder + LOD DAG); greedy v1
  doğru ve correctness için cone fit gerekmiyor (sıfır-koni cull no-op). GPU
  dispatch zaten frozen rhi'da. RED — promote-on-need (gerçek Nanite-tarzı tüketici).
- **Üç kütüphanenin charter-complete yüzeyini pad etmek**: render umbrella'nın
  Tonemap/MeshStats/TextLayoutMetrics/ClearColor/DrawBatchKey header'ları + async
  primitive'lerin happy-path/stress'i + clusterizer'ın temel cap/bounds davranışı
  zaten round-trip+boundary test-doluydu. Yapay test "add tests ONLY for a
  genuinely-untested branch … don't pad" kuralına aykırı. RED — yalnız gerçekten
  dokunulmamış dallar (sort-key significance, emit_all replay, chain no-match,
  ctor-default-capacity, ring-wrap, degenerate/dedup/cap-flush) hedeflendi.
- **engine/render/rhi/'ı / diğer render lib'lerini / samples'ı / hello_*'ı /
  % docs'u düzenlemek**: kapsam DIŞI (brief: SCOPE EXCLUSION — ONLY
  engine/render/{include,src,async_submit,mesh_shader}/ + docs/ADR/). RED.

## 4. Sonuçlar

- (+) 3/3 BAND-3 render-core alt-küme kütüphanesi honest-rule terminal durumuna
  geçti: üçü de charter-complete SEALED (render umbrella src-depth =
  consumer-bağlı; async_submit frame-fence = renderer-integration concern;
  mesh_shader cone-spatial-opt = geometri-optimizasyon promote-on-need) — hepsi
  promote-on-need çıkış kapısıyla. Hiçbir kütüphanede placeholder/TODO yok (zaten
  yoktu — üçü de "real aggregation / deliberately-minimal / real greedy").
- (+) 25 yeni test, üç kütüphanede gerçek test-edilmemiş aggregation/edge dalında:
  render +14 (SortKey layer/blend/pass/user_lo significance + transparent-depth-flip,
  DrawBucket emit_all GPU-free replay + reuse + empty + reserve, PostProcessChain
  empty/no-match/clear/remove-empty/params), async_submit +4 (ctor-default-capacity,
  capacity-1 ring-wrap FIFO, fresh-wait-idle, fresh-counters), mesh_shader +7
  (positions-empty, trailing-partial-triangle, vertex-dedup, cap-flush-reslot,
  degenerate-sphere, cone-cutoff-sentinel, task-GLSL). Hepsi edge/negative/contract
  + fail-on-revert; anti-flakiness korundu (cv-tabanlı async testleri, deterministik
  sentetik mesh/key fixture'ları, yeni testlerde sleep_for YOK).
- (+) SortKey blend-significance testi gerçek bir contract netleştirmesi getirdi:
  bit-layout'ta blend-group (57..56) material-id'nin (55..32) ÜSTÜNDE — testin ilk
  hâli ters varsaymıştı, gerçek layout'a göre düzeltildi + pinlendi (test-the-real-
  contract değeri; bir consumer'ın yanlış-varsayımını da engeller).
- (+) Chrome golden BYTE-IDENTICAL (fixture #5, baseline ile `cmp` eşit). Üç
  kütüphanede de SADECE test-dosyaları değişti (lib header/src davranışı aynı) →
  hello_engine render-yolu bayt-aynı. Build -Werror temiz, 0 yeni clang-tidy WAE.
- (−) Mühürler render-thread pipeline frame-fence entegrasyonunu, mesh_shader cone
  fit'ini, ya da bindless draw-stream'i bu pass'te ÜRETMEZ; consumer/prerequisite
  doğunca ayrı ADR'larla gelir. Kabul: BAND 3 render-core alt-kümesi "seal-charter-
  complete + edge-test-topup" karakterinde (baseline'ın "real aggregation /
  deliberately-minimal / real greedy" cluster'ı — real-vs-fake değil,
  complete-vs-consumer-bağlı-derinleştirme).

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: temiz+net+consumer-mantıklı boşluğu
  implement et, consumer-bağlı/prerequisite-out-of-scope olanı seal et — bu pass'in
  bölünme çizgisi (band1 + band2 + band3-foundation ADR ile aynı). Render-core'da
  üç named gap de consumer-bağlı (render src-depth) ya da renderer-integration/
  geometri-optimizasyon (async frame-fence, mesh cone) → üçü de SEALED + gerçek
  edge-coverage boşluğu test edildi.
- "deepen Renderer aggregation vs header surface; tests" (roadmap'in render umbrella
  ifadesi): Renderer.cpp'nin gerçek derinleştirmesi bir render-thread consumer'ına
  bağlı (yok) → SEAL; "tests" kısmı header-aggregation'ın (SortKey/DrawBucket/
  PostProcessChain) test-edilmemiş load-bearing dallarına +14 test olarak
  yorumlandı. Brief'in "add tests ONLY for a genuinely-untested aggregation branch
  (e.g. SortKey ordering edge, DrawBucket overflow, PostProcessChain empty/single)"
  talimatıyla birebir uyumlu (DrawBucket'ta cap/overflow YOK — std::vector büyür —
  onun yerine emit_all-replay + reuse + empty branch'leri hedeflendi).
- async_submit "frame-fence integration … or seal" → temiz testable hook'un
  Renderer-fence-ring + render-thread-consumer prerequisite'i mevcut değil → SEAL;
  brief'in "OR add a small clean frame-fence hook if testable" şartı (testable hook)
  karşılanmadığı için "ELSE SEAL". Eklenen testler ctor-default + ring-wrap gerçek
  boşluğunu kapattı.
- mesh_shader "cone/spatial clusterizer optimization (greedy today)" → cone fit
  correctness için gerekmez (sentinel-sıfır cull no-op) + GPU dispatch frozen
  rhi'da → SEAL; degenerate/trailing-partial/dedup/cap-flush gerçek edge-boşluğu
  test edildi (brief'in "degenerate mesh, single-triangle meshlet, cap boundary"
  örnekleriyle birebir).
- DrawBucket emit_all testi GPU gerektirmesin diye cd::rhi::NullCommandBuffer
  (header-only, default-constructible) kullanıldı — replay yolu önceden YALNIZ
  Vulkan SubmitDraws'tan (ICD-gated SKIP) kapsanıyordu; NullCommandBuffer onu her
  host'ta deterministik koşturur. cd::rhi zaten test DEPS'inde (NullCommandBuffer
  header-only, ek bağımlılık yok).
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/render/tests/
  (test_render.cpp), engine/render/async_submit/tests/ (test_async_submit.cpp),
  engine/render/mesh_shader/tests/ (test_mesh_shader.cpp) + bu docs/ADR/ dosyası
  altında. engine/render/include + src (Renderer.cpp/headers) DAVRANIŞ-DEĞİŞMEDİ
  (yalnız test eklendi); rhi/ + diğer render lib'leri + samples/ + hello_* + % docs
  DOKUNULMADI.
- Golden byte-identical doğrulaması: üç kütüphanenin de yalnız test dosyaları
  değişti (header/src binary davranışı aynı) → hello_engine rendering yolu
  etkilenmez; fixture #5 capture baseline ile bayt-bayt eşit doğrulandı
  (`cmp baseline.png b3rc.png` → identical, b3rc.png silindi).

## Sonraki

- BAND 3'ün geri kalan render-feature kütüphaneleri (lighting_clusters, ibl,
  cluster, texture_compress, material_authoring, texture_synth, light) bu mühür
  şablonunu (impl-the-clean-gap + seal-the-rest + targeted-edge-topup +
  promote-on-need) tekrar kullanabilir; bu grubun büyük çapraz-kesen item'ı 3×
  froxel-clustering de-dup'ıdır (cluster ↔ lighting_clusters ↔ light::ClusterGrid)
  — render-core'da DEĞİL, render-features'ta.
- Bu pass'in mühürlenen promote tetikleyicileri: render umbrella için gerçek
  render-thread pipeline (frame-fence çift-yönlü entegrasyonu, bindless draw-stream);
  async_submit için aynı render-thread consumer'ı (small-clean-testable-hook'un
  prerequisite'i); mesh_shader için Nanite-tarzı GPU-driven geometri consumer'ı
  (cone fit + spatial reorder + LOD DAG). Üçü de ayrı ADR ister, üçü de geriye-uyumlu
  (yüzey sabit).
