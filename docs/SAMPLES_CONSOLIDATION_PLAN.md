# Samples Konsolidasyon Planı — 38 → ≤10 (phase1139)

> Run 24 (106→38) devamı. Kullanıcının duran direktifi
> [feedback_sample_consolidation_and_per_feature_demo]: yeni özellikler
> hello_engine içinde panel/log olarak demolanır; ayrı binary açılmaz.
> Hedef ≤10 sample binary (apps/editor + apps/web_demo ayrı sayılır).

## Hedef küme (10 binary)

| # | Binary | Rol |
|---|--------|-----|
| 1 | engine/hello_engine | Flagship mega-showcase (tüm görsel demolar) |
| 2 | editor/hello_editor | Editör/ürün yüzeyi (.cdproject, panel ailesi) |
| 3 | rhi/hello_triangle | Minimal Vulkan bring-up (kanonik ilk üçgen) |
| 4 | render/hello_d3d12_pbr | D3D12 parite amiral gemisi (X4-F hedefi) |
| 5 | rhi/hello_metal | L1 Metal placeholder (macOS kapısı) |
| 6 | rhi/hello_opengl | GL bring-up (boot+resources+triangle BİRLEŞİK) |
| 7 | rhi/hello_path_trace | RT/path-trace bağımsız vitrin |
| 8 | asset/hello_asset_pipeline | Cooked/import akışı (6 asset örneği BİRLEŞİK) |
| 9 | world/bench_archetype | ECS perf aracı (kategori c: tool) |
| 10 | foundation/hello_stress | 24-saat CI stres aracı (kategori c: tool, Run 24 kararı) |

## Silme/katlama dalgaları

- **Batch 1 (bu commit — porting YOK, tamamen kapsanıyor):**
  - `audio/hello_audio_wasapi` (102L) → hello_engine Audio paneli + WASAPI backend zaten canlı
  - `render/hello_imgui` (230L) → ImGui hello_engine + hello_editor'da her yerde
  - `render/hello_gpu_cluster` (165L) → hello_engine cluster demo + 3D heat-map overlay
  - `rhi/hello_rhi_features` (88L) → feature-bit sorgusu test_backend_parity + engine log'unda
  - `rhi/hello_opengl_boot` (47L) → hello_opengl_triangle aynı boot'u zaten yapıyor
- **Batch 2 (görsel kapsam doğrulaması sonrası):** hello_skybox,
  hello_pbr, hello_framegraph, hello_render_thread, hello_cube,
  hello_rt (engine sky/PBR grid/framegraph/X1 submit/RT yansıma
  yollarıyla bire bir karşılık; her silmeden önce ekran kapsamı
  kontrol listesi).
  - phase1144: `hello_framegraph` silindi — NEEDS-PORT şartı
    karşılandı; gerçek-cihaz kanıtı phase1143'te
    `engine/render/framegraph/tests/test_framegraph_vulkan.cpp`
    olarak gemiye girdi.
