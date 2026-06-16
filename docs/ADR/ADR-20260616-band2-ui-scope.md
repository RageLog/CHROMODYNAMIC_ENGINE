# ADR-20260616 — ALL-MODULES-TO-100 BAND 2 / UI grubu Kapsam Mührü (7 kütüphane)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD d33fb24)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 2 UI alt-grubu close-out — kapsam mührü + test-topup pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 2 listesi + "100% = IMPLEMENTED+test
    YA DA formally SEALED" honest-rule, §"What 100% MEANS")
  - `docs/PROJECT_COMPLETION_STATUS.md` §7 (engine/ui baseline %'leri)
  - `docs/ADR/ADR-20260530-ui-widget-library.md` (UI yığını mimari sözleşmesi:
    renderer-agnostic widget tree → batcher → RHI/WebGPU)
  - `docs/ADR/ADR-20260616-band1-scope.md` (aynı honest-rule + mühür formatı)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar dokümanıdır. Mühürlenen
  maddeler "deferred-by-design"dır — açık (open) sayılmazlar. Her madde
  "ihtiyaç doğunca promote" çıkış kapısı taşır. Bu pass implementasyon
  değişikliği YAPMADI; yalnız (1) gerçek bir yüzey-boşluğu olan tek noktaya
  test ekledi (ui_widgets NativeWindowAdapter aggregate per-frame yüzeyi) ve
  (2) bu mührü kayda geçirdi. Kapsam YALNIZ
  `engine/ui/{layout,animation,input,theme,renderer,renderer_rhi,widgets}`;
  `editor`, `font`, `a11y`, `renderer_webgpu` ve `ui` umbrella daha alt
  band'lerde, bu mührün DIŞINDA.

---

## 1. Bağlam

BAND 2, completion-band'lerin 80–89% dilimindeki kütüphaneleri 100%'e taşır.
Bu ADR, o band'in **UI alt-grubunu** (engine/ui altındaki 7 charter-complete
kütüphane) kapsar. Roadmap'in honest-rule'una göre bir modül 100%'tür ancak
HER dokümante boşluk iki terminal durumdan birindeyse: (a) IMPLEMENTED + test,
VEYA (b) bir paragraflık kapsam-ADR'ı ile formally SEALED. "TODO/placeholder"
üçüncü durumu kalamaz.

