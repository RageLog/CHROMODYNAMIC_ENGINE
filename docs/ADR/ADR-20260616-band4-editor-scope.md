# ADR-20260616 — ALL-MODULES-TO-100 BAND 4 / cd::editor Kapsam Mührü (editor-v1: shell + 27 panel + cdproj)

- **Status**: Accepted
- **Date**: 2026-06-16
- **Branch**: dev (HEAD eb7df52)
- **Deciders**: Cemal TATLI
- **Author**: Developer (BAND 4 cd::editor close-out — functional-but-placeholder-visuals editor-v1 seal + untested-panel-logic-topup + editor.exe-entry decision + false-banner correction pass)
- **Related**:
  - `docs/ROADMAP_ALL_MODULES_TO_100.md` (BAND 4 listesi: editor 68 "**BIGGEST
    SURFACE (~15.8k LOC)**: real in-panel text/glyph + ImGui-ize the 27 panels
    (placeholder visuals today); single editor.exe entry"; §"What 100% MEANS"
    honest-rule — IMPLEMENTED+test VEYA formally SEALED, "No fourth
    TODO/placeholder state"; §"Effort: L. The editor is the dominant item — treat
    as its own multi-wave sub-plan")
  - `docs/PROJECT_COMPLETION_STATUS.md` §7 (engine/ui: editor 68, partial —
    "~15.8k src / ~10k test / 497 cases; shell wires ecs::World+scene+input+widget
    tree; 27 panels bind real engine systems (IkChainEditor 756, Cutscene 555) BUT
    most render simplified DrawBatcher visuals (accent bars for text, gradient
    placeholders) — only Inspector uses ImGui; heavily tested, visually incomplete
    vs a real DCC"; group rollup "the honest gap is feature-completeness/visual-
    fidelity, not unimplemented skeletons")
  - `docs/ADR/ADR-20260530-editor-binary.md` (standalone `cd_editor_app`
    binary'nin TAM tasarımı — `apps/editor/`, `.cdproj` parser, asset browser,
    PIE controller, hot-reload bus, plugin host; Phase-2 ~6 KLOC + ~1.2 KLOC test,
    ~6.5 hafta tek-developer; bu ADR'ın "single editor.exe entry" kararı buna
    yaslanır)
  - `docs/ADR/ADR-20260616-band3-ui-aisquad-scope.md` §2.2 (ui_font:
    default-stb-raster + opt-in-shaping SEALED; gerçek BiDi/ligature/glyph-shaping
    promote-on-need) + §2.4 (ui umbrella: text-as-DrawKind, gerçek glyph LAYOUT
    promote-on-need — "glyph layout = consumer-integration işi") — editor'ün
    "real in-panel text/glyph" gap'inin İKİ DOWNSTREAM BAĞIMLILIĞI da B3'te
    SEALED; bu yüzden editor'ün text/glyph genişlemesi B3-mühürlü temellere
    yaslandığından bu pass'te ÜRETİLEMEZ
  - `docs/ADR/ADR-012-editor-mechanics.md` + `docs/ADR/ADR-009-ui-architecture.md`
    + `docs/ADR/ADR-20260530-ui-widget-library.md` (editor mekanikleri + UI
    mimarisi + widget kütüphane charter'ı)
  - kardeş band-mühür ADR'ları (`ADR-20260616-band4-render-features-scope.md`,
    `ADR-20260616-band3-ui-aisquad-scope.md`; aynı şablon: seal-the-v1 +
    lock-the-untested-logic-branches + promote-on-need + false-banner-fix)
- **Scope guard**: Bu ADR YALNIZ kapsam-karar + untested-panel-logic-topup +
  editor.exe-entry-decision + false-banner-fix dokümanıdır. Mühürlenen madde
  ("placeholder-visuals + no-in-panel-glyph + no-standalone-binary")
  "deferred-by-design"dır — promote-on-need çıkış kapısı taşır. Bu pass'in
  değişiklikleri YALNIZ engine/ui/editor/ (3 test dosyası + 1 README) + bu
  docs/ADR/ dosyası altında. Diğer ui lib'leri (B3-sealed: font/a11y/umbrella;
  B2-sealed: layout/animation/input/theme/renderer/renderer_rhi/widgets;
  renderer_webgpu B7) + samples/ + hello_* + % docs DEĞİL. Bu pass yalnız
  TEST dosyalarına ekleme yaptı + 1 README banner düzeltti (panel header/src
  davranışı DEĞİŞMEDİ) → public API yüzeyi sabit, ABI sabit, hello_engine render
  yolu etkilenmez. Chrome golden BYTE-IDENTICAL (fixture #5 cmp baseline).

---

## 1. Bağlam

BAND 4 (60–69%) cd::editor — BAND-4'ün en büyük tek yüzeyi (~15.8k src / ~10k
test / 497 case) — 100%'e taşınır. cd::editor mimarisi:

- **Shell** (`cd::editor`, `src/Editor.cpp` + `include/cd/editor/`): root widget
  panel + üç-pencere layout (scene-tree solda, inspector sağda, gizmo overlay) +
  `ecs::World` + `cd::scene::Scene` + `cd::input::InputContext` + retained-mode
  widget tree wiring; `select()` / `dispatch_input_()` / `tick()` → DrawCommand
  toplama. Çevre tipler: HierarchyView, PropertyDrawer, AxisGizmo,
  SelectionSet/Outline, EditHistory, TransformCommands, CommandPalette, MenuBar,
  Bookmark, PreferencesStore.
- **editor_ui** (`cd::editor_ui`, `src/ui/EditorWidgets.cpp`): SceneTreeView /
  PropertyInspector / TransformGizmo editor-spesifik widget'ları (renderer-
  agnostic widget tree + DrawBatcher draw path).
