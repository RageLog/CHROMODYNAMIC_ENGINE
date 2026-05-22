# ADR-20260522 — Phase 4 closure design (forward-plan)

## Bağlam

Marathon (Wave 1-12) sonu engine durumu:

- 48 library / 24 sample / 50 ctest / **4/4 compilers 50/50 green**
- 8/8 asset format'ı AssetRegistry'ye out-of-the-box register edilebilir
- Scene serializer + enumeration API + ImGui inspector çalışıyor
- Tooling katmanı tamam (Doxygen 0 warning, smoke 24/24, coverage, sanitizers)

Geriye kalan büyük taşlar Phase 4 (Editor + Production hazırlık)
kapsamında ele alınacak. Bu ADR bunların **tasarım kararlarını ve
sırasını** kayıt altına alır; kod yazmaz. Implementasyon her madde
için kendi sprint ADR'sini doğuracak.

## Karar — Phase 4 sprint timeline

Sıralamada üç prensip:
1. **Sahip olduğumuz halen-kullanılmayan yetenekleri önce ürünleştir**
   (PBR rendering, animasyon playback, audio mixer).
2. **Risk-yoğun concurrency işlerini orta sprintlerde yap**
   (parallel ECS dispatcher, render thread); audit + benchmark
   prosedürleri elimizde.
3. **Editor / asset pipeline polish son** — diğer subsistemler stabilleşmeden
   editör'ü genişletmek erken.

### Sprint S4.1 (1-2 hafta) — Render path completion

**Hedef:** flat-shaded `hello_*` örneklerini PBR-shaded yapmak.

* `cd::material` extension: BaseColorFactor + Metallic + Roughness + Normal
  texture binding. Mevcut `cd::asset_gltf::GltfMaterial` struct'ı PBR
  factor'larını çekiyor — `cd::material::PbrInstance` adapter yazmak yeterli.
* GLSL fragman shader: physically-based microfacet (Cook-Torrance Lambert
  + Schlick + GGX). Reference: Filament + glTF 2.0 spec § 5.22.
* Sample: `hello_pbr` (gltf mesh + IBL fallback HDR).
* Eski sample'lar değişmeden çalışmaya devam etmeli (default = legacy flat).

**Reddedilen alternatifler:**
* Forward+ veya cluster shading — Phase 5'e ertelendi.
* Compute shader-based pre-integration (LUT bake) — gerekli ama Sprint
  S4.6'da skybox ile birlikte.

### Sprint S4.2 (1 hafta) — Animation playback

**Hedef:** `cd::anim`'in sahip olduğu skeletal data'yı bir sample'da
oynatmak.

* `cd::anim::Skeleton` (bone hierarchy) + `cd::anim::Track` (sample data)
  zaten var; eksik olan animasyon engine'i — `cd::anim::Animator`'ın
  `tick(dt)` ile time'ı ilerletip skinning matrix'i compute etmesi.
* glTF skinning data import (zaten parse ediyoruz; vertex skin attribute'ları
  henüz iletilmiyor renderer'a).
* Sample: `hello_anim` — bir glTF dosyası (Fox.gltf, Khronos sample asset)
  yükleyip skinned mesh draw.

### Sprint S4.3 (1.5 hafta, risk: ORTA) — Parallel ECS scheduler

ADR-20260522-ecs-storage-validation v2 hook'ları zaten mevcut.

* `cd::ecs::Scheduler` v2: `tick(World&, IJobDispatcher&)` overload.
  Stage'lere böl (DAG'da aynı in-degree=0 olanlar paralel).
* `cd::concurrency::IJobDispatcher` (zaten var, work-stealing pool).
* Methodology: 1k entity × 3 system × 120 frame bench → multi-thread
  ölçüm. Single-thread baseline = 32 ms (mevcut hello_scheduler).
  Target: 4-core'da < 12 ms.
* Risk: race condition. **safety-integration audit ZORUNLU.**
* Sample: `hello_scheduler` extend, `hello_scheduler_parallel` ayrı sample
  (paralel/sequential A/B karşılaştırması).

### Sprint S4.4 (1.5 hafta, risk: YÜKSEK) — Render thread

ADR-20260522-render-thread-design'da tasarım hazır. Implementation:

* `cd::render::FrameContext` — main thread'in tek-yön ring queue ile
  render thread'e komut paketi yolladığı producer/consumer pattern.
* Sample loop'ları main thread'de kalır; render thread sadece submit
  yapar. `wait_idle()` çağrıları render-thread-aware.
* Risk: deadlock, swapchain re-create timing, ImGui multi-thread context.
* Methodology: **5-Why + dedicated TSan run + 7-day stress test** öncesi
  merge yok.
