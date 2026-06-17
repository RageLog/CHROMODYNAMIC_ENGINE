# ADR-20260616 — ALL-MODULES-TO-100 BAND 5 / foundation subset: profile_gpu_marker + runtime Kapsam Kararı (2 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD d6675de)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 5 foundation-subset close-out — implement-the-now-unblocked-named-gap (profile_gpu_marker A-QUERY) + seal-the-DI-context-v1 (runtime) + edge-test-deepening + promote-on-need pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 5 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; bu alt-kümenin named
    gap'leri: profile_gpu_marker "wire real GPU timing via the now-implemented RHI
    query subsystem (`IDevice::create_query_pool` shipped phase1218) — the
    dependency it was waiting for EXISTS now", runtime "grow from DI-context-holder
    to a real runtime loop or seal as context" §BAND 5 tablo + cross-band note
    "profile_gpu_marker is now UNBLOCKED by the Band-already-done A-QUERY work")
  - `docs/PROJECT_COMPLETION_STATUS.md` §1 (engine/foundation: profile_gpu_marker 55
    "stub — full marker bookkeeping BUT GPU timing is a 1GHz monotonic-counter stub
    awaiting IDevice::gpu_tick_frequency(); 5 gtests") + §2 (runtime 55 "skeleton —
    thin DI aggregator wiring VFS + asset registry + lazy thread pool + logger;
    9 gtests; context holder, not a runtime loop")
  - `docs/ADR/ADR-20260616-band3-ui-aisquad-scope.md` +
    `docs/ADR/ADR-20260616-band3-world-scope.md` +
    `docs/ADR/ADR-20260616-band3-foundation-scope.md` +
    `docs/ADR/ADR-20260616-band2-ui-scope.md` (kardeş band-mühür ADR'ları, aynı
    şablon: impl-the-small-clean-named-gap + seal-the-rest + promote-on-need)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Bu pass'te değişiklikler
  YALNIZ engine/foundation/profile_gpu_marker/ (include + src + tests) +
  engine/runtime/ (tests) + bu docs/ADR/ dosyası altında. samples/ + hello_* +
  engine/render/rhi/ (A-QUERY surface'i salt-tüketildi, DOKUNULMADI) + diğer
  gruplar/lib'ler + % docs'a DOKUNULMADI. profile_gpu_marker public header'ına
  YALNIZ EKLEME yapıldı (yeni `prepare()` metodu + banner güncellemesi; mevcut
  begin/end/resolve/clear imzaları + Scope ABI sabit kaldı, eski 5 test
  davranış-değişmeden geçer). runtime'da YALNIZ test eklendi (header/src
  DOKUNULMADI). İki lib de hello_engine render yoluna girmez (foundation/asset
  context lib'leri) → golden byte-identical.

---

## 1. Bağlam

BAND 5 (50–59%) foundation alt-kümesi 2 kütüphaneyi 100%'e taşır:
cd::profile_gpu_marker (224 hdr + 180 src; tam marker bookkeeping —
alloc_slot/in-flight free-list/resolve pipeline/RAII Scope — AMA GPU timing
açık bir 1GHz monotonic-counter STUB'tı, gerçek bir RHI timestamp kaynağı
bekliyordu; 5 gtest) + cd::runtime (EngineContext: CVarRegistry + VFS +
AssetRegistry + lazy WorkStealingThreadPool + DeadlineMonitor + injectable
ILogger DI-aggregator'ı; bir runtime/frame loop DEĞİL, bir service-context
holder; 9 gtest).

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu iki kütüphanenin named gap'leri ZIT karakterdedir, dolayısıyla ZIT terminal
durum alırlar:

- **profile_gpu_marker — IMPLEMENTED** (sealed DEĞİL). Bekledigi tek bağımlılık
  ARTIK VAR: A-QUERY subsystem (phase1218) `IDevice::create_query_pool(QueryType::
  kTimestamp)` + `ICommandBuffer::write_timestamp(pool,index)` +
  `IDevice::get_query_results(...)` (timestamp slot'larını NANOSANIYE'ye device
  timestamp-period uygulayarak çözer) + `DeviceFeatures::timestamp_queries`
  flag'ini ekledi. Bu, header'ın phase739 RHI_CONTRACT_GAP bloğunda 1:1 listelenen
  beş ekin TAMAMIDIR. "named unblock" gerçekleşti → stub'ı gerçek timing'e
  bağlamak küçük, temiz ve test edilebilir; SEAL değil IMPLEMENT doğru karardır.
- **runtime — SEALED (DI-context-v1) + topup**. "grow to a real runtime loop"
  yarısı KASTEN bu lib'in dışındadır: gerçek bir frame/runtime loop app-level bir
  ilgidir (cd::sample_framework / hello_engine loop'u OWN eder); bu lib
  service-CONTEXT'tir, loop DEĞİL. Doğru terminal durum: DI-context-v1'i SEAL et +
  gerçekten test-edilmemiş gerçek dalları (lazy-init idempotence, missing-service,
  double-init, logger-flush-on-shutdown) IMPLEMENTED(test) yap.

Kümülatif +8 yeni test (3 profile_gpu_marker real-path + 5 runtime topup), hepsi
edge/contract + anti-flakiness (yeni testlerde sleep_for YOK; deterministik fake
timestamp device + senkron pool join). profile_gpu_marker public API'sine yalnız
`prepare()` EKLENDİ; runtime header/src DEĞİŞMEDİ.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::profile_gpu_marker (foundation/profile_gpu_marker) — 55 → 100  [A-QUERY real-timing IMPLEMENTED(test) + stub-fallback SEALED]

- **Bağlam**: Recorder begin_marker/end_marker/resolve/clear + free-list in-flight
  table + RAII Scope hepsi gerçek; tek STUB GPU timing'di: 1GHz monotonic counter,
  `IDevice::gpu_tick_frequency()` (o zaman yok) bekliyordu. phase739 header'ı
  düzeltilecek RHI contract'ı 1:1 pinlemişti (create_query_pool / reset_query_pool
  / write_timestamp / get_query_pool_results / gpu_tick_frequency + timestamp_queries
  feature flag). Named gap (roadmap, named unblock): "wire real GPU timing via the
  now-implemented RHI query subsystem — the dependency it was waiting for EXISTS now".
- **A-QUERY KARARI — WIRE EDİLDİ (IMPLEMENT, not seal)**: phase1218 A-QUERY
  yüzeyi pinlenen contract'ın TAMAMINI sağladı, üstelik daha temiz bir biçimde:
  `get_query_results` timestamp slot'larını ZATEN NANOSANIYE döndürür (backend
  device timestamp-period'u kendi uygular) → lib'in artık ayrı bir
  `gpu_tick_frequency()`'e ihtiyacı YOK. Recorder QueryPool'u UÇTAN UCA OWN eder:
  - Yeni `prepare(IDevice& device, uint32_t max_markers)` lifecycle metodu:
    `device.features().timestamp_queries` true ise `2*max_markers` slot'luk bir
    kTimestamp QueryPool yaratır (marker başına begin+end), Impl'de tutar, dtor'da
    serbest bırakır. Idempotent (aynı device'ta yeniden prepare no-op; büyük
    max_markers pool'u büyütür). timestamp_queries false ise (Null/headless/mobile
    GLES2) NO-OP döner — Recorder eski 1GHz stub yolunda kalır, API DEĞİŞMEZ.
  - begin_marker/end_marker, pool aktifse `cmd.write_timestamp(pool, slot)` emit
    eder (marker başına 2 slot ayrılır); aktif değilse monotonic counter tick'ler.
  - resolve(device), pool aktifse `device.get_query_results` ile ns slot'larını
    okur → `duration_ms = (end_ns - start_ns) / 1e6`; aksi halde stub 1GHz hesabı.
  Bu entegrasyon RENDERER-tarafı plumbing GEREKTİRMEZ: Recorder begin/end'de zaten
  `ICommandBuffer&`, resolve'da `IDevice&` alıyordu — tek ön-koşul tüketicinin
  (1) bir kez prepare(device, N) çağırması, (2) marker'ları kendi cmd buffer'ında
  kaydetmesi, (3) producing submit fence/wait_idle sonrası resolve(device)
  çağırması — bu ZATEN stub'ın gerektirdiği aynı lifetime'dır. Brief'in "if the
  integration needs a command-buffer/frame-fence plumbing that crosses into a
  consumer (renderer) it can't own alone → SEAL" koşulu OLUŞMADI: Recorder pool'u
   owner; cmd buffer + resolve point zaten parametre, sahiplik değil.
- **Doğrulama (gerçek-path, deterministik, GPU'suz)**: NullDevice `final` olduğu
  için (test_material_recreate.cpp pattern'i) gerçek-path testi bir
  `FakeTimestampDevice` (forwarding IDevice, owns NullDevice inner) kullanır:
  `features().timestamp_queries==true`, kTimestamp pool verir, canned ns döndürür
  (slot i → 100*(i+1) ns) → write_timestamp/get_query_results gerçek kod yolu
  ns→ms deterministik koşar. 3 yeni gtest (5→8):
  `PrepareNoOpWithoutTimestampSupport` (NullDevice → prepare false, stub yolu live),
  `ResolvesRealNanoseconds` (pool 2*N slot, begin/end başına 1 write_timestamp,
  resolve 100ns delta → 1e-4 ms), `PrepareIdempotentAndReleasesPool` (ikinci
  prepare küçük N ile pool reuse → 0 destroy; dtor pool'u tam 1 kez serbest bırakır).
  RTX 3080 / Vulkan üzerinde GERÇEK on-GPU doğrulama, renderer'ın mevcut A-QUERY
  query-pool device testlerinin (cd_test_rhi_vulkan) işidir — bu foundation
  binary'sine bir backend (cd::rhi_vulkan) linklemek foundation→backend katman
  ihlali olur (lib yalnız header-only cd::rhi'ya bağlı). Brief'in "do NOT run
  cd_test_rhi_vulkan" kuralıyla uyumlu: lib gerçek-path'i implement+test eder,
  on-GPU pixel-doğrulama backend test katmanına aittir.
- **stub-fallback SEALED**: 1GHz monotonic counter yolu SİLİNMEDİ — bilinçli bir
  graceful-degradation katmanı olarak mühürlendi (Null device, headless tooling,
  timestamp_queries'i olmayan donanım: mobile GLES2). prepare() çağrılmazsa veya
  device timestamp desteklemiyorsa Recorder eskisi gibi deterministik duration
  üretir → editor overlay + headless test live kalır.
- **Banner düzeltmesi**: header + .cpp "awaiting RHI / RHI_CONTRACT_GAP BLOCKED"
  banner'ı "RHI gap CLOSED — A-QUERY (phase1218) shipped" olarak düzeltildi; beş
  pinlenen ek artık wire edilen çağrılara 1:1 maplenir, "gpu_tick_frequency
  gerekmiyor çünkü get_query_results ns döner" notuyla.
- **Gerekçe**: Bu lib'in named gap'i KASTEN bir "named unblock"tu — beklediği RHI
  primitive yokken üretilemezdi; artık var (phase1218). honest-rule (a):
  bağımlılık mevcut + entegrasyon küçük/net/lib-owned + deterministik test
  edilebilir → IMPLEMENT (SEAL değil). Stub yolunu silmemek (Null/headless için
  graceful fallback) doğru mühürdür.
- **Promote-on-need**: kTimestamp dışı query (kPipelineStatistics / kOcclusion)
  marker'ları + multi-frame ring (frame-in-flight başına ayrı pool/region) +
  per-stage pipeline-stage timestamp (write_timestamp2 stage mask) — gerçek bir
  GPU profiling overlay tüketicisi (>=N marker/frame, N-frame pipelining) ihtiyacı
  + ayrı ADR ile gelir; prepare/begin_marker/end_marker/resolve API yüzeyi +
  GpuMarkerSample ABI sabit kalır.

### 2.2 cd::runtime (engine/runtime) — 55 → 100  [DI-context-v1 SEALED; IMPLEMENTED(test)]

- **Bağlam**: EngineContext composition-root: CVarRegistry + VirtualFileSystem +
  AssetRegistry(vfs) + DeadlineMonitor watchdog + lazy/eager WorkStealingThreadPool
  + injectable ILogger; ctor eager-pool dalı + thread_pool() lazy dalı + shutdown()
  reverse-order teardown (pool.shutdown → logger.flush) + idempotent shut_down_
  guard. 9 gtest. Named gap (roadmap): "grow from DI-context-holder to a real
  runtime loop or seal as context".
- **Karar**: **DI-context-v1 SEALED** (as-context); gerçek runtime/frame loop
  **promote-on-need** (app-level). EngineContext'in charter'ı bir SERVICE CONTEXT'tir
  (header'ın açık sözleşmesi: "EngineContext is the engine's 'kernel' … a single
  object that game code uses to reach engine-provided services … *not* a singleton:
  multiple may coexist"). Bir frame/update loop (tick → fixed-step → render → present
  pacing) KASTEN burada değildir: o app-level bir ilgidir ve cd::sample_framework /
  hello_engine on_frame loop'u OWN eder (RunX marathon'larında main()→sample_framework
  port edildi). Context'i loop'a dönüştürmek (a) tek bir loop policy'yi tüm
  tüketicilere zorlar (editor preview-world + main-world + headless test ZITEN
  multi-context senaryolar), (b) FramePacer/RateLimiter (cd::time, zaten var) ile
  çakışan ikinci bir pacing kaynağı yaratır. Doğru terminal durum: DI-context'i
  SEAL et + gerçekten test-edilmemiş gerçek service-lifecycle dallarını
  IMPLEMENTED(test) yap. Gerçekten test-edilmemiş 5 dal **IMPLEMENTED(test)**
  (9→14 gtest):
  `LazyThreadPoolInitIsIdempotent` (thread_pool()'un `if(!pool_)` guard'ı yalnız
  BİR kez ateşlemeli — iki çağrı AYNI pool ref'ini döndürür, yeni pool spin-up
  ETMEZ; prior LazyThreadPool testi yalnız "var oldu" diyordu, "aynı instance" değil),
  `ExplicitWorkerCountHonoured` (config worker_threads=2 → worker_count()==2; ctor'un
  explicit-count dalı; prior testler yalnız >0 kontrol ediyordu),
  `WorkerCountZeroWhenPoolAbsent` (worker_count()'in pool-yok arm'ı → 0; "missing
  service" accessor dalı, canlı-pool'dan farklı, hiç koşulmamıştı),
  `ShutdownFlushesLoggerOnce` (shutdown()'ın `if(logger_) logger_->flush()` arm'ı —
  CountingLogger ile flush TAM 1 kez; ikinci shutdown() guard'tan erken döner, dtor
  da; üç-katmanlı idempotence; logger-flush-on-shutdown HİÇ test edilmemişti),
  `ThreadPoolReacquirableAfterShutdown` (shutdown sonrası thread_pool() lazy guard
  yeniden ateşler → kullanılabilir yeni pool, job koşar; bir-kerelik shut_down_
  flag'i re-init'i ENGELLEMEZ — "services are re-obtainable" context sözleşmesi,
  crash yok, deterministik join ile doğrulanır).
- **Gerekçe**: runtime'ın gerçek değeri renderer-agnostic, multi-instance,
  lazy-init service-context'tir ve o charter-complete. Named gap "grow to a real
  runtime loop" KASTEN bu lib'in dışındadır: loop app-level (sample_framework/
  hello_engine), pacing zaten cd::time'da. Context'i loop'a çevirmek wrong-layer +
  multi-context senaryoları (editor/headless) kırar + cd::time pacing'ini duplike
  eder. honest-rule (b) promote-on-need tam bunun için. Service-lifecycle edge
  testleri (lazy-idempotence/explicit-count/missing-service/flush-once/re-acquire)
  küçük+net+deterministik — honest-rule (a); DI-context invariant'ları artık
  fail-on-revert kilitli.
- **Promote-on-need**: Gerçek bir runtime/frame loop (fixed-step tick + interpolated
  render + present pacing + subsystem update ordering) — bir app-level harness'te
  (cd::sample_framework genişlemesi veya yeni bir cd::app/cd::game_loop lib) gerçek
  bir standalone-app ihtiyacıyla + ayrı ADR ile gelir; EngineContext service-accessor
  API yüzeyi (cvars/vfs/assets/watchdog/thread_pool/logger + shutdown idempotence)
  sabit kalır (loop o context'i TÜKETİR, onu MİRAS ALMAZ).

---

## 3. Reddedilen alternatifler

- **profile_gpu_marker'ı SEAL etmek (stub'ı kapsam olarak mühürleyip gerçek timing'i
  ertelemek)**: RED — beklediği RHI bağımlılığı (A-QUERY, phase1218) ARTIK var;
  entegrasyon küçük/net/lib-owned + deterministik test edilebilir → honest-rule (a)
  IMPLEMENT'i emreder, "named unblock" gerçekleşti. Stub'ı SEAL etmek dürüst-olmayan
  bir "deferred" olurdu çünkü blocker kalkmıştı.
- **profile_gpu_marker gerçek-path'ini cd::rhi_vulkan linkleyip RTX 3080'de on-GPU
  test etmek**: RED — foundation lib (yalnız header-only cd::rhi'ya bağlı) bir
  backend'e linklenirse foundation→backend katman ihlali + brief "do NOT run
  cd_test_rhi_vulkan". on-GPU pixel/ns doğrulama renderer'ın mevcut A-QUERY device
  testlerine aittir; lib-katmanı gerçek kod yolunu FakeTimestampDevice ile
  deterministik koşar.
- **profile_gpu_marker'ın 1GHz stub yolunu SİLMEK**: RED — Null device, headless
  tooling ve timestamp_queries'i olmayan donanım (mobile GLES2) için gerçek bir
  graceful-degradation katmanı; silmek editor overlay + headless test'i kırar.
  SEALED as bilinçli fallback.
- **runtime'ı gerçek bir frame/runtime loop'a dönüştürmek**: RED + wrong-layer —
  loop app-level (sample_framework/hello_engine OWN eder), pacing zaten cd::time'da;
  context'i loop'a çevirmek multi-context (editor/headless) senaryolarını kırar +
  cd::time FramePacer'ı duplike eder. DI-context-v1 SEALED, service-lifecycle
  dalları kilitlendi.
- **runtime'ın WatchdogIntegration testindeki sleep_for'u düzeltmek / refactor**:
  RED + kapsam dışı — pre-existing test (bu pass'in eklediği satırlarda değil);
  CLAUDE.md §5 anti-flakiness YALNIZ yeni/düzenlenen satırlara uygulanır; mevcut
  flaky-pattern'i bu seal-pass'te düzeltmek scope creep olur → Sonraki'ye yazıldı.
- **Herhangi bir cd::rhi (A-QUERY) yüzeyini değiştirmek**: RED — A-QUERY salt
  tüketildi; create_query_pool/write_timestamp/get_query_results imzaları aynen
  kullanıldı, RHI header'ına dokunulmadı.
- **% docs'u / samples'ı / kapsam-dışı foundation lib'lerini düzenlemek**: kapsam
  DIŞI (brief SCOPE EXCLUSION). RED.

## 4. Sonuçlar

- (+) 2/2 BAND-5 foundation-subset kütüphanesi honest-rule terminal durumuna geçti:
  profile_gpu_marker (A-QUERY real-timing IMPLEMENTED + 3 real-path test;
  stub-fallback SEALED), runtime (DI-context-v1 SEALED + 5 service-lifecycle test).
  Hiçbir kütüphanede placeholder/TODO-state kalmadı; profile_gpu_marker'ın
  "awaiting RHI" banner'ı düzeltildi.
- (+) profile_gpu_marker: gerçek GPU timing artık A-QUERY timestamp QueryPool
  üzerinden çalışır (prepare → write_timestamp → get_query_results ns → ms);
  Null/headless yolu stub'a graceful düşer; pool ownership + idempotent prepare +
  dtor-release artık fail-on-revert kilitli (+3 test). Public API'ye yalnız
  prepare() eklendi → eski 5 test + Scope ABI sabit.
- (+) runtime: lazy-init idempotence + explicit-worker-count + missing-service
  (worker_count==0) + logger-flush-on-shutdown (tam 1 kez) + re-acquire-after-
  shutdown artık kilitli (+5 test); service-context vs runtime-loop sınırı
  dürüstçe belgelendi (loop = app-level/sample_framework işi). header/src
  DOKUNULMADI.
- (+) Toplam +8 yeni test (3+5), yeni testlerin hepsi edge/contract + anti-flakiness
  (sleep_for YOK; deterministik FakeTimestampDevice + senkron pool wait_all). Build
  -Werror temiz (2 test target re-link); 0 yeni clang-tidy WAE defect-class. İki lib
  de hello_engine render yoluna girmez → golden byte-identical.
- (+) Her mühür "promote-on-need" tetikleyici taşır → genişleme yolu
  (pipeline-statistics/occlusion marker + multi-frame ring + per-stage timestamp;
  gerçek runtime/frame loop) nettir ama bugün dead-code/over-engineering/wrong-layer
  olmaz.
- (−) profile_gpu_marker mührü on-GPU (RTX 3080) ns doğrulamasını bu foundation
  binary'sinde KOŞMAZ (backend-link katman ihlali); o renderer A-QUERY device
  testine aittir. runtime mührü gerçek frame loop'u ÜRETMEZ; standalone-app
  ihtiyacı doğunca ayrı ADR'la gelir. Kabul: BAND 5 foundation-subset
  "implement-the-now-unblocked-named-gap + seal-the-as-context-v1 +
  lock-the-untested-edges" karakterinde.

---

## Varsayımlar

- Brief'in path notasyonu `engine/foundation/{profile_gpu_marker,runtime}/`
  diyor; profile_gpu_marker GERÇEKTEN `engine/foundation/profile_gpu_marker/`
  altında, runtime ise `engine/runtime/` altında (lib adı cd::runtime tek-anlamlı;
  PROJECT_COMPLETION_STATUS §2 onu group-2 asset+runtime'da listeler). Path notu
  hafif kayık ama lib kimliği belirsiz değil → `engine/runtime/` in-scope kabul
  edildi.
- Brief'in "100% RULE per lib" yorumu: gap → IMPLEMENTED+tested (küçük/net) VEYA
  SEALED. profile_gpu_marker named gap'i "named unblock" (A-QUERY artık var) →
  IMPLEMENT. runtime named gap'i "loop vs context" → SEAL as context + topup.
- profile_gpu_marker test target'ı `cd_test_profile_gpu_marker`
  (engine/foundation/profile_gpu_marker/tests, `cd_add_test(profile_gpu_marker)`);
  runtime=`cd_test_runtime` (engine/runtime/tests). İkisi de PASS
  (`ctest -R "profile_gpu_marker|cd_test_runtime"` ile doğrulandı; cd_test_rhi_vulkan
  KOŞULMADI).
- profile_gpu_marker gerçek-path testi NullDevice `final` olduğu için
  test_material_recreate.cpp'deki forwarding-IDevice pattern'ini izler
  (FakeTimestampDevice owns NullDevice inner, yalnız features() + 3 A-QUERY entry
  point override); get_query_results timestamp slot'larının NANOSANIYE döndürdüğü
  RHI sözleşmesi (IDevice.hpp doc) gerçek-path duration matematiğinin temeli.
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/foundation/
  profile_gpu_marker/ (include+src+tests) + engine/runtime/tests/ + bu docs/ADR/
  dosyası altında. samples/ + hello_* + engine/render/rhi/ + diğer gruplar + %
  docs'a dokunulmadı.
- Golden byte-identical: iki lib de foundation/asset-context katmanı, hello_engine
  render yoluna girmez; profile_gpu_marker'a yalnız EKLEME (prepare + banner)
  yapıldı, mevcut begin/end/resolve davranışı stub yolunda DEĞİŞMEDİ → fixture #5
  capture baseline (research/reports/parity1121/baseline.png) ile bayt-bayt eşit
  doğrulandı (b5f.png cmp → identical, sonra silindi).

## Sonraki

- runtime WatchdogIntegration testi `std::this_thread::sleep_for` kullanıyor
  (pre-existing, bu pass'in eklediği satırlarda değil) — CLAUDE.md §5 anti-flakiness
  ihlali; bir gelecek pass'te DeadlineMonitor'ün deterministik/injectable-clock
  veya event-tabanlı evaluate()'ine taşınabilir (kapsam dışıydı, dokunulmadı).
- BAND 5'in geri kalan kütüphaneleri (decal, virtual_textures, velocity,
  virtual_geometry) bu pass'in ZIT-terminal şablonunu kullanabilir:
  named-unblock → IMPLEMENT (bağımlılık varsa), wrong-layer/app-level → SEAL +
  lock-the-untested-edges, naive-algorithm → real-impl (QEM/BC-fit).
- profile_gpu_marker promote tetikleyicisi: pipeline-statistics/occlusion marker +
  multi-frame-in-flight pool ring + per-stage timestamp (gerçek GPU profiling
  overlay tüketicisi ihtiyacıyla, RTX 3080'de on-GPU A-QUERY device testiyle).
  runtime promote tetikleyicisi: gerçek runtime/frame loop (standalone-app
  ihtiyacıyla, sample_framework genişlemesi veya cd::app lib'inde).
