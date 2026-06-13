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
- **Batch 4 (editor/world katlama):** hello_animator/behavior_designer/
  material_editor/hot_reload + world/hello_anim/inspector/scene_graph +
  ui/hello_ui → hello_editor panellerine (panel kütüphaneleri
  engine/ui/editor'da zaten mevcut; sample'lar ince sarmalayıcı).
  game/hello_world (885L) gameplay döngüsü → hello_engine'e taşınacak
  modüller belirlenerek.
- **OpenGL birleşik:** hello_opengl_resources + hello_opengl_triangle →
  tek `hello_opengl` (Batch 2 ile).

## Kurallar

- Her batch ayrı commit; build + 267 ctest + chrome_probe golden her
  checkpoint'te. Silinen örneğin kapsama kanıtı commit mesajında.
- `git rm` ile geçmiş korunur; geri dönüş gerekirse tag'siz geçmişten.
- ctest hedefleri sample'lara bağımlı DEĞİL (golden CLI yalnız
  hello_engine) — yine de her batch öncesi grep ile doğrulanır.
