# ADR-20260614 — cd::ui::Submitter Flagship Entegrasyonu (samples ≤10 son iki binary)

- **Status**: Accepted — **VERDICT: DOABLE-NOW-SAFE** (incremental, golden-nötr, 6-adımlı reçete)
- **Date**: 2026-06-14
- **Branch**: dev (HEAD f523ffb)
- **Deciders**: Cemal TATLI
- **Author**: architect (samples + engine/ui dosya:satır grep kanıtlarıyla — kanıt bloğu §2)
- **Related**:
  - `docs/ADR/ADR-20260530-ui-widget-library.md` (cd::ui::* 9-katman ailesi; Phase 1.5 = `hello_ui`, §2.2 renderer kontratı)
  - `docs/ADR/ADR-20260613-editor-panel-fold-strategy.md` (animator + material_editor → ImGui host fold; behavior_designer DEFERRED — bu ADR o ertelemeyi GERİ ALIR)
  - `docs/SAMPLES_CONSOLIDATION_PLAN.md` (samples ≤10 endgame; NEEDS-PORT tablosu)
- **Scope guard**: Bu ADR YALNIZ tasarım kontratı + Developer fold reçetesidir. `.cpp` değişikliklerini (gate flip, hello_engine overlay enjeksiyonu, sample silme) Developer yapar. cd::ui::* kütüphane `.hpp`/`.cpp`'lerine DOKUNULMAZ — entegrasyon tamamen mevcut public API (`create_with_inline_shader` + `upload` + `record`) üzerinden tüketici-tarafıdır.

---

## 1. Bağlam

### 1.1 Problem

samples konsolidasyonu 15 → 12'ye indi. Hedef ≤10 setine göre iki fazla
binary kaldı; ikisi de ImGui DEĞİL, engine-native `cd::ui::renderer_rhi::Submitter`
üzerine kurulu:

- `samples/ui/hello_ui` — `cd::ui_layout`+`cd::ui_font`+`cd::ui_renderer`+
  `cd::ui_input`+`cd::ui_widgets` Phase-1/2 zincirini sergileyen flagship UI demo.
- `samples/editor/hello_behavior_designer` — `cd::editor::panel::behavior_designer::BehaviorDesigner`
  node-graph panelini sergileyen tek-pencere demo.

Bu iki sample fold edilemiyor sanılıyordu, çünkü `hello_ui`'de
`kSubmitterPipelineReady = false` gate'i Submitter'ın DRAW-LIVE yolunu
kapatıyor (sözde "Submitter pipeline henüz hazır değil"). Panelleri ImGui'ye
port etmek Submitter-demo değerini yok eder; Submitter'ı entegre etmek
gerekiyor sanılıyordu — büyük iş.

### 1.2 Gerçek durum (gate yanıltıcı)

Kanıt taraması bu varsayımı çürüttü: **`kSubmitterPipelineReady` global bir
Submitter blokeri DEĞİL — yalnızca `hello_ui`'nin YANLIŞ FACTORY seçiminin
yan etkisidir.** Submitter'ın çalışan pipeline yolu (Route B,
`create_with_inline_shader`, Phase 554) bugün repo'da CANLI ve test edilmiş;
`hello_behavior_designer` onu gatesiz kullanıp her frame `record()` çağırıyor.
Yani entegrasyonun "eksik ön-koşul"u **yok** — gerekli her şey hazır.

---

## 2. Kanıt (dosya:satır)

### Kanıt 1 — `hello_ui` YANLIŞ factory'i (`create`, Route A) çağırıyor

`samples/ui/hello_ui/main.cpp:412`:

```cpp
auto sub_r = urr::Submitter::create(*device, sci);   // Route A
```

Route A (`Submitter::create`, header satır 112-113) bir cd::material UI
variant gerektirir; o variant ADR-20260530 Phase 1.5 zincirinde henüz
sample'a bağlanmamış. Dolayısıyla `hello_ui` satır 566-576'daki yorum
"Submitter does NOT bind a graphics pipeline today" diyor — **ama bu yalnız
seçilen factory için doğru.** Satır 577:

```cpp
constexpr bool kSubmitterPipelineReady = false;
if constexpr (kSubmitterPipelineReady) { submitter.record(cmd, frame.extent); }
```

### Kanıt 2 — Route B (`create_with_inline_shader`) CANLI ve gatesiz çalışıyor

`samples/editor/hello_behavior_designer/main.cpp:199` + `:304`:

```cpp
auto sub_r = cd::ui::renderer_rhi::Submitter::create_with_inline_shader(device, sci);  // :199 Route B
...
submitter.record(cmd, frame.extent);   // :304 — GATE YOK, her frame çiziyor
```

Route B (header satır 115-137) INLINE-derlenmiş GLSL pipeline ile gerçek bir
graphics pipeline + pipeline-layout + descriptor set kurar. ADR-20260613
Kanıt 3 de aynı bulguyu teyit eder: `hello_animator`/`hello_behavior_designer`/
`hello_material_editor` üçü de `create_with_inline_shader` ile başarıyla
çalışır; `kSubmitterPipelineReady` onları etkilemez.

### Kanıt 3 — Route B kütüphane testiyle doğrulanmış

`engine/ui/renderer_rhi/tests/test_submitter_inline.cpp:61` ve `:75`:

```cpp
TEST(SubmitterInline, CreateWithInlineShaderProducesValidHandle) { ... }
auto r = rh::Submitter::create_with_inline_shader(*vk, info);   // ASSERT_TRUE(r.has_value())
```

Test inline GLSL'i glslang ile derler, pipeline kurar, `is_valid()` + sıfır
frame state assert eder; ikinci case 4 quad push sonrası `record()` draw-call
sayısını doğrular. Yani Route B capability'si sample'a bağlı değil.

### Kanıt 4 — BehaviorDesigner paneli LIBRARY'de, sample sadece sarmalıyor

`engine/ui/editor/panel_behavior_designer/include/cd/editor/panel_behavior_designer/BehaviorDesigner.hpp:141`:

```cpp
void draw(cd::ui::renderer::DrawBatcher& batcher,
          const cd::ui::widgets::Theme&  theme,
          const cd::ui::widgets::Rect&   bounds) const;
```

Panel state + node-graph layout algoritması (`measure_subtree`,
`draw_real_tree`, `draw_demo_nodes`) tamamen kütüphanede. `hello_behavior_designer/
main.cpp:263` sadece `panel.draw(batcher, widget_theme, full_rect)` ile sarıyor.
Library testi `engine/ui/editor/panel_behavior_designer/tests/test_behavior_designer.cpp`
12 headless case (default/root/selection/zero-bounds/real-BT-5-node/50-node-stress)
ile capability'yi sample'dan bağımsız kanıtlar.

### Kanıt 5 — `hello_engine` co-existence noktası mevcut + golden gate hazır

`samples/engine/hello_engine/main.cpp:6604-6605`:

```cpp
ctx.render(cmd);          // ImGui composite/swapchain render pass içine çiziyor
cmd.end_render_pass();
```

ImGui, açık composite (swapchain, BGRA8) render pass'ine `ctx.render(cmd)` ile
çiziyor. Bir `submitter.record(cmd, frame.extent)` aynı render pass içinde,
`ctx.render(cmd)` ÖNCESİNE eklenebilir — aynı swapchain hedefi, format eşleşir.
Golden kilidi de hazır (`main.cpp:6429`, `:6540`):

```cpp
const bool kHideEditorUiForGolden = cd::hello_engine::golden::enabled();
if (!kHideEditorUiForGolden) { /* tüm editor UI panelleri */ }
```

### Kanıt 6 — DAG temiz (cycle yok)

