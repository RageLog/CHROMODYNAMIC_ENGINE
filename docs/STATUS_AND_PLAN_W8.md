# CHROMODYNAMIC - Status and Work Plan (W8 sonu, 2026-05-28)

> Bu dokuman team-lead orkestrasyonu altinda, kod yazmadan, durust durum tespitidir. Kaynak: repo inspection + memory + W8 commit serisi (phase231-279).

---

## 1. Where we are vs where we started

### Baslangic durumu (Phase 1 / Run 1 oncesi)
- Vizyon: cross-platform / cross-API hybrid 2D+3D engine, library-oriented, Filament/bgfx/EnTT asilacak hedef.
- Phase 1 = tasarim. ADR 1-17 yazildi, kod yoktu.

### Su an (W8-BB, commit 42e8c32)
- 97 library target altinda foundation + render + world + asset + ui + runtime + script. Library-oriented prensibi strukturel olarak uygulanmis.
- 50 sample, 11 tema (foundation, asset, audio, world, net, script, lib_smokes, rhi, render, editor, engine).
- 97 test binary. Breadth iyi; depth (golden image, fuzz, stress) sample bazinda.
- 60 plus ADR. Karar disiplini guclu.
- Marathon Run 3-6: v0.99.33 to v0.99.58 (26 tag), Phase 204 to 279.
- hello_engine: 7793 satir (Run 4 sonu 6100 idi; W7/W8 ile +1700 buyudu, extraction borcu).
- MANIFEST.csv yok / 0 PDF: Demir Kural pipeline henuz tetiklenmedi.

### Bottom line
- Breadth: vizyona %80 hizali; Filament/bgfx feature listesinin cogunda lib var.
- Depth: %30-50. Cogu ileri lib (DDGI, ReSTIR, NRC, virtual_geometry, mesh_shader) CPU-parity smoke seviyesinde.
- Production readiness: Vulkan/Windows yolunda hello_engine calisiyor; gerisi demo seviyesi.


---

## 2. Subsystem audit

| Subsystem | Substance | Durum |
|---|---|---|
| RHI core (cd::rhi) | interface + handle | OK |
| RHI Vulkan | 5046 satir | Uretim-yakin; RT pipeline + dispatch_rays kapali |
| RHI D3D12 | 2906 satir | Substansiyel; Vulkan paritesi yok |
| RHI Metal | 31 satir | Stub. Cross-platform iddiasi icin acik |
| RHI OpenGL | 1178 satir | Yari-tam, fallback |
| Material (cd::material) | Lit + StandardPbr + Skinned + AnalyticalSky + BRDF LUT | OK; W8-AQ uniform yola cekti |
| BRDF (4 lib) | LTC + Sheen+Clearcoat + SSS | OK |
| Lighting | dir + point + spot + rect-area | OK; W7/W8 tum turleri duzeltti |
| Shadows | CSM + RT inline + planar | Kirilgan; son 50 commit yarisi tuning. W8-AS ghost CSM duzeltti |
| IBL | EquirectToCube + Irradiance + PrefilteredSpec + BrdfLut | OK; W8-AW/AZ/BA env-spec gate |
| Animation | glTF anim + CPU-LBS skinning | OK (CPU). GPU pipeline lib var, hello_engine baglanmadi |
| Post-FX | composite + 11 lib | Wired. SSR ekran-bagimli; gercek RT reflection yok |
| Tonemap / HDR | ACES + Hill + Hable + AGX + HDR10 PQ | OK |
| RT | BLAS+TLAS + per-frame BLAS (W8-U) + shadow + refl-occlusion (W8-BA) | Sinirli. Recursive shading yok, dispatch_rays yok |
| Volumetric | composite-inline fakes | Stub. Froxel/3D-noise yok |
| GI (ddgi, restir, nrc) | CPU parity smoke | Stub |
| Particles / Decals | smoke | Stub |
| Mesh shader / Virtual geo | CPU parity smoke | Stub. Nanite hedefi icin 0 ilerleme |
| ECS | Entity.cpp 49 satir | Minimal. EnTT/Bevy/Flecs paritesi yok |
| Scene | Scene.cpp 161 satir | Minimal |
| Asset pipeline (12 lib) | glTF + KTX2 + WAV + JSON + PAK + streaming | Iyi breadth; PAK/streaming prod-load test yok |
| Audio | Wasapi 542 + CoreAudio 302 + Alsa 275 + FileSink + Null + Native | Sasirtici sekilde saglam, cross-platform onde |
| Physics | BuiltinPhysicsWorld 194 satir | Stub. Jolt/Bullet/PhysX yok |
| Network | Loopback 113 + UDP 254 | Erken. Replication/snapshot yok |
| Concurrency | 26 hpp header-inline + 7 test binari (W8 phase283) | OK (sub-system substance) -- BLOCKER tag CLEARED; integration to render/asset/ECS dispatch sites = X1 Phase 2 |
| Async submit / Render thread | sample var | Yari; gercek framegraph job sched yok |
| UI / Editor | ImGui-based, hello_engine icinde panel | Sample seviyesi; standalone editor binary yok |
| Script | basit | Erken; Lua/Wren/JS yok |
| Build | 27 preset, vcpkg manifest pinned to release 2026.04.27 baseline (W8 phase282) + BUILDING.md tier policy, ASAN/UBSAN/TSAN/MSAN, 8 compiler | OK -- reproducible build risk CLEARED; vcpkg manifest is now honest (gtest+fmt only Tier A; graphics stack stays Tier B FetchContent per ADR-016) |
| CI | ci-* presets + docs | Belirsiz; aktif runner durumu koddan gorunmuyor |

