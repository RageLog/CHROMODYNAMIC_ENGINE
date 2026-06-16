# ADR-20260616 — ALL-MODULES-TO-100 BAND 1 Kapsam Mührü (7 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD dd98367)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 1 close-out — test-deepening + kapsam mührü pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 1 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS")
  - `docs/ADR/ADR-20260612-shader-library-architecture.md` (gluon SL-B: closure-hash §2.3)
  - `docs/ADR/ADR-20260613-gluon-module-manifest.md` (gluon 21-modül + P0/P1/P2 öncelik)
  - `docs/ADR/ADR-20260530-gameplay-library-family.md` (quest/save/ai_bt G-aile sözleşmeleri)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen
  maddeler "deferred-by-design"dır — açık (open) sayılmazlar. Her madde
  "ihtiyaç doğunca promote" çıkış kapısı taşır. Implementasyon değişikliği
  YOK; bu pass yalnız edge/negative/fuzz test derinliği ekledi + bu mührü
  kayda geçirdi.

---

## 1. Bağlam

BAND 1, completion-band'lerin en üstündeki (90–99%) 7 kütüphaneyi 100%'e
taşır. Roadmap'in honest-rule'una göre bir modül 100%'tür ancak HER
dokümante boşluk iki terminal durumdan birindeyse: (a) IMPLEMENTED + test,
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED. "TODO/placeholder"
üçüncü durumu kalamaz.

Bu 7 kütüphane zaten production-grade çekirdeğe sahipti; eksik olan yalnız
(1) belirli edge/negative/fuzz test derinliği ve (2) "ileride genişletilecek"
maddelerin resmî mührüydü. Bu pass her ikisini de kapatır: küçük/net
boşluklar test ile dolduruldu; gerçekten design-scope ya da çok-ileri olan
maddeler aşağıda dürüst gerekçeyle mühürlendi.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::frame_timing (foundation/frame_timing) — 92 → 100

- **Bağlam**: Sabit boyutlu dt-ring + O(N log N) min/mean/median/p99
  HUD istatistikleri. hello_engine main.cpp'deki statik-dizi snippet'inin
  kütüphaneleştirilmiş hali. Header-only, tahsis-yok.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Eklenen edge testleri:
  ring overflow (kapasitenin ötesinde push → eski örnek üzerine yaz +
  pencere doğru kayar), tek-örnek p99/median, partial-fill p99 indeksi,
  reset-sonrası boş-ring istatistikleri, wrap'lı stats order-bağımsızlığı.
  Çok-boyutlu HUD plot widget / GPU-timeline overlay entegrasyonu kapsam
  DIŞI — bu kütüphane sadece dt-ring + CPU istatistiğidir (görsel taraf
  cd::profile_* ailesinin işi). **SEALED**: küçük, odaklı, eksiksiz dt-ring.
- **Gerekçe**: Tek sorumluluk (recent-dt istatistiği). Pencereleme/percentile
  matematiği tam; overflow + boş + tek-örnek sınırları artık test-kilitli.
- **Promote-on-need**: Yüzdelik dilim için online-quantile (P²/t-digest)
  istenirse ayrı bir ADR + benchmark ile gelir (bugünkü N≤birkaç-bin için
  sort kabul edilir maliyettir).

### 2.2 cd::bench (foundation/bench, tooling) — 90 → 100

- **Bağlam**: Header-only mikro-benchmark aracı: warmup + inner-call
  kalibrasyonu (≥1µs/sample) + mean/median/min/max/stddev/p90/p95/p99 +
  CSV/Markdown/JSON emit + do_not_optimize bariyeri.
- **Karar**: Bir geliştirici-aracı (tooling) olarak EKSİKSİZ → **SEALED**
  (tooling-100). Engine-runtime derinliği (motor içi sürekli telemetri,
  perfetto/chrome-trace export, regression-gate sunucusu) hedef DEĞİL —
  araç "hot-path'te hızlı geri-bildirim"dir, google-benchmark klonu değil.
  Test kapsamı zaten geniş (stats sıralaması, noop inner-scale, CSV alan
  sayısı, JSON alanları, dizi-birleştirme, ölçülebilir-iş tabanı).