- **27 panel** (her biri ayrı STATIC lib `panel_*`): inspector, console,
  asset_browser, viewport, material_editor, animator, behavior_designer,
  asset_drop_target, cutscene_player, input_recorder, light_editor,
  dialog_tree_editor, vehicle_editor, pathfinding_viz, material_preview,
  debug_viz, keyboard_shortcut_overlay, scene_navigator, settings, build,
  perf_profiler, asset_pipeline_status, ik_chain_editor, scene_palette,
  lobby_browser, auto_save_indicator, status_bar_fps_sparkline. Her panel gerçek
  bir engine alt-sistemine BAĞLANIR (LightEditor → lighting_clusters::PointLight +
  ClusterGrid; VehicleEditor → physics::vehicle::VehicleConfig/State;
  DialogTreeEditor → game::dialog_tree::DialogTree; CutscenePlayerPanel →
  game::cutscene_player; PathfindingViz → ai::pathfinding::NavMesh; vs.) ve gerçek
  STATE/COMMAND/BINDING/HIT-TEST logic'i taşır + dedicated gtest binary'si var.
- **cdproj** (`cd::editor_cdproj`): `.cdproj` proje-dosyası JSON read/write.

Roadmap honest-rule'una göre bir modül 100%'tür ancak HER dokümante boşluk iki
terminal durumdan birindeyse: (a) IMPLEMENTED + test (küçük/net), VEYA (b) bir
paragraflık kapsam-ADR'ı ile formally SEALED (promote-on-need tetikleyicisiyle).
"TODO/placeholder" üçüncü durumu kalamaz.

**editor'ün named gap'inin KARAKTERİ (kritik ayrım)**: roadmap'in "real in-panel
text/glyph + ImGui-ize the 27 panels + single editor.exe entry" maddesi bir
**CORRECTNESS gap'i DEĞİL, bir VISUAL-FIDELITY / UX-COMPLETENESS gap'idir**.
Editor FONKSİYONELDİR:

- shell gerçek `ecs::World` + scene + input + widget tree bağlar (Editor.cpp),
- 27 panel gerçek engine alt-sistemlerine bağlanır + gerçek logic taşır
  (set/get/select/hit-test/state-machine/binding),
- 497 case (35 gtest dosyası) bu logic'i exhaustively test eder.

Eksik olan, panellerin gerçek bir DCC gibi GÖRÜNMESİDİR: panellerin çoğu metni
"accent bar"/strip/colour-coded quad placeholder olarak çizer (DrawBatcher
quad'ları; in-panel glyph YOK) — örn. DialogTreeEditor.cpp'deki "Dim inner label
strip (simulates a text placeholder without requiring a font atlas)". Yalnız
`panel_inspector` gerçek bir ImGui draw path'i taşır (`Inspector::draw_imgui`
gerçek `imgui.h` ile). Bu, "implemented-but-visually-simplified"tır,
"unimplemented-skeleton" DEĞİL.

Bu yüzden editor'ün dürüst terminal durumu **REWRITE DEĞİL, SEAL'dir**: editor-v1
(functional shell + 27 real-binding panel + 497 test) charter-complete bir v1
KATMAN; "real DCC text/glyph + ImGui-ize-all-27-panels + standalone editor.exe"
onun ÜSTÜNDE bir multi-week ayrı sub-project'tir (her biri B3-mühürlü temellere —
ui_font shaping + ui umbrella glyph-layout — ve Phase-2 binary tasarımına bağlı).
Bu pass'in bölünme çizgisi: **editor-v1'i SEAL et + gerçekten test-edilmemiş
panel-LOGIC dallarını (görsel DEĞİL) IMPLEMENTED(test) yap + text/glyph + binary
genişlemesini precise trigger ile promote-on-need ayır + 1 yanıltıcı README
banner'ını düzelt.** 27 panele gerçek DCC text/glyph implement etmeyi DENEME
(kapsam dışı + multi-week + B3-mühürlü bağımlılık).

---

## 2. Karar

### 2.1 cd::editor (editor-v1: shell + editor_ui + 27 panel + cdproj) — 68 → 100  [editor-v1 SEALED; panel-logic IMPLEMENTED(test)]

- **Bağlam**: yukarıda §1. Üç katman: functional shell (Editor.cpp + widget
  tree), 27 real-binding panel (her biri ayrı lib + gtest), cdproj. 497 case.
  Named gap (roadmap): "real in-panel text/glyph + ImGui-ize the 27 panels
  (placeholder visuals today); single editor.exe entry."