* Sample: yok (transparent — mevcut tüm sample'lar fayda görmeli).

### Sprint S4.5 (1 hafta) — Audio platform output

`cd::asset_wav` + `cd::audio` mixer'ı platform output ile bağla.

* `cd::audio::IAudioBackend` interface (var ama tek implementation'ı yok).
* Windows: WASAPI exclusive-mode render client (low-latency).
* Linux: ALSA + PulseAudio (PA preferred for desktop).
* macOS: CoreAudio (Phase 5).
* Sample: `hello_audio_play` — WAV load → mixer kanalı → speaker.

**Reddedilen:** OpenAL Soft — runtime overhead büyük; native backends
daha verimli ve dependency-free.

### Sprint S4.6 (1.5 hafta) — Skybox + IBL

PBR rendering'i tamamlamak için.

* `cd::asset_image::ImageHdr` zaten HDR import yapıyor (Radiance .hdr).
* Cubemap projection (equirectangular → cubemap, compute shader bake).
* IBL: pre-filtered radiance + irradiance + LUT (BRDF integration).
* Sample: `hello_skybox` (HDR cubemap + reflective sphere).

### Sprint S4.7 (2 hafta) — Editor polish

`cd::editor` ve `cd::editor_ui` katmanlarının üzerine asset browser +
scene save/load + per-entity component inspector.

* hello_inspector → cd::editor::SceneTreePanel + InspectorPanel
  (production-grade); refactor mevcut sample kodunu library'e taşı.
* `cd::editor::AssetBrowserPanel` — VFS tree + thumbnail preview
  (image format'ları cd::asset_image üzerinden).
* `cd::editor::Project` — yeni sahne, save/load (cd::scene::Serializer
  yeterli).
* Drag-drop entity reordering.

### Sprint S4.8 (1 hafta) — Final polish + release

* Performance regression bench suite (`hello_bench` extension):
  every release → CSV → diff against previous release.
* Lavapipe CI smoke job (Linux runner'da GPU sample'ları).
* Headless golden image diff (FLIP/SSIM) — ADR-20260522-render-thread'in
  açık sorularından.
* Cumulative LIBRARIES.md, README, ChangeLog güncellemesi.
* `v0.2.0` tag + GitHub release.

## Reddedilen alternatifler (Phase 4 kapsamı dışı)

- **Forward+ / clustered shading:** Phase 5.
- **Volumetric / atmospheric scattering:** Phase 5+.
- **Networking (cd::net) gerçek implementation:** Phase 5; replication
  pattern'ı için ECS scheduler v2 önce stabilleşmeli.
- **Mobile / Web target:** Phase 5+. WebGPU/Dawn researcher kararı.
- **Multi-thread renderer (recording paralel command buffer):** Render
  thread + parallel ECS yeterli — multi-recorder Phase 5.
- **Hot-reload C++ kod:** RAD-toolchain işi; engine v0.x'de kapsam dışı.
- **Asset bundle / pak format:** v0.2'de cdmesh/cdtex zaten "bundled by
  cooker" pattern'ini sağlar; gerçek bundling Phase 5.

## Sonuçlar

| Sprint | Süre | Çıktı | Risk |
|---|---|---|---|
| S4.1 PBR | 1-2 hf | hello_pbr + cd::material genişlemesi | Düşük |
| S4.2 Animation | 1 hf | hello_anim + cd::anim::Animator | Düşük |
| S4.3 Parallel ECS | 1.5 hf | Scheduler v2 + bench | **Orta** |
| S4.4 Render thread | 1.5 hf | FrameContext + audit | **Yüksek** |
| S4.5 Audio | 1 hf | hello_audio_play + WASAPI/ALSA | Düşük |
| S4.6 Skybox/IBL | 1.5 hf | hello_skybox + BRDF LUT | Düşük |
| S4.7 Editor | 2 hf | cd::editor production paneller | Düşük |
| S4.8 Release | 1 hf | v0.2.0 + bench regression suite | Düşük |

Toplam: ~11 hafta. Phase 5 = real-game prototype + advanced rendering.

## Açık sorular

* **Sprint sırası değişikliği:** Audio (S4.5) PBR (S4.1) öncesi mi olmalı?
  Görsel demolar PBR'ı önceliyor; audio tek başına sample'da göstermesi
  zor. Karar: PBR önce.
* **PBR shader cache:** mevcut `cd::shader::CachedCompiler` ile pre-compile?
  Sprint S4.1 sonu karar.
* **Render thread + ImGui:** ImGui'nin docking branch'i thread-safe değil.
  Render thread'e taşımak yerine main thread'de bırakmak: ana sample loop
  ImGui için main'de kalır, sadece RHI submit render thread'inde.
* **Editor save format:** JSON yeterli mi yoksa binary (.cdscene) production
  için gerekli mi? cd::asset_json benchmark'ları (Wave 6) Debug ~350 µs /
  16 node; production'da 10k node için Release ~3-5 ms → kabul edilebilir.
* **Phase 5 hedefi:** real-time strategy oyun prototipi mi, AAA-style demo
  level mi? Karar Phase 4 sonu.
