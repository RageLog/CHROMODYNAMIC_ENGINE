# ADR-20260616 — ALL-MODULES-TO-100 BAND 2 / FOUNDATION Kapsam Mührü (12 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 2c102a5)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 2 foundation group close-out — impl-where-clean + test-deepening + kapsam mührü pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 2 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; events/config/plugin
    named gaps §BAND 2 tablo)
  - `docs/PROJECT_COMPLETION_STATUS.md` §1 (foundation baseline %'leri + Basis sütunu)
  - `docs/ADR/ADR-20260616-band1-scope.md` (kardeş band-mühür ADR'ı, aynı şablon)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Tek implementasyon değişikliği cd::events
  priority-ordered delivery'dir (aşağıda §2.10, küçük+net+tüketici-mantıklı);
  geri kalan 11 kütüphane çekirdeği zaten production-grade'ti ve bu pass yalnız
  gerçek yüzey-boşluğu olan yerlere edge/negative test ekledi + bu mührü kayda
  geçirdi. samples/ + hello_* + % docs'a DOKUNULMADI.

---

## 1. Bağlam

BAND 2 (80–89%) foundation grubu 12 kütüphaneyi 100%'e taşır: concurrency,
math, core, io, log, time, diag, profile, vfs (hepsi zaten derin test edilmiş
124/140/118/42/24/26/16/10/11 gtest ile) + events (named gap: async/priority/
weak-token delivery) + config (named gap: persist-only, typed-CVar-schema yok)
+ plugin (named gap: HotReload "ince 35 LOC").

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi: 12 kütüphanenin tamamı charter-complete idi; tek
gerçek "küçük+net+tüketici-mantıklı" implementasyon-fırsatı events priority
delivery'siydi (sync bus'a ek bağımlılık gerektirmez). config typed-schema ve
plugin background-reload gerçek çok-feature genişlemelerdir (validation/clamping/
registration-DSL; thread+signal protokolü) ve tüketicisi-yok → dürüst gerekçeyle
mühürlendi. 9 "deepen+seal" kütüphanesinde yalnız gerçekten test-edilmemiş
defensive-branch/contract bulunan yerlere (profile BufferSink clear()+zero-clamp,
core Version packed() field-ordering) odaklı test eklendi; geri kalan yüzey zaten
round-trip + negative + boundary testleriyle doluydu, pad EDİLMEDİ.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::concurrency (foundation/concurrency) — 88 → 100

- **Bağlam**: 26 header / ~3.7k LOC; gerçek Chase-Lev work-stealing deque +
  hazard-pointer reclaim, thread pool, WorkStealingThreadPool, JobGraph (DAG,
  exactly-once submission — bkz [[jobgraph-exactly-once-submission]]), CoroTask,
  Channel/Future/Latch/Barrier/SpinLock/Backoff. 124 gtest 7 dosyada
  (deterministic-executor + hazard-ptr + job-graph + threadpool + work-stealing
  deque + pool ayrı binary'ler).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (stress + fuzz +
  per-node execution-count assert + ctest TIMEOUT). Bu pass'te yeni test
  eklenmedi — yüzey (lock-free primitive seti + DAG scheduler + coroutine task)
  zaten production-bar'ında ve regresyon-kilitli. **SEALED**: charter tam.
- **Gerekçe**: Lock-free reclamation + work-stealing balance + DAG topology
  invariant'leri zaten fail-on-revert testle kapalı; pad etmek roadmap'in
  "don't pad" kuralına aykırı olurdu.
- **Promote-on-need**: NUMA-aware steal heuristic, priority-aware deque, ya da
  io_uring/IOCP async-IO entegrasyonu gerçek bir profil-güdümlü ihtiyaçla +
  ayrı ADR ile gelir; primitive yüzeyi sabit kalır.

### 2.2 cd::math (foundation/math) — 88 → 100

- **Bağlam**: 31 header / ~2.6k LOC; constexpr Vec/Mat (4×4 invers), Quat
  slerp/log/pow, Transform, Duff-ONB, Catmull-Rom/CubicBezier spline, Perlin/
  Simplex noise, Easing/Damping/Smoothstep, QuadraticSolver, BarycentricInterp,
  RangeMap, Statistics/Histogram. 140 gtest tek dosyada (37 suite grubu).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi — her header en az
  bir suite grubuyla kapsanıyor (Vec 12, Quat 7+slerp 3+log 3+pow 2, Mat 5+
  invers 4+perspektif 2+ortho 1+lookAt 1, Functions 7, QuadraticSolver 5,
  RangeMap 5, Statistics 5, Noise 5, Easing 6 …). Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: 140 test zaten round-trip + edge (singular matris, degenerate
  bary, sıfır-uzunluk quat normalize, range clamp sınırları) kapsıyor; ek test
  pad olurdu. Math header-only + constexpr → ABI/runtime stub yok.
- **Promote-on-need**: SIMD-vektörize (SSE/AVX/NEON) Vec/Mat yolu ya da
  dual-quaternion blend yardımcıları bir hot-path profil ihtiyacıyla + ayrı ADR
  ile gelir; skaler constexpr referans yüzeyi korunur.

### 2.3 cd::core (foundation/core) — 85 → 100

- **Bağlam**: ~4.7k LOC header (Handle/HandleStore, SmallVector, PoolAllocator/
  FrameAllocator/ArenaScope, Result/ErrorCode/ErrorFormat, CVar/CVarRegistry,
  Bitset/BitOps/EnumFlags, FixedString/StringSplit, RingBuffer, Ref, ScopeGuard,
  RetryPolicy, CounterTable, Version + portability shim) + 2 ince .cpp
  (ErrorFormat + CorePlaceholder ODR anchor). 118 gtest 30 suite grubunda.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Bu pass'te bir
  gerçek yüzey-boşluğu kapandı: **Version.packed() bit-layout sözleşmesi** artık
  test-kilitli (`CoreSmoke.VersionPackedFieldOrdering` — major high-16 / minor
  <<8 / patch low-byte; major-domine sıralama; sıfır-versiyon sentinel). Önceden
  yalnız `packed()>0` ve makro `CD_VERSION_ENCODE` dolaylı kapsanıyordu; alan
  sırası (regresyon en olası noktası) doğrudan iddia edilmiyordu. **SEALED**:
  geri kalan yüzey (Handle generation-counter, SmallVector inline→heap geçiş,
  Pool/Frame allocator reset, Result monadic, CVar typed get/set) zaten
  test-dolu.
- **Gerekçe**: Version.packed() comparison'lar (build artifact gating) için
  load-bearing; alan-sırası swap'ı sessiz versiyon-karşılaştırma bug'ı üretirdi.
  Diğer yüzeyi pad etmek roadmap "don't pad" kuralına aykırı olurdu.
- **Promote-on-need**: SemVer tag karşılaştırma (pre-release precedence) ya da
  CalVer asset-version varyantı (Version.hpp yorumundaki "future") gerçek bir
  versiyonlama tüketicisiyle + ayrı ADR ile gelir.

### 2.4 cd::io (foundation/io) — 85 → 100

- **Bağlam**: 8 header / 933 LOC; BinaryStream (LE wire, string round-trip,
  seek/release), BitStream (değişken genişlik, mask-truncate), ByteBuffer,
  Crc32 (bilinen vektör + incremental==one-shot), Endian (bswap 16/32/64 +
  float/double round-trip), Framing (chunked-reassemble, oversized-error),
  Hex (encode/decode + invalid-char/odd-length reject), PathUtils (filename/
  stem/extension/parent, zero-alloc string_view). 42 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi — her header
  round-trip + negative (EndOfStream, OversizedFrame, RejectsInvalidChar,
  RejectsOddLength, AllZeroMaxWidth) kapsıyor. Yeni test eklenmedi. **SEALED**:
  charter tam.
- **Gerekçe**: 42 test serileştirme yüzeyini (LE wire-format kararlılığı dahil)
  ve hata yollarını zaten kapatıyor; ek test pad olurdu.
- **Promote-on-need**: Big-endian wire-mode, memory-mapped okuyucu, ya da
  varint/zigzag kodlama gerçek bir asset/net tüketicisiyle + ayrı ADR ile gelir.

### 2.5 cd::log (foundation/log) — 85 → 100

- **Bağlam**: 10 header / ~1k LOC; ConsoleLogger/JsonLogger, RingBufferSink,
  AuditTrail, Format (compile-time format), PanicDump, LogService, LogLevel/
  LogRecord. 24 gtest 8 suite grubunda.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi (RingBufferSink 6,
  AuditTrail 3, JsonLogger 3, Format 3, PanicDump 3, Service 3, Console 2,
  Level 1). Yeni test eklenmedi. **SEALED**: charter tam.
- **Gerekçe**: Sink fan-out + ring retention + JSON emit + panic-dump yolu zaten
  kapalı; ek test pad olurdu. cd::events EventRecorder yorumundaki "JSONL
  persistence cd::log JsonlBackend" zaten LogService/JsonLogger ile karşılanmış.
- **Promote-on-need**: Async/lock-free log queue (worker-drain), syslog/ETW
  platform sink'leri, ya da log-rotation policy gerçek bir shipping-telemetri
  ihtiyacıyla + ayrı ADR ile gelir.

### 2.6 cd::time (foundation/time) — 84 → 100

- **Bağlam**: 8 header + 1 .cpp; HiResClock/SteadyClock/SimClock/IClock,
  FramePacer, RateLimiter, TimerQueue (cv-tabanlı, sleep_for YOK), IntervalTicker,
  Types. 26 gtest 8 suite grubunda (SimClock 8, FramePacer 4, RateLimiter 3,
  TimerQueue 3, IntervalTicker 2 …).
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: SimClock scale/pause + FramePacer hedef-dt + RateLimiter token-
  bucket + cv-tabanlı TimerQueue (anti-flakiness uyumlu) zaten kapalı; ek test
  pad olurdu.
- **Promote-on-need**: Audio-clock domain'i ya da network-time-sync (NTP-tarzı
  drift düzeltme) gerçek bir tüketiciyle + ayrı ADR ile gelir.

