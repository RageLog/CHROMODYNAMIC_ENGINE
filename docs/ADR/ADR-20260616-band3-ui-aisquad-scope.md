# ADR-20260616 — ALL-MODULES-TO-100 BAND 3 / UI subset + ai_squad Kapsam Mührü (4 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD b1af321)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 3 ui-subset + ai_squad close-out — seal-the-deferred-by-design + edge-test-deepening + promote-on-need pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 3 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS"; bu alt-kümenin named
    gap'leri: ai::squad "finish the GpuBatchSolver (thin dispatch, no buffer
    ownership)", ui_font "default build has NO shaping → wire FreeType/HarfBuzz on
    by default or ship a real fallback shaper", ui_a11y "screen-reader / UIA /
    AT-SPI bridge (baseline only today)", ui umbrella "real glyph layout (text is a
    bare DrawKind today) + widen widget surface" §BAND 3 tablo)
  - `docs/PROJECT_COMPLETION_STATUS.md` §3 (engine/game: ai::squad 75, "Only
    ai_squad's GPU path is genuinely deferred") + §7 (engine/ui: ui_font 78,
    ui_a11y 75, ui umbrella 70; "the honest gap is feature-completeness/visual-
    fidelity, not unimplemented skeletons … default font build has no shaping")
  - `docs/ADR/ADR-20260616-band3-world-scope.md` +
    `docs/ADR/ADR-20260616-band3-foundation-scope.md` +
    `docs/ADR/ADR-20260616-band2-ui-scope.md` (kardeş band-mühür ADR'ları, aynı
    şablon: impl-the-small-clean-named-gap + seal-the-rest + promote-on-need)
  - `docs/ADR/ADR-20260530-ui-widget-library.md` (cd::ui widget-tree + ui_font
    raster/MSDF charter — bu ADR §2.1/2.3/2.4 onu BAND-3 honest-rule terminal
    durumuna mühürler)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen maddeler
  "deferred-by-design"dır — açık (open) sayılmazlar. Her madde "ihtiyaç doğunca
  promote" çıkış kapısı taşır. Bu pass'te değişiklikler YALNIZ
  engine/game/ai_squad/ + engine/ui/{font,a11y}/ + engine/ui umbrella
  (include + src) test'leri + bu docs/ADR/ dosyası altında. samples/ + hello_* +
  engine/ui/{layout,animation,input,theme,renderer,renderer_rhi,widgets,editor,
  renderer_webgpu} + diğer gruplar + % docs'a DOKUNULMADI. Bu pass yalnız
  test-dosyalarına ekleme yaptı (header/src davranışı DEĞİŞMEDİ) → public API
  yüzeyi sabit, ABI sabit, hello_engine render yolu etkilenmez.

---

## 1. Bağlam

BAND 3 (70–79%) ui alt-kümesi + ai_squad 4 kütüphaneyi 100%'e taşır:
cd::ai::squad (CPU squad/formation tam + GpuBatchSolver gated, configure()
capacity-only, no buffer ownership) + cd::ui_font (stb_truetype raster always-on
+ skyline pack + MSDF + opt-in FreeType/HarfBuzz/msdfgen; default build identity
shaper) + cd::ui_a11y (A11yTree register/meta/tab-nav + WCAG contrast + focus
rect; baseline, no AT bridge) + cd::ui umbrella (retained-mode widget tree
hit-test/dispatch/draw + Panel/Label/Button; text bare DrawKind, no glyph
layout).

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net/tüketicisi olan),
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need
tetikleyicisiyle). "TODO/placeholder" üçüncü durumu kalamaz.