- **Gerekçe**: Tooling kütüphaneleri tamamlanmış+mühürlenmiş araçla 100'e
  ulaşır, motor-runtime derinliğiyle değil (roadmap "Cross-band notes:
  tooling libs"). Anti-flakiness korunmuş (sleep_for yerine determinist
  busy-loop; bkz test yorumu).
- **Promote-on-need**: CI regression-gate / multi-run karşılaştırma
  derinleşirse `cd_bench_compare` aracı (ayrı binary, zaten var) genişler;
  bench kütüphanesi yüzeyi sabit kalır.

### 2.3 cd::game::quest (game/quest) — 92 → 100

- **Bağlam**: Tahsis-hafif quest-log VM: objective tracker + journal,
  auto-complete/auto-fail kuralı, 4 durum kovası, self-describing
  binary serialize/restore (magic CDQL + version).
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Eklenen branch/failure-path
  + rollback testleri: non-active quest'te progress/complete/fail
  (kQuestNotActive), tamamlanmış/başarısız objective'e tekrar mutasyon
  (kObjectiveNotActive), bilinmeyen quest/objective, zaten-aktif/tamamlanmış/
  başarısız quest'te activate, sıfır-objective quest activate→auto-complete,
  ön-tamamlanmış objective'lerle activate→auto-complete, restore-rollback
  (yarım-bozuk buffer önceki log'u SİLER ve boş bırakır — dokümante
  davranış), version-mismatch + corrupt-status-byte restore. **SEALED**:
  journal/objective sözleşmesi tam.