### 2.7 cd::diag (foundation/diag) — 82 → 100

- **Bağlam**: 3 header + 2 .cpp; signal-tabanlı CrashReporter (SIGSEGV/ABRT/FPE),
  Assert (yapılandırılabilir handler), DeadlineMonitor. 16 gtest 3 suite grubunda
  (Assert 6, CrashReporter 6, DeadlineMonitor 4) — 3 header için yoğun.
- **Karar**: Çekirdek IMPLEMENTED + zaten derin test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Sinyal-handler kurulum/restore + assert-handler injection +
  deadline-aşımı tespiti zaten kapalı; signal-handler testleri doğası gereği
  platform-hassas, daha fazlasını pad etmek flake riski + sıfır kazanç olurdu.
- **Promote-on-need**: Minidump/breakpad-tarzı symbolized backtrace ya da
  Windows SEH/Vectored-handler derinliği gerçek bir crash-telemetri pipeline
  ihtiyacıyla + ayrı ADR ile gelir.

### 2.8 cd::profile (foundation/profile) — 82 → 100

- **Bağlam**: 5 header + 1 .cpp; Scope (RAII CPU timer + CD_PROFILE_SCOPE),
  BufferSink (in-memory ring), ChromeTraceSink (gerçek chrome-trace JSON),
  CsvSink, StatsAggregator. 10 gtest.
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Bu pass'te 2 gerçek
  test-edilmemiş defensive-branch kapandı: **BufferSink.clear()** (kapasiteyi
  korur, sonrasında yeniden kullanılabilir) ve **zero-capacity ctor clamp**
  (`capacity==0 ? 1U` — snapshot rotate-path'inde sıfıra-bölmeyi önler). İkisi de
  `submit()` ile determinist Sample inject ederek test edildi — sleep_for YOK
  (anti-flakiness; mevcut Scope-tabanlı testler timing kullanıyor ama bunlar
  PRE-EXISTING, kapsam-kilidi gereği dokunulmadı). **SEALED**: sink yüzeyi tam.
- **Gerekçe**: clear() ve zero-clamp savunma kodu test-edilmemişti (sessiz
  regresyon yüzeyi); diğer sink'ler (chrome-trace JSON şekli, CSV alanları,
  stats aggregation) zaten kapalı. Görsel HUD/overlay tarafı cd::profile_*
  ailesinin işi (ayrı kütüphaneler).
