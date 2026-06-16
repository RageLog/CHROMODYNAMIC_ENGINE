# ADR-20260616 — ALL-MODULES-TO-100 BAND 2 / GAME Kapsam Mührü (15 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 052f987)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 2 game group close-out — impl-where-clean + test-deepening + kapsam mührü pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 2 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; game named gap:
    trigger broad-phase §BAND 2 satır)
  - `docs/PROJECT_COMPLETION_STATUS.md` §3 (engine/game baseline %'leri + Basis
    sütunu; grup rollup ~86%, "most uniform cluster — 18/20 production, zero
    genuine kNotImpl/stub")
  - `docs/ADR/ADR-20260616-band1-scope.md` + `docs/ADR/ADR-20260616-band2-foundation-scope.md`
    (kardeş band-mühür ADR'ları, aynı şablon)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Tek "implementasyon-fırsatı" trigger'ın query-
  indexed broad-phase'iydi; aşağıda §2.15'te NOT-clean gerekçesiyle v1-SEALED
  edildi (tick imzası body-only swap noktasını zaten taşıyor → promote sıfır
  API churn). Geri kalan 14 kütüphane çekirdeği zaten production-grade'ti ve bu
  pass yalnız gerçek yüzey-boşluğu olan 4 kütüphaneye (trigger/l10n/query/
  save_compression) edge/negative test ekledi + bu mührü kayda geçirdi.
  samples/ + hello_* + % docs'a + diğer gruplara DOKUNULMADI.

---

## 1. Bağlam

BAND 2 (80–89%) game grubu 15 kütüphaneyi 100%'e taşır: dialogue 88,
input_recorder 88, l10n 88, settings 88, ai_director 85, ai_pathfinding 85,
anim_graph 85, asset_hot_reload 85, camera 85, dialog_tree 85, fsm 85,
particles_event 85, query 85, save_compression 85, trigger 82. (quest 92,
save 92, ai_bt 90, cutscene_player 90, ai_squad 75 bu grubun DIŞINDA — band1
ya da band3.)

Bu, baseline raporunun "en uniform cluster" dediği gruptur: her kütüphanenin
substantive bir .cpp'si, dokümante bir formatı, akademik atıfları ve adanmış
bir gtest binary'si var; **sıfır gerçek kNotImpl/stub** (tüm "stub/TODO"
hit'leri yorumdur). Yani çoğu için 100% = charter-complete'i + dokümante
design-scope'u kabul eden bir mühür + YALNIZ gerçek bir yüzey-boşluğu olan
yerde hedefli test-topup.

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi: 15 kütüphanenin tamamı charter-complete idi. Tek
adı geçen feature-boşluğu trigger'ın brute-force O(V·S) broad-phase'iydi
(roadmap: "integrate cd::game::query spatial broad-phase **if clean**, ELSE
seal as v1 with promote-on-need"). İnceleme NOT-clean verdi (§2.15 gerekçe) →
v1-SEALED. Geri kalan 14'te yalnız gerçekten test-edilmemiş contract/defensive
yüzeyi olan 3 kütüphaneye (l10n plural edge, query degenerate ray/radius,
save_compression RLE corruption-path) odaklı test eklendi + trigger'a re-add
occupancy-wipe ve empty-callback no-op testleri. Geri kalan yüzey zaten
round-trip + negative + boundary testleriyle doluydu, pad EDİLMEDİ.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::game::dialogue (game/dialogue) — 88 → 100

- **Bağlam**: 492 src; branching dialogue VM + DSL parser (NODE/TEXT/CHOICE/END)
  + Condition lambda gate'leri + kBrokenLink tolerance (kötü hedef → graceful).
  14 gtest / 96 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (parse + VM walk +
  choice routing + broken-link tolerance + condition gating). Yeni test
  eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: DSL parse + dallanma + koşullu choice + bozuk-link tolere etme
  yüzeyi zaten round-trip + negative testle kapalı; pad etmek roadmap "don't
  pad" kuralına aykırı olurdu.
- **Promote-on-need**: Localized-text entegrasyonu (cd::game::l10n köprüsü),
  voice-line/timing metadata, ya da nested sub-dialogue çağrı yığını gerçek bir
  narrative-tooling tüketicisiyle + ayrı ADR ile gelir; VM/parse yüzeyi sabit.

### 2.2 cd::game::input_recorder (game/input_recorder) — 88 → 100

- **Bağlam**: 267 src; HL2-tarzı record/replay, CDIR v1 LE binary format +
  timestamp cursor (frame-accurate playback). 10 gtest / 66 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (record→serialize→
  deserialize→replay round-trip + timestamp ilerleme + LE wire kararlılığı).
  Yeni test eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: Deterministik record/replay ve binary format kararlılığı zaten
  kapalı; pad olurdu. (Anti-flakiness: cursor timestamp-tabanlı, sleep_for yok.)
- **Promote-on-need**: Compressed-stream entegrasyonu (cd::game::save_compression
  köprüsü), keyframe seek/scrub, ya da çok-cihaz (gamepad+kbd) merge gerçek bir
  demo/replay-UI tüketicisiyle + ayrı ADR ile gelir; CDIR v1 format sabit kalır.

### 2.3 cd::game::l10n (game/l10n) — 88 → 100  [test-topup + SEALED]

- **Bağlam**: 457 src; key=value .lang parser + CLDR plural kategorileri
  (zero/one/two/few/many/other) + per-locale PluralRuleFn (en/tr pre-baked) +
  {n} substitution + is_rtl + L10nManager fallback chain (current→base→key) +
  locale-change observer'ları. 14→18 (+1 disk round-trip = 19 case) gtest.
  Named edge gap (roadmap): "l10n plural edge cases".
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Bu pass'te 4 gerçek
  test-edilmemiş plural-edge yüzeyi kapandı: **negatif count** (`{n}` → "-3",
  rule .other'a route), **çoklu + trailing {n}** (tek-pass replace tüm
  occurrence'ları + string-sonu off-by-one boundary'yi doğru işler),
  **non-matching token verbatim** (`{m}` / unterminated `{n ` dokunulmaz —
  parser değil, tek literal `{n}` match), ve **plural_rule_for unknown-locale
  fallback** (bilinmeyen + boş + explicit-nullptr locale güvenli en-rule'a
  düşer, asla nullptr dönmez). **SEALED**: geri kalan yüzey (en/tr round-trip,
  .other fallback, fallback chain, RTL, observer attach/detach) zaten test-dolu.
- **Gerekçe**: Negatif/çoklu/malformed {n} ve unknown-locale rule-fallback
  load-bearing edge'lerdi (UI sayaç gösterimi + bilinmeyen-dil güvenliği) ve
  substitute_count/plural_rule_for'da test-edilmemiş dallardı. ICU bağlamamak
  bilinçli (header banner: 30+ MB transitive dep'ten kaçınma) → tr/en CLDR
  rule'ları yeterli charter.
- **Promote-on-need**: Ek locale'ler için CLDR rule'ları (ar tam 6-kategori, ru
  few/many) bir gerçek lokalizasyon tüketicisiyle bir-fonksiyon-per-locale +
  register call ile gelir; ICU/Fluent selector-expression ya da gender/case
  varyantları ayrı ADR ile gelir; .lang format + manager yüzeyi sabit kalır.

### 2.4 cd::game::settings (game/settings) — 88 → 100

- **Bağlam**: 389 src; INI key=value (yorum-koruyan round-trip) + charconv
  tabanlı heuristic typed parser (bool/int/float/string). 12 gtest / 64 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (typed get/set +
  yorum koruma + round-trip + malformed-line tolerance). Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: INI parse + tip çıkarımı + comment-preserving yeniden-yazma
  zaten kapalı; pad olurdu.
- **Promote-on-need**: Section/group ([graphics]) desteği, schema-validation
  katmanı (cd::config typed-CVar mührüyle simetrik), ya da live-watch reload
  gerçek bir settings-UI tüketicisiyle + ayrı ADR ile gelir; key=value wire
  format sabit kalır.

### 2.5 cd::game::ai_director (game/ai_director) — 85 → 100

- **Bağlam**: 165 src; L4D-tarzı intensity pacing + encounter-template seçim +
  saturating score (kümülatif yoğunluk kararlı clamp'lenir). 17 gtest / 46 assert
  — 165 LOC için yoğun.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (pacing ramp +
  template select + score saturation sınırları). Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Yoğunluk pacing eğrisi + template seçimi + skor doygunluğu zaten
  17 testle (LOC-yoğun) kapalı; pad olurdu.
- **Promote-on-need**: Per-player stress modeli (L4D survivor-state), dinamik
  spawn-budget, ya da ML-güdümlü pacing gerçek bir gameplay tüketicisiyle +
  ayrı ADR ile gelir; pacing/score API yüzeyi sabit kalır.

### 2.6 cd::ai::pathfinding (game/ai_pathfinding) — 85 → 100

- **Bağlam**: 541 src; A* navmesh (Hart/Nilsson/Raphael admissible heuristic) +
  Triangulator (Recast/Snook tarzı). 16+? gtest (test_pathfinding +
  test_triangulator ayrı binary'ler) / 43 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (A* optimal-path +
  no-path + triangülasyon round-trip). Yeni test eklenmedi. **SEALED**: charter
  tam.
- **Gerekçe**: A* admissible-heuristic optimallik + navmesh triangülasyon zaten
  iki ayrı test binary'siyle kapalı; pad olurdu.
- **Promote-on-need**: Hierarchical/HPA* uzun-mesafe, dynamic obstacle re-plan
  (D*-Lite), string-pulling/funnel path-smoothing, ya da off-mesh link
  (jump/ladder) gerçek bir AI-navigation tüketicisiyle + ayrı ADR ile gelir;
  A*/Triangulator yüzeyi sabit kalır.

### 2.7 cd::game::anim_graph (game/anim_graph) — 85 → 100

- **Bağlam**: 323 src; PlayClip + 1D/2D blend-tree tick (cd::anim Pose/Skeleton
  üzerinde). 8 gtest / 56 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (clip playback +
  1D/2D blend interpolasyon + pose çıktısı). Yeni test eklenmedi. **SEALED**:
  charter tam.
- **Gerekçe**: Blend-tree node tick'i + pose blend yüzeyi zaten kapalı; pad
  olurdu. State-machine geçişleri cd::anim StateMachine'in işi (alt katman).
- **Promote-on-need**: Layered/additive blending, IK-pass hook (cd::anim_ik
  köprüsü), ya da event/notify track gerçek bir animation-tooling tüketicisiyle
  + ayrı ADR ile gelir; blend-tree API yüzeyi sabit kalır.

### 2.8 cd::game::asset_hot_reload (game/asset_hot_reload) — 85 → 100

- **Bağlam**: 309 src; note-then-flush over FileWatcher + create/delete probe +
  throttle (dosya değişikliği biriktir, frame-boundary'de tek seferde flush).
  7 gtest / 61 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi (note→flush batching +
  create/delete tespiti + throttle window). Yeni test eklenmedi. **SEALED**:
  charter tam.
- **Gerekçe**: Değişiklik biriktirme + throttle + probe yüzeyi zaten 7 testle
  (61 assert, LOC-yoğun) kapalı; pad olurdu. (Anti-flakiness: FileWatcher
  poll_once determinist, background-thread testi event-tabanlı.)
- **Promote-on-need**: OS-native watch (inotify/ReadDirectoryChangesW — cd::plugin
  mührüyle simetrik), dependency-graph'lı cascade reload, ya da asset-registry
  invalidation köprüsü gerçek bir editor live-reload tüketicisiyle + ayrı ADR
  ile gelir; note/flush API yüzeyi sabit kalır.

### 2.9 cd::game::camera (game/camera) — 85 → 100

- **Bağlam**: 314 src; Cinemachine-tarzı CameraBrain priority/blend +
  VirtualCamera (en yüksek priority aktif, blend süresi boyunca interpolate).
  11 gtest / 39 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi (priority seçimi + blend
  interpolasyon + vcam ekle/çıkar). Yeni test eklenmedi. **SEALED**: charter
  tam.
- **Gerekçe**: Priority-tabanlı aktif-vcam seçimi + blend zaten kapalı; pad
  olurdu. (cd::camera (render-core) ile karışmaz — bu gameplay-tier
  brain/vcam orchestration, o math-tier Camera/Frustum.)
- **Promote-on-need**: Look-at/follow/dolly composer modülleri, noise-tabanlı
  camera shake, ya da collision-aware boom-arm gerçek bir gameplay-camera
  tüketicisiyle + ayrı ADR ile gelir; Brain/VirtualCamera API yüzeyi sabit.

### 2.10 cd::game::dialog_tree (game/dialog_tree) — 85 → 100

- **Bağlam**: 229 src; BG3-tarzı condition-routed graph + kCondition chaining +
  O(1) hash index (node-id → node). 8 gtest / 39 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi (koşul-routed geçiş +
  condition chain + hash lookup). Yeni test eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: Koşul-yönlü graph walk + O(1) index zaten kapalı; pad olurdu.
  (dialogue ile farklı charter: dialog_tree koşul-routed BG3-graph, dialogue
  lineer-VM + DSL; ikisi de production, duplikasyon değil — farklı narrative
  modeli.)
- **Promote-on-need**: Blackboard/variable binding (FSM/BT köprüsü), weighted
  random routing, ya da dialogue-VM ile birleşik authoring gerçek bir
  narrative-tool tüketicisiyle + ayrı ADR ile gelir; graph API yüzeyi sabit.

### 2.11 cd::game::fsm (game/fsm) — 85 → 100

- **Bağlam**: header-only by-design (478 LOC Harel statechart: flat + hierarchical
  + shallow/deep history); .cpp yalnız ABI anchor. 12 gtest / 44 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (flat geçiş +
  hierarchical nesting + shallow/deep history restore). Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Harel statechart (hiyerarşi + history) yüzeyi zaten kapalı; pad
  olurdu. Header-only + constexpr-dostu → runtime stub yok.
- **Promote-on-need**: Orthogonal-region (paralel state), entry/exit/do
  action-callback genişliği, ya da SCXML import gerçek bir gameplay-FSM
  tüketicisiyle + ayrı ADR ile gelir; statechart API yüzeyi sabit kalır.

### 2.12 cd::game::particles_event (game/particles_event) — 85 → 100

- **Bağlam**: 228 src; recipe-staging burst dispatcher (GPU coupling YOK —
  bilinçli) + dense-handle + tombstone (silinen slot tombstone'lanır, handle
  generation ile geçersizlenir). 10 gtest / 73 assert.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (recipe stage +
  burst dispatch + dense-handle generation + tombstone reuse). Yeni test
  eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: Burst event staging + handle lifecycle (Run-25 iterator-
  invalidation güvenli snapshot-iteration pattern dahil, bkz MEMORY Run 25)
  zaten kapalı; pad olurdu. GPU-particle dispatch ayrı kütüphane (cd::gpu_particles,
  Band 6) — bilinçli ayrım.
- **Promote-on-need**: cd::gpu_particles ile köprü (CPU event → GPU emit kernel),
  pooled-recipe authoring, ya da timeline-driven burst sequencing gerçek bir
  VFX tüketicisiyle + ayrı ADR ile gelir; event/handle API yüzeyi sabit kalır.

### 2.13 cd::game::query (game/query) — 85 → 100  [test-topup + SEALED]

- **Bağlam**: 422 src; ray/AABB slab (Williams et al.) + frustum (n/p-vertex
  Akenine-Möller) + sphere/box over spatial-hash broad phase + dedup. 10→13
  gtest. Named edge gap (roadmap): "query degenerate rays".
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Bu pass'te 3 gerçek
  test-edilmemiş degenerate yüzeyi kapandı: **negatif-radius + empty-world
  sphere_query** (early-out guard'ları → boş set, crash değil), **slab'a paralel
  origin-dışarıda ray** (intersect_ray_aabb'nin `d[i]==0` paralel-slab dalı →
  miss; içeride → hit), ve **origin kutu-içinde raycast** (tmin<0 → t_hit=0
  clamp dalı). **SEALED**: geri kalan yüzey (nearest-hit, zero-dir/zero-dist
  nullopt, sphere sort, box overlap-only, frustum keep/drop, rebuild_from,
  surgical remove) zaten test-dolu.
- **Gerekçe**: Negatif radius, paralel-slab grazing, ve origin-inside ray-box
  load-bearing degenerate'lerdi (gameplay raycast/overlap'ta gerçekleşir) ve
  doğrudan iddia edilmemiş dallardı. Brute-force fallback uzun-ray/sparse-hash
  pathology'si için zaten kodda + test-dolu.
- **Promote-on-need**: 3D-DDA tam cell-walk (bugünkü conservative sphere-bound
  yerine), capsule/OBB shape query, ya da k-nearest/top-N variant gerçek bir
  AI-perception ya da gameplay-targeting tüketicisiyle + ayrı ADR ile gelir;
  QueryWorld facade yüzeyi sabit kalır.

### 2.14 cd::game::save_compression (game/save_compression) — 85 → 100  [test-topup + SEALED]

- **Bağlam**: 374 src; custom RLE codec (packet-based, repeat/literal flag) +
  optional LZ4 block (CD_SAVE_COMPRESSION_HAS_LZ4 gate, vcpkg/FetchContent
  3-tier). 13→15 gtest (7 RLE + 6 LZ4 + 2 yeni RLE corruption = 15; LZ4 bu
  build'de AKTİF). Named gap (roadmap): "save_compression LZ4-gated path".
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Bu pass'te 2 gerçek
  test-edilmemiş corruption-reject dalı kapandı: **repeat-run-header-payload-yok**
  (decompress_rle `pos>=blob_end` dalı → nullopt; T5 yalnız literal-truncation'ı
  kapsıyordu, bu distinct repeat-path'i), ve **empty-blob-nonzero-original_size**
  (empty-blob early-out yalnız `original_size==0`'da boş-vektör döner, aksi
  corrupt → nullopt; T4 happy-path'i kapsıyordu, bu corrupt dalı). LZ4-gated path
  zaten `#if CD_SAVE_COMPRESSION_HAS_LZ4` altında 6 testle (round-trip + all-zero
  + empty + corrupt-reject + benchmark + RLE-vs-LZ4 ratio) kapalı. **SEALED**:
  codec yüzeyi tam.
- **Gerekçe**: İki RLE corruption-reject dalı save-load güvenlik kritikti
  (bozuk blob sessiz garbage okumamalı) ve doğrudan iddia edilmemişti. LZ4 named
  gap zaten gate-test'li → ek mühür yeterli (codec opaque, RLE/LZ4 takas-edilemez
  envelope-içi dokümante).
- **Promote-on-need**: zstd codec (header "no zstd by design"), dictionary-trained
  LZ4, ya da streaming/chunked compress gerçek bir büyük-world save tüketicisiyle
  + ayrı ADR ile gelir; CompressedSave envelope + RLE/LZ4 API yüzeyi sabit kalır.

### 2.15 cd::game::trigger (game/trigger) — 82 → 100  [SEALED broad-phase-v1 + test-topup]

- **Bağlam**: 274 src; TriggerWorld enter/stay/exit + despawn-exit (subject
  occupancy'de ama bu tick görünmedi → on_exit) + 64-kanal layer-mask + AABB/
  Sphere variant shape. tick imzası **zaten** `tick(QueryWorld* query_world, ...)`
  alır (opaque forward-declared; nullptr → brute-force). 12→14 gtest. Named gap
  (roadmap, group içinde adı geçen TEK feature-boşluğu): "brute-force O(V·S)
  broad phase → integrate cd::game::query spatial broad-phase if clean, ELSE
  seal as v1 with promote-on-need".
- **Karar**: query-indexed broad-phase **NOT-clean** → **broad-phase-v1 SEALED**.
  Bu pass'te 2 gerçek test-edilmemiş yüzey kapandı: **re-add occupancy-wipe**
  (aynı owner altına yeniden add → prior occupancy silinir → subject fresh
  on_enter, stay DEĞİL; eski callback'ler tekrar tetiklenmez) ve **empty-callback
  no-op** (on_enter/stay/exit hiçbiri set değil → enter/stay/exit/subject-removal
  geçişlerinin tamamı crash etmez, occupancy bookkeeping yine doğru). **SEALED**:
  geri kalan yüzey (enter-once, stay-per-tick, exit-once, multi-trigger, layer-
  mask, disabled-noop, teleport-no-fire, remove-stops, sphere-parity, despawn-
  exit, is_inside, disable-then-find) zaten test-dolu.
- **Gerekçe (NOT-clean kararı, üç sebep)**:
  1. **İş-yükü modeli ters**: trigger'ın tipik bütçesi "onlarca volume × tek-
     haneli subject" (header banner). query::QueryWorld entity-AABB'leri
     indeksler ve şekil-overlap döner; broad-phase için her tick subject'leri
     (ya da volume'ları) bir spatial-hash'e REBUILD etmek gerekir. Bu rebuild +
     per-volume query'nin sabit maliyeti, gerçekçi ölçeklerde brute-force iç
     döngüsünden DAHA PAHALI — break-even ~1k×1k üstünde (Trigger.cpp banner).
     Yani indexed yol bugünkü tüketiciler için bir pesimizasyon olurdu.
  2. **API mismatch**: query "entity-AABB query-şekliyle-overlap mı?" sorusunu
     yanıtlar; trigger "subject-noktası volume-şekli-içinde mi?" sorusunu sorar.
     Yakın ama özdeş değil — köprü, volume'ları AABB'ye genişletip box_query
     adayları sonra precise point-in-shape ile süzmeyi gerektirir (iki-aşamalı,
     ek alloc/tick).
  3. **DAG bağımlılık-oku**: query PUBLIC cd::scene'e bağlı; trigger bugün
     cd::core/math/ecs/physics'e bağlı (scene YOK). Indexed yol cd::scene'i
     trigger'ın transitif bağımlılığına çeker — band-2'lik bir test-topup için
     orantısız bir mimari genişleme.
  Roadmap'in fallback clause'u (`ELSE seal as v1 with promote-on-need`) tam bu
  durum için var. tick imzası `QueryWorld*`'ı zaten taşıdığından promote
  **sıfır API churn**'lü body-only bir değişikliktir.
- **Promote-on-need**: Gerçek bir 1k+ volume × 1k+ subject yük profili doğunca
  (örn. büyük-ölçekli RTS proximity-trigger sahnesi), indexed yol mevcut
  `tick(query_world, ...)` parametresinin gövdesinde plumb edilir + bir perf
  golden/bench ile doğrulanır + ayrı ADR ile gelir. Subject-removal exit
  semantiği indexed yolda korunur (occupancy-sweep zaten broad-phase'ten
  bağımsız: volume'dan çıkan subject box_query'de görünmez → sweep on_exit'i
  doğru tetikler). API + event-sözleşmesi sabit kalır.

---

## 3. Reddedilen alternatifler

- **trigger'ı query-indexed broad-phase ile bu pass'te implement etmek**:
  iş-yükü modeli ters (rebuild maliyeti realistik ölçekte brute-force'tan
  pahalı), API mismatch (overlap-query ≠ point-in-volume), ve cd::scene'i
  trigger DAG'ına çeker. RED — roadmap fallback clause "ELSE seal as v1 with
  promote-on-need" tam bunun için; tick imzası swap noktasını zaten taşıyor.
- **14 charter-complete kütüphanenin yüzeyini pad etmek**: dialogue/input_recorder/
  settings/ai_director/pathfinding/anim_graph/asset_hot_reload/camera/dialog_tree/
  fsm/particles_event yüzeyi zaten round-trip+negative+boundary test-dolu (grup
  baseline'ı "most uniform cluster, every lib substantive .cpp + dedicated
  gtest"); yapay test eklemek "add tests ONLY where coverage is genuinely thin …
  don't pad" kuralına aykırı. RED — yalnız 4 kütüphanede (trigger/l10n/query/
  save_compression) gerçek test-edilmemiş contract/degenerate/corruption dalları
  kapandı.
- **dialogue + dialog_tree'yi birleştirmek (duplikasyon iddiası)**: ikisi farklı
  narrative modeli (lineer-VM+DSL vs koşul-routed BG3-graph); ikisi de production
  + test-dolu. Birleştirme bir feature-değişimi olur, kapsam DIŞI. RED.
- **% docs'u / samples'ı / diğer grupları düzenlemek**: kapsam DIŞI (brief:
  "work ONLY in engine/game/<lib>/ + docs/ADR/. NOT samples/, hello_*, other
  groups, % docs"). RED.

## 4. Sonuçlar

- (+) 15/15 BAND-2 game kütüphanesi honest-rule terminal durumuna geçti:
  çekirdek IMPLEMENTED+test, tek feature-boşluğu (trigger broad-phase) NOT-clean
  gerekçesiyle v1-SEALED, ileri/design-scope maddeler dürüst gerekçeyle SEALED,
  promote-on-need çıkış kapısıyla. Hiçbir kütüphanede placeholder/TODO kalmadı.
- (+) Sıfır yeni implementasyon (trigger broad-phase bilinçli SEALED). 11 yeni
  test, 4 kütüphanede gerçek yüzey-boşluğunda: trigger +2 (re-add occupancy-wipe,
  empty-callback no-op-across-transitions), l10n +4 (negatif-n, çoklu/trailing
  {n}, non-matching-token-verbatim, plural_rule_for unknown-fallback), query +3
  (negatif-radius/empty-world, paralel-slab-miss, origin-inside-zero-t),
  save_compression +2 (repeat-run-no-payload, empty-blob-nonzero-size). Hepsi
  edge/negative/contract + fail-on-revert; anti-flakiness korundu (yeni testlerde
  sleep_for YOK; deterministik fixture/blob inject).
- (+) Her mühür "promote-on-need" tetikleyici taşır → "deferred-by-design"
  açık-boşluk sayılmaz ama genişleme yolu nettir. trigger özelinde promote
  sıfır API churn (tick imzası swap noktasını taşıyor).
- (−) Mühürler trigger indexed-broad-phase, l10n ek-locale-CLDR, save_compression
  zstd gibi feature'ları bu pass'te ÜRETMEZ; tüketici/profil doğunca ayrı
  ADR'larla gelir. Kabul: BAND 2 game grubu "seal + targeted-topup" karakterinde
  (baseline'ın "most uniform, zero stub" cluster'ı) — büyük feature-item'lar
  (cd::framegraph aliasing) bu grup DIŞINDA (render-core).

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: küçük/net/tüketicisi-olan boşluğu
  implement et, gerçekten design-scope/large-future olanı seal et — bu pass'in
  bölünme çizgisi (band1 + band2-foundation ADR ile aynı). Game grubunda
  implement-edilebilir net boşluk ÇIKMADI (tek feature-boşluğu trigger
  broad-phase NOT-clean) → pass "seal + targeted-topup" oldu.
- trigger broad-phase "if clean" testi: üç-sebepli NOT-clean (workload-ters +
  API-mismatch + DAG-genişleme) yargısı; roadmap fallback clause açıkça
  "ELSE seal as v1 with promote-on-need" diyor → seal seçildi.
- "Topup ONLY where genuinely thin, don't pad" kuralı: 11 charter-complete
  kütüphanenin round-trip+negative+boundary yüzeyine DOKUNULMADI; yalnız
  substitute_count/plural_rule_for (l10n), intersect_ray_aabb degenerate
  dalları + sphere_query guard (query), decompress_rle iki corruption dalı
  (save_compression), add_trigger occupancy-wipe + empty-callback (trigger)
  gibi DOĞRUDAN-İDDİA-EDİLMEMİŞ dallar hedeflendi.
- "samples/hello_*/% docs/diğer-grup dokunma" kuralı uygulandı: tüm değişiklikler
  engine/game/<lib>/tests (4 dosya) + docs/ADR (bu dosya) altında. Kütüphane
  src/include'larına dokunulmadı (yeni implementasyon yok).
- Golden byte-identical doğrulaması: game kütüphaneleri rendering yolunu
  etkilemez (yalnız test-dosyası değişti, lib binary'leri bayt-aynı); fixture #5
  capture baseline ile bayt-bayt eşit doğrulandı.

## Sonraki

- BAND 2'nin geri kalan grupları (render-core/world/asset/ui ~39 kütüphane) bu
  mühür şablonunu (seal-with-promote-on-need + targeted-topup) tekrar
  kullanabilir; tek gerçek feature-item cd::framegraph transient-aliasing
  grafiğidir (roadmap §BAND 2, render-core grubu).
- Mühürlenen game maddelerinin promote tetikleyicileri: trigger indexed-broad-
  phase için 1k+ volume×subject yük profili; l10n ek-locale için gerçek
  lokalizasyon hedefi; save_compression zstd için büyük-world save; dialogue/
  dialog_tree l10n-köprüsü için narrative-tooling; particles_event GPU-köprüsü
  için VFX tüketicisi; ai_director/pathfinding derin-feature'lar için gameplay
  AI tüketicisi.
- ai_squad (75, partial — GpuBatchSolver Sprint-3 deferred) bu grubun BAND-3
  üyesi; band-2 game pass'ine dahil DEĞİL (roadmap %85 eşiğinin altında).