Bu alt-kümenin dört named gap'i de aynı KARAKTERdedir: her biri gerçek,
charter-complete bir v1 KATMAN + onun ÜSTÜNDE bir platform/GPU/integration
GENİŞLEMESİ (GPU batch dispatch, OS-specific real shaping, OS-specific AT bridge,
ui_font glyph-layout entegrasyonu). Bu genişlemeler doğal "promote-on-need"
adaylarıdır: ya gerçek bir tüketici (GPU squad consumer, opt-in shaping build,
OS AT-bridge lib, glyph-layout renderer) yokken üretmek dead-code/over-
engineering olur, ya da OS-bağımlıdır (UIA/AT-SPI/NSAccessibility). Dolayısıyla
bu pass'in bölünme çizgisi: **her v1 katmanını SEAL et + gerçekten test-edilmemiş
edge dallarını IMPLEMENTED(test) yap + genişlemeyi precise trigger ile promote-
on-need ayır.** Yapay yüzey-şişirme (don't pad) ya da OS-bridge'i bu pass'te
kurmak (kapsam dışı + multi-week) RED.

Kümülatif +13 yeni test (3 ai_squad CPU + 2 ui_font + 4 ui umbrella + 4 ui_a11y),
hepsi edge/contract + anti-flakiness (sleep_for YOK; deterministik sentetik
girdi). Hiçbir public header/src davranışı değişmedi.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::ai::squad (game/ai_squad) — 75 → 100  [CPU-v1 + GPU-stub SEALED; IMPLEMENTED(test)]

- **Bağlam**: 281 src CPU squad (SquadRole + Formation centroid/radius +
  Blackboard threat-decay + add/remove/update/tick/query) — 10 CPU gtest. Sprint-2
  GpuBatchSolver (`CD_AI_SQUAD_ENABLE_GPU` default OFF): configure() yalnız
  capacity saklar, buffer ownership YOK; tick_batch() doğru group-count'la
  dispatch shape emit eder ama compute pipeline/descriptor-set/SSBO mapping
  embedding renderer'a bırakılmıştır; kSquadFormationCS GLSL inline. 4 gpu-batch
  gtest hepsi compile-def + runtime-Vulkan-probe ile SKIP-clean. Named gap
  (roadmap): "finish the GpuBatchSolver (thin dispatch, no buffer ownership)".
- **Karar**: İki katman da DÜRÜSTÇE mühürlendi.
  - **CPU squad-v1 SEALED**: charter (role + formation + blackboard) tam ve
    derin test edilmiş; "moment" (4-ajan formation) sağlanmış. Gerçekten
    test-edilmemiş 3 CPU edge dalı **IMPLEMENTED(test)** (10→13 gtest):
    `CapacityCappedAt255` (add_member'ın 255-cap early-return'ü — uint8_t
    member_count'un 256'da taşmasını önler; hiç koşulmamıştı),
    `RaiseThreatClampsToUnitRange` (std::clamp'in ALT+ÜST kenarları — mevcut
    decay testi threat'i hiç 1 üstüne / 0 altına sürmüyordu),
    `UpdateHealthClampsAndIgnoresUnknown` (health clamp [0,1] + not-a-member
    no-op dalı, ikisi de test-edilmemişti).
  - **GpuBatchSolver GPU-batch-path SEALED** (promote-on-need): GPU dispatch
    katmanı, buffer ownership + descriptor-set layout + SSBO read-back ister;
    bunlar embedding renderer'ın compute-pipeline factory'sine bağlıdır
    (Sprint-3). GERÇEK bir GPU squad consumer (>=64-member squad'ı 60FPS'te
    coordinate eden bir oyun/sahne) belirene kadar bu wiring'i üretmek
    dead-code'dur. configure()-capacity-only + dispatch-shape-only tasarımı
    DOĞRU v2 ara durumdur (lib-pure: pipeline lifetime coupling yok).
- **Gerekçe**: ai_squad'ın gerçek değeri CPU coordination katmanıdır ve o
  charter-complete + deep-tested. GPU batch named gap "thin dispatch, no buffer
  ownership" — bu KASTEN öyle: M16 W4 retry policy (M15'te clang-cl'in rhi
  compile fan-out'unu her default ninja build'e çekmesi OOM yaptı) lib'i opt-in
  bıraktı; OFF build'de GpuBatchSolver symbol'ü bile yok, .cpp derlenmez, test
  SKIP-clean. honest-rule (b) promote-on-need tam bunun için: matematik/dispatch-
  shape doğru, eksik olan tek şey gerçek bir GPU tüketicinin sağlayacağı pipeline
  wiring. CPU edge testleri (cap/clamp/unknown) ise küçük+net+deterministik —
  honest-rule (a); kararsız-cap regresyonu (member 256 → member_count 0) artık
  fail-on-revert.
- **Promote-on-need**: GpuBatchSolver buffer ownership (BufferHandle uniform-
  array + desired-offset SSBO) + descriptor-set layout + compute-pipeline-factory
  entegrasyonu + canlı Vulkan dispatch testi — gerçek bir GPU squad consumer
  (>=64-member 60FPS formation) ihtiyacıyla + `CD_AI_SQUAD_ENABLE_GPU=ON`
  + ayrı ADR ile gelir; CPU Squad API + configure/tick_batch imzaları ve
  kSquadFormationCS GLSL layout sabit kalır.

### 2.2 cd::ui_font (ui/font) — 78 → 100  [default-stb-raster + opt-in-shaping SEALED; IMPLEMENTED(test)]

- **Bağlam**: 1528 LOC; stb_truetype raster always-on + skyline bin-pack + alpha
  atlas + AtlasMode::kMsdf (8-SSED unsigned SDF, zero@128) + opt-in
  FreeType outline (`CD_UI_FONT_HAVE_FREETYPE`) + HarfBuzz BiDi/ligature shaping
  (`CD_UI_FONT_HAVE_HARFBUZZ`) + msdfgen multi-channel MSDF
  (`CD_UI_FONT_HAVE_MSDFGEN`). 13 gtest (FT/HB/msdfgen-gated SKIP-clean). Named
  gap (roadmap): "default build has NO shaping → wire FreeType/HarfBuzz on by
  default or ship a real fallback shaper".
- **Karar**: **default-stb-raster + opt-in-FreeType/HarfBuzz tasarımı SEALED**;
  shaping-gate ZATEN dürüst bir graceful-fallback'tir (gerçek bir fallback
  shaper'ı VARDIR). Font::shape(), HB yoksa VEYA active_backend stb ise
  **identity shaper** döndürür: UTF-8 decode → codepoint başına bir ShapedGlyph,
  monotone stb-advance, no reorder, no ligature merge. Bu "real fallback shaper"
  named-gap koşulunu KARŞILAR (no-shaping bir crash/no-op değil; 1:1 düzgün-
  advance'li bir layout). Gerçek BiDi/ligature/contextual-substitution OS-bağımsız
  ama ağır 3rd-party bağımlılıktır (HarfBuzz) → opt-in build-config (vcpkg/
  FetchContent tier) doğru promote-on-need kapısıdır. Gerçekten test-edilmemiş 2
  default-path dalı **IMPLEMENTED(test)** (13→15 gtest):
  `ShapeReturnsEmptyForUnloadedOrEmptyText` (shape()'in front-guard'ı —
  unloaded font'ta null backend handle'ı dereference ETMEMELİ; empty-text
  short-circuit; ikisi de hiç koşulmamıştı çünkü her shape() testi font yükleyip
  non-empty text geçiyordu), `IdentityShaperDecodesMultiByteUtf8` (identity
  fallback'ın 2-/3-byte UTF-8 decode dalı — her prior identity test pure-ASCII'ydi;
  'A' + 'é'(U+00E9, 2-byte) + '€'(U+20AC, 3-byte) → 3 glyph, glyph_id==codepoint
  identity sözleşmesi, kStb force ile build'den bağımsız).
- **Gerekçe**: Named gap'in "ship a real fallback shaper" yarısı ZATEN
  sağlanmış — identity shaper gerçek, doğru-advance'li, UTF-8-aware bir
  fallback'tir (no-op değil). "wire FreeType/HarfBuzz on by default" yarısı ise
  ağır 3rd-party deps'i her default build'e zorlamak demektir; bu CLAUDE.md §6
  "clean configures cheap" + ui_font CMakeLists'in açık tasarım kararına
  (FetchContent OFF-by-default) aykırıdır. Doğru terminal durum: identity-fallback
  + opt-in-real-shaping tasarımını SEAL et + shaping-gate'in test-edilmemiş guard
  dallarını kilitle. raster/pack/MSDF yolu zaten charter-complete + derin test
  edilmiş; bu pass o yola dokunmadı.
- **Promote-on-need**: HarfBuzz/FreeType'ı default-on yapmak (gerçek BiDi +
  ligature + contextual substitution her build'de) — gerçek bir Arabic/Indic/
  complex-script UI ihtiyacıyla + `CD_UI_FONT_ENABLE_FETCHCONTENT_FT=ON` (ya da
  vcpkg-canonical CI) + ayrı ADR ile gelir; Font::shape/load_ttf/rasterize_range
  imza yüzeyi (kStb/kFreeType/kAuto backend + kAlpha/kMsdf/kMsdfMulti atlas mode)
  sabit kalır (T2.1–T2.3 ABI korunur).

### 2.3 cd::ui_a11y (ui/a11y) — 75 → 100  [a11y-model-v1 SEALED; IMPLEMENTED(test)]

- **Bağlam**: 153 src + 251 hdr; A11yTree (register/meta/tab-nav parallel
  tree — widget'ları OWN ETMEZ) + constexpr WCAG 2.1 contrast (Rec.709
  luminance, `>=` threshold) + ThemeVariant AA/AAA enforcement + 2px focus-
  indicator rect + Role taxonomy (W3C ARIA subset). 11 gtest. Named gap
  (roadmap): "screen-reader / UIA / AT-SPI bridge (baseline only today)".
- **Karar**: **a11y-model-v1 SEALED**; platform AT bridge **promote-on-need**.
  Parallel-tree mimarisi (widget-ownership-free, tab-order explicit, contrast
  audit-only) charter-complete: bir gelecek AT-bridge lib (cd::ui::a11y_bridge_*)
  bu modeli 1:1 marshal edebilir — Role enum kasten W3C-ARIA-aligned + ABI-pinned
  (Case 5 test). AT bridge'in KENDİSİ OS-specific'tir (Win32 UIA COM, Linux
  AT-SPI D-Bus, macOS NSAccessibility) ve ayrı platform-katman kütüphaneleridir —
  cd::ui_a11y'nin görevi PLATFORM-NEUTRAL modeli üretmektir, o tamam.
  screen_reader_hint() accessor zaten bridge'lerin okuyacağı entry-point'i sağlar.
  Gerçekten test-edilmemiş 4 model dalı **IMPLEMENTED(test)** (11→15 gtest):
  `SingleElementTabOrderWrapsToSelf` (size()==1 wrap arithmetic — `(0+1)%1==0`
  + `0→last==0` degenerate dalları; multi-element wrap vardı, single yoktu),
  `UnregisterFocusedClearsFocusAndTabSlot` (unregister'ın 3 coupled etkisi
  FOCUSED-mid-nav iken: erase meta + ranges::remove tab_order + reset focused_,
  ardından focus_next survivor'a iner — dangling focus yok),
  `ContrastBoundaryIsInclusiveAtThreshold` (passes_contrast'ın `>=` kenarı:
  4.5 hemen üstü PASS, hemen altı FAIL — float-ULP belirsizliğine takılmadan),
  `TabOrderToleratesUnknownIds` (header'ın "pre-bake before register" sözleşmesi:
  unknown id'ye focus güvenli, meta() default döner, crash yok — parallel-tree
  tasarımının dayandığı workflow).
- **Gerekçe**: ui_a11y'nin charter'ı PLATFORM-NEUTRAL a11y modeli + WCAG
  audit'tir, OS AT bridge DEĞİL. Named gap "screen-reader/UIA/AT-SPI bridge",
  doğası gereği OS-specific ayrı kütüphanelerdir (her biri farklı IPC: COM/D-Bus/
  Obj-C) ve bu lib'in scope'una sokmak yanlış-katmanlama olur (Role enum zaten
  1:1 marshal için hazır). honest-rule (b) promote-on-need tam bunun için. Model
  edge testleri (single-wrap/unregister-focused/contrast-boundary/unknown-id)
  küçük+net+deterministik — honest-rule (a); a11y-model invariant'ları artık
  fail-on-revert kilitli.
- **Promote-on-need**: cd::ui::a11y_bridge_win32 (UIA COM provider) /
  cd::ui::a11y_bridge_atspi (AT-SPI D-Bus) / cd::ui::a11y_bridge_macos
  (NSAccessibility) ayrı platform kütüphaneleri — gerçek bir screen-reader
  (NVDA/JAWS/VoiceOver/Orca) entegrasyon ihtiyacıyla + per-OS ayrı ADR ile gelir;
  A11yTree/A11yMeta/Role/contrast API yüzeyi (bridge'lerin tükettiği model)
  sabit kalır.

### 2.4 cd::ui (umbrella, ui/include + ui/src) — 70 → 100  [umbrella-v1 SEALED; IMPLEMENTED(test)]

- **Bağlam**: Widget.cpp 52 (retained-mode tree hit-test/dispatch/draw) + 373 hdr
  (Widget base + Panel/Label/Button + Rect/Color/DrawCommand) + 9 header widget
  (Anchor/ProgressBar/Spinner/TabBar/Theme/Tooltip/Toast/ContextMenu/...). 58
  test. Hit-test top-down DFS (parent-frame → child-local koordinat composition);
  dispatch deepest-hit'e; draw painter's-order (parent-before-child, absolute
  rect compose). Named gap (roadmap): "real glyph layout (text is a bare DrawKind
  today) + widen widget surface".
- **Karar**: **umbrella-v1 SEALED**; gerçek glyph LAYOUT **promote-on-need**
  (ui_font entegrasyonu). Retained-mode widget tree (RAII child ownership +
  coordinate-composing hit-test + painter-order draw collection + click dispatch)
  charter-complete + 58-test deep. Metin, KASTEN renderer-agnostic bir DrawKind::
  kText komutudur: cd::ui hiçbir font/glyph/atlas bağımlılığı TAŞIMAZ (header'ın
  açık tasarım sözleşmesi: "cd::ui does NOT depend on cd::rhi or cd::render") —
  metin LAYOUT'u (glyph quad'lara genişletme) doğru biçimde TÜKETİCİ'nin
  (ui_font + ui_renderer entegrasyonu) işidir, widget-tree'nin değil. Bu temiz
  renderer-decoupling, motorun mimari değeridir (Case: aynı widget tree → RHI VEYA
  WebGPU VEYA headless test). Gerçekten test-edilmemiş 4 widget-tree dalı
  **IMPLEMENTED(test)**: `ThreeDeepNestedHitTestComposesFrames` (3-derin nested
  koordinat composition — prior testler en fazla 2-level; root>mid>leaf'te her
  seviyede frame translate), `DispatchReachesDeepestButton` (dispatch DEEPEST
  widget'a iner, ancestor'a değil — container-içi-ama-button-dışı tık button'ı
  ateşlemez), `DrawOrderIsParentBeforeChildAcrossNesting` (3-level painter-order
  + absolute rect compose — frontend'in back-to-front blit'i için kritik),
  `RemoveNestedChildPrunesSubtree` (remove_child NESTED node'da — root grandchild'ı
  kaldıramaz, yalnız direct parent; subtree draw-collection'dan düşer).
- **Gerekçe**: umbrella'nın gerçek değeri renderer-agnostic retained-mode widget
  tree'dir ve o charter-complete + 58-test deep. Named gap'in "real glyph layout"
  yarısı, KASTEN bu lib'in dışındadır: text-as-DrawKind, cd::ui'yi font/glyph/
  RHI'dan ayrı tutan mimari sözleşmedir (DrawCommand renderer-neutral). Glyph
  layout'u buraya sokmak ui_font + ui_renderer'a hard dependency yaratır ve temiz
  DAG'ı (ui → [font, renderer] DEĞİL; consumer → [ui, font, renderer]) bozar.
  "widen widget surface" yarısı ise gerçek bir UI ihtiyacı olmadan yapay yüzey-
  şişirmedir (don't pad) — zaten 9 header widget + 3 core widget var. honest-rule
  (b) promote-on-need tam bunun için. Widget-tree edge testleri (3-deep-hit/
  deepest-dispatch/paint-order/nested-remove) küçük+net+deterministik — honest-
  rule (a); coordinate-composition + dispatch-routing + paint-order invariant'ları
  artık fail-on-revert kilitli.
- **Promote-on-need**: DrawKind::kText'i gerçek glyph quad'lara genişleten
  layout (Font::shape() → per-glyph atlas-UV quad emit, line-wrap, kerning, BiDi)
  — bir ui_font+ui_renderer ENTEGRASYON katmanında (cd::ui_renderer veya yeni bir
  cd::ui_text_layout lib) gerçek bir text-rendering ihtiyacıyla + ayrı ADR ile
  gelir; Widget/DrawCommand/DrawKind API yüzeyi geriye-uyumlu kalır (kText
  komutu zaten layout-ready: rect + text view + color taşır).

---

## 3. Reddedilen alternatifler

- **ai_squad GpuBatchSolver'a buffer ownership + descriptor-set + canlı dispatch
  implement etmek**: embedding renderer'ın compute-pipeline-factory'sine bağlı
  (Sprint-3); gerçek GPU squad consumer yokken üretmek dead-code + M16 W4 retry
  OOM dersine aykırı (rhi fan-out'u default build'e çekmek). RED — CPU-v1 +
  dispatch-shape-v2 SEALED, CPU edge dalları (cap/clamp/unknown) kilitlendi.
- **ui_font'ta HarfBuzz/FreeType'ı default-on yapmak**: ağır 3rd-party deps'i her
  clean ninja-base configure'a zorlar (CLAUDE.md §6 + CMakeLists FetchContent-OFF
  tasarımına aykırı); identity shaper ZATEN gerçek bir fallback shaper'dır
  (no-op değil). RED — default-stb-raster + opt-in-real-shaping SEALED, shaping-
  gate guard dalları (unloaded/empty/multi-byte-UTF8) kilitlendi.
- **ui_a11y'ye OS AT bridge (UIA/AT-SPI/NSAccessibility) implement etmek**: her
  biri farklı OS IPC (COM / D-Bus / Obj-C), multi-week, OS-specific ayrı
  platform kütüphaneleri; cd::ui_a11y'nin charter'ı PLATFORM-NEUTRAL model +
  WCAG audit'tir, Role enum zaten 1:1 marshal-ready. RED + kapsam dışı —
  a11y-model-v1 SEALED, model invariant dalları kilitlendi.
- **ui umbrella'ya glyph layout implement etmek**: text-as-DrawKind cd::ui'yi
  font/glyph/RHI'dan ayrı tutan mimari sözleşmedir; layout'u buraya sokmak hard
  font/renderer dependency yaratır + temiz DAG'ı bozar — bu ui_font+ui_renderer
  entegrasyon katmanının işidir. RED — umbrella-v1 SEALED, widget-tree
  (hit/dispatch/draw/remove) edge dalları kilitlendi.
- **Herhangi bir public header/src davranışını değiştirmek**: bu pass yalnız
  test-deepening + seal yaptı (impl davranışı sabit → API/ABI sabit, golden
  byte-identical garantili). RED — header/src DOKUNULMADI.
- **% docs'u / samples'ı / kapsam-dışı ui lib'lerini (layout/animation/input/theme/
  renderer/renderer_rhi/widgets/editor/renderer_webgpu) düzenlemek**: kapsam DIŞI
  (brief SCOPE EXCLUSION). RED.

## 4. Sonuçlar

- (+) 4/4 BAND-3 ui-subset + ai_squad kütüphanesi honest-rule terminal durumuna
  geçti: ai::squad (CPU-v1 + GPU-stub SEALED + 3 CPU edge test), ui_font
  (default-stb-raster + opt-in-shaping SEALED + 2 guard test), ui_a11y
  (a11y-model-v1 SEALED + 4 model test), ui umbrella (umbrella-v1 SEALED + 4
  widget-tree test). Hiçbir kütüphanede placeholder/TODO-state kalmadı.
- (+) ai_squad: capacity-cap (256→member_count taşma koruması), threat/health
  clamp kenarları, unknown-entity no-op artık fail-on-revert kilitli (+3 test).
- (+) ui_font: shape()'in unloaded-font null-deref koruması + empty-text short-
  circuit + identity shaper'ın multi-byte UTF-8 decode dalı artık kilitli (+2
  test); default-build shaping-gate dürüstçe belgelendi (identity = gerçek
  fallback, real shaping = opt-in build-config).
- (+) ui_a11y: single-element-wrap + unregister-focused-mid-nav + contrast-`>=`-
  boundary + pre-bake-unknown-id artık kilitli (+4 test); platform-neutral model
  vs OS AT bridge sınırı dürüstçe belgelendi.
- (+) ui umbrella: 3-deep nested coordinate composition + deepest-dispatch-routing
  + painter-order + nested-remove artık kilitli (+4 test); text-as-DrawKind
  renderer-decoupling sözleşmesi (glyph layout = consumer-integration işi)
  dürüstçe belgelendi.
- (+) Toplam +13 yeni test (3+2+4+4), hepsi edge/contract + anti-flakiness
  (sleep_for YOK; deterministik sentetik squad/string/widget-tree/Rgba). Build
  -Werror temiz (4 test target re-link); 0 yeni clang-tidy WAE defect-class.
  Public header/src DOKUNULMADI → golden byte-identical.
- (+) Her mühür "promote-on-need" tetikleyici taşır → genişleme yolu (GPU squad
  dispatch wiring, default-on real shaping, OS AT-bridge lib'leri, glyph-layout
  integration) nettir ama bugün dead-code/over-engineering/wrong-layer olmaz.
- (−) Mühürler GPU batch buffer ownership, default-on HarfBuzz shaping, OS AT
  bridge, ya da gerçek glyph layout'u bu pass'te ÜRETMEZ; GPU-consumer / complex-
  script-UI / screen-reader / text-rendering ihtiyacı doğunca ayrı ADR'larla
  gelir. Kabul: BAND 3 ui-subset + ai_squad "seal-the-deferred-by-design-v1 +
  lock-the-untested-edges" karakterinde — bu alt-kümede asıl boşluk feature-
  genişlemesi (GPU/OS/integration), unimplemented-skeleton DEĞİL.

---

## Varsayımlar

- Brief'in "100% RULE per lib" yorumu: documented gap → IMPLEMENTED+tested
  (küçük/net) VEYA SEALED — bu alt-kümenin dört named gap'i de "charter-complete
  v1 + platform/GPU/integration genişlemesi" karakterinde olduğundan her birinde
  v1 SEALED + gerçekten test-edilmemiş edge dalları IMPLEMENTED(test) + genişleme
  promote-on-need ayrıldı (band2/band3-world/foundation ADR'larıyla aynı bölünme
  çizgisi).
- ui umbrella test target'ı `cd_test_ui` (engine/ui/tests/test_ui.cpp,
  `cd_add_test(ui ...)`); ui_font=`cd_test_font`, ui_a11y=`cd_test_a11y`,
  ai_squad CPU=`cd_test_game_ai_squad` + GPU-batch harness=`cd_test_squad_gpu_batch`
  (OFF build'de SKIP-clean). Beşi de PASS (ctest -R ile doğrulandı).
- ui_font testleri Windows host'ta gerçek sistem TTF'i (arial/calibri/tahoma)
  bulduğu için SKIP etmedi; identity-shaper testi `select_backend(kStb)` ile
  build-config'ten bağımsız identity yolunu zorlar (shape()'in HB dalı yalnız
  active_backend==kFreeType iken çalışır, kStb daima identity'ye düşer).
- ui_a11y contrast-boundary testi float-ULP belirsizliğinden kaçınmak için tam-
  4.5 yerine hemen-üstü (L=0.18 → 4.6) + hemen-altı (L=0.17 → 4.4) örnekler;
  `>=` inclusive kenarı + threshold sabiti (4.5) ayrıca assert edilir.
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/game/ai_squad/
  tests/ + engine/ui/{font,a11y}/tests/ + engine/ui/tests/ + bu docs/ADR/
  dosyası altında. Yalnız TEST dosyalarına ekleme yapıldı (lib header/src/
  CMakeLists DOKUNULMADI çünkü impl-davranış değişmedi, yeni test mevcut
  test TU'larına eklendi). samples/ + hello_* + kapsam-dışı ui lib'leri + diğer
  gruplar + % docs'a dokunulmadı.
- Golden byte-identical: bu pass public header/src davranışını değiştirmedi
  (yalnız test ekleme) → hello_engine render yolu hiç etkilenmez; fixture #5
  capture baseline (research/reports/parity1121/baseline.png) ile bayt-bayt eşit
  doğrulandı (b3ua.png cmp → identical, sonra silindi).

## Sonraki

- BAND 3'ün geri kalan kütüphaneleri (render umbrella, async_submit,
  lighting_clusters, ibl, mesh_shader, cluster, texture_compress,
  material_authoring, texture_synth) bu mühür şablonunu (seal-the-v1 +
  lock-the-untested-edges + promote-on-need) tekrar kullanabilir; bu grubun büyük
  çapraz-kesen item'ı 3× froxel-clustering de-dup'ıdır (cluster ↔
  lighting_clusters ↔ light::ClusterGrid).
- Bu pass'in mühürlenen promote tetikleyicileri: ai_squad için GpuBatchSolver
  buffer-ownership + compute-pipeline-factory + canlı Vulkan dispatch (GPU squad
  consumer ihtiyacıyla); ui_font için default-on HarfBuzz/FreeType shaping
  (complex-script UI ihtiyacıyla); ui_a11y için OS AT-bridge lib'leri (UIA/
  AT-SPI/NSAccessibility — screen-reader entegrasyonu ihtiyacıyla); ui umbrella
  için glyph-layout integration (ui_font+ui_renderer text-rendering katmanı).