- **Promote-on-need**: Per-thread lock-free ring (Scope.hpp yorumundaki
  "per-thread ring buffer"), sampling-profiler, ya da hierarchical call-tree
  sink gerçek bir telemetri ihtiyacıyla + ayrı ADR ile gelir.

### 2.9 cd::vfs (foundation/vfs) — 82 → 100

- **Bağlam**: 4 header + 1 .cpp; overlay VFS (mount-priority katmanları,
  mod-override), FilesystemSource, MemorySource, IFileSource arayüzü. 11 gtest
  3 suite grubunda (Overlay 4, MemorySource 4, Filesystem 3).
- **Karar**: Çekirdek IMPLEMENTED + zaten test edildi. Yeni test eklenmedi.
  **SEALED**: charter tam.
- **Gerekçe**: Mount-priority çözümleme + mod-override + filesystem/memory
  kaynak okuma zaten kapalı; ek test pad olurdu.
- **Promote-on-need**: Pak/zip arşiv kaynağı (cd::asset::pak ile köprü), write-
  through overlay, ya da async/streaming okuma gerçek bir asset-streamer
  tüketicisiyle + ayrı ADR ile gelir.

### 2.10 cd::events (foundation/events) — 82 → 100  [IMPLEMENTED + SEALED]

- **Bağlam**: 406 LOC header typed pub/sub EventBus (shared_mutex subscriber
  registry, ScopedConnection RAII, deferred queue_publish/drain), EventRecorder
  (in-memory ring). 14→18 gtest. Named gap: async/priority/weak-token delivery
  deferred.
