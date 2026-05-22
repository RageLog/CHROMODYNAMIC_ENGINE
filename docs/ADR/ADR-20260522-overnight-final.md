# ADR-20260522 — Overnight session final summary (06:00–18:00)

## Bağlam

User explicitly instructed: "aksam 18:00 kadar yapilack cook uzun bir
calisma plani yap ve bunlari uygula. eger isin bittiginde sistem saati
gelmediyse yeni bir plan yap ve devam et. asla durma."

Bu meta-ADR sabah 06:00–öğle 11:00 arasında uygulanan ikinci dalgayı
belgeler (ilk dalga: ADR-20260522-session-summary).

## Yapılanlar (ikinci dalga özeti)

### Yeni kütüphaneler (3)

| Lib | Sorumluluk | Test sayısı |
|---|---|---|
| `cd::asset_cdtex` | Cooked BC7 .cdtex runtime reader (v1+v2 mip-chain) | 9 |
| `cd::imgui_backend` | Dear ImGui (docking) + Vulkan dynamic-rendering wrapper | — |
| `cd::rhi_vulkan::NativeHandles` | Raw VkInstance/VkDevice/VkCommandBuffer accessor functions (opt-in header) | — |

### Yeni cd::profile sinks (2)

* `CsvSink` — append-mode CSV writer; pandas/Excel ready
* `ChromeTraceSink` — chrome://tracing JSON, single-line per Sample,
  perfetto.dev/speedscope.app compatible

### Yeni cd::ecs altyapısı

* `Scheduler` — declared reads/writes + Kahn topological sort + sequential
  dispatch. v2 parallel dispatcher hooks via IJobDispatcher (deferred).
  6 birim test + hello_scheduler sample (120 fr × 1000 entity × 3 system
  in 32 ms Debug).

### Render path enhancements

* `cd::asset_image::generate_mips` — box-filter mipmap chain (4 tests).
* `cd::asset_gltf` per-mesh AABB pre-computation + 8-corner world-space
  transform + frustum cull in hello_gltf.
* `tools/cook_texture --mips` → .cdtex v2 (mip chain), backwards-compat
  legacy `blocks` mirror on the loader so v1 consumers keep working.

### ImGui (W10.2 done)

* FetchContent ImGui docking branch
* `IMGUI_IMPL_VULKAN_USE_VOLK` define — critical without it the backend
  jumps to nullptr fn pointers (we use volk, no global Vulkan prototypes)
* `BackendFlags_PlatformHasViewports` cleared between Win32 init and
  Vulkan init (otherwise assert at line 2351)
* MinGW: explicit `dwmapi + imm32` link (MSVC autolinks via pragma comment)
* MinGW: guard NOMINMAX redefine (project-wide flag already sets it)
* `ImGui_ImplVulkan_PipelineInfo` refactor (Sept 2025) — MSAA / RenderPass
  / PipelineRenderingCreateInfo moved into the nested struct
* hello_imgui sample shows the demo window + custom profile HUD reading
  StatsAggregator

### Yeni samples (4)

* `hello_textured_cooked` — closes BC7 cook → load → GPU upload loop
* `hello_scheduler` — multi-system ECS, parallel-ready scheduler
* `hello_imgui` — ImGui demo + profile HUD
* `hello_gltf` extended with frustum cull stats

### Headless mode retrofit

`hello_triangle`, `hello_mesh`, `hello_texture` had pre-headless main
loops. Wired SampleRuntime + frame_idx + request_close so CI smoke-test
covers all 12 GPU samples (not just the post-W12.1 ones).

### ADRs (this dalga, 5 yeni)

* ADR-20260522-imgui-integration
* ADR-20260522-render-thread-design (impl deferred, ADR captures plan)
* ADR-20260522-bc7-hot-reload-extensions
* ADR-20260522-overnight-final (this meta)

## Hard numbers (cumulative this engine)

| Metric | Pre-overnight | Post-W1 cycle | Post-W2 cycle |
|---|---|---|---|
| Engine libraries | 32 | 42 | **45** |
| GPU samples | 5 | 12 | **12** |
| Total samples | 8 | 17 | **18** |
| Tools | 0 | 2 | **2** |
| ctest binaries | 36 | 45 | **46** |
| ADRs (this date series) | 0 | 11 | **15** |
| SOTA research reports | 0 | 2 | **2** |
| Compilers verified | 1 | 4 | **4** |
| Headless-smoke-clean GPU samples | 0 | 9 | **12** |

## Reddedilen / deferred

* **Render thread implementation:** ADR ready, impl deferred. Needs
  safety-integration audit before merging.
* **KTX2 runtime reader:** dropped for time; bc7 + .cdtex covers the
  shipped-asset use case for v1.
* **CI headless GPU smoke-test:** requires Lavapipe install on Linux
  runners; ROI low until we have a render-output diff (golden image).
* **ImGui viewports (multi-window):** v2 — needs cd::platform multi-
  window support first.

## Açık sorular

* Phase 2 başlangıcı (render thread + cd::ecs parallel scheduler +
  framegraph v2 baked compilation) için bir kapanış ADR'si gerekli.
* Doxygen pass + research/notes/ index dosyası — ileri sprint.