Bu 7 kütüphanenin tamamı zaten production/partial-but-charter-complete
çekirdeğe sahip (ADR-20260530-ui-widget-library mimarisini izleyen,
renderer-agnostic katmanlı yığın). İnceleme sonucu: 6 kütüphane
charter-complete idi (dokümante "gap"ler ya design-scope reserved-no-op ya da
başka bir band'deki bağımlılığa dayalı) → SEAL; **ui_widgets** ise gerçek bir
yüzey-boşluğu taşıyordu (NativeWindowAdapter'ın `pump_all` + `collect_closed_panels`
aggregate per-frame API'sinin 0 test kapsamı) → test ile dolduruldu + kalan
cross-platform maddesi mühürlendi. **ui_renderer_rhi**'nin "glyph atlas draw
path" maddesi incelemede ZATEN IMPLEMENTED + device-test edilmiş bulundu
(Phase 648 / M10 W3A Sprint-3 Route A material UI-variant) → IMPLEMENTED
olarak teyit + multi-backend device-verification mührü.

---

## 2. Karar — kütüphane başına bir bölüm

### 2.1 cd::ui_layout (engine/ui/layout) — 88 → 100

- **Bağlam**: İki bağımsız çözücü: Yoga-şekilli Flex (grow/shrink, 6 justify
  modu, align, gap, clamp, nested subtree solve) + Cassowary-stili artımlı
  simplex Constraint çözücü. 946 src + 23 gtest.
- **Karar**: Çekirdek IMPLEMENTED + test edildi (full `solve()` tüm rect'leri
  tek geçişte hesaplar). Tek dokümante boşluk: `FlexTree::compute_main` /
  `compute_cross` / `position_children` private üye fonksiyonları
  *reserved-no-op* (Flex.cpp:406-408) — "gelecekteki artımlı çözücü" için
  ayrılmış ÖLÜ-yer-tutucu, public yüzey DEĞİL. Bugünkü `solve()` zaten
  tam-yeniden-hesap yapan tek-geçiş çözücüdür ve artımlı yola ihtiyaç yoktur
  (UI ağaçları küçük; tam re-solve <birkaç-yüz-node için kabul edilebilir).
  **SEALED**: artımlı (dirty-subtree) çözücü deferred-by-design.
- **Gerekçe**: Reserved-no-op'lar derleme-içi ölü-kod değil; gelecekteki
  dirty-tracking çözücünün imza-iskeletidir. Bunları silmek API plan-niyetini
  kaybettirir; doldurmak ise consumer-olmayan spekülatif iş olurdu (kapsam
  kilidi: bonus yok). Tam çözücü + 23 test halihazırda fail-on-revert kilitli.
- **Promote-on-need**: 1000+ node'lu canlı düzenlerde tam re-solve profil'de
  görünürse, dirty-subtree artımlı çözücü ayrı bir ADR + benchmark ile o üç
  imzayı doldurur.

### 2.2 cd::ui_animation (engine/ui/animation) — 85 → 100

- **Bağlam**: Penner easing ailesinin tamamı (linear/quad/cubic/quart/quint/
  sine/expo/circ/elastic/back/bounce × in/out/inout) + Tweener + Timeline
  keyframe enterpolasyonu. Self-contained, atıflı (Robert Penner). 213 src +
  14 gtest.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Dokümante kNotImpl/placeholder
  YOK. Easing katalogu + tween + timeline tam; kütüphane kendi kapsamında
  (zaman-tabanlı skalar enterpolasyon) eksiksiz. Spring-fizik (Hooke/damping)
  ve stagger-orkestrasyon (gruba-yayılan gecikme) kapsam DIŞI — bunlar üst
  katman widget-animasyon politikasıdır (cd::ui_widgets'in `test_widgets_animation`
  zaten tween'i widget üzerinde sürüyor). **SEALED**: easing/tween/timeline
  charter eksiksiz.
- **Gerekçe**: Tek sorumluluk (parametrik easing + zaman çizelgesi). Eklenecek
  her şey (spring, stagger) yeni bir kavram ailesi olurdu; bu kütüphaneyi
  şişirmek mimari katmanı bulanıklaştırır.
- **Promote-on-need**: Material-3 motion'un spring-tabanlı geçişleri için bir
  `SpringSolver` istenirse ayrı bir başlık + ADR ile gelir.

### 2.3 cd::ui_input (engine/ui/input) — 85 → 100

- **Bağlam**: HitTester (z-order ön-arka tarama) + FocusManager (tab sırası +
  modal yığını + trap) + GestureRecognizer FSM (press/drag/long-press/
  double-click/pinch/swipe). 559 src + 450 hdr + 21 gtest.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Dokümante kNotImpl/placeholder
  YOK. Hit-test + focus-chain + jest FSM tam. IME / dead-key kompozisyonu
  (Widgets.hpp:144'te dokümante) ve platform-yerel jest (3-parmak swipe gibi
  OS-seviye) kapsam DIŞI — bunlar cd::platform OS-event köprüsünün işi (başka
  band) ve cd::ui_widgets TextInput'un Phase-4 maddesi. **SEALED**: pointer/
  focus/gesture charter eksiksiz.
- **Gerekçe**: Bu kütüphane *normalize edilmiş* girdi olaylarını işler; ham OS
  kompozisyon/yerel-jest, platform katmanının çözüp yukarı verdiği bir
  girdidir. Katman ayrımı korunur.
- **Promote-on-need**: IME kompozisyon-penceresi UI'si istenirse
  cd::platform + cd::ui_widgets TextInput birlikte Phase-4 ADR'ı ile gelir.

### 2.4 cd::ui_theme (engine/ui/theme) — 85 → 100

- **Bağlam**: Material-3 token sistemi: dark/light/high-contrast şemaları +
  brand-override + WCAG on-color kontrast türetimi + spacing/motion/elevation
  ölçekleri. 175 src + 293 hdr + 22 gtest (palet-refresh regresyon kilidi).
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Dokümante kNotImpl/placeholder
  YOK. Token üretimi + kontrast hesabı + şema varyantları tam. Runtime
  tema-dosyası hot-reload (JSON'dan tema yükleme) ve dinamik-renk (Material-3
  HCT'den-tohum üretim) kapsam DIŞI — bunlar bir asset-pipeline + ayrı renk
  bilim modülü gerektirir. **SEALED**: token/şema/kontrast charter eksiksiz.
- **Gerekçe**: Sabit Material-3 token kümesi + WCAG kontrast, bir tema
  *kütüphanesinin* sorumluluğudur; HCT renk-uzayı üretimi ayrı bir bilim
  modülüdür (skopu genişletmek kütüphaneyi çift-sorumlu yapar).
- **Promote-on-need**: Material-3 dinamik-renk (tek tohum-renkten tam palet)
  istenirse `cd::ui_theme_hct` ayrı kütüphane + ADR ile gelir; bu kütüphanenin
  token yüzeyi sabit kalır.

### 2.5 cd::ui_renderer (engine/ui/renderer) — 85 → 100

- **Bağlam**: Saf-CPU DrawBatcher: quad/textured/glyph emit, RGBA8 vertex,
  u16 indeks, scissor yığını, komşu-merge (aynı variant+texture+scissor →
  tek DrawCommand). RHI-bağımsız (test edilebilir, GPU gerektirmez). 134 src +
  10 gtest.
- **Karar**: Çekirdek IMPLEMENTED + test edildi. Tek dokümante limit:
  >65535 vertex'te otomatik index-buffer bölme YOK (u16 indeks tavanı,
  DrawBatcher.hpp'de dokümante). Bu deliberate v1 sınırıdır — bir UI sayfası
  tek 65535-vertex bütçesine sığar; aşımda çağıran `begin_frame()`'i daha sık
  çağırır (renderer_rhi `upload()` aşımda `false` döner, sessiz yutmaz).
  **SEALED**: CPU-batcher charter eksiksiz, u16-tavan dokümante v1 sınırı.
- **Gerekçe**: u16 indeks std140/AVX-dostu vertex'i küçük tutar (16B); u32'ye
  geçiş tüm vertex/upload yolunu etkiler ve bugünkü tek-sayfa UI'lar için
  gereksiz maliyettir. Aşım sessiz değil — `upload()` reddeder.
- **Promote-on-need**: Tek-frame'de 65535+ vertex'li UI (devasa tablo/terminal)
  gerekirse u32-indeks variant + otomatik chunk-split ayrı bir ADR ile gelir.

### 2.6 cd::ui_renderer_rhi (engine/ui/renderer_rhi) — 80 → 100 (glyph yolu IMPLEMENTED)

- **Bağlam**: cd::rhi üzerinde ring vb/ib + iki pipeline yolu: Route B
  (glslang inline solid-quad, glyph-yok), Route A (cd::material UI-variant ile
  theme-UBO + SDF-glyph sampler descriptor). 712 src + 4 gtest dosyası
  (submitter / submitter_inline / material_route / material_route_end_to_end).
- **Karar**: "Glyph-atlas draw path (material UI-variant ile gelir)" maddesi
  incelemede ZATEN IMPLEMENTED + test edilmiş bulundu (Phase 648 / M10 W3A
  Sprint-3). Kanıt: `create_with_material_ui_variant` bir SDF-sampler taşıyan
  Sprint-2 variant'tan MaterialInstance + theme-UBO ayırır, descriptor yazar;
  `set_sdf_atlas(view, sampler)` glyph atlasını canlı bağlar; `record()`
  variant pipeline'ını bind eder, inv-viewport push-constant'ı geçer ve
  descriptor-set'i bağlar. Per-vertex `variant` alanı (kGlyph=2) fragment
  shader'da çözülür (shader cd::material'da — başka band). `test_submitter_material_route_end_to_end`
  4 solid quad + 1 glyph'i tam Route A + Sprint-2 variant + canlı 4×4 R8 SDF
  doku ile gerçek Vulkan command-buffer'a kaydeder (RTX 3080 üzerinde device
  test; Vulkan ICD yoksa GTEST_SKIP — bandaj değil, donanım-kapısı). Kalan tek
  madde: fonksiyonel çizimler bugün YALNIZ Vulkan'da doğrulanmıştır; D3D12/Metal
  parite-doğrulaması cd::rhi BAND 0 (3-backend parite mega-marathon, %100)
  işidir, bu UI kütüphanesinin değil. **IMPLEMENTED (glyph) + SEALED
  (multi-backend device-verification cd::rhi'ye delege)**.
- **Gerekçe**: Glyph yolu uçtan-uca canlı + device-test kilitli — açık değil.
  UI submitter backend-agnostiktir (cd::rhi::ICommandBuffer'a kaydeder); D3D12/
  Metal'de aynı kodun çalışması cd::rhi'nin cross-backend parite garantisinin
  (zaten %100) bir sonucudur, bu kütüphanenin tekrar-ispatlayacağı bir şey
  değil. Yeni implementasyon (kapsam kilidi gereği) eklenmedi.
- **Promote-on-need**: D3D12/Metal UI golden-paritesi açıkça istenirse, cd::rhi
  pixel-parity capstone'una bir UI-fixture eklenir (o band'in işi); submitter
  yüzeyi değişmez.

### 2.7 cd::ui_widgets (engine/ui/widgets) — 80 → 100 (yüzey-boşluğu test ile dolduruldu)

- **Bağlam**: 8 somut widget (ColorPicker/CurveEditor/DockSpace/Table/TreeView/
  PopoutDock + temel Button/Slider/Toggle/…) + NativeWindowAdapter (208 LOC,
  bugün Win32-yerel pencere). 6797 LOC + 99 gtest/9 dosya — editor-dışı en güçlü
  kapsam.
- **Karar**: Widget mantığı IMPLEMENTED + 99 test ile derinlemesine kapsanmış.
  İnceleme NativeWindowAdapter'da GERÇEK bir yüzey-boşluğu buldu: aggregate
  per-frame API'si `pump_all(out)` (GAP-2 toplu OS-event tahliyesi) ve
  `collect_closed_panels(callback)` (yerel-X kapanma hasadı) public yüzeydi
  ama 0 test kapsamına sahipti. Bu pass üç path-agnostik test ekledi
  (stub-path = no-op, gerçek Win32 = kuyruk-tahliye; ikisinde de çökmez,
  yeni-açılan pencere should_close raporlamaz, event'ler yalnız managed
  panel-id ile etiketlenir). Kalan dokümante boşluk: cross-platform yerel
  pencere — bugün Win32-only. GAP-1 (set_position non-Win32) cd::platform
  IWindow'a `set_position(x,y)` eklenmesine dayanır (engine/foundation/platform
  — başka band); GAP-2 (aggregate pump) zaten adapter'ın `pump_all`'ı ile
  Win32'de çözüldü. Adapter, platform `kNotImplemented` dönerse gap kaydedip
  Sprint-1 floating-internal'a düşer (çökmez, sessiz-kayıp yok). **SEALED
  (Win-first cross-platform native window) + yüzey-boşluğu (aggregate per-frame)
  test ile dolduruldu**.
- **Gerekçe**: 99 test widget mantığını (state-machine, hit-test, draw-emit)
  zaten kilitler; eksik olan yalnız adapter'ın iki aggregate metodu idi — o
  dolduruldu. Çok-monitör yerel-pencere genişletmesi cd::platform IWindow'un
  set_position + non-Win32 backend'lerine bağlıdır (foundation band); bunu
  burada eklemek out-of-scope kütüphaneye dokunmak olurdu (kapsam ihlali).
  Adapter zaten ileriki platformlar için GERÇEK promote-on-need iskelesi taşır
  (gap kaydı + unblock_api dizesi).
- **Promote-on-need**: cd::platform foundation band'inde IWindow `set_position`
  + non-Win32 backend kazandığında, `platform_try_set_position` tek-satır
  promote olur ve NativeWindowAdapter otomatik gerçek çok-monitör'e geçer
  (adapter kodu değişmeden — gap kaydı boşalır).

---

## 3. Sonuçlar

- **Olumlu**: 7 UI kütüphanesi de honest-rule terminal durumuna ulaştı
  (IMPLEMENTED+test ya da SEALED). 6 SEAL + 1 gerçek test-topup (ui_widgets
  aggregate per-frame yüzeyi, 3 yeni gtest). ui_renderer_rhi glyph yolu
  IMPLEMENTED olarak teyit edildi (yeni iş gerekmedi). Hiçbir out-of-scope
  kütüphaneye dokunulmadı.
- **Olumsuz / borç**: Cross-platform yerel pencere (ui_widgets) ve D3D12/Metal
  UI device-paritesi (ui_renderer_rhi) başka band'lerdeki bağımlılıklara
  delege edildi — bu band'de "açık" sayılmaz ama tam-cross-platform UI
  hedefine ulaşmak için o band'ler tamamlanmalı.
- **Nötr**: Reserved-no-op Flex imzaları + u16 indeks tavanı bilinçli v1
  kararları olarak kayda geçti; ileride profil/ihtiyaç doğarsa promote yolları
  dokümante.

## 4. Reddedilen alternatifler

- **Flex artımlı çözücüyü şimdi doldur**: Consumer yok + tam re-solve bugünkü
  node sayıları için yeterli → spekülatif iş, kapsam kilidi ihlali. Reddedildi.
- **u32 indeks'e geç**: Tüm vertex/upload yolunu etkiler, tek-sayfa UI için
  gereksiz → reddedildi (promote-on-need).
- **ui_widgets'e cross-platform pencere ekle**: cd::platform IWindow
  `set_position` + non-Win32 backend (foundation band, out-of-scope) gerektirir
  → reddedildi, SEAL + promote-on-need.
- **ui_renderer_rhi'ye D3D12/Metal UI golden ekle**: cd::rhi BAND 0 cross-backend
  parite capstone'unun işi → reddedildi, oraya delege.