- **Batch 3 (asset birleştirme):** hello_mesh/obj/texture/cooked/
  textured_cooked/gltf → tek `hello_asset_pipeline` (cooked akışı
  taşınır; glTF görsel yolu hello_engine'de zaten Sponza/CesiumMan).
  **phase1150 DONE** — 6 binary → 1; git rm'd; CMakeLists + Readme + scripts güncellendi.
- **OpenGL birleşik:** hello_opengl_resources + hello_opengl_triangle →
  tek `hello_opengl`. **phase1148 DONE** — 2 binary → 1.
- **Batch 4 (endgame audit, 2026-06-13 — analyst kanıt-tabanlı):**
  - **Batch-4a phase1152 DONE (SAFE-DELETE, 4 binary, 21→17):**
    `world/hello_inspector` (hello_editor panel_inspector+HierarchyView+
    save/load host), `world/hello_scene_graph` (hello_engine Scene
    Serializer + ECS stress probes + scene unit tests 9),
    `rhi/hello_d3d12_clear` ve `rhi/hello_d3d12_triangle`
    (hello_d3d12_pbr Win32+device+swapchain+clear + PSO+draw+HLSL yolunu
    taşıyor; D3D12_PARITY_AUDIT).
  - **Batch-4b phase1156 DONE (FOLD, 2 binary, 17→15): hello_material_editor +
    hello_animator folded into hello_editor as ImGui-native panels (Seçenek B,
    ADR-20260613). State objects g_mat_editor / g_animator; DrawBatcher path
    NOT used in hello_editor — pure ImGui ColorEdit3/SliderFloat/Checkbox/ListBox.
    git rm both sample dirs. hello_behavior_designer DEFERRED per ADR-20260613
    (node-graph layout reimplementation ~200L extra, not justified for sample count).**
  - **NEEDS-PORT (5 binary, ODAKLI GELECEK SESSION — gerçek entegrasyon,
    "ince sarmalayıcı" DEĞİL):**

    | Sample | Taşınacak | Nereye | Efor | Kapı | Durum |
    | --- | --- | --- | --- | --- | --- |
    | hello_animator | panel_animator + Animator::draw() | hello_editor | ~30L+dep | UI-stack uyumu | **DONE phase1156** |
    | hello_material_editor | panel_material_editor | hello_editor | ~30L+dep | UI-stack uyumu | **DONE phase1156** |
    | hello_behavior_designer | panel_behavior_designer | hello_editor | ~200L | node-graph layout | DEFERRED — ADR-20260613 |
    | hello_hot_reload | FileWatcher+CachedCompiler+material swap | hello_editor/engine toggle | ~100L | FileWatcher threading; ADR-20260522 canonical | **DONE phase1171 (SAFE-DELETE — coverage in HelloShaderWatch.hpp)** |
    | hello_anim | AnimationClip+Player+GoldenCapture | hello_engine showcase | ~200L | CI run_smoke `hello_anim` adını taşıyor — script+golden güncelle | **DONE phase1172 (SAFE-DELETE — capability superset HelloSkinnedAnim; golden redundant, cd::anim 55 unit tests in test_anim.cpp + test_dual_quat.cpp)** |
    | hello_ui | ui_layout+ui_widgets+ui_renderer_rhi::Submitter | hello_editor/engine | orta-yüksek | architect kapısı: kSubmitterPipelineReady gate | PENDING |
    | hello_world | cd::game::{camera,trigger,particles_event,query} | hello_engine Gameplay panel | ~400L | architect kapısı: 4 gameplay-lib dep | PENDING |

  - Architect onayı NEEDS-PORT öncesi gerekli (hello_ui submitter gate +
    hello_world gameplay-lib dep). ≤10 hedefi kalan 5 port +
    (gerekirse) ek d3d12 katlamasıyla kapanır.

## Kurallar

- Her batch ayrı commit; build + 267 ctest + chrome_probe golden her
  checkpoint'te. Silinen örneğin kapsama kanıtı commit mesajında.
- `git rm` ile geçmiş korunur; geri dönüş gerekirse tag'siz geçmişten.
- ctest hedefleri sample'lara bağımlı DEĞİL (golden CLI yalnız
  hello_engine) — yine de her batch öncesi grep ile doğrulanır.

## Feasibility Re-Assessment (phase1170, a68fe1f)

> architect kanıt-tabanlı yeniden değerlendirme. Mevcut: 15 binary, hedef ≤10
> (kalan 5 NEEDS-PORT). Her iddia bir `Grep`/`#include`/CMake okuması ile
> doğrulandı (CLAUDE.md §3). Kod/build/ctest YOK — yalnız tasarım kontratı.

### Özet tablo

| Sample | Sınıflandırma | Somut kanıt | Eksik ön-koşul / gerekçe |
| --- | --- | --- | --- |
| **hello_hot_reload** | **DOABLE-NOW** | FileWatcher = polling (tek-thread); main.cpp'de `std::thread`/`detach` YOK; tüm dep'ler (`cd::material/shader/render/rhi_vulkan`) hello_engine'de zaten var | — |
| **hello_anim** | **DOABLE-NOW (script/golden re-home şart)** | `cd::anim` dep'i PUBLIC_DEPS = yalnız `cd::math + cd::core` (saf-yukarı DAG); ama hello_anim 3 yerde hard-wired golden producer | golden capture'ı hello_engine golden CLI'ya re-home + 3 referans edit (kod değil config) |
| **hello_ui** | **GATED** | `samples/ui/hello_ui/main.cpp:577` `constexpr bool kSubmitterPipelineReady = false` HÂLÂ false; Submitter pipeline bağlamıyor | cd::material UI variant'ları (Submitter VS/FS link + descriptor seti) — ADR-20260530 Phase 1.5 |
| **hello_world** | **DOABLE-NOW (DAG temiz, panel scope orta)** | 4 gameplay-lib + `cd::gameplay_time` + `cd::anim`(yok) hello_engine'de DEĞİL; ama hepsi saf-yukarı (game_query→cd::scene, game_camera→cd::camera/ecs — tümü hello_engine closure'unda); cycle YOK | yalnız ~400L gameplay panel + 5 yeni DEP edit (mimari blok YOK) |
| **hello_behavior_designer** | **DEFER** | ADR-20260613: node-graph layout (`measure_subtree`/`draw_real_tree`) ~200L ImGui DrawList reimpl + `cd::game::ai_bt::BehaviorTree` yeni hello_editor dep | sample-azaltma için orantısız; DrawBatcher→ImGui köprü maliyeti |

### Kanıt detayı (her satır)

#### hello_hot_reload → DOABLE-NOW