- **Gerekçe**: Tüm mutasyon dönüş-kodları + auto-kural geçişleri + restore
  failure yüzeyi artık fail-on-revert testle kilitli. Reaktif-quest /
  ön-koşul-grafiği (DAG quest unlock) kapsam DIŞI (üst-katman gameplay
  scripting'in işi; quest VM bunu id ile referanslar).
- **Promote-on-need**: Quest-arası bağımlılık grafiği (A tamamlanınca B
  açılır) istenirse üst-katman bir QuestDirector + ayrı ADR ile gelir.

### 2.4 cd::game::save (game/save) — 92 → 100

- **Bağlam**: Slot-tabanlı atomik kalıcılık (tmp+rename), meta.json header,
  format flip temizliği, version-migration zinciri (edge tablosu, hop-cap),
  cloud upload/download hook, slot-id doğrulama.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Eklenen corrupt/partial-file
  + version-migration forward-compat fuzz testleri: kısmen-yazılmış (truncate)
  body fuzz (rastgele uzunlukta kesik body → load davranışı determinist),
  bozuk-meta varyantları (eksik zorunlu alan, çöp suffix, yarım sayı),
  ileri-uyumlu meta (bilinmeyen gelecek anahtarları sessizce yok sayılır →
  Phase-1 reader Phase-2 dosyayı okur), çok-adımlı migration fuzz (rastgele
  v1→vN zinciri determinist sonuç + her ara adım diske yazılır), kayıp-edge
  ortasında zincir (kMigrationMissing + disk bozulmaz), migration-fn-fail
  (kMigrationFailed + disk bozulmaz). **SEALED**: kalıcılık + migration +
  cloud-hook yüzeyi tam.
- **Gerekçe**: Atomiklik (crash-window) + forward-compat + migration
  rollback + corrupt-input dayanıklılığı artık test-kilitli. Gerçek bir
  cloud-SDK (Steam/Epic/PlayFab) bağlama kapsam DIŞI — kütüphane
  bilinçli provider-agnostik (host kendi handler'ını takar); gerçek bir
  sıkıştırma/şifreleme katmanı (cd::game::save_compression ayrı kütüphane)
  burada değil.
- **Promote-on-need**: Sıkıştırma/şifreleme body-transform pipeline'ı
  istenirse `save_with_meta` üstüne bir codec katmanı + ayrı ADR ile gelir.

### 2.5 cd::game::ai_bt (game/ai_bt) — 90 → 100

- **Bağlam**: Tahsis-hafif Behavior Tree: Sequence/Selector/Parallel
  (eşik-tabanlı) composite'leri, Inverter/Repeater/UntilSuccess decorator'ları,
  variant-blackboard, memory-sequence (running-child'dan resume), reset.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Eklenen running-state +
  abort + decorator edge testleri: Parallel'in tam başarı-eşiği + tam
  başarısızlık-eşiği sınırları, Parallel running→reset ile in-flight child
  iptali, Repeater'ın N-tur ortasında child fail'i (son sonuç propagasyonu),
  Selector/Sequence'in running-child'da reset-ile-abort'u (cursor sıfırlanır),
  nested composite running propagasyonu, UntilSuccess'in reset-arası child-state
  temizliği, child'sız decorator'ların kFailure'a düşmesi, boş Parallel'in
  vacuously-success'i. **SEALED**: composite/decorator/blackboard çekirdeği
  tam. **Reaktif-decorator (koşul yeniden-değerlendirme ile running-child'ı
  ABORT eden conditional/observer-aborts decorator) bu sürümün kapsamı
  DIŞINDA** — bugünkü model memory-sequence + threshold-parallel'dir;
  conditional-abort ayrı bir ConditionDecorator + abort-protokolü gerektirir.
- **Gerekçe**: Var olan node setinin running-state + abort (reset) + threshold
  + decorator-kompozisyon davranışı artık fail-on-revert testle kilitli.
  Reaktif-decorator gerçek bir mimari ekleme (BehaviorTree.CPP'nin
  reactive-sequence + condition-abort'u); mevcut hiçbir tüketicide gerek
  yok (CLAUDE.md "her feature'ın bir anı olmalı") → bugün dead-code olurdu.
- **Promote-on-need**: Reactive-sequence + condition-abort (LowerPriority/Self/
  Both) bir gameplay tüketicisi doğduğunda yeni node tipleri + ADR ile gelir.

### 2.6 cd::game::cutscene_player (game/cutscene_player) — 90 → 100

- **Bağlam**: Veri-güdümlü cutscene timeline: faz-dizisi + zamanlı eventler,
  playhead state-machine (Idle/Playing/Paused), per-tick fired-event buffer,
  can_skip advisory, çok-faz dt-tüketim döngüsü, JSON round-trip.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Eklenen event-window boundary
  + dt-straddle fuzz testleri: tam-sınır eventi (offset == phase_duration
  faz-sonunda ateşlenir), sınır-altı eventi, faz-başı (offset 0) eventi,
  negatif-offset eventi (sessizce atlanır), çok-küçük-tick vs tek-büyük-tick
  eşdeğerliği fuzz (aynı eventler aynı sırada, çift sayım YOK), faz-içi event
  ateşleme-sırası (authoring sırası korunur), pause-sırasında-event-yok,
  faz-sınırını-tam-straddle eden tek tick. **SEALED**: timeline player tam.
- **Gerekçe**: Pencere yarı-açıklık semantiği [(prev,next] + faz-başı [0,next]]
  + dt-straddle + idempotans + per-tick buffer temizliği artık test-kilitli.
  Easing/interpolation eval (event arası blend), gerçek bir kamera/audio/render
  backend tie-in kapsam DIŞI — kütüphane bilinçli backend-bağımsız (event
  sinyalleri yayar, yorumlamaz).
- **Promote-on-need**: Track-bazlı keyframe interpolation (Sequencer-tarzı
  curve eval) istenirse ayrı bir CutsceneTrack katmanı + ADR ile gelir.

### 2.7 cd::gluon (render/gluon) — 90 → 100

- **Bağlam**: SOTA GLSL shader-modül kataloğu (21 modül, configure-time embed)
  + IIncludeResolver + VariantDomain. Closure-hash (ADR-20260612 §2.3) editlenen
  bir paylaşılan modülü, ona bağımlı her cache'lenmiş root'u invalidate eder.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Eklenen testler: (1)
  include-closure cache-key testi — bir paylaşılan modülün içeriğini editleyen
  mutable IIncludeResolver ile CachedCompiler üstünden: ilk compile MISS+write,
  ikinci aynı-içerik compile HIT, paylaşılan modül EDİTLENDİKTEN sonra bağımlı
  root tekrar MISS (closure-hash değişti → stale-hit YOK); editlenmeyen bağımsız
  root HIT kalır; (2) önceden dedicated-entry-point testi OLMAYAN modüllerin
  kapanması — cone_atten, cotangent_frame, light_atten, ltc_polygon entry-point'leri
  resolver üzerinden çağrılarak standalone derlenir (önceden yalnız
  EveryModuleCompilesStandalone'da dolaylı kapsanıyordu). 21-modül seti
  **SEALED** (ADR-20260613 manifest P0/P1 dalgaları tamamlandı; P2 spekülatif
  modüller "no consumer yet" — tüketiciyle doğacak, bkz manifest §2.3 P2).
- **Gerekçe**: Closure-key invalidation kütüphanenin merkezî sözleşmesiydi
  (editlenen modül → bağımlı invalidate) ve artık fail-on-revert testle
  kilitli. Tüm 21 modülün her ana entry-point'i ya dedicated ya da
  compile-all kapısından geçiyor.
- **Promote-on-need**: P2 modüller (parallax/dither/vsm/cloth/refraction/
  scattering/transforms/depth_reconstruct/normal_mapping) ADR-20260613 §2.3
  kuralıyla — her biri bir tüketici/demo ile birlikte — eklenir; gluon
  yüzeyi (registry/resolver/variant) sabit kalır.

---

## 3. Reddedilen alternatifler

- **Her boşluğu implement etmek (mühür yok)**: reaktif-decorator, cloud-SDK
  bağlama, easing-interpolation, online-quantile gibi maddeler gerçek
  çok-haftalık feature'lar ya da tüketicisi-olmayan eklemeler — bugün
  yazılırsa dead-code/bakım-borcu olur. RED — honest-rule (b) maddesi tam
  bunun için var: design-scope/large-future maddeler dürüst gerekçeyle
  mühürlenir.
- **Mühür yerine % rakamını manuel 100 yapmak**: roadmap kapsam DIŞI bıraktı
  (% docs'a dokunma). Mühür kanıt + gerekçe taşır, rakam değiştirmek taşımaz.
- **Tooling kütüphanelerini (bench) engine-runtime derinliğiyle ölçmek**:
  over-scope; roadmap'in "tooling libs reach 100 by completing+sealing the
  tool" notuna aykırı. RED.

## 4. Sonuçlar

- (+) 7/7 BAND 1 kütüphanesi honest-rule terminal durumuna geçti: çekirdek
  IMPLEMENTED+test, ileri/design-scope maddeler dürüst gerekçeyle SEALED.
  Hiçbir kütüphanede placeholder/TODO kalmadı.
- (+) Eklenen testlerin hepsi edge + negative + (uygun yerde) fuzz +
  fail-on-revert; anti-flakiness korundu (sleep_for yok). Her dokunulan
  kütüphane kendi gtest binary'sinde geçer.
- (+) Her mühür bir "promote-on-need" çıkış kapısı taşır → "deferred-by-design"
  açık-boşluk olarak sayılmaz ama gelecekteki genişleme yolu nettir.
- (−) Mühürler gelecekteki feature'ları (reaktif-BT, easing, cloud-SDK,
  online-quantile) bu pass'te ÜRETMEZ; tüketici doğunca ayrı ADR'larla gelir.
  Kabul: BAND 1 hedefi "smallest gaps, fastest wins"; büyük feature'lar
  alt-band'lere / gelecek marathon'lara aittir.

---

## Varsayımlar

- Roadmap'in "100% RULE per lib" yorumu: implement small/clear gaps, seal
  genuinely design-scope/large-future ones — bu pass'in bölünme çizgisi.
- gluon closure-key testi CachedCompiler (cd::shader) üzerinden yazıldı;
  cd::gluon zaten cd::shader'a PUBLIC bağımlı (CMakeLists PUBLIC_DEPS),
  ek bağımlılık eklenmedi.
- "samples/hello_* dokunma" kuralı uygulandı: tüm değişiklikler
  engine/<group>/<lib>/tests/ + docs/ADR/ altında.

## Sonraki

- BAND 2 (80–89): bu mühür şablonu (seal-with-promote-on-need) oradaki
  66 kütüphane için tekrarlanabilir; tek gerçek feature-item cd::framegraph
  transient-aliasing grafiğidir (roadmap §BAND 2).
- Mühürlenen maddelerin promote tetikleyicileri: reaktif-BT/easing/cloud-SDK
  için gerçek bir gameplay tüketicisi; online-quantile için bir benchmark
  gereksinimi; gluon P2 için modül başına bir demo.