- **Karar**: Named gap'in **priority** parçası IMPLEMENTED + test edildi (küçük,
  net, tüketici-mantıklı, ek-bağımlılık-yok): `subscribe<EventT>(handler,
  priority)` overload'u — yüksek priority ÖNCE çalışır, eşitlikte subscription
  sırası korunur (stable insert). No-priority overload priority 0'a yönlenir →
  v1/v2 FIFO davranışı bayt-bayt korunur. 4 yeni test: higher-priority-first,
  equal-priority-FIFO-stable, default-overload-is-priority-0, priority-aware
  RAII-unsubscribe doğru entry'yi siler. Named gap'in **async + weak-token + one-
  shot + max-concurrency** parçaları **SEALED**: async fan-out zaten ayrı
  `cd::concurrency::EventBus`'ta yaşıyor (bu bus'ın charter'ı determinist,
  sıralı, aynı-thread sync pub/sub); weak-token/coroutine varyantları
  promote-on-need.
- **Gerekçe**: Priority sync-bus'a doğal bir sıralama-düğmesi (örn. logging/audit
  handler'ı gameplay handler'ları state mutate etmeden ÖNCE event'i görmeli) ve
  sıfır yeni bağımlılık gerektiriyor → bugün implement edilebilir net kazanç.
  Async ise ThreadPool/coroutine-scheduler bağlama gerektiren çapraz-kesen bir
  feature ve zaten concurrency tarafında mevcut → bu sync bus'ta dead-code
  olurdu.
- **Promote-on-need**: weak_ptr-token aliveness (handler sahibi yok olunca
  otomatik detach) ya da coroutine-await delivery gerçek bir tüketici doğunca
  yeni overload + ADR ile gelir; mevcut subscribe/publish/queue_publish/drain
  yüzeyi sabit kalır.

### 2.11 cd::config (foundation/config) — 80 → 100  [SEALED narrow scope]

- **Bağlam**: 192 LOC header; CVarRegistry binary save/load (magic 'CVAR' +
  version + sorted-key canonical output + std::expected hata yolu, 4 tip-tag:
  bool/int64/double/string). 8 gtest (4-tip round-trip, canonical-order,
  overwrite-existing, BadMagic/BadVersion/Truncated/BadTypeTag negative). Named
  gap: persist-only dar kapsam, typed CVar schema yok.