- **Karar (editor-v1 SEALED — three sub-decisions)**:
  - **(A) Placeholder-visuals + no-in-panel-glyph = SEALED editor-v1**
    (promote-on-need). Editor şu an FONKSİYONELDİR (shell binds + 27 panel
    real-binding + 497 test); eksik olan VISUAL-FIDELITY'dir (panellerin çoğu
    DrawBatcher accent-bar/strip/colour-quad placeholder çizer; in-panel glyph
    yok; yalnız Inspector ImGui-ized). Gerçek DCC text/glyph her panele
    eklemek + 27 paneli ImGui-ize etmek bir **multi-week ayrı sub-project**'tir
    ve İKİ DOWNSTREAM BAĞIMLILIĞI da B3'te SEALED'dir:
    (1) **ui_font shaping** — default build identity shaper (B3 §2.2 SEALED,
        gerçek BiDi/ligature/contextual-substitution opt-in promote-on-need),
    (2) **ui umbrella glyph LAYOUT** — text bir bare DrawKind::kText komutu
        (B3 §2.4 SEALED, glyph quad'lara genişletme = consumer-integration işi,
        widget-tree'nin değil).
    Editor'e gerçek text/glyph eklemek, BU İKİ B3-mühürlü genişlemenin önce
    promote edilmesini ister (ui_font default-on shaping + bir ui_text_layout
    katmanı). Dolayısıyla editor'ün text/glyph gap'i, kendi başına değil, B3
    promote-on-need zincirinin DOWNSTREAM TÜKETİCİSİ olarak çözülür. honest-rule
    (b) promote-on-need tam bunun için: editor-v1 (functional + tested)
    charter-complete, görsel genişleme ayrı sub-project.
  - **(B) Single editor.exe entry = DOCUMENTED-deferred (no thin stub)**.
    Standalone `cd_editor_app` binary'si ZATEN TAM TASARLANMIŞ:
    `ADR-20260530-editor-binary.md` `apps/editor/` topolojisini + `.cdproj`
    parser + asset browser + drag-drop glTF flow + PIE controller +
    snapshot/restore + hot-reload bus + Lua plugin host + cross-platform native
    dialog'u çiziyor (Phase-2, ~6 KLOC + ~1.2 KLOC test, ~6.5 hafta
    tek-developer). Brief'in "if adding a thin entry is small+clean, do it; else
    document" dalı uygulandı: **thin entry small+clean DEĞİL** — anlamlı bir
    editor.exe `.cdproj` parser + dock layout serialize + RHI window + native
    file-dialog + asset-browser routing ister (binary kütüphaneleri bilir ama
    `apps/editor/` yok, `chroma::editor::app` namespace boş); yarım bir
    `int main(){}` stub'ı dead-code/yanıltıcı olur (kullanıcı çift-tıklar, hiçbir
    şey açılmaz) ve SCOPE EXCLUSION'a girer (`apps/`, `samples/` bu pass'te
    DIŞI; bu pass YALNIZ engine/ui/editor/ + docs/ADR/). → **DOCUMENT**: binary
    designed-but-deferred (ADR-20260530), README'de işaretlendi (§2.3).
  - **(C) Gerçekten test-edilmemiş panel-LOGIC dalları = IMPLEMENTED(test)**
    (görsel DEĞİL, +5 test, 497 → 502). Brief: "topup REAL untested
    panel-LOGIC branches if any exist (panel state/command/binding logic that
    lacks a test — NOT visuals)". 27 panel exhaustively test edilmiş; ama 3
    panelde gerçek dokunulmamış LOGIC dalı bulundu (aşağıda).
- **Eklenen panel-logic edge testler (gerçek test-edilmemiş LOGIC dalı, görsel
  değil)**:
  - **panel_settings** (7 → 8): `DuplicateKey_FirstMatchWins` — header'ın
    dökümante "Duplicate keys are allowed (last write wins in set_value; first
    match wins in get_value)" SÖZLEŞMESİ. Tüm prior fixture'lar UNIQUE key
    kullandığından `set_value`'nun first-match-early-return'ü + `get_value`'nun
    first-match-return'ü gerçek bir DUPLICATE'e karşı hiç koşmamıştı. İki "vsync"
    entry'si: get_value first'i döner, set_value yalnız first'i günceller,
    entry_count değişmez.
  - **panel_dialog_tree_editor** (7 → 9):
    `SetTreeClearsPriorSelectionAndRects` — `set_tree()` selected_id_ + cached
    node_rects_'i resetler; hiçbir prior test select-sonrası rebind etmediğinden
    clear-on-rebind dalı (+ stale-click eski rect'e re-select EDEMEZ, fresh
    draw() öncesi) kapsanmamıştı. `OrphanNodeUnreachableFromRootStillRenders` —
    BFS'in "Nodes not reachable from root get depth 0 (they still render)"
    fallback dalı: hiçbir next_ids'te referans edilmeyen + root olmayan bir orphan
    node depth-0'a düşer + rect alır (selectable). Tüm prior fixture'lar fully
    reachable'dı → fallback dalı test-edilmemişti.
  - **panel_vehicle_editor** (7 → 9):
    `SimulateClickApplyInvalidBoundsDoesNotArm` — `simulate_click_apply`
    `bounds.is_valid()` guard'ı: invalid (zero-size) bounds apply_pending_'i
    ARM ETMEMELİ. Tüm prior test standard_bounds() geçtiğinden guard'ın
    false dalı koşmamıştı. `ApplyFlagCanRearmAfterConsume` —
    click→pending→consume→click re-arm döngüsü: consume_apply() sonrası fresh
    valid click flag'i yeniden arm eder (re-arm-after-consume yolu test-
    edilmemişti; test 5 consume'da bitiyordu).