- `samples/editor/hello_hot_reload/CMakeLists.txt:3`: DEP'ler
  `cd::platform cd::rhi cd::rhi_vulkan cd::shader cd::material cd::render
  cd::core cd::sample_common` — hepsi hello_engine DEPS'inde mevcut.
- **Threading riski (ADR-20260522 + FileWatcher.hpp incelemesi)**:
  `cd::shader::FileWatcher` **polling** tabanlı, açıkça "does not use
  ReadDirectoryChangesW / inotify / FSEvents ... no per-OS background
  threads ... correct by construction" (FileWatcher.hpp:5-14). Sample loop
  içinde `w.poll()` çağrılır; arka-plan thread YOK. `main.cpp` grep'i:
  `std::thread`/`detach`/`jthread` **SIFIR** match (`<thread>` include
  yalnız `sleep_for` için). **Threading riski yönetilebilir değil —
  YOK.**
- Fold hedefi: hello_engine'e bir "Hot Reload" panel-toggle'ı + on-disk
  shader edit→FileWatcher.poll()→Material rebuild yolu. hello_engine ZATEN
  `HELLO_ENGINE_USE_ON_DISK_SHADERS` + POST_BUILD shader-stage'e sahip
  (CMakeLists:3-5, 82-86) — FileWatcher entegrasyonu mevcut altyapıya
  oturur. GPU-golden ile doğrulanabilir (shader-edit öncesi/sonrası).
- **Uyarı**: [feedback_ondisk_shader_golden_staleness] — fold sonrası
  golden öncesi SOURCE==EXE-SIDE force-sync zorunlu.

#### hello_anim → DOABLE-NOW (golden/script re-home prerequisite)

- `cd::anim` PUBLIC_DEPS = `cd::math cd::core` (anim/CMakeLists.txt:4) —
  en temiz saf-yukarı DAG; cycle imkânsız.
- **CI/golden bağımlılığı (KRİTİK — fold öncesi çözülmeli)**: hello_anim,
  `hello_triangle` ile birlikte sadece **2 wired golden producer'dan
  biri**:
  - `scripts/run_golden.sh:61` `SAMPLES=( hello_triangle hello_anim )`
  - `scripts/run_golden.ps1:53` `'hello_anim'`
  - `.github/workflows/ci-amd-radv.yml:93`
    `bash scripts/run_smoke_samples.sh hello_triangle hello_anim`
    (NOT: bu script `scripts/run_smoke_samples.sh` repo'da YOK —
    phase1142 pre-existing defect; lane zaten kırık).
- hello_anim `main.cpp`: `cd::sample::schedule_golden_capture` +
  `finish_golden_capture` (GoldenCapture.hpp) kullanıyor — canlı golden
  üretici. hello_engine ZATEN GoldenCapture.hpp + golden CLI'ya sahip
  (HelloGoldenCli.hpp, test_hello_engine_golden_cli.cpp).
- **Yol**: AnimationClip+Player demosu hello_engine showcase paneline
  taşınır; golden capture hello_engine'in mevcut golden CLI'sına re-home
  edilir; 3 script/CI referansı `hello_anim`→hello_engine golden hedefine
  güncellenir (KOD değil, config edit). Bu yapılmadan fold golden
  pipeline'ı yetimleştirir. Re-home sonrası DOABLE.

#### hello_ui → GATED (kSubmitterPipelineReady HÂLÂ kapalı)

- **Gate GERÇEKTEN kapalı**: `samples/ui/hello_ui/main.cpp:577`
  `constexpr bool kSubmitterPipelineReady = false;` — HÂLÂ false.
  Satır 566-576 yorumu: "Submitter does NOT bind a graphics pipeline
  today ... Issuing draw_indexed without a bound pipeline causes
  vkQueueSubmit2 to fail validation, so we gate the record call."
- `cd::ui::renderer::Submitter` var ve hello_ui'de instantiate ediliyor,
  ama `submitter.record(cmd, frame.extent)` gate ardında ölü. NullDevice
  (CI smoke) no-op kabul ediyor; gerçek-donanım DRAW-LIVE kapalı.
- Eksik ön-koşul (docs/MARATHON_PLAN_NEXT.md:53-54 + AUDIT/
  editor-black-screen-2026-05-31.md): **cd::material UI variant'larının
  shipped olması** → Submitter VS/FS link + descriptor seti → ardından
  gate `apps/editor` + `samples/ui/hello_ui` İKİSİNDE de flip edilir.
  ADR-20260530 Phase 1.5 işi, ~1-2 hafta.