- **Karar**: Dar persist-only kapsam **SEALED**. typed-CVar-schema (declared
  default + min/max + description + registration DSL + validation/clamping)
  gerçek bir çok-feature genişlemedir ve mevcut hiçbir tüketicisi yok →
  bugün dead-code/over-engineering olurdu. config'in charter'ı: CVarRegistry'yi
  determinist, git-dostu, version'lı, forward-reject-eden bir binary blob'a
  serileştirmek/geri yüklemek — bu tam ve test-kilitli. Yeni test eklenmedi
  (8 test 4 tipi + 4 hata yolunu + canonical-order garantisini zaten kapsıyor).
- **Gerekçe**: Typed-schema katmanı cd::core::CVarRegistry'nin (zaten typed
  get/set'i var) ÜSTÜNE bir authoring/validation katmanı; bir editor/inspector
  ya da CVar-console tüketicisi olmadan yazmak honest-rule (b)'nin tam hedefi
  olan "tüketicisi-olmayan-büyük-ekleme" durumudur. Persist wire-format zaten
  reproducible (sorted-key) + forward-reject (version) → kalıcılık sözleşmesi
  eksiksiz.
- **Promote-on-need**: Typed CVar schema (CVarSpec<T>{default,min,max,desc} +
  register + validate-on-set) bir editor CVar-inspector ya da dev-console
  tüketicisi doğunca ayrı bir katman + ADR ile gelir; mevcut save/load wire-
  format sabit kalır (sadece ek alan eklenirse version bump).

### 2.12 cd::plugin (foundation/plugin) — 80 → 100  [SEALED HotReload-v1]