- **Gerekçe**: editor'ün gerçek değeri functional shell + 27 real-binding panel +
  497-test logic katmanıdır ve o charter-complete + deep-tested. Named gap'in
  "real in-panel text/glyph + ImGui-ize-all-panels" yarısı bir VISUAL-FIDELITY /
  UX-completeness genişlemesidir (correctness gap'i değil) ve İKİ B3-mühürlü
  bağımlılığa (ui_font shaping + ui umbrella glyph layout) yaslanır — multi-week
  ayrı sub-project. "single editor.exe entry" yarısı ZATEN tam tasarlanmış bir
  Phase-2 binary'dir (ADR-20260530, ~6.5 hafta); yarım stub small+clean değil +
  scope dışı. honest-rule (b) promote-on-need tam bunun için. Panel-logic edge
  testleri (duplicate-key/set-tree-clear/orphan-BFS/invalid-bounds-guard/
  re-arm) küçük+net+deterministik — honest-rule (a); panel state/command/binding
  invariant'ları artık fail-on-revert kilitli. Bu testler GÖRSELİ test etmez
  (DrawBatcher vertex sayısı yalnız "orphan participates in layout" anchor'ında
  ikincil); LOGIC dallarını sınar.
- **Promote-on-need**:
  - **text/glyph + ImGui-ize 27 panel**: önce B3'ün iki promote tetikleyicisi
    (ui_font default-on HarfBuzz/FreeType shaping + bir ui_text_layout /
    ui_renderer glyph-layout katmanı) → sonra editor panellerini DrawBatcher
    placeholder'dan gerçek text/glyph + ImGui draw path'e taşıma; her dalga ayrı
    ADR (gerçek bir DCC-quality editor UI ihtiyacıyla). Panel STATE/COMMAND/
    BINDING/hit-test API yüzeyi (set_*/get_*/selected_*/simulate_click/draw imza)
    geriye-uyumlu kalır — yalnız draw() gövdesi placeholder→glyph evrilir.
  - **single editor.exe**: `ADR-20260530-editor-binary.md` Phase-2 deliverable
    şeması (apps/editor/ skeleton + .cdproj parser + asset browser + inspector
    drawer registry + PIE + hot-reload bus + Lua plugin host + native dialog +
    installer) — gerçek bir shippable-editor ihtiyacıyla + ayrı PR/ADR'larla;
    binary kütüphaneleri tüketir, kütüphane API'si değişmez (DAG: editor→libs).

### 2.2 cdproj + editor_ui + shell — charter-complete, test-deep, dokunulmadı

cdproj (`.cdproj` JSON round-trip, 12 test), editor_ui (SceneTreeView/
PropertyInspector/TransformGizmo, 13 test), shell (Editor.cpp + axis_gizmo_v2 13 +
composite_presets 11 + viewport_gizmo_active_axis 3 + editor 40) hepsi
charter-complete + test-dolu; bu pass header/src davranışlarına DOKUNMADI
(yalnız 3 panel test dosyası + 1 README). Inspector'ın gerçek ImGui draw path'i
(`draw_imgui`) editor'ün "ImGui-ize" yolunun zaten-çalışan PROOF-OF-PATTERN'ıdır
(diğer 26 panel aynı patterni promote-on-need izleyecek).

### 2.3 False-banner-fix (README)

`engine/ui/editor/README.md` Notes bölümü "Editor UI lives in cd::editor_ui
(ImGui panels, property widget library)" diyordu — bu YANILTICI: editor_ui ImGui
DEĞİL renderer-agnostic widget-tree + DrawBatcher kullanır; 27 panelden YALNIZ
Inspector'ın gerçek ImGui path'i var. Düzeltildi (brief: "Correct any false
banner"): (1) editor_ui'nin DrawBatcher/widget-tree doğası + "yalnız
panel_inspector gerçek ImGui, kalanı placeholder DrawBatcher visuals, no in-panel
glyph" + bu ADR'a seal referansı; (2) "no single editor.exe entry yet; standalone
binary ADR-20260530'da designed + deferred" notu eklendi. Banner artık gerçek
durumu (functional-but-placeholder-visuals + designed-deferred-binary) doğru
tanıtır.

---

## 3. Reddedilen alternatifler

- **27 panele gerçek DCC text/glyph implement etmek + hepsini ImGui-ize etmek**:
  multi-week ayrı sub-project; İKİ DOWNSTREAM bağımlılığı (ui_font default-on
  shaping + ui umbrella glyph LAYOUT) B3'te SEALED (promote-on-need) — editor'ün
  text'i bu iki genişleme promote edilmeden gerçek olamaz (text-as-DrawKind
  mimari sözleşmesi: glyph layout = consumer-integration işi). RED + kapsam dışı
  (brief "Do NOT attempt to implement real DCC text/glyph across 27 panels") —
  editor-v1 SEALED, downstream-on-B3 promote-on-need.
- **Yarım bir editor.exe (`apps/editor/Main.cpp` thin stub) eklemek**: anlamlı
  bir binary `.cdproj` parser + dock layout + RHI window + native file-dialog +
  asset-browser routing ister (ADR-20260530 Phase-2 ~6.5 hafta); `int main(){}`
  stub'ı dead-code/yanıltıcı (çift-tıkla → hiçbir şey açılmaz) + SCOPE EXCLUSION
  (`apps/` bu pass'te DIŞI). RED — brief'in "else document" dalı: binary
  designed-but-deferred (ADR-20260530), README'de işaretlendi.
- **497 case'in charter-complete panel yüzeyini pad etmek**: 27 panel
  exhaustively test edilmiş; yapay test "topup REAL untested branches … NOT
  visuals" kuralına aykırı (don't pad). RED — yalnız 3 panelde gerçek
  dokunulmamış LOGIC dalı (duplicate-key contract / set-tree-clear /
  orphan-BFS-fallback / invalid-bounds-guard / re-arm-after-consume) hedeflendi
  (+5 test).
- **DrawBatcher placeholder görsellerini "test" diye sınamak (vertex-count-only
  assert pad)**: brief açıkça "NOT visuals". Eklenen 5 test panel LOGIC'ini
  (state/command/binding) sınar; vertex-count yalnız "orphan participates in
  layout" anchor'ında ikincildir. RED — logic-only topup.
- **Herhangi bir panel header/src davranışını değiştirmek**: bu pass yalnız
  test-deepening + 1 README banner-fix + seal yaptı (impl davranışı sabit →
  API/ABI sabit, golden byte-identical garantili). RED — panel header/src
  DOKUNULMADI.
- **Kapsam-dışı ui lib'lerini (font/a11y/umbrella B3; layout/animation/input/
  theme/renderer/renderer_rhi/widgets B2; renderer_webgpu B7) / samples / hello_*
  / apps / % docs'u düzenlemek**: kapsam DIŞI (brief SCOPE EXCLUSION: ONLY
  engine/ui/editor/ + docs/ADR/). RED.

