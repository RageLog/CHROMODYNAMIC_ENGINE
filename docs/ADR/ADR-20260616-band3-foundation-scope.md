# ADR-20260616 — ALL-MODULES-TO-100 BAND 3 / FOUNDATION Kapsam Mührü (3 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD 0b382de)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 3 foundation subset close-out — impl-where-clean + visual-side test-deepening + kapsam mührü pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 3 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; mem munmap named gap +
    profile_cpu_marker_overlay / frame_graph_timeline "golden-image test of the
    overlay (visual side untested)" §BAND 3 tablo)
  - `docs/PROJECT_COMPLETION_STATUS.md` §1 (foundation baseline %'leri + Basis sütunu:
    mem 78 "POSIX munmap leaks length", overlay/timeline 78 "visual side untested")
  - `docs/ADR/ADR-20260616-band2-foundation-scope.md` (kardeş band-mühür ADR'ı, aynı şablon)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Bu pass'te ÜÇ kütüphanenin de named gap'i gerçekte
  IMPLEMENTED + test edildi (küçük/net/tüketici-mantıklı): mem POSIX munmap
  length-leak FIX'i + iki overlay'in görsel-tarafı için DrawBatcher-içeriği
  assert eden birim testleri. samples/ + hello_* + diğer gruplar + % docs'a
  DOKUNULMADI. Sadece engine/foundation/{mem,profile_cpu_marker_overlay,
  profile_frame_graph_timeline}/ + bu ADR yazıldı.

---

## 1. Bağlam

BAND 3 (70–79%) foundation alt-kümesi 3 kütüphaneyi 100%'e taşır: cd::mem
(named gap: PageAllocator POSIX munmap mapping length'i sızdırıyor — dokümante
v1 TODO; Win32 yolu tam) + cd::profile_cpu_marker_overlay (Overlay cd::ui
DrawBatcher'a çiziyor ama GÖRSEL taraf test edilmemiş) + cd::profile_frame_graph_timeline
(aynı: TimelineOverlay'in emit ettiği per-pass çizim çıktısı test edilmemiş).

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu pass'in bölünme çizgisi: üç named gap de "küçük+net+tüketici-mantıklı"
implementasyon-fırsatıydı, hepsi IMPLEMENTED + test edildi. mem munmap-leak
gerçek bir doğruluk bug'ıydı (POSIX'te adres-alanı rezervasyonu kalıcı
sızıyordu); iki overlay'in görsel-tarafı ise GPU'suz tam test-edilebilirdi —
DrawBatcher CPU-side batcher'dır ve `vertices()`/`indices()`/`commands()`
span'leriyle emit edilen geometriyi/rengi/draw-command'ı doğrudan introspect
ettirir, yani "golden-image test" yerine bayt-tam içerik-assert'i mümkündü
(daha güçlü + GPU/golden-pipeline bağımlılığı yok).

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::mem (foundation/mem) — 78 → 100  [IMPLEMENTED + SEALED]

- **Bağlam**: 689 hdr + 90 src; Linear/Pool/Tracking/Page allocator + Pmr
  adapter. 14→19 gtest. Named gap: PageAllocator POSIX `munmap` mapping
  length'i sızdırıyor — `VirtualFree(ptr, 0, MEM_RELEASE)` Win32'de length
  istemez ama POSIX `munmap(ptr, len)` orijinal length'i ZORUNLU ister; eski
  .cpp length'i saklamıyordu → POSIX'te her deallocate adres-alanı
  rezervasyonunu kalıcı sızdırıyordu (dokümante "v1 limitation + TODO(S2.1.c)").
- **Karar**: Named gap **IMPLEMENTED** + test edildi. PageAllocator artık
  PIMPL (`Impl`) içinde mutex-korumalı bir `unordered_map<const void*,
  size_t>` side-table tutuyor: her `do_allocate` rounded mapping length'i
  kaydeder; `deallocate` length'i tablodan geri okur ve POSIX'te
  `munmap(ptr, len)`, Win32'de (length ignore edilen) `VirtualFree(ptr, 0,
  MEM_RELEASE)` çağırır, sonra entry'yi siler. İki yeni gözlemci accessor:
  `mapping_length(const void*)` (kayıtlı rounded length ya da 0) ve
  `live_mapping_count()` (canlı eşleme sayısı). 5 yeni test: length-recorded-
  and-page-rounded (`ps+1 → 2*ps`), many-pages-no-leak (64 sayfa alloc/free →
  live_mapping_count 0'a döner), double-free-no-op (ikinci free bilinmeyen
  length'i unmap etmez), foreign-pointer-rejected (yabancı işaretçi reddedilir),
  ve basic-allocate (mevcut) korunur. Side-table aynı zamanda double-free ve
  yabancı-pointer-free'yi sessizce güvenli no-op'a çevirir (eski kod POSIX'te
  hiç unmap etmiyordu, Win32'de yabancı pointer'da UB riski vardı). **SEALED**:
  PageAllocator charter'ı (page-granularity OS allocation + doğru reclaim) tam.
- **Gerekçe**: Length-leak gerçek bir doğruluk defect'iydi (sunucu/uzun-ömürlü
  POSIX süreçte adres-alanı tükenmesine yol açar); side-table fix küçük, net,
  ve PageAllocator'ın doğrudan tüketicisi var (RHI staging / büyük GPU upload
  buffer'ları). Accessor'lar testin leak/double-free'yi platform-bağımsız
  (Windows host dahil) assert etmesini sağlar — POSIX-only probe gerekmez.
  noexcept yüzeyi korundu (`std::scoped_lock` + map-insert: OOM'da küçük insert
  throw edebilir, o tek senaryoda mapping sızar ama süreç abort etmez — kabul
  edilebilir OOM davranışı, yorumla işaretli).
- **Promote-on-need**: Lock-free / sharded side-table (mutex contention bir
  hot-path profilinde sorun olursa), `madvise`/`MADV_DONTNEED` partial-decommit,
  ya da huge-page (`MAP_HUGETLB` / `MEM_LARGE_PAGES`) desteği gerçek bir
  allocator-tüketici ihtiyacıyla + ayrı ADR ile gelir; allocate/deallocate
  yüzeyi sabit kalır.

### 2.2 cd::profile_cpu_marker_overlay (foundation/profile_cpu_marker_overlay) — 78 → 100  [IMPLEMENTED + SEALED]

- **Bağlam**: 186 hdr + 259 src; ring Collector (begin/end → MarkerSample,
  mutex-korumalı, free-list slot reuse) + Scope RAII + Overlay (Tracy-tarzı
  yatay bar-chart → cd::ui::renderer::DrawBatcher quad emit, thread başına lane,
  djb2 hash → renk). 7→8 gtest. Named gap: "visual side untested by golden" —
  Overlay::draw çizim ÇIKTISI (geometri/renk) doğrulanmamış; eski testler yalnız
  vertex/index SAYISINI kontrol ediyordu.
- **Karar**: Named gap **IMPLEMENTED** (test) + SEALED. 1 yeni derin test
  (`DrawEmitsExactBatchContents`): bilinen 2-sample (tek thread, tek lane) seti
  için DrawBatcher'a emit edilen TAM içerik assert edilir — bar top-left
  vertex pozisyonu (`bounds.x` ve `bounds.x + rel_start*pixels_per_ms`), bar
  yüksekliği (`lane_height*0.8`), djb2-türetilmiş RGBA renk (sample adıyla
  birebir), ve iki quad'ın tek merged solid (untextured, variant=kSolid,
  texture_slot=0xFFFFFFFF, index_count=12) draw-command'a indirgendiği. Bu,
  "golden-image" yerine GPU'suz CPU-side bayt-tam içerik-assert'i (DrawBatcher
  introspection span'leri sayesinde — daha güçlü, golden-pipeline'a bağımlı
  değil). **SEALED**: Collector + Scope + Overlay charter'ı tam.
- **Gerekçe**: DrawBatcher CPU-side batcher'dır (RHI bilmez); görsel doğruluğu
  emit edilen geometri+renk span'lerinden tam doğrulanabilir — gerçek bir
  golden-image (FLIP/SSIM) pipeline'ı bu kütüphane için aşırı + GPU-bağımlı
  olurdu, oysa içerik-assert hem deterministik hem fail-on-revert. Collector'ın
  ring/RAII/cutoff davranışı zaten 7 testle kapalıydı; eklenen tek şey gerçek
  boşluk olan görsel-emit doğrulamasıydı, pad yok.
- **Promote-on-need**: Gerçek GPU golden-image diff (overlay'i hello_engine HUD
  pipeline'ında render edip FLIP/SSIM ile karşılaştırmak) bir editor-profiler
  görsel-regresyon UX'iyle + ayrı ADR ile gelir; Overlay::draw API yüzeyi sabit
  kalır.

### 2.3 cd::profile_frame_graph_timeline (foundation/profile_frame_graph_timeline) — 78 → 100  [IMPLEMENTED + SEALED]

- **Bağlam**: 158 hdr + 127 src; Timeline (begin_frame/record_pass/end_frame
  double-buffer + last_frame_passes/total_ms, single-thread, mutex'siz) +
  TimelineOverlay (yatay Gantt-chart → DrawBatcher quad emit, tek lane,
  djb2 hash → renk). 7→8 gtest. Named gap: cpu_marker_overlay ile aynı —
  TimelineOverlay'in emit ettiği per-pass çizim çıktısı test edilmemiş.
- **Karar**: Named gap **IMPLEMENTED** (test) + SEALED. 1 yeni derin test
  (`DrawEmitsExactPerPassOutput`): bilinen begin_frame/record_pass×2/end_frame
  dizisi → `last_frame_passes()` → TimelineOverlay::draw için DrawBatcher'a
  emit edilen TAM per-pass çıktı assert edilir — bar top-left/top-right/
  bottom-left vertex pozisyonları (`bounds.x`, `bounds.x + dur*pixels_per_ms`,
  dikeyde ortalanmış `bar_y = bounds.y + (h - 0.75h)/2`, `bar_h = 0.75h`),
  djb2-türetilmiş RGBA renk (pass adıyla birebir), rel_start ofseti
  (`bounds.x + 4ms*pixels_per_ms`), ve iki bar'ın tek merged solid draw-command
  (index_count=12) olduğu. Begin/record/end → emit zinciri uçtan-uca bilinen
  dizi üzerinde doğrulanır. **SEALED**: Timeline + TimelineOverlay charter'ı tam.
- **Gerekçe**: cpu_marker_overlay (§2.2) ile aynı gerekçe — DrawBatcher
  introspection ile GPU'suz bayt-tam görsel-emit doğrulaması golden-image'tan
  daha güçlü + bağımlılıksız. Timeline double-buffer / total_ms / round-trip
  davranışı zaten 7 testle kapalıydı; eklenen tek şey gerçek boşluk olan
  görsel-emit doğrulamasıydı.
- **Promote-on-need**: Gerçek GPU golden-image diff (cpu_marker_overlay ile
  aynı tetikleyici), ya da çok-lane / nested-pass Gantt layout'u gerçek bir
  frame-graph debugger UX'iyle + ayrı ADR ile gelir; TimelineOverlay::draw API
  yüzeyi sabit kalır.

---

## 3. Reddedilen alternatifler

- **Overlay görsel-tarafını gerçek bir GPU golden-image (FLIP/SSIM) testiyle
  kapatmak**: DrawBatcher CPU-side'dır ve emit span'leri zaten bayt-tam
  introspect ettirir → GPU render + golden pipeline kurmak hem aşırı hem
  GPU/donanım-bağımlı (foundation lib'i için yanlış katman) olurdu. RED —
  CPU-side içerik-assert daha güçlü, deterministik, fail-on-revert.
- **mem munmap-leak'i seal etmek (implement etmemek)**: named gap gerçek bir
  doğruluk defect'iydi (POSIX adres-alanı sızıntısı), tüketicisi var (RHI
  staging), fix küçük+net → honest-rule (a) tam bunun için. Seal etmek dürüst
  olmazdı. RED.
- **mem side-table'ı lock-free / sharded yapmak**: PageAllocator infrequent
  large-block path'tir (per-frame hot-path değil); mutex+unordered_map yeterli
  ve basit. Lock-free karmaşıklığı profil-güdümlü ihtiyaç olmadan
  over-engineering olurdu → promote-on-need. RED.
- **Collector/Timeline çekirdek yüzeyini pad etmek**: ring/RAII/cutoff/
  double-buffer/total_ms zaten 7+7 testle round-trip+boundary kapalıydı; yapay
  test eklemek "don't pad" kuralına aykırı. RED — yalnız gerçek boşluk
  (görsel-emit) test edildi.
- **% docs'u / samples'ı düzenlemek**: kapsam DIŞI (brief: SCOPE EXCLUSION —
  ONLY engine/foundation/{mem,…}/ + docs/ADR/). RED.

## 4. Sonuçlar

- (+) 3/3 BAND-3 foundation alt-küme kütüphanesi honest-rule terminal durumuna
  geçti: üç named gap de IMPLEMENTED + test (mem munmap-leak fix + iki overlay
  görsel-emit içerik-assert'i). Hiçbir kütüphanede placeholder/TODO kalmadı
  (mem .cpp'deki "TODO(S2.1.c)" yorumu silindi).
- (+) mem fix gerçek bir doğruluk kazancı: POSIX'te adres-alanı sızıntısı
  giderildi + double-free ve yabancı-pointer-free güvenli no-op'a çevrildi
  (eski Win32 yolunda potansiyel UB de kapandı). +5 mem testi.
- (+) İki overlay'in görsel-tarafı artık fail-on-revert kilitli: bar geometrisi
  (pozisyon/genişlik/yükseklik), djb2 renk, ve draw-command merge davranışı
  bilinen sample/pass seti için bayt-tam assert ediliyor. +1 +1 test (toplam
  +7 yeni test, hepsi edge/contract + anti-flakiness uyumlu — yeni testlerde
  sleep_for YOK; deterministik sentetik sample/pass inject).
- (+) Her mühür "promote-on-need" tetikleyici taşır → genişleme yolu (gerçek
  GPU golden, lock-free side-table, huge-page) nettir ama bugün dead-code/
  over-engineering olmaz.
- (−) Mühürler gerçek GPU golden-image overlay-regresyonu ya da lock-free
  allocator side-table'ı bu pass'te ÜRETMEZ; tüketici/profil-ihtiyacı doğunca
  ayrı ADR'larla gelir. Kabul: BAND 3 foundation alt-kümesi "implement-the-named-
  gap + seal-the-rest" karakterinde.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: documented gap → IMPLEMENTED+tested
  (küçük/net) VEYA SEALED — bu pass'te üçü de implement edildi çünkü üçü de
  küçük/net/tüketici-mantıklıydı (band1/band2 ADR'larıyla aynı bölünme çizgisi).
- "golden-image test of the overlay" (roadmap'in BAND 3 ifadesi) DrawBatcher'ın
  CPU-side introspection span'leri sayesinde bir GPU golden yerine bayt-tam
  içerik-assert olarak yorumlandı — daha güçlü + GPU-bağımsız + deterministik;
  bu yorum brief'in "no GPU -- inspect the DrawBatcher output" talimatıyla
  birebir uyumlu.
- mem accessor'ları (`mapping_length` / `live_mapping_count`) testin leak/
  double-free'yi Windows host'ta da assert edebilmesi için eklendi (gözlemci,
  [[nodiscard]] const noexcept); POSIX-only davranışı Windows CI'da test
  edilemezdi.
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/foundation/
  {mem,profile_cpu_marker_overlay,profile_frame_graph_timeline}/ (header + src +
  test) + bu docs/ADR/ dosyası altında. samples/ + hello_* + diğer gruplar +
  % docs'a dokunulmadı.
- Golden byte-identical doğrulaması: bu üç foundation kütüphanesi hello_engine
  rendering yolunu etkilemez (mem PageAllocator RHI-staging tüketicisi
  geriye-uyumlu kaldı — allocate/deallocate davranışı aynı, yalnız POSIX reclaim
  düzeldi; iki overlay yalnız test aldı, .cpp/hpp davranışı değişmedi); fixture
  #5 capture baseline ile bayt-bayt eşit doğrulandı.

## Sonraki

- BAND 3'ün geri kalan 17 kütüphanesi (render umbrella, async_submit,
  net_session_replay, physics_jolt, ui_font, audio_dsp_fx, physics_vehicle,
  ai::squad, ui_a11y, lighting_clusters, ibl, ui umbrella, mesh_shader, cluster,
  texture_compress, material_authoring, texture_synth) bu mühür şablonunu
  (impl-the-named-gap + seal-the-rest + promote-on-need) tekrar kullanabilir;
  bu grubun büyük çapraz-kesen item'ı 3× froxel-clustering de-dup'ıdır
  (cluster ↔ lighting_clusters ↔ light::ClusterGrid).
- Bu pass'in mühürlenen promote tetikleyicileri: mem için lock-free side-table /
  huge-page (profil-güdümlü); iki overlay için gerçek GPU golden-image regresyon
  (editor-profiler görsel UX'i).