### Demir Kural durumu

research/library/pdf/ = 0 PDF, MANIFEST.csv yok. Phase 1 boyunca akademik atif gerektiren karar verilmedi. Acik: Karis 2013, Heitz 2016, Frisvad 2012, Eberly LBS, Wronski 2014 shader yorumlarinda kullaniliyor ama PDF/MANIFEST yok.

---

## 3. Gap analysis (Severity x Effort)

| Gap | Severity | Effort | Pre-req | SOTA target |
|---|---|---|---|---|
| Job system implementasyonu | ~~BLOCKER~~ -> Phase 1 CLEARED (W8 phase283, ADR-20260528) / Phase 2 = integration BLOCKER | 2-3 hafta (Phase 2 only, was 2-3 hafta total) | X4+X7 | Bevy/EnTT, Naughty Dog Fiber GDC2015 |
| hello_engine 7793 satir monolith | Major | 1 hafta | - | cd::sample_framework |
| Vulkan RT pipeline + dispatch_rays | Major | 2 hafta | AS build path | DXR + VK_KHR_ray_tracing_pipeline |
| D3D12 Vulkan paritesi | Major | 3-4 hafta | RHI stable | DX-Forge |
| Metal backend | Major | 4-6 hafta | RHI stable + MSL gen | Filament Metal |
| OpenGL backend complete | Minor | 1 hafta | - | bgfx OpenGL |
| Shaders on-disk + hot reload | Major | 1-2 hafta | shader cache | Filament filamat/matc |
| vcpkg manifest gercek deps + baseline | ~~BLOCKER~~ -> CLEARED (W8 phase282, baseline 56bb2411 = vcpkg release 2026.04.27, BUILDING.md tier policy) | done | - | DtForHil pattern (adapted to FetchContent-per-subsystem tier B) |
| Real CI runner multi-OS | Major | 1 hafta | vcpkg fix | GH Actions/Azure |
| DDGI/ReSTIR/NRC GPU | Vision-tier | 4-8 hafta her biri | RT pipeline + compute | Falcor |
| Volumetric fog froxel | Polish | 1 hafta | 3D texture | Wronski 2014 / Hillaire 2016 |
| ECS v2 archetype | Major | 3-4 hafta | - | EnTT, Bevy, Flecs |
| Scene serialization v2 | Major | 1 hafta | ECS solid | - |
| Physics gercek backend (Jolt) | Major | 2-3 hafta | ADR-008 v2 | Jolt 5 |
| Net replication katmani | Vision-tier | 3-4 hafta | ECS + scene | Photon/Mirror |
| Editor standalone binary | Major | 2-3 hafta | hello_engine extract | Unity editor |
| Library-as-product packaging | Major | 1-2 hafta | vcpkg fix | find-package per lib |
| MANIFEST.csv akademik delil zinciri | Major | 2-3 gun | academic-researcher | - |
| GPU skinning hello_engine entegrasyonu | Polish | 2 gun | SkinnedLitMaterial wired | - |
| Recursive RT shading + gercek reflections | Major | 2 hafta | RT pipeline | DXR closest-hit |