## 4. Sonuçlar

- (+) cd::editor honest-rule terminal durumuna geçti: **editor-v1 SEALED**
  (functional shell + 27 real-binding panel + cdproj + editor_ui, 497→502 test).
  Hiçbir katmanda placeholder/TODO-state YOK — DrawBatcher placeholder visuals
  KASTEN bir v1 görsel tasarım kararıdır (no-glyph, no-font-atlas), açık
  promote-on-need tetikleyicili (B3 ui_font shaping + ui umbrella glyph layout
  downstream + ADR-20260530 binary).
- (+) **editor.exe-entry kararı verildi (DOCUMENTED-deferred)**: standalone
  binary ZATEN tam tasarlanmış (ADR-20260530, Phase-2 ~6.5 hafta); thin stub
  small+clean değil + scope dışı → DOCUMENT (README'de işaretlendi). honest-rule
  (b): designed-but-deferred-by-design, açık promote yolu (apps/editor/ Phase-2).
- (+) +5 panel-logic edge test (settings +1, dialog_tree_editor +2,
  vehicle_editor +2), gerçek test-edilmemiş LOGIC dalında (görsel DEĞİL):
  duplicate-key first-match contract, set-tree-clears-selection+rects,
  orphan-node-BFS-depth-0-fallback, invalid-bounds-apply-guard,
  re-arm-after-consume. Hepsi edge/contract + fail-on-revert; anti-flakiness
  korundu (deterministik fixture'lar, sleep_for YOK). cd_test_editor_panel_settings
  7→8, dialog_tree_editor 7→9, vehicle_editor 7→9.
- (+) 1 README false-banner düzeltildi: editor_ui DrawBatcher/widget-tree (ImGui
  değil) + yalnız Inspector ImGui-ized + no-in-panel-glyph + no-standalone-binary
  (ADR-20260530 designed-deferred) → banner artık functional-but-placeholder-
  visuals + designed-deferred-binary gerçeğini doğru tanıtır.
- (+) Chrome golden BYTE-IDENTICAL (fixture #5, baseline ile cmp eşit). Editor'de
  SADECE 3 panel test dosyası + 1 README + bu ADR değişti (panel header/src
  davranışı aynı; hello_engine'e dokunulmadı) → render-yolu bayt-aynı. Build
  -Werror temiz, 0 yeni clang-tidy WAE class (eklenen test kodu: emplace_back
  yok-gereği/push_back yok, using-namespace yok, unused-using yok,
  explicit-widening cast'lar size-math/uint8'de, single-decl izole).
- (−) Mühür editor'ün gerçek DCC text/glyph'ini + 27 panelin ImGui-ize'ini +
  standalone editor.exe'sini bu pass'te ÜRETMEZ; üçü de promote-on-need
  (text/glyph B3 ui_font+umbrella zincirine, binary ADR-20260530 Phase-2'ye
  bağlı) + ayrı ADR/PR'larla gelir. Kabul: BAND 4 editor "seal-functional-v1 +
  lock-untested-panel-logic + document-deferred-binary + banner-fix"
  karakterinde — asıl boşluk VISUAL-FIDELITY / UX-completeness (downstream-on-B3
  + Phase-2-binary), unimplemented-skeleton DEĞİL (shell + 27 panel + 502 test
  gerçek + bağlı).

---

## Varsayımlar

- Brief'in "100% RULE per lib" + "'100%' RULE — this is a LARGE lib; the honest
  terminal state is SEAL not rewrite" yorumu: editor FONKSİYONELDİR (shell + 27
  real-binding panel + 497 test); gap VISUAL-FIDELITY'dir (placeholder DrawBatcher
  visuals + no real in-panel glyph; only Inspector ImGui-ized) = UX-completeness
  ENHANCEMENT, correctness gap DEĞİL → editor-v1 SEALED + downstream-on-B3-sealed
  (ui_font shaping + ui umbrella glyph layout) promote-on-need. Band3-ui-aisquad
  + band4-render-features ADR ile aynı bölünme çizgisi.
- editor.exe-entry kararı (brief "if adding a thin entry is small+clean, do it;
  else document"): thin entry small+clean DEĞİL (anlamlı binary = .cdproj parser
  + dock layout + RHI window + native dialog + asset routing, ADR-20260530 ~6.5
  hafta; yarım stub dead-code + apps/ scope dışı) → **DOCUMENT** (ADR-20260530
  designed-deferred, README'de işaretlendi).
- panel-logic-topup (brief "topup REAL untested panel-LOGIC branches … NOT
  visuals"): 27 panel exhaustively test edilmiş; 3 panelde gerçek dokunulmamış
  LOGIC dalı bulundu (settings duplicate-key contract, dialog_tree_editor
  set-tree-clear + orphan-BFS-fallback, vehicle_editor invalid-bounds-guard +
  re-arm) → +5 test (497→502), görsel DEĞİL.
- false-banner (brief "Correct any false banner"): README "editor_ui (ImGui
  panels)" YANILTICI (editor_ui DrawBatcher/widget-tree; yalnız Inspector
  ImGui-ized) → düzeltildi + no-glyph + no-standalone-binary (ADR-20260530)
  notu eklendi.
- editor test target'ları: shell `cd_test_editor` (40) + editor_ui
  `cd_test_editor_ui` (13) + axis_gizmo_v2/composite_presets/
  viewport_gizmo_active_axis/cutscene_pan_zoom + 27 panel
  `cd_test_editor_panel_*` + `cd_test_editor_cdproj`. Hepsi `ctest -R "editor"
  -E "rhi_vulkan"` ile (+ 4 shell test ayrı isimle) PASS doğrulandı; cd_test_ui_
  curve_editor / hello_editor_bridge_contract / input_recorder_editor_binding
  filtreye takılır ama kapsam dışı lib'ler (yalnız regex "editor" eşleşmesi).
- "scope exclusion" kuralı uygulandı: tüm değişiklikler engine/ui/editor/
  (3 panel test dosyası: panel_settings/tests, panel_dialog_tree_editor/tests,
  panel_vehicle_editor/tests + 1 README) + bu docs/ADR/ dosyası altında. Diğer
  ui lib'leri + samples/ + hello_* + apps/ + % docs DOKUNULMADI. Panel header/
  src/CMakeLists DOKUNULMADI (impl davranış değişmedi, yeni test mevcut TU'lara
  eklendi).
- Golden byte-identical: bu pass panel header/src davranışını değiştirmedi
  (yalnız 3 test dosyası + 1 README) → hello_engine render yolu hiç etkilenmez;
  fixture #5 capture baseline (research/reports/parity1121/baseline.png) ile
  bayt-bayt eşit doğrulandı (b4e.png cmp → identical, sonra silindi).

## Sonraki

- **editor text/glyph + ImGui-ize 27 panel** (promote-on-need, downstream-on-B3):
  önce ui_font default-on shaping (B3 §2.2 trigger) + ui umbrella glyph LAYOUT
  katmanı (B3 §2.4 trigger: ui_text_layout / ui_renderer entegrasyonu) → sonra
  panelleri DrawBatcher placeholder'dan gerçek text/glyph + ImGui draw path'e
  taşı (Inspector::draw_imgui patterni 26 panele yayılır); her dalga ayrı ADR.
- **single editor.exe** (promote-on-need): ADR-20260530-editor-binary Phase-2
  deliverable (apps/editor/ skeleton + .cdproj parser + asset browser + drawer
  registry + PIE + hot-reload bus + Lua plugin host + native dialog + installer);
  binary kütüphaneleri tüketir, lib API değişmez.
- **BAND 4 kalan**: cd::platform / cd::particle_system / cd::asset::* (shader_cache
  / vfx_authoring / streamer_pool / validator) / cd::imgui_backend (tooling) — ayrı
  pass'ler; bu seal şablonunu (seal-functional-v1 + lock-untested-logic +
  document-deferred + banner-fix) tekrar kullanabilirler.
