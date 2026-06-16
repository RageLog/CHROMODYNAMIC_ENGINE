# ADR-20260616 — ALL-MODULES-TO-100 BAND 2 / RENDER-CORE Kapsam Mührü (10 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD cd89ab5)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 2 render-core group close-out — impl-where-clean + test-deepening + kapsam mührü pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 2 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; render-core named gaps:
    framegraph transient-aliasing/auto-barrier/dead-pass-cull, spirv_cross_glue
    coverage, debug_draw coverage — §BAND 2 satırları)
  - `docs/PROJECT_COMPLETION_STATUS.md` §4 (engine/render CORE baseline %'leri +
    Basis sütunu; grup rollup ~84%, "no real stub/skeleton libs here; the spread
    is production-vs-partial, not real-vs-fake")
  - `docs/ADR/ADR-20260616-band1-scope.md` + `docs/ADR/ADR-20260616-band2-foundation-scope.md`
    + `docs/ADR/ADR-20260616-band2-game-scope.md` (kardeş band-mühür ADR'ları, aynı
    şablon: seal-with-promote-on-need + targeted-topup)
  - `docs/ADR/ADR-20260614-d3d12-binding-model.md` (spirv_cross_glue'nun MSL
    set-per-argument-buffer + push-constant remap kararının kaynağı; bu pass o
    yüzeyi test-kilitledi)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Tek gerçek BAND-2 render-core feature-item'ı
  cd::framegraph'tı; aşağıda §2.10'da **dead-pass culling IMPLEMENTED+tested** (net
  + self-contained + tüketicisiz-olduğu-için-golden-güvenli), **transient-memory
  aliasing + auto-barrier-reordering ise SEALED** (temiz yapılamazdı — bir RHI
  placed/aliased-resource primitive'i gerektirir, o ise frozen Band-0 rhi/ kapsamı
  DIŞINDA). Geri kalan 9 kütüphane charter-grade'di; bu pass yalnız gerçek
  yüzey-/coverage-boşluğu olan 2 kütüphaneye (spirv_cross_glue +6, debug_draw +4)
  test ekledi + bu mührü kayda geçirdi. engine/render/rhi/ + samples/ + hello_* +
  % docs'a + diğer gruplara DOKUNULMADI; chrome golden BYTE-IDENTICAL.

---

## 1. Bağlam

BAND 2 (80–89%) render-core grubu 10 kütüphaneyi 100%'e taşır: material 88,
shader 88, debug_line 88, camera 85, spirv_cross_glue 85, hdr_display 85,
brdf 82, scene_ingest 82, framegraph 80, debug_draw 80. (cd::rhi 100 = Band 0
frozen; cd::gluon 90 = Band 1; render umbrella/async_submit/mesh_shader 70–78 =
Band 3 — bu grubun DIŞINDA.)

Baseline raporunun §4 değerlendirmesi: "no real stub/skeleton libs here; the
spread is production-vs-partial, not real-vs-fake". Yani çoğu için 100% =
charter-complete'i + dokümante design-scope'u kabul eden bir mühür + YALNIZ
gerçek bir coverage-boşluğu olan yerde hedefli test-topup.

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi:
- **7 charter-complete (SEAL only)**: material (51 test), shader (32),
  debug_line (20), camera (29), hdr_display (6), brdf (17), scene_ingest (13) —
  yüzeyleri zaten derin; pad EDİLMEDİ.
- **2 test-topup + SEAL**: spirv_cross_glue (4→10 test; named gap "raise coverage,
  HLSL/MSL emit matrix"), debug_draw (5→9 test; named gap "raise test coverage,
  only 5 tests, weakest in cluster").
- **1 IMPLEMENT + SEAL (tek gerçek feature-item)**: framegraph — dead-pass
  culling IMPLEMENTED+tested, transient-aliasing SEALED (RHI-primitive gerektirir,
  out-of-scope).

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::material (render/material) — 88 → 100

- **Bağlam**: 1035 src + 2.6k hdr; StandardPbr/LitPbr/Skinned/Sky/BrdfLut/
  RtClosestHit material'ları + MaterialInstance + UiVariant. 51 gtest (7 dosya),
  RT chrome-sphere-reflects-geometry + alpha-predicates + MR-contract +
  recreate-hot-reload dahil.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Material create/apply + instance MR/alpha contract + RT closest-hit
  packer + hot-reload deferred-release ordering zaten 51 testle (kümede en yüksek
  coverage) kapalı; pad roadmap "don't pad" kuralına aykırı olurdu.
- **Promote-on-need**: Yeni material arketipleri (clearcoat-stack, anisotropy,
  transmission-thin-film), bindless-material descriptor-array, ya da MaterialX
  graph-authoring gerçek bir shading-tooling tüketicisiyle + ayrı ADR ile gelir;
  MaterialDesc/MaterialInstance yüzeyi sabit kalır.

### 2.2 cd::shader (render/shader) — 88 → 100

- **Bağlam**: 756 src; CachedCompiler + glslang-backed GlslangCompiler +
  FileWatcher hot-reload + SpirvFile. 32 gtest (live-edit smoke + hot-reload +
  cached compiler dahil). Slang path bilinçli gate-stub.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: GLSL→SPIR-V compile + cache hit/miss + FileWatcher hot-reload
  (live-edit smoke dahil) zaten 32 testle kapalı; pad olurdu. Slang gate-stub
  dokümante design-scope (vendored Slang absent-by-default).
- **Promote-on-need**: Gerçek Slang frontend (HLSL2021/Slang module sistemi),
  DXC entegrasyonu (cd::spirv_cross_glue HLSL emit + DXC köprüsü), ya da
  reflection-metadata cache gerçek bir cross-API shader pipeline tüketicisiyle +
  ayrı ADR ile gelir; ICompiler/CompileDesc yüzeyi sabit kalır.

### 2.3 cd::debug_line (render/debug_line) — 88 → 100

- **Bağlam**: header-only 363 LOC; RHI-bağımsız CPU LineBatch
  (line/aabb/obb/frustum/circle/polyline/cross/sphere/arrow/grid →
  kLineList stream). 20 gtest; bgfx DebugDrawEncoder + Bevy Gizmos cited.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: 10 şekil-üretici + degenerate-kutu normalizasyonu + frustum
  unproject + ONB-fallback circle/arrow zaten 20 testle kapalı; pad olurdu.
  (GPU yarısı cd::debug_draw, §2.10-komşusu — ayrı kütüphane, bilinçli ayrım.)
- **Promote-on-need**: Text/billboard label emit, screen-space (pixel-genişlikli)
  line, ya da depth-sorted transparency gerçek bir debug-overlay tüketicisiyle +
  ayrı ADR ile gelir; LineBatch append API yüzeyi sabit kalır.

### 2.4 cd::camera (render/camera) — 85 → 100

- **Bağlam**: header-only 803 LOC; Camera/Frustum/FirstPerson/Orbit/CameraPath/
  Lens (gerçek math gövdeleri). 29 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: View/proj matris + frustum extraction + first-person/orbit
  controller + spline camera-path + lens (FOV/aperture) zaten 29 testle kapalı;
  pad olurdu. (gameplay-tier cd::game::camera brain/vcam ile karışmaz — bu
  math-tier primitive.)
- **Promote-on-need**: Reverse-Z/infinite-far projeksiyon helper'ları,
  jitter/TAA sub-pixel offset API'si (bugün hello_engine'de Halton inline), ya da
  physically-based exposure-coupled lens gerçek bir cinematic-camera tüketicisiyle
  + ayrı ADR ile gelir; Camera/Frustum/Lens yüzeyi sabit kalır.

### 2.5 cd::spirv_cross_glue (render/spirv_cross_glue) — 85 → 100  [test-topup + SEALED]

- **Bağlam**: 394 src; SPIRV-Cross C++ bridge — `translate(spirv, target, version)`
  (GLSL/HLSL/MSL) + `translate_msl(spirv, cfg)` (set-per-argument-buffer +
  push-constant remap, ADR-20260614 §4) + exception→Result policy. Named gap
  (roadmap, baseline): "only 4 tests (thin coverage) — raise coverage, HLSL/MSL
  emit matrix". 4→10 gtest.
- **Karar**: Çekirdek IMPLEMENTED + bu pass'te 6 gerçek test-edilmemiş yüzey
  kapandı: **malformed-SPIR-V exception→Result** (3 target'ta birden — geçersiz
  magic'li non-empty kelime akışı → CompilerError yakalanır → populated error,
  crash/throw DEĞİL; empty-input pre-check'ten distinct gerçek exception yolu),
  **HLSL explicit SM version** (51 = SM 5.1; auto-pick dışı `version` parametresi
  onurlanır, cbuffer emit edilir), **GLSL explicit version directive** (330 →
  `#version 330` emit), **translate_msl(cfg) entry-point cleanse** (argument-buffer
  yolu + fragment "main"→"main0" rename → entry_point non-empty), **MSL compute
  workgroup-size reflection** (local_size 8,4,2 → WorkgroupSize{8,4,2}, M6
  source-of-truth), ve **MSL fragment 1x1x1 default** (non-compute → 0→1
  normalize, sıfır-thread dispatch'i engeller). **SEALED**: GLSL/HLSL/MSL emit
  matrix + version-param + exception path + workgroup reflection artık test-dolu.
- **Gerekçe**: Bu altı yüzey load-bearing'di — malformed-input güvenliği (asla
  throw etmemeli, cross-backend pipeline'ın hata-sınırı), version-param onurlanması
  (D3D12 SM 5.1 + GL-uyumlu GLSL 330 yolu), MSL entry-point cleanse (.mm device
  MTLLibrary lookup'ı buna bağlı) ve compute workgroup reflection (Metal
  threads-per-threadgroup'un TEK kaynağı, ComputePipelineDesc'te alan yok) — ve
  doğrudan iddia edilmemiş dallardı. Önceki 4 test yalnız happy-path tek-shot
  HLSL/MSL/GLSL + empty-input'tu.
- **Promote-on-need**: DXC entegrasyonu (HLSL emit → DXIL), WGSL emit target
  (cd::ui_renderer_webgpu köprüsü), ya da SPIRV-Cross reflection-surface açığa
  çıkarma (descriptor-set introspection) gerçek bir cross-API toolchain
  tüketicisiyle + ayrı ADR ile gelir; Translate/MslBindingConfig yüzeyi sabit.

### 2.6 cd::hdr_display (render/hdr_display) — 85 → 100

- **Bağlam**: header-only 100 LOC; PQ ST-2084 encode/decode + Rec.2020 matris +
  scRGB pack, atıflı sabitler. 6 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi (PQ round-trip + Rec.2020
  dönüşüm + scRGB pack). Yeni test eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: 100 LOC'luk saf-fonksiyon yüzeyi (PQ EOTF/inverse + gamut matris +
  scRGB) 6 testle round-trip+sınır kapalı; pad olurdu. Atıflı sabitler (ST-2084
  m1/m2/c1/c2/c3) compile-time doğru.
- **Promote-on-need**: HLG transfer (BT.2100), tone-mapping-curve seçimi
  (Reinhard/ACES/AgX HDR-output varyantı, bugün cd::post composite'te), ya da
  display-capability sorgulama (RHI swapchain HDR metadata) gerçek bir HDR-output
  pipeline tüketicisiyle + ayrı ADR ile gelir; encode/decode yüzeyi sabit kalır.

### 2.7 cd::brdf (render/brdf) — 82 → 100

- **Bağlam**: header umbrella; ltc/sheen_clearcoat/sss lobe'larını topluyor;
  ~16 atıflı fonksiyon (Heitz LTC / Estevez-Kulla sheen / Burley SSS). 17 gtest
  (ltc + sheen_clearcoat + sss alt-dizinleri). GPU lobe'ları cd::gluon'da yaşar.
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi (LTC magnitude/fresnel +
  sheen normalization + SSS diffusion profile). Yeni test eklenmedi. **SEALED**:
  charter tam.
- **Gerekçe**: LTC area-light + sheen-clearcoat + SSS CPU-reference lobe'ları 17
  testle (atıf-bazlı analitik değer kontrolleri) kapalı; pad olurdu. CPU-tarafı
  bilinçli "reference"; GPU eval cd::gluon GLSL modüllerinde (Band 1, ayrı mühür).
- **Promote-on-need**: Ek lobe'lar (iridescence/thin-film, hair Marschner,
  multi-scatter GGX energy-comp ki cd::gluon'da Fdez-Aguera var), ya da CPU↔GPU
  parity golden gerçek bir material-authoring/validation tüketicisiyle + ayrı ADR
  ile gelir; lobe API yüzeyi sabit kalır.

### 2.8 cd::scene_ingest (render/scene_ingest) — 82 → 100

- **Bağlam**: 275 src; ingest_gltf_scene DFS → GPU upload + ECS entity/transform
  + AABB accum + rollback (kısmi başarısızlıkta temiz geri-al) + RenderBucket
  split. 13 gtest (scene_ingest + render_bucket_split).
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi (DFS ingest + entity/
  transform kurulum + AABB birikimi + hata-rollback + bucket opaque/alpha split).
  Yeni test eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: glTF→ECS+GPU ingest pipeline'ı + transactional rollback +
  render-bucket sınıflandırma zaten 13 testle kapalı; pad olurdu.
- **Promote-on-need**: Streaming/async ingest (cd::asset::scene_streamer gerçek
  glTF-load mührüyle simetrik, Band 6), instancing-aware merge, ya da
  material-dedup-on-ingest gerçek bir büyük-sahne streaming tüketicisiyle + ayrı
  ADR ile gelir; ingest_gltf_scene + RenderBucket yüzeyi sabit kalır.

### 2.9 cd::debug_draw (render/debug_draw) — 80 → 100  [test-topup + SEALED]

- **Bağlam**: 213 src + 143 hdr; cd::debug_line'ın GPU yarısı — kLineList pipeline
  (cd::material üzerinden) + lazily-grown host-visible VB + frames-in-flight-safe
  PARK/reclaim (outgrown buffer `frame_idx + kDestroyMargin`'e kadar park edilir,
  phase-1034 use-after-free review-bulgusu) + once-per-frame flush. Named gap
  (roadmap, baseline): "only 5 tests (weakest coverage in cluster) — raise to bar".
  5→9 gtest (5 NullDevice + 4 YENİ Vulkan-integration).
- **Karar**: Çekirdek IMPLEMENTED + bu pass'te en zayıf coverage doldurularak
  YENİ bir Vulkan-integration test dosyası eklendi (test_debug_draw_vulkan.cpp,
  no-ICD'de graceful SKIP, RTX 3080 host'ta PASS): **real-device create**
  (glslang→SPIR-V→Vulkan pipeline geçerli), **growth+park+reclaim across frames**
  (küçük→büyük batch → kapasite büyür + eski buffer PARK edilir + tam
  `1+kDestroyMargin` frame'inde reclaim edilir — phase-1034 contract'ı, daha önce
  YALNIZ "nightly hello_engine"de koşuyordu, CI'da test-edilmemişti), **çok-şekilli
  batch tek-allocation** (aabb/obb/circle/sphere/cross/arrow/grid karışık batch
  tek upload'a sığar, ikinci flush re-alloc ETMEZ), ve **empty-batch no-alloc +
  queue-tick**. **SEALED**: VB lifecycle + flush contract artık gerçek-cihaz
  test-dolu.
- **Gerekçe**: Önceki 5 test NullDevice'ti — NullDevice GLSL derleyemediğinden
  material hep invalid'di ve flush kısa-devre oluyordu, yani **load-bearing growth/
  park/reclaim yolu (phase-1034 use-after-free guard'ı) CI'da hiç koşmamıştı**.
  Bu pass o boşluğu gerçek Vulkan cihazla kapattı (kümenin "weakest coverage"
  etiketinin asıl sebebi buydu). NullDevice testleri (create-error, move,
  empty-flush no-op, kDestroyMargin static_assert, recreate-failure) korundu.
  (Anti-flakiness: frame_idx deterministik sayaç; sleep_for YOK; submit+wait_idle
  ile her frame tam retire edilir.)
- **Promote-on-need**: Pixel-level golden (line-overlay'in ekranda görünmesi —
  bugün hello_engine fixture'larında dolaylı), MRT-attachment shader-override
  matris testi, ya da multi-backend (D3D12/Metal) debug-draw parity gerçek bir
  cross-API overlay tüketicisiyle + ayrı ADR ile gelir; Renderer flush/recreate/
  destroy yüzeyi sabit kalır.

### 2.10 cd::framegraph (render/framegraph) — 80 → 100  [IMPLEMENT dead-pass-cull + SEAL aliasing]

- **Bağlam**: 322 src + 495 hdr; gerçek add_pass/compile/execute + per-pass CPU
  timing instrumentation + per-pass barrier emission (read/write state-transition,
  identity elide) + imported-resource final-state transition + PassTopology (Kahn
  topo-sort). 20→24 gtest (+ ayrı 5-test Vulkan-integration binary). Named gap
  (roadmap, **grubun TEK gerçek feature-item'ı, "HIGH-VALUE"**): "real
  transient-resource aliasing + auto-barrier + dead-pass cull (today linear
  execute)".
- **Karar (DECIDE: implement vs seal — brief §framegraph)**:
  - **dead-pass culling → IMPLEMENTED + tested.** `set_dead_pass_culling(bool)`
    (default OFF) + `culled_pass_count()` introspection eklendi. `compile()` artık
    (opt-in) backward-reachability cull yapar: bir pass LIVE'dır ancak (a) bir sink
    yazıyorsa — meaningful `final_state`'li imported resource (swapchain/dış-gözlemli
    target), VEYA (b) yazdığı bir resource sonra başka bir live pass tarafından
    okunuyor/yazılıyorsa (read-after-write + RMW producer-chain transitif erişim).
    Write'ı OLMAYAN pass HER ZAMAN tutulur (execute-callback'i izlenmeyen yan-etki
    taşıyabilir: clear/query/copy → cull etmek unsound). +4 gtest: default-OFF her
    pass'i tutar (v1 contract bayt-aynı), unconsumed-transient yazan pass cull
    edilir, sink-besleyen producer-chain korunur, write'sız side-effect pass tutulur.
  - **transient-memory aliasing + auto-barrier-reordering → SEALED (framegraph-v1).**
- **Gerekçe (üç katmanlı karar)**:
  1. **dead-pass culling temiz + self-contained + golden-güvenli**: yalnız pass
     listesi üzerinde saf bir reachability analizi; RHI'ya dokunmaz; tüketicisi
     olmadığı için (aşağı bkz) golden'ı etkilemesi imkânsız; default-OFF tüm
     mevcut testleri + v1 "her pass'i çalıştır" contract'ını bayt-aynı korur.
     → IMPLEMENT.
  2. **transient-memory aliasing temiz YAPILAMAZ — RHI primitive gerektirir,
     out-of-scope**: gerçek aliasing iki non-overlapping-lifetime transient'i TEK
     bir GPU bellek bölgesinde paylaştırmaktır; bu, `IDevice`'ta bir placed/aliased-
     resource API'si (örn. `create_aliased_texture`/heap-offset binding) ister.
     `IDevice::create_texture` bugün her zaman dedicated memory ayırır ve aliasing
     primitive'i YOK. O API'yi eklemek `engine/render/rhi/`'a dokunur — bu dispatch'in
     **açıkça yasakladığı** frozen Band-0 kapsamı. Dolayısıyla aliasing'i "temiz +
     gerçek test'li" yapmak bu pass içinde MÜMKÜN DEĞİL → brief'in "ELSE SEAL"
     clause'u tam bunun için. Lifetime-interval hesabı + bellek-overlap allocator +
     aliasing-barrier merge çok-günlük doğru-yapılması-gereken iş; yarım bir aliasing
     (yanlış reuse → sessiz GPU corruption) project quality-bar'ına aykırı olurdu.
  3. **auto-barrier-reordering separable optimizasyon**: bugünkü execute zaten
     read/write state'inden DOĞRU barrier'ları emit eder (identity elide dahil) +
     RT-ordering invariant'ını CHROMA_DEBUG'da assert eder. Pass'leri yeniden
     SIRALAMAK (reorder) DAG topo-sort + heuristic ister ve correctness için
     gerekli DEĞİL (registration-order zaten doğru); bir perf-optimizasyonu →
     promote-on-need.
  - **Golden-byte-identity gerekçesi**: `FrameGraph` SINIFI hiçbir production
    renderer tarafından kullanılmıyor — hello_engine yalnız `cd::framegraph::ColorTarget`
    /`DepthTarget` POD'larını (`Targets.hpp`, DEĞİŞMEDİ) kullanır; editor yalnız
    header'ı pass-timing için include eder, `FrameGraph` instantiate ETMEZ ve
    `execute()` ÇAĞIRMAZ. Yani chrome render-yolu `FrameGraph::execute()`'tan
    geçmez. dead-pass culling default-OFF + tüketicisiz → golden bayt-aynı
    (doğrulandı: fixture #5 baseline ile `cmp` eşit).
- **Promote-on-need**:
  - *transient-aliasing*: bir RHI placed/aliased-resource primitive'i (Band-0 rhi/
    içinde, ayrı bir RHI-genişletme dispatch'inde) eklendiğinde, framegraph
    first/last-use interval'leri + bir memory-overlap allocator + aliasing-barrier
    (kFromAlias) emit eder + bir gerçek bellek-tasarrufu golden/bench ile doğrular
    + ayrı ADR. Bugünkü `create_texture`/`compile`/`execute` API'si sabit kalır
    (aliasing dahili, opt-in bir compile-flag olur — dead-pass-cull gibi).
  - *auto-barrier-reordering*: gerçek bir multi-pass GPU-driven renderer
    framegraph'ı tüketmeye başladığında, PassTopology DAG'ı üzerinden reorder +
    cross-queue barrier scheduling + ayrı ADR ile gelir.
  - *dead-pass-cull default-ON*: gerçek bir consumer cull'a güvenmeye başladığında
    default ON'a alınabilir (bugün opt-in, çünkü tüketici yok + v1 contract korunur).

---

## 3. Reddedilen alternatifler

- **framegraph transient-memory aliasing'i bu pass'te implement etmek**: temiz
  yapılamaz — `IDevice`'ta placed/aliased-resource primitive'i yok; eklemek
  `engine/render/rhi/`'a (frozen Band-0, açıkça yasak) dokunur. Yarım/heuristik
  aliasing yanlış-reuse → sessiz GPU corruption riski taşır (quality-bar ihlali).
  RED — roadmap "ELSE SEAL" clause'u + brief "Sealing is acceptable … aliasing is
  a separable optimization" tam bunun için. dead-pass-cull (temiz + self-contained)
  IMPLEMENT edildi.
- **framegraph dead-pass-cull'ı default-ON yapmak**: hiçbir production consumer
  `FrameGraph::execute()`'u kullanmıyor; default-ON tüm mevcut testlerin barrier-
  count beklentilerini değiştirir + v1 contract'ı bozar (gereksiz risk). RED —
  opt-in + default-OFF; tüketici doğunca promote.
- **7 charter-complete render-core kütüphanesinin (material/shader/debug_line/
  camera/hdr_display/brdf/scene_ingest) yüzeyini pad etmek**: yüzeyleri zaten
  round-trip+negative+boundary test-dolu (material 51, shader 32, camera 29 dahil);
  baseline "no real stub/skeleton libs here". Yapay test "add tests ONLY where
  coverage is genuinely thin … don't pad" kuralına aykırı. RED — yalnız 2
  kütüphanede (spirv_cross_glue 4→10, debug_draw 5→9) gerçek coverage-boşluğu
  kapandı.
- **debug_draw'a NullDevice-only test eklemek**: NullDevice GLSL derleyemez →
  material hep invalid → flush kısa-devre → growth/park/reclaim yolu (asıl boşluk)
  test-edilemez. RED — gerçek Vulkan-integration testi (no-ICD'de SKIP) eklendi,
  bu phase-1034 use-after-free contract'ını ilk kez CI'da koşturuyor.
- **engine/render/rhi/'ı / samples'ı / hello_*'ı / % docs'u / diğer grupları
  düzenlemek**: kapsam DIŞI (brief: "work ONLY in engine/render/<lib>/ + docs/ADR/.
  NOT engine/render/rhi/, NOT samples/, hello_*, other groups, % docs"). RED.

## 4. Sonuçlar

- (+) 10/10 BAND-2 render-core kütüphanesi honest-rule terminal durumuna geçti:
  9'u çekirdek-IMPLEMENTED+test (charter-complete ya da test-topup'lı), framegraph
  dead-pass-cull IMPLEMENTED+tested, transient-aliasing NOT-clean (RHI-primitive
  out-of-scope) gerekçesiyle v1-SEALED — hepsi promote-on-need çıkış kapısıyla.
  Hiçbir kütüphanede placeholder/TODO kalmadı.
- (+) 1 gerçek implementasyon (framegraph dead-pass culling, opt-in + introspection).
  14 yeni test, 3 kütüphanede gerçek coverage-boşluğunda: framegraph +4
  (default-off-keeps-all, prune-unconsumed-transient, keep-producer-chain,
  keep-write-less-side-effect), spirv_cross_glue +6 (malformed→Result-3-target,
  HLSL-SM51, GLSL-330-directive, MSL-cfg-entry-point, MSL-compute-workgroup,
  MSL-frag-unit-workgroup), debug_draw +4 (real-device-create, growth/park/reclaim
  across frames, multi-shape-single-alloc, empty-batch-no-alloc). Hepsi
  edge/negative/contract/integration + fail-on-revert; anti-flakiness korundu
  (deterministik frame_idx + seeded/sabit fixture; sleep_for YOK).
- (+) Chrome golden BYTE-IDENTICAL (fixture #5, baseline ile `cmp` eşit). framegraph
  değişikliği tüketicisiz + opt-in default-OFF olduğundan render-yolunu etkilemez.
  Build -Werror temiz, 0 yeni clang-tidy WAE.
- (+) Her mühür "promote-on-need" tetikleyici taşır → "deferred-by-design" açık-boşluk
  sayılmaz ama genişleme yolu nettir. framegraph aliasing özelinde tetikleyici net:
  bir RHI placed/aliased-resource primitive'i (ayrı Band-0 rhi/ dispatch'i).
- (−) Mühürler framegraph transient-aliasing/auto-reorder, spirv_cross_glue
  DXC/WGSL, debug_draw pixel-golden gibi feature'ları bu pass'te ÜRETMEZ;
  prerequisite/consumer doğunca ayrı ADR'larla gelir. Kabul: BAND 2 render-core
  grubu "seal + targeted-topup + tek-temiz-feature" karakterinde (baseline'ın
  "production-vs-partial, not real-vs-fake" cluster'ı); tek HIGH-VALUE item'ın
  (framegraph) temiz-yapılabilir yarısı (dead-pass-cull) yapıldı, RHI'ya-bağlı
  yarısı (aliasing) dürüstçe sealed.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: küçük/net/tüketicisi-olan/temiz boşluğu
  implement et, RHI-primitive-gerektiren/large-future olanı seal et — bu pass'in
  bölünme çizgisi (band1 + band2-foundation + band2-game ADR ile aynı). Render-core'da
  tek implement-edilebilir temiz boşluk framegraph dead-pass-cull'dı; aliasing
  RHI-bağımlı (out-of-scope) → SEALED.
- framegraph "implement IF clean + real test + golden byte-identical, ELSE SEAL"
  testi: dead-pass-cull üçünü de geçti (temiz + 4 test + golden bayt-aynı) →
  IMPLEMENT; transient-aliasing "clean" testinden kaldı (RHI placed-resource
  primitive yok, eklemek frozen rhi/ kapsamına girer) → SEAL. Brief açıkça
  "Sealing is acceptable — aliasing is a separable optimization" diyor.
- "Topup ONLY where genuinely thin, don't pad" kuralı: 7 charter-complete
  kütüphanenin (material 51 / shader 32 / camera 29 / debug_line 20 / brdf 17 /
  scene_ingest 13 / hdr_display 6) test yüzeyine DOKUNULMADI; yalnız iki named-gap
  kütüphanesinde (spirv_cross_glue "only 4 tests", debug_draw "only 5 tests,
  weakest") gerçek test-edilmemiş yüzey hedeflendi.
- "engine/render/rhi/ + samples/hello_*/% docs/diğer-grup dokunma" kuralı
  uygulandı: tüm kaynak/test değişiklikleri engine/render/{framegraph,spirv_cross_glue,
  debug_draw} altında + bu ADR docs/ADR'da. rhi/ + samples/ + hello_* + % docs'a
  DOKUNULMADI. (hello_engine yalnız framegraph header-only `Targets.hpp`'a transitif
  bağlı olduğu için RELINK oldu — Targets.hpp DEĞİŞMEDİ, davranış aynı, golden
  bayt-aynı.)
- Golden byte-identical doğrulaması: framegraph FrameGraph SINIFI tüketicisiz +
  dead-pass-cull default-OFF; spirv_cross_glue/debug_draw yalnız test-dosyası
  değişti (lib binary'leri davranış-aynı). fixture #5 capture baseline ile
  bayt-bayt eşit doğrulandı (`cmp baseline.png b2rc.png` → identical, b2rc.png
  silindi).

## Sonraki

- BAND 2'nin geri kalan grupları (world/asset/ui ~36 kütüphane) bu mühür şablonunu
  (seal-with-promote-on-need + targeted-topup) tekrar kullanabilir. Render-core'un
  tek HIGH-VALUE feature-item'ı (framegraph) bu pass'te kapandı (dead-pass-cull
  done + aliasing sealed); BAND 2'de başka render-core feature-build kalmadı.
- framegraph transient-aliasing promote tetikleyicisi: bir RHI placed/aliased-
  resource primitive'i. Bu, frozen Band-0 rhi/'a bir genişletme dispatch'i ister
  (IDevice'a heap-offset binding / create_aliased_texture); o landıktan sonra
  framegraph aliasing'i first/last-use interval + memory-overlap allocator +
  aliasing-barrier ile opt-in compile-flag olarak eklenir + bench/golden ile
  doğrulanır + ayrı ADR.
- spirv_cross_glue DXC/WGSL emit + debug_draw pixel-golden + multi-backend
  debug-draw parity: ilgili cross-API toolchain / overlay tüketicileri doğunca
  ayrı ADR'larla.
- cd::gluon (90, Band 1) + cd::rhi (100, Band 0) + render umbrella/async_submit/
  mesh_shader (70–78, Band 3) bu grubun DIŞINDA; kendi band-mühürlerinde ele alınır.