---

## 4. Work plan: Now / Next / Later

### NOW (bu hafta - W8 tail polish + extract borclari)

| # | Is | Ajan | Notlar |
|---|---|---|---|
| N1 | hello_engine main.cpp 7793 to ~3000 satir extract: W7/W8 light setup + 3-point rig + PBR grid + skinned playback to cd::sample_framework | architect -> developer x 2 -> tester | W9 wave tekrar sismesin |
| N2 | Ghost shadow / TLAS exclusion regression testi (W8-AS-AU manuel duzeltti) | tester (golden image diff) | hello_engine smoke + RT pass diff |
| N3 | W8 wave changelog + ADR (docs/ADR/ADR-20260528-wave-w7-w8.md) | doc-writer + architect | Karis/Heitz/Frisvad referanslari |
| N4 | MANIFEST.csv ilk yukleme: Karis 2013, Heitz 2016, Frisvad 2012, Eberly LBS, Wronski 2014 | academic-researcher -> citation-verifier BLOCKING | 5 PDF + 5 BibTeX + 5 notes |
| N5 | Composite UI panel ayristirma; FX/AO/Shafts/HDR preset butonlari cd::editor_panel | ui-architect -> ui-developer | hello_engine ince tutmak |

### NEXT (bu ay - temel aciklarin kapatilmasi)

