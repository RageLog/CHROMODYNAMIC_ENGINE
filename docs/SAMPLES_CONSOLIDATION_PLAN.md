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
    | hello_hot_reload | FileWatcher+CachedCompiler+material swap | hello_editor/engine toggle | ~100L | FileWatcher threading; ADR-20260522 canonical | PENDING |
    | hello_anim | AnimationClip+Player+GoldenCapture | hello_engine showcase | ~200L | CI run_smoke `hello_anim` adını taşıyor — script+golden güncelle | PENDING |
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