`hello_engine/CMakeLists.txt:9-79` bugün `cd::editor`+`cd::editor_panel`+
`cd::imgui_backend` link ediyor; cd::ui_renderer_rhi / cd::ui_widgets / cd::ui_font
listede DEĞİL. Eklenecek deps `hello_behavior_designer/CMakeLists.txt:18-30`'dan
birebir kopyalanabilir: `cd::editor_panel_behavior_designer`, `cd::ui_widgets`,
`cd::ui_renderer_rhi`, `cd::ui_font`, `cd::ui_theme`. Bunlar ADR-20260530 §8 DAG'ında
`widgets → {ui,font,layout,input,renderer,theme}` ve `renderer_rhi → {rhi, renderer}`
alt-katmanlarıdır; hepsi `hello_engine`'in ALTINDA. Sample bir binary olduğu için
geri-kenar fiziksel olarak imkânsız (kimse sample'a include yapmaz). **Cycle yok,
layer violation yok.**

---

## 3. Karar

### 3.1 Üç bağımsız iş — net ayrım

| İş | Ne | Risk | Golden |
|----|-----|------|--------|
| **A. Gate flip** | `hello_ui` Route A → Route B + gate kaldır | Düşük (Route B kanıtlı) | hello_ui'nin kendi golden'ı yok; flagship'e dokunmaz |
| **B. behavior_designer fold** | paneli `hello_engine` ImGui overlay'ine taşı | Orta (yeni Submitter overlay) | golden gate ile nötr |
| **C. hello_ui fold** | widget'ları `hello_engine`/`hello_editor` paneline taşı | Orta-yüksek | golden gate ile nötr |

**Verdict: A + B = 12 → 11 DOABLE-NOW-SAFE.** C ayrıca yapılabilir ama daha
geniş (Phase-1/2 widget zinciri); A+B endgame'i 11'e indirir, C'yi 10 için
ayrı dispatch'e bırakır (§3.5).

### 3.2 İş A — `hello_ui` gate flip (golden-nötr, izole)

`hello_ui` flagship golden'ına (chrome_probe) DOKUNMAZ — bağımsız bir UI
demo binary'sidir. Reçete:

1. `main.cpp:412` `Submitter::create(*device, sci)` →
   `Submitter::create_with_inline_shader(*device, sci)`.
2. `main.cpp:577-581` `kSubmitterPipelineReady` if-constexpr bloğunu kaldır;
   gerçek-hardware dalında doğrudan `submitter.record(cmd, frame.extent)` çağır
   (NullDevice dalı zaten ayrı `report_headless_frame`'de).
3. Satır 566-576'daki yanıltıcı yorumu güncelle (Route B inline pipeline aktif).

Bu, `hello_ui`'yi gerçek-hardware'de CANLI çizen bir sample'a dönüştürür —
silinmeden önce yapılması gereken doğru durum. (Not: glyph atlas sampling
inline FS'de henüz yok; solid + label-quad çizimi görünür. Route A glyph
desteği ileri faz, bu ADR kapsamı dışı.)

### 3.3 İş B — behavior_designer → `hello_engine` paneli (12 → 11)

ADR-20260613 behavior_designer'ı DEFERRED bıraktı çünkü o ADR yalnız ImGui-host
seçeneğini (Seçenek B) değerlendirdi ve node-graph'ı ImGui DrawList ile yeniden
yazmanın ~200 satır olduğunu söyledi. **Bu ADR daha iyi bir yol açar: DrawBatcher
yolunu OLDUĞU GİBİ koru, hello_engine'e bir cd::ui::Submitter overlay ekle.**
Panel `draw()` imzası değişmez; ImGui'ye port YOK.

Co-existence modeli — **ek overlay, replace DEĞİL**:

- ImGui ve Submitter aynı composite/swapchain render pass'ine çizer (Kanıt 5).
  İkisi de bir önceki çizimin üstüne alpha-blend ile katman ekler; Submitter
  pipeline `depth test OFF + alpha blend ON` (header satır 126-127). Çakışma yok.
- Çizim sırası: scene → composite → **`submitter.record(cmd)`** → `ctx.render(cmd)`
  (ImGui en üstte kalır, panel'i bir ImGui penceresi gibi çerçeveleyebilir) →
  `end_render_pass`.

Golden-safe garanti (ZORUNLU invariant):

- Overlay **default OFF**. Yeni bir `bool show_behavior_designer = false`
  flag'i + bir ImGui menü/panel toggle.
- Overlay çizimi `if (!kHideEditorUiForGolden && s.show_behavior_designer)`
  bloğunun İÇİNDE (Kanıt 5 gate'i). golden::enabled() iken Submitter ASLA
  record etmez → composite çıktısı bit-bit değişmez.
- chrome_probe fixture golden::enabled() ile koşar → **5 byte-identical
  garantisi korunur** (çünkü Submitter `upload`/`record` golden frame'de hiç
  çağrılmaz; pipeline create boot'ta olsa bile çizim emit edilmez).

incremental adımlar (her biri ayrı yeşil-checkpoint):

1. **Boot wiring** — `hello_engine` boot'ta bir `cd::ui::renderer_rhi::Submitter
   ui_submitter = create_with_inline_shader(device, {color_format=kBGRA8Unorm})`
   + `cd::ui::renderer::DrawBatcher ui_batcher` + `cd::editor::panel::behavior_designer::BehaviorDesigner bd_panel`
   üyelerini `SampleAppState`/`HelloEngineFx` aggregate'ine ekle. Henüz hiç
   çizim yok → 254/254 PASS, golden değişmez.
2. **CMake deps** — `hello_engine/CMakeLists.txt`'e Kanıt 6'daki 5 dep (+
   `cd::game_ai_bt` BehaviorDesigner'ın BT tipini taşıyorsa) ekle. Sadece link;
   golden değişmez.
3. **Overlay record** — composite pass içinde, `ctx.render(cmd)` ÖNCESİNE,
   golden-gate + toggle bloğunda: `ui_batcher.begin_frame(); bd_panel.draw(ui_batcher,
   widget_theme, panel_rect); ui_submitter.upload(ui_batcher); ui_submitter.record(cmd, frame.extent);`.
   Toggle default OFF → golden frame'de hiç çalışmaz → byte-identical. Toggle
   ON iken kullanıcı node-graph'ı görür.
4. **Sample silme** — `samples/editor/hello_behavior_designer/` dizini +
   CMakeLists referansını sil. 12 → 11.

### 3.4 İş C — `hello_ui` widget seti → panel fold (11 → 10, opsiyonel-ileri)

`hello_ui` Button + Slider + Label + Flex-layout zincirini hello_engine'e bir
"UI Widgets Demo" overlay paneli olarak taşı (İş B ile aynı Submitter + golden
gate altyapısını yeniden kullanır). Bu daha geniş bir iş çünkü `cd::ui_layout`+
`cd::ui_input`+`cd::ui_widgets` etkileşim (tick/hit-test) yolunu da taşımak
gerekir. İş B'nin Submitter overlay altyapısı bir kez kurulduğunda İş C ek bir
DrawBatcher+panel bloğudur — aynı golden-gate invariantı geçerlidir. **Bu ADR İş
C'yi DOABLE olarak işaretler ama İş A+B'den sonraki ayrı dispatch'e bırakır**
(endgame 11'den 10'a son adım).

### 3.5 Sample sayısı etkisi

| Aşama | Sample | Not |
|-------|--------|-----|
| Bugün | 12 | hello_ui (gated) + hello_behavior_designer + 10 |
| İş A+B sonrası | **11** | hello_behavior_designer silindi; hello_ui canlı (henüz duruyor) |
| İş C sonrası | **10** | hello_ui da fold edildi → HEDEF |

İş A tek başına sample sayısını değiştirmez (hello_ui'yi düzeltir); silme İş C'de.
İş A+B ile 11'e, İş C ile 10'a inilir.

---

## 4. Reddedilen alternatifler

### 4.1 behavior_designer'ı ImGui DrawList ile yeniden yaz (ADR-20260613 Seçenek B)

_Red_: ~200 satır node-graph render kodu ImGui primitive'leri ile baştan
yazılır; mevcut kütüphane `draw(batcher,...)` yolu boşa harcanır; library ile
sample arasında ikinci bir görsel implementasyon (drift riski) doğar. Submitter
overlay yolu kütüphane imzasını HİÇ değiştirmez ve tek görsel kaynak kalır.
ADR-20260613'ün bu paneli DEFERRED bırakma gerekçesi (Submitter altyapısı yoktu
varsayımı) bu ADR'nin Kanıt 2+5'i ile geçersizleşti.

### 4.2 DrawBatcher → offscreen texture → `ImGui::Image` (ADR-20260613 Seçenek A)

_Red_: offscreen render target + per-frame texture blit + descriptor-per-frame
güncelleme gerektirir (~400 satır + yeni bağımlılık). Submitter zaten swapchain
pass'ine doğrudan çizebiliyorken (Kanıt 5) gereksiz dolaylama. samples ≤10
endgame için orantısız.

### 4.3 Submitter overlay'i ImGui'yi DEĞİŞTİRSİN (tek UI loop)

_Red_: hello_engine'in tüm editor UI'si (inspector, outliner, lights, history,
gizmo, command palette — main.cpp:6441-6539) ImGui-native. Submitter'a port
multi-hafta iş; golden riski yüksek. ADR-20260530 §5 ImGui'nin dev-overlay için
KALACAĞINI zaten söylüyor. Co-existence (ek overlay) doğru model; ikisi aynı
pass'e blend eder, çakışmaz.

### 4.4 hello_ui gate'i olduğu gibi bırak, sadece sil

_Red_: gate yanlış-pozitif; düzeltilmeden silmek "Submitter pipeline hazır değil"
yanlış inancını repo'da bırakır ve Route B'nin canlı olduğu gerçeğini gizler.
İş A (gate flip) ucuz ve doğru durumu kayda geçirir; İş C silmeyi sonra yapar.

### 4.5 hello_ui'yi Route A (`create_with_material_ui_variant`) ile düzelt

_Red_: Route A cd::material UI variant'ı sample'a bağlamayı gerektirir
(ADR-20260530 Phase 1.5 henüz tamamlanmamış zincir). Route B inline GLSL ile
aynı görünür sonucu BUGÜN verir (solid + quad). Glyph-sampling Route A
özelliği ileri faz; gate flip için Route B yeterli ve risksiz.

---

## 5. Sonuçlar

### 5.1 Developer kontratı (imzalar + invariantlar)

- **Kullanılacak public API (değişmez)**: `Submitter::create_with_inline_shader(device, info)`,
  `submitter.upload(batcher)`, `submitter.record(cmd, extent)`,
  `BehaviorDesigner::draw(batcher, theme, bounds)`. Hiçbir cd::ui::* `.hpp`/`.cpp`
  düzenlenmez.
- **ZORUNLU golden invariant**: hello_engine'deki HER yeni Submitter
  `upload`/`record` çağrısı `if (!kHideEditorUiForGolden && show_X)` bloğunun
  İÇİNDE olmak ZORUNDA. Bir Submitter çizimi golden gate dışında bırakılırsa
  chrome_probe byte-identical bozulur → bu bir VETO sapmasıdır.
- **Submitter overlay default OFF**: `show_behavior_designer = false` boot
  varsayılanı; toggle ImGui menüsünden.
- **Format eşleşmesi**: `SubmitterCreateInfo.color_format = kBGRA8Unorm`
  (composite/swapchain formatıyla eşleşmeli — Kanıt 5 + hello_behavior_designer:196).
- **record() render pass içinde**: yalnız composite pass açıkken (`ctx.render`
  öncesi), `end_render_pass` sonrası DEĞİL.

### 5.2 Developer fold reçetesi (özet)

```
İş A — hello_ui gate flip:
  1. main.cpp:412   create() -> create_with_inline_shader()
  2. main.cpp:577   kSubmitterPipelineReady bloğunu kaldır; record() doğrudan
  3. main.cpp:566   yanıltıcı yorumu güncelle
  -> ctest 254/254 PASS; chrome_probe golden DOKUNULMAZ (ayrı binary)

İş B — behavior_designer fold:
  1. hello_engine aggregate'ine ui_submitter + ui_batcher + bd_panel üye ekle
  2. hello_engine/CMakeLists.txt: +cd::editor_panel_behavior_designer +cd::ui_widgets
     +cd::ui_renderer_rhi +cd::ui_font +cd::ui_theme (+cd::game_ai_bt gerekiyorsa)
  3. composite pass: ctx.render(cmd) ÖNCESİNE golden-gate+toggle bloğunda
     begin_frame/draw/upload/record
  4. samples/editor/hello_behavior_designer/ + CMakeLists referansını sil
  -> ctest PASS; show_behavior_designer=false iken golden byte-identical
  -> 12 -> 11

İş C — hello_ui fold (ayrı dispatch, 11 -> 10):
  İş B Submitter overlay altyapısını yeniden kullan; Button/Slider/Label +
  Flex layout + input tick'i bir "UI Widgets Demo" overlay paneline taşı;
  samples/ui/hello_ui/ sil.
```

### 5.3 Doğrulama planı

- `cmake --build --preset ninja-debug` her checkpoint'te temiz.
- `ctest --preset ninja-debug --output-on-failure` 254/254 (veya güncel sayı) PASS.
- chrome_probe golden fixture: golden::enabled() iken Submitter overlay HİÇ
  record etmediği için 5 byte-identical KORUNUR — bu, fold'un golden-nötr
  olduğunun kanıtıdır (her checkpoint'te golden diff sıfır olmalı).
- hello_engine çalıştırıldığında toggle ON → BehaviorDesigner node-graph'ı bir
  overlay olarak görünür; OFF → hiç çizilmez.
- `hello_behavior_designer` binary'sinin build grafiğinden çıktığı doğrulanır.

### 5.4 Etkilenen modüller

| Dosya | Değişiklik türü |
|-------|-----------------|
| `samples/ui/hello_ui/main.cpp` | İş A: factory + gate flip (~6 satır) |
| `samples/engine/hello_engine/main.cpp` | İş B: Submitter overlay (~40 satır, golden-gate içinde) |
| `samples/engine/hello_engine/CMakeLists.txt` | İş B: +5 (±1) cd::ui_* / panel dep |
| `samples/editor/hello_behavior_designer/` | İş B: Silinir |
| `samples/ui/hello_ui/` | İş C: Silinir (ayrı dispatch) |
| `docs/SAMPLES_CONSOLIDATION_PLAN.md` | behavior_designer DEFERRED → DONE; hello_ui güncelle |
| `docs/ADR/ADR-20260613-...` | behavior_designer DEFERRED kararını bu ADR'ye işaret eden not |

### 5.5 Risk + VETO

- **GPU-driver flakiness**: `cd_test_rhi_vulkan` stres testinde flaky rapor
  edildi, ancak golden path SAĞLIKLI ve Submitter overlay golden frame'de HİÇ
  çalışmaz (gate). Risk golden'a yansımaz. Boot'taki `create_with_inline_shader`
  hata dönerse (glslang/pipeline) → `if (!ui_submitter.has_value())` ile log + skip;
  golden etkilenmez. Düşük risk.
- **VETO bulgusu**: YOK. Circular dependency yok (Kanıt 6: sample-binary, geri-kenar
  imkânsız). Layer violation yok (cd::ui_* hello_engine'in altında). God object yok
  (Submitter tek-sorumluluk: UI batch → RHI). Magic-type yok (strong handle'lar
  `TextureViewHandle`/`SamplerHandle`).

### 5.6 VERDICT

**DOABLE-NOW-SAFE.** Eksik somut ön-koşul YOK — Route B pipeline canlı
(Kanıt 2-3), panel library'de + test edilmiş (Kanıt 4), co-existence noktası +
golden gate hazır (Kanıt 5), DAG temiz (Kanıt 6). İş A+B incremental + golden-nötr
ile 12 → 11; İş C ile 10. `kSubmitterPipelineReady = false` bir engel değil,
yanlış-factory'nin yan etkisidir ve İş A onu doğru duruma getirir.

— Architect, 2026-06-14