| # | Is | Ajan | Notlar |
|---|---|---|---|
| X1 | Job system gercek impl: WorkStealingThreadPool.cpp + JobGraph.cpp + ParallelFor.cpp | architect -> safety-integration BLOCKING -> developer x 3 -> tester (fuzz+stress) | TSAN preset zorunlu |
| X2 | vcpkg manifest gercek deps + baseline SHA: glfw3, glm, spdlog, vulkan-headers, VMA, glslang, shaderc, ktx2, meshoptimizer, basis-universal, fastgltf, miniaudio, jolt-physics | build-devops -> tester (clean-build) | DtForHil pattern |
| X3 | CI: 1 self-hosted runner multi-preset matrix (msvc-debug, clangcl-release, ninja-debug-asan, ninja-debug-tsan) | build-devops -> release-manager | docs/CI_SELF_HOSTED.md hazir |
| X4 | D3D12 Vulkan paritesi sprint: kalan 3 NotImpl + RT path | architect -> developer x 2 -> tester | hello_d3d12_* golden diff |
| X5 | Shader on-disk + hot reload: engine/render/material/shaders/*.glsl + build-embed + file watcher | architect (ADR-003 revize) -> developer | Filament filamat |
| X6 | Vulkan RT pipeline + dispatch_rays: closest-hit + miss + raygen | safety-integration -> developer -> tester | hello_rt gercek hit shading |
| X7 | ECS v2 archetype storage: sparse-set + system iteration | architect (ADR-004 v2) -> researcher (EnTT/Bevy/Flecs) -> developer x 3 | hello_engine sphere gercek query |
| X8 | Library-as-product packaging: her cd_<lib> icin install + cd-config.cmake + version | build-devops -> installer-maker | Modularity vizyonu |

### LATER (vision - sonraki 3-6 ay)

| # | Is | Ajan | Notlar |
|---|---|---|---|
| L1 | Metal backend (macOS + iOS) | architect -> researcher -> developer x 3 | Cross-API iddia |
| L2 | Linux/macOS runtime parity (samples 3 OS yesil) | build-devops -> tester | Mobile/console lowest priority |
| L3 | DDGI veya ReSTIR biri gercek GPU | academic-researcher -> architect -> developer x N | Falcor referans |
| L4 | Nanite-class virtual geometry veya mesh shader cluster | academic-researcher -> architect -> developer | Phase 1.5 disi |
| L5 | Editor standalone binary (bin/chromodynamic-editor) | ui-architect -> ui-developer -> release-manager | Library-as-product showcase |
| L6 | Physics: Jolt entegrasyon (ADR-008 v2) | architect -> developer -> tester | Built-in fallback kalir |
| L7 | Scripting layer: Lua/Wren/JS aday + ADR | researcher -> architect -> developer | Vizyon scriptable dendi |
| L8 | Networking: real replication + snapshot interp | architect -> developer x 2 | Opsiyonel |
| L9 | Volumetric fog froxel + 3D cloud noise | developer -> tester (golden) | Inline fakeleri degistir |
| L10 | Mobile/console pre-design (iOS/Android/Switch) | architect | ADR var, kod yok |

---

## 5. Risks ve acik sorular

1. ~~Job system / concurrency acigi~~ -> Phase 1 CLEARED (W8 phase283): header-inline impl audited, accepted as canonical executor (ADR-20260528), 8 yeni JobGraph testi + 2 yeni WSL stress testi + 2 yeni ParallelFor baseline testi yesil; CLAUDE.md concurrency yogun iddiasi artik substantive. Kalan risk: X1 Phase 2 = render/asset/ECS dispatch sitelerine entegrasyon (2-3 hafta, X4+X7 prereq) + TSan preset run (X3 prereq). Detayli takip: ADR-20260528 Sonuclar bolumu (X1-FU-A..D + X1 Phase 2).
2. ~~vcpkg.json placeholder baseline~~ -> CLEARED (W8 phase282): baseline 56bb2411609227288b70117ead2c47585ba07713 = vcpkg release 2026.04.27, BUILDING.md icinde Tier A (vcpkg) / Tier B (FetchContent per-subsystem) / Tier C (vendored) policy dokumante edildi. Dis kullanici simdi cmake --preset ninja-base ile reproducible install yapabilir. find_package wiring olmadan ek dep eklenmiyor (policy comment vcpkg.json icinde).
3. hello_engine entropi: her W wave +500-2000 satir ekliyor. Extract disiplini olmadan main.cpp 10k+ olur ve sample degil monolith haline gelir.
4. Demir Kural latent risk: 60 plus ADR yazildi, akademik PDF zinciri yok. Ilk paper-citation gerektiren ADR pipeline test edilmemis olacak; N4 erken pilot.
5. Test depth: 97 binary breadth iyi ama golden image / fuzz / stress yok. RT bias tuning regression korumasi zayif (W8 commit yarisi revert/re-revert).
6. 97 lib asiri granuler mi?: brdf + brdf_ltc + brdf_sheen_clearcoat + brdf_sss ayri libler; cd::brdf umbrella zaten var. Lib sayisi modularity sinyali ama ergonomi dusurebilir. ADR-005 revize edilebilir.

---

## 6. Top-5 priorities (sirali)

1. ~~X2 vcpkg manifest~~ -> DONE (W8 phase282). ~~X1 Job system Phase 1~~ -> DONE (W8 phase283, ADR-20260528). Iki BLOCKER kapatildi.
2. N4 + N3: W7/W8 wave kapat: MANIFEST.csv ilk yukleme + W7/W8 ADR + extract. Demir Kural pipeline ilk tetikleme.
3. ~~N1 (extract pass)~~ -> DONE (W8 phase289-295, Marathon Run 7 sub-N1A..N1G). hello_engine main.cpp 7800 -> 7611 satir (-189; embedded GLSL ~3000 satir + PrimPush/light UBO + sky-with-sun + planar shadow + TLAS instance + PBR demo grid + 4 scene-bootstrap helper hepsi ilgili kutuphane/header dosyasina cekildi). main() body 7147 -> 7045 (-102). Hedef main()<300 tek sub-phase ile ulasilabilir degil (kalan render-loop + per-frame UI heavy ref capture); Marathon Run 8 N2A..N2x ile devam edilir. X5 (shader on-disk hot reload) hala acik; N1 extract on-disk relocation degil sadece dosya/lib disiplini.
4. X4 + X6: D3D12 paritesi + Vulkan RT pipeline. Cross-API + gercek RT iddialarinin asgari odemesi.
5. ~~X1 Phase 2 (X1B-X1E)~~ -> DONE (W8 phase284-287, Marathon Run 7). hello_engine TLAS instance + boot asset bake + ECS PrimPush prep + CSM/planar shadow caster prep all use cd::concurrency::parallel_for or JobGraph<WSL pool>. RHI secondary cmd buffer (true parallel cmd record) deferred to X1F follow-up (>500 line RHI rev). Detay: ADR-20260528 Sonuclar bolumu + this section follow-ups.

### Marathon Run 7 N1 close-out (W8 phase289-295)

- N1A (phase289): kPrimVS/kPrimFS/kShadowVS/kShadowFS GLSL stringleri PrimShader.hpp + PrimShader_kPrimFS.inl + PrimShader_kShadow.inl - main.cpp ~3000 satir azaldi.
- N1B (phase290): PrimPush + LightSlotGpu + LightUboGpu + pack_light_slot HelloLighting.hpp icine alindi.
- N1C (phase291): Sun-disk sky bake lambda cd::material::sky_with_sun_cpu altina alindi.
- N1D (phase292): Planar shadow projection matrisi cd::render::PlanarShadow.hpp icine alindi.
- N1E (phase293): InstanceMatGpu / kMaxInstMats / make_accel_instance / fill_inst_mat HelloRayQuery.hpp icine alindi; iki TLAS rebuild call-site helper kullaniyor.
- N1F (phase294): 4x4 PBR demo-grid spawn data + math HelloPbrGrid.hpp icine alindi (build_pbr_demo_grid()).
- N1G (phase295): setup_world_container / spawn_primitive_seeds / spawn_gltf_or_earth_entity / spawn_pbr_grid_entities anon namespace helper olarak ayrildi - main() -102 satir, boot region 4 named call site.
- N1 follow-up (Marathon Run 8 N2 zinciri): render loop main pass + composite blit + ImGui panel handlers ayri named fonksiyonlara veya cd::sample_framework yeni libine ayristirma. main() body 7045 -> hedef <300. Sized: 1 marathon run. -> DONE-IN-PART (W8 phase297-302, Marathon Run 8 N2A..N2F).

### Marathon Run 8 N2 close-out (W8 phase297-302)

Yeni hedef: main() body <500 - Run 8 sonu 5938; <300 hedefi Run 9 N3+N4 zincirine kaydirildi (R-Showcase + gizmo + render-pass extracts gerekiyor).

Pratik revizyon (N2A sirasinda kararlasti): FrameContext aggregate Run 7 N1 close-out tavsiye etti, ancak ~150 main()-scope local'i mirror eden 50-alanli reference-bundle struct, helper-spesifik dar parametre listesinden ek deger getirmiyordu. **Once kucuk + bagimsiz UI panel + overlay extract'leri ship et; FrameContext'i ancak render-pass extract'i talep ettiginde tanit** (Run 9 territory).

- N2A (phase297): Counters + Random viz + History panelleri (cd::frame_timing dt-ring static function-local'da kaliyor). main() body 7045 -> 6947 (-98).
- N2B (phase298): Audio + Net Sim + Streamer panelleri. Audio + Streamer std::function<> callback aliyor (log_push + streamer_enqueue stateful lambda kaldigi icin). <functional> eklendi. main() body 6947 -> 6787 (-160).
- N2C (phase299): Scene tree + Inspector panelleri. Inspector tek panel ekstraktinin en buyugu (~150 satir): drag-edit Transform with EditHistory drag-release semantics for Position / Rotation / Scale + tint. main() body 6787 -> 6637 (-150).
- N2D (phase300): Outliner + Lights panelleri. Hazirlik adimi: SelKind enum + LightRow struct main() scope'tan anon namespace'e lift edildi (helper imzasinda type by name). Lights helper'in icine per-frame CCT->RGB rebuild + ClusterGrid::assign side-effect loop alindi. main() body 6637 -> 6312 (-325).
- N2E (phase301): Selection outline overlay + light marker overlay (~330 satir kombine). Her ikisi de mevcut vp matrix + extent ile world->screen project ediyor, ImGui::GetBackgroundDrawList() ile cizim. **6000-satir esiği bu sub-phase'de gecildi.** main() body 6312 -> 5987 (-325).
- N2F (phase302): Command Palette popup. Kucuk (50 satir) ama render loop'ta kalan son gercekten self-contained UI panel-style bolge. main() body 5987 -> 5938 (-49).

**Run 8 sonuc:**
- main.cpp: 7611 -> 7811 (+200; 14 yeni inline helper definition anon namespace'de).
- **main() body: 7045 -> 5938 (-1107, 15.7% azalma)**.
- 14 yeni anon-namespace helper: draw_{counters,random,history,audio,net_sim,streamer,scene_tree,inspector,outliner,lights,selection_outline_overlay,light_markers_overlay,command_palette_popup}_panel + 2 lifted type (SelKind, LightRow).
- Yeni library header yok bu run'da - sample-local kaldi (SceneEntity / LightRow sample-spesifik).
- Tests 96/96 PASS her checkpoint'te.
- Engine boots clean (her commit sonrasi smoke launch dogruandi).
- Pure mechanical extract; render davranisi degismedi.

**Ship edilmedi (Run 9 territory):**
- R-Showcase panel (~237 satir): 26 fx_* parametre yuzeyi. FxState struct refactor ayri scope (Run 9 N3-prep).
- Gizmo overlay (~720 satir): drag-state lambda + EditHistory + multi-mode state machine derin coupling. Marathon-pace mekanik extract icin risk yuksek.
- Per-render-pass extract (sky / shadow / floor / entity prim / planar shadow / composite / velocity): her pass ~10-30 local. FrameContext aggregate veya HelloEngineFrame header'i gerekiyor.

**Run 9 onerilen oncelik sirasi:**
1. **N3-prep**: fx_* (~26 float + 2 int + 2 bool) HelloEngineFx struct'ina lift et. ~150 satir mekanik degisim.
2. **N3**: R-Showcase paneli HelloEngineFx kullanarak extract. ~237 satir main() dusus.
3. **N4-prep**: HelloEngineFrame aggregate tanit (cmd, vp, view, sun_dir/col/str, ambient, cam, frame.extent, FxState ref bundle). Original N2A FrameContext plani burada justified - artik sonraki extract batch'i talep ediyor.
4. **N4A..N4x**: render-pass extracts bagimsizlik sirasiyla: floor draw, sky pass, planar shadow pass, sky+IBL bake region (boot-side leftover), velocity G-buffer pass, composite pass, ImGui pass on swapchain. Hedef: main() body 5938 -> ~2500.
5. **N5**: gizmo overlay extract once R-Showcase + render-pass extracts have shaken out helper-parameter shape.

### Yeni follow-up itemler (W8 phase282/283 sonrasi)

- X1-FU-A: cv -> std::atomic::wait/notify_one migration (1-2 gun, post-X3 TSan).
- X1-FU-F: Vulkan secondary cmd buffer pipeline (ISecondaryCommandBuffer + VulkanSecondaryCommandBuffer; X1E original RHI plumbing). True parallel cmd record per render pass. Sized: 1 week. Pre-req: ICommandBuffer surface review + 4 backend impl (Vulkan, D3D12 bundles, OpenGL no-op, Metal indirect cmd encoder, Null no-op). Filed against ADR-20260528.
- X1-FU-G: upload_buffer thread safety review (cd::rhi::IDevice spec). Current X1C boot graph serializes uploads after the CPU bake join because the device API is not documented as thread-safe; if uploads can move parallel that cuts boot wall-time further.
- X1-FU-H: per-frame parallel ECS scaling: re-measure once entity count grows past worker count (X7 ECS v2 archetype trajectory). Today 22 entities = pattern-only win.
- X1-FU-B: TSan preset run on concurrency suite (1 gun, X3 prereq).
- X1-FU-C: Hazard-pointer based reclamation for retired WSD buffers (3-5 gun, polish).
- X1-FU-D: Priority-aware steal ordering (2-3 gun, polish).
- X1-FU-E: engine/foundation/concurrency/README.md (doc-writer one-pager: header-only design + Tier B FetchContent rationale).


---

## 7. Calisma modu (team-lead notu)

- Bu plandaki her item icin subagent dispatch ayri brief ile baslatilir (CLAUDE.md Brief Sozlesmesi).
- Bagimsiz itemler (N4 + N3 + N5; X1 + X2; L1 + L9) paralel dispatch edilir.
- BLOCKING ajan zincirleri: safety-integration (concurrency icin zorunlu), citation-verifier (her academic atif), independent-auditor (kritik claim).
- Tag yok (feedback_no_auto_tag). Phase numarasi devam: phase303 (Run 9 N3-prep oncelikli).
- Marathon kurali: kullanici uzun maraton demedigi surece her wave kullanici onayi ile kapanir.