- **NOT**: ADR-20260613 Kanıt 3, folded panellerin (animator/material_editor)
  `kSubmitterPipelineReady`'den ETKİLENMEDİĞİNİ gösterdi — onlar Route B
  `create_with_inline_shader` kullanıyordu. Ama hello_ui'nin KENDİSİ
  Route A Submitter'ı gate ardında; fold edilebilmesi için gate'in
  gerçekten açılması (material UI variant'ı) gerek. **GPU gerektiren iş
  değil ama cd::material UI-variant feature'ı eksik = GATED.**

#### hello_world → DOABLE-NOW (DAG temiz; mimari blok yok, scope orta)

- DEP'ler (hello_world/CMakeLists.txt:40-57): `cd::core math ecs
  gameplay_time game_query game_trigger game_camera game_particles_event
  physics platform rhi rhi_vulkan render camera material shader`.
- hello_engine'de **OLMAYANLAR**: `cd::gameplay_time`, `cd::game_query`,
  `cd::game_trigger`, `cd::game_camera`, `cd::game_particles_event`
  (hello_engine CMakeLists grep'i: bu 5 isim SIFIR match). `cd::physics`,
  `cd::material`, `cd::render`, `cd::camera`, `cd::ecs`, `cd::math` zaten
  var.
- **DAG cycle kontrolü (architect kapısı — TEMİZ)**: gameplay libs strict
  upward — `engine/game/query/CMakeLists.txt:27` `game_query →
  cd::scene`; `game_camera → cd::core/math/camera/ecs`
  (camera/CMakeLists.txt:23-27). cd::scene + cd::camera + cd::ecs ZATEN
  hello_engine closure'unda. Gameplay tier "strictly above world/render,
  below editor/samples" (camera/CMakeLists.txt:8 yorumu) — hello_engine'e
  5 PUBLIC edge eklemek **paralel yukarı-dal**; gameplay→sample geri-kenarı
  YOK. **Cycle imkânsız, layer ihlali yok.**
- **Tek maliyet**: ~400L "Gameplay" panel (3rd-person walk + trigger +
  vcam + particle-event demo) + 5 DEP satırı. Mimari blok YOK — architect
  kapısı AÇIK. [feedback_sample_consolidation_and_per_feature_demo] ile
  uyumlu (panel-içi demo).

#### hello_behavior_designer → DEFER

- ADR-20260613-editor-panel-fold-strategy Karar: Seçenek C (ERTELEME).
  Node-graph layout (`measure_subtree`/`draw_real_tree`/`draw_demo_nodes`
  + ortogonal kenarlar) ImGui DrawList ile ~200L ekstra reimpl gerektirir;
  `cd::game::ai_bt::BehaviorTree` hello_editor'a yeni kütüphane dep'i
  ekler. Sample-azaltma için orantısız. Karar değişmedi — DEFER.

### Net öncelik sırası (15 → ≤10)

**ŞİMDİ otonom yapılabilecekler (mimari blok YOK, GPU-golden ile doğrulanır):**
1. **hello_world** (15→14) — DAG temiz, ~400L gameplay panel + 5 DEP;
   en yüksek kesinlik (hiçbir gate/script bağı yok).
2. **hello_hot_reload** (14→13) — FileWatcher polling (thread riski yok),
   tüm dep'ler mevcut, on-disk shader altyapısı hazır. Golden öncesi
   SOURCE==EXE force-sync uyarısı geçerli.
3. **hello_anim** (13→12) — fold KOLAY ama **önce** golden capture'ı
   hello_engine golden CLI'ya re-home + 3 script/CI referansını güncelle
   (run_golden.sh:61 + run_golden.ps1:53 + ci-amd-radv.yml:93). Bu config
   işi yapılmadan golden pipeline yetim kalır.

→ Bu 3 fold ile **15 → 12**. ≤10 hedefi için kalan 2 birim ek d3d12
   katlaması (SAMPLES_CONSOLIDATION_PLAN Batch-4a deseni) veya hello_metal
   placeholder konsolidasyonuyla kapatılabilir — ayrı değerlendirme.

**Gate-bloklu (eksik ön-koşul; otonom YAPILAMAZ):**
4. **hello_ui** — GATED on cd::material UI variant feature
   (kSubmitterPipelineReady gerçek-donanım pipeline'ı). Bu feature
   shipped olunca gate `apps/editor` + `hello_ui` ikisinde flip edilir,
   SONRA fold edilebilir. Feature yokken fold = ölü Submitter taşımak.

**Kullanıcı/donanım kararı gerektiren:**
5. **hello_behavior_designer** — DEFER (ADR-20260613). Node-graph reimpl
   ~200L + yeni dep; sample-azaltma değerini haklı kılmıyor. Kullanıcı
   "node-graph görseli hello_editor'da şart" derse yeniden değerlendirilir.

**Sonuç**: Otonom 15→12 (hello_world + hello_hot_reload + hello_anim/
re-home); ≤10 için +2 d3d12/metal katlaması (ayrı plan); hello_ui feature-
gate ardında; hello_behavior_designer DEFER.