- **Bağlam**: 422 hdr + 788 src; FileWatcher (polling + native, deterministik
  poll_once + background-thread), Loader (gerçek dlopen/LoadLibrary + RAII unmap
  + factory-symbol çözümleme), HotReloader (180 LOC header: mount/unmount/poll
  version-fn + before_unload/after_load hook + last-known-good fallback;
  HotReload.cpp 35 LOC yalnız default mtime-watcher + factory'dir). 15 gtest.
  Named gap: "HotReload thin (35 LOC)".
- **Karar**: HotReload-v1 kapsam **SEALED**. Roadmap'in "thin 35 LOC" notu
  yalnız .cpp'yi (default mtime-watcher + factory wiring) sayıyor — gerçek
  reload mantığı 180 LOC'luk HEADER'da ve zaten eksiksiz + derin test edilmiş:
  mount/poll-no-change/version-change-triggers-reload/before_unload+after_load-
  order/failed-reload-keeps-previous (load-before-unload sırası → başarısız
  reload host'u plugin'siz BIRAKMAZ)/poll-before-mount-error/default-version-
  zero-for-missing. FileWatcher de tam test-dolu (poll_once mtime-fire, missing-
  file-error, already-watching-error, native-factory-non-null, background-thread-
  fires). Yeni test eklenmedi — yüzey zaten production-bar'ında. Background-
  thread-driven / signal-driven / inotify/ReadDirectoryChangesW-native otomatik
  reload promote-on-need.
- **Gerekçe**: Polling-tabanlı reload (host frame/editor-tick başına poll())
  + RAII-güvenli swap + last-known-good fallback + injectable load/version-fn
  (test için mock) bir hot-reload v1 charter'ının tamamıdır; explicit-poll
  model bilinçli tasarım (no background thread, no signals, no deadlocks —
  predictable ordering, bkz HotReload.hpp banner). Native OS-event-driven watch
  gerçek bir editor live-reload UX'iyle gelir.
- **Promote-on-need**: OS-native değişiklik bildirimi (ReadDirectoryChangesW /
  inotify / FSEvents) ile background otomatik reload, ya da dependency-graph'lı
  çoklu-plugin reload sırası bir editor plugin-manager tüketicisi doğunca +
  ayrı ADR ile gelir; HotReloader API yüzeyi (mount/poll/hook) sabit kalır.

---

## 3. Reddedilen alternatifler

- **config typed-CVar-schema'yı bu pass'te implement etmek**: validation/
  clamping/registration-DSL gerçek bir çok-haftalık authoring katmanı; mevcut
  tüketici yok → dead-code/bakım-borcu. RED — honest-rule (b) maddesi tam bunun
  için var (tüketicisi-olmayan-büyük-ekleme dürüst gerekçeyle mühürlenir).
- **plugin background/native-event reload'u bu pass'te implement etmek**:
  thread+signal+OS-native-watch protokolü; v1 charter (explicit-poll) bilinçli
  ve tam; editor live-reload UX'i olmadan yazmak speculative. RED.
- **events async/weak-token delivery'yi de implement etmek**: async fan-out
  zaten cd::concurrency::EventBus'ta; bu sync bus'ta tekrar yazmak duplikasyon
  + dead-code. RED — yalnız priority (net+bağımsız+tüketici-mantıklı) implement
  edildi.
- **9 "deepen+seal" kütüphanesinin yüzeyini pad etmek**: concurrency/math/io/
  log/time/diag/vfs yüzeyi zaten round-trip+negative+boundary+stress test-dolu;
  yapay test eklemek roadmap'in "add tests ONLY where coverage is genuinely
  thin … don't pad" kuralına aykırı. RED — yalnız 2 gerçek defensive-branch
  (profile clear()+zero-clamp) ve 1 contract (core Version field-order)
  kapandı.
- **% docs'u / samples'ı düzenlemek**: kapsam DIŞI (brief: "Do NOT touch
  samples/, hello_*, other groups, or the % docs"). RED.

## 4. Sonuçlar

- (+) 12/12 BAND-2 foundation kütüphanesi honest-rule terminal durumuna geçti:
  çekirdek IMPLEMENTED+test, ileri/design-scope maddeler dürüst gerekçeyle
  SEALED, promote-on-need çıkış kapısıyla. Hiçbir kütüphanede placeholder/TODO
  kalmadı.
- (+) Tek implementasyon: events priority-ordered delivery (geriye-uyumlu —
  v1/v2 FIFO bayt-bayt korunur, default overload priority 0'a yönlenir). 4 yeni
  events testi + 2 profile (BufferSink clear/zero-clamp) + 1 core (Version
  packed field-order) = 7 yeni test; hepsi edge/negative/contract + fail-on-
  revert; anti-flakiness korundu (yeni testlerde sleep_for YOK, determinist
  Sample inject / publish-order assert).
- (+) Her mühür "promote-on-need" tetikleyici taşır → "deferred-by-design"
  açık-boşluk sayılmaz ama genişleme yolu nettir.
- (−) Mühürler config typed-schema, plugin native-reload, events async/weak-
  token gibi feature'ları bu pass'te ÜRETMEZ; tüketici doğunca ayrı ADR'larla
  gelir. Kabul: BAND 2 foundation grubu "deepen + seal" karakterinde; büyük
  feature-item (cd::framegraph transient-aliasing) bu grup DIŞINDA (render-core).

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: küçük/net/tüketicisi-olan boşluğu
  implement et, gerçekten design-scope/large-future olanı seal et — bu pass'in
  bölünme çizgisi (band1 ADR ile aynı).
- events priority "plausible consumer" testi: logging/audit-before-gameplay
  ordering (sync bus'ın doğal kullanımı) yeterli gerekçe sayıldı; mevcut bir
  callsite zorunlu tutulmadı çünkü ekleme geriye-uyumlu (default davranış
  değişmez) ve ek-bağımlılık-yok.
- "samples/hello_*/% docs dokunma" kuralı uygulandı: tüm değişiklikler
  engine/foundation/<lib>/ (events header + 3 lib test) + docs/ADR/ altında.
- Golden byte-identical doğrulaması: foundation kütüphaneleri rendering yolunu
  etkilemez (events header değişikliği geriye-uyumlu); fixture #5 capture
  baseline ile bayt-bayt eşit doğrulandı.

## Sonraki

- BAND 2'nin geri kalan grupları (render-core/game/world/asset/ui ~54 kütüphane)
  bu mühür şablonunu (seal-with-promote-on-need + impl-where-clean) tekrar
  kullanabilir; tek gerçek feature-item cd::framegraph transient-aliasing
  grafiğidir (roadmap §BAND 2, render-core grubu).
- Mühürlenen foundation maddelerinin promote tetikleyicileri: config typed-schema
  için editor CVar-inspector / dev-console; plugin native-reload için editor
  plugin-manager live-reload; events async/weak-token için gerçek bir async
  consumer; concurrency NUMA/io_uring + math SIMD için profil-güdümlü hot-path.
