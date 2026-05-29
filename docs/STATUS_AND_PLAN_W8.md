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
- hello_engine: 6187 satir (Run 16 phase347-348 NF1+NF2 final extraction: HelloMeshes + HelloEnginePalette). main() body: 2479 -> 2143 (-336 / 13.5% Run 16; cumulative Run 7->16: 7147 -> 2143, -5004 / 70%). main()<500 hedefi mekanik extractionla ULASILAMAZ -- kalan ~2143 satirin ~1900u boot resource alloc + per-frame closure-heavy frame-loop body; gerisi temizleme. Extraction phase DECLARED DONE; sonraki yapisal redesign cd::sample_framework gerektirir (post-extraction architectural follow-up). Net cumulative Run 11->16: main() body 3360 -> 2143 (-1217 / 36.2%); main.cpp 7381 -> 6187 (-1194 / 16.2%).
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
| hello_engine 6187 satir (Run 16 mechanical extraction DONE; main() body 2143; cd::sample_framework redesign gerektirir) | Medium | Architecture (post-mechanical) | RHI stable | cd::sample_framework |
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

### Marathon Run 9 N3+N4 close-out (W8 phase304-311)

Yeni hedef: main() body <500 - Run 9 sonu 5261; <300 hedefi Run 10 N5 (gizmo) + N6 (composite) zincirine kaydirildi.

Pratik revizyon (N4 sirasinda kararlasti): HelloEngineFrame aggregate Run 8 N2Z tavsiye etti, fakat her render pass helper'i kucuk + odakli imza ile dogal cikti (sun + cmd + sample-spesifik state). Aggregate gerekmedi; SunLight + HelloEngineFx + fill_prim_push_shared 3'lusu yeterli soyutlama saglar.

- N3-prep (phase304): HelloEngineFx flat POD (~26 float + 2 int + 5 bool) aggregate HelloEngineFx.hpp icine alindi. main() 31 stack vars -> 1 fx struct, ~230 fx_xxx -> fx.xxx mekanik rewrite. main() body 5938 -> 5900 (-38).
- N3 (phase305): R-Showcase paneli draw_r_showcase_panel(fx, lights, log_push) helper'ina cikarildi (~210 satir body). main() body 5900 -> 5691 (-210).
- N4A (phase306): Multi-light UBO fill upload_multi_light_ubo(device, lights_ubo, lights, counters) helper'ina cikarildi. main() body 5691 -> 5669 (-22).
- N4B (phase307): SunLight + resolve_sun_light(lights) helper'i tanitildi. main()'in 21-satir per-frame sun walk + sun_dir/col/str/ambient_w/has_sun lokali POD'a tasindi; downstream callsiteler sun.dir / sun.strength / sun.col / sun.ambient_w okuyor. main() body 5669 -> 5644 (-25).
- N4C (phase308): fill_prim_push_shared(pp, fx, sun, cam) helper'i floor + ECS PrimPush yazimini ortak 17-satir bloga aldi. Floor + ECS callsiteleri sirasiyla 35 -> 9 ve 28 -> 6 satira indi. main() body 5644 -> 5602 (-42).
- N4D (phase309): Sky pass draw_sky_pass(cmd, cam, aspect, sun, sky_material) helper'ina cikarildi. Onemli mimari kazanc: sky pass'in kendi inline directional-light walk'i kaldirildi, SunLight resolve sky'dan once tasinarak ECS-row'daki dup sun resolve da dustu. main() body 5602 -> 5527 (-75).
- N4E (phase310): Planar projective shadows draw_planar_shadows<MeshFor> template helper'ina cikarildi (~80 satir). MeshFor template parametre main()'in mesh_for lambda'sini headerless aldi. main() body 5527 -> 5450 (-77).
- N4F (phase311): Faz 1.6 CSM shadow map pass draw_shadow_map_pass<MeshFor> template helper'ina cikarildi (~193 satir - Run 9'un en buyuk tek extracti). Texture-barrier choreography (Undefined/ShaderResource -> DepthWrite -> ShaderResource) artik tamamen internal. SunLight resolve shadow pass'den once tasindi -> 4 pass (shadow + sky + floor + entity + planar shadow) tek directional-light walk paylasiyor. main() body 5450 -> 5261 (-189).

**Run 9 sonuc:**
- main.cpp: 7811 -> 7707 (-104; 7 yeni anon-namespace helper + HelloEngineFx.hpp aggregate header).
- **main() body: 5938 -> 5261 (-677, 11.4% azalma)**.
- 7 yeni anon-namespace helper: draw_r_showcase_panel, upload_multi_light_ubo, resolve_sun_light (+ SunLight POD), fill_prim_push_shared, draw_sky_pass, draw_planar_shadows<>, draw_shadow_map_pass<>.
- 1 yeni sample-local header: HelloEngineFx.hpp (POD aggregate). Library promotion tetiklenmedi - hellow_engine ozelinde knob surface.
- 4 per-frame directional-light walk -> 1 walk (SunLight ortak resolve).
- Tests 96/96 PASS her checkpoint'te.
- Engine boots clean (her commit sonrasi smoke launch dogruandi). Renderer davranisi degismedi (Hable tonemap + 0.55 AO + 0.75 shafts visual baseline korundu).

**Ship edilmedi (Run 10 territory):**
- Gizmo overlay (~693 satir, line ~6313): drag-state lambda + EditHistory + multi-mode state machine derin coupling. Marathon-pace mekanik extract icin yuksek risk - Run 10'da once kucuk on-step (gizmo_target_pos lambda + per-axis hit-test math hellper'a cek), sonra body extract.
- Composite + frame feedback pass (~549 satir, line ~7067): TAA history ping-pong + PrevCamBasis + prev_vp_unjittered feedback state. Aggregate tasinabilir ama once gizmo'yu temizleyince composite imzasi netlesir.
- HDR + G-Buffer render pass scaffold (~107 satir, line ~6107 sonrasi): begin_render_pass + viewport/scissor + Halton jitter math. Kucuk + bagimsiz, Run 10 N5/N6 araliginda dahil edilebilir.

**Run 10 onerilen oncelik sirasi:**
1. **N5-prep** (opsiyonel): gizmo_target_pos lambda + axis-hit-test math helper'a cek. Drag-state mutable lambdalar zor.
2. **N5**: gizmo overlay'i draw_translate_gizmo helper'ina extract. ~693 satir main() dusus. Marathon-pace mekanik risk gerceklestiyse scope-down ship + N5-followup.
3. **N6-prep**: FrameFeedback aggregate (prev_cam_basis + prev_vp_unjittered + history_states) tanit - composite imzasini kuculur.
4. **N6**: Composite pass + post-frame snapshot draw_composite_pass / capture_frame_feedback helper'larina extract. ~549 satir main() dusus.
5. **N7**: HDR + G-Buffer render pass scaffold extract (begin_render_pass setup + Halton jitter + vp_unjittered). Kucuk (~107 satir) ama main()'in son render-loop kabugu bunlar.
6. Hedef: main() body 5261 -> <500. Eger N5+N6+N7 toplami <300'e gotururse Run 10 close-out + tag bekleme.

**Library promotion candidates (Run 10 close-out trigger)**:
- cd::sample_framework: hello_engine bootstrap (Vulkan device + window + swapchain + dockspace setup) - second consumer ortaya cikinca (yeni full-engine sample yazildiginda).
- cd::editor::gizmo: translate/rotate/scale axis gizmo - editor UI consumer geldikten sonra.
- cd::ui::editor_ui: ImGui dockspace + menu bar + toolbar scaffold - second consumer trigger.

### Marathon Run 10 N5+N6+N7+N8 close-out (W8 phase313-319)

Yeni hedef: main() body <500 - Run 10 sonu 3829; <500 hedefi structural wall'a takildi (TLAS rebuild + command palette setup + material creation chain icin inline-tanimli tip lift + 50+ ref capture aggregate gerekiyor).

Pratik revizyon (N5+N6+N7+N8 sirasinda kararlasti): FrameFeedback aggregate Run 9 N4Z tavsiye etti, fakat begin_composite_pass'in iki ref parametresi (prev_cam_basis + prev_vp_unjittered) + bir bool ref (prev_vp_valid) ile yeterince netti; PrevCamBasis sadece file-scope POD'a tasindi, ayri aggregate gereksizdi. Ayni desen yine: kucuk POD'lar (GizmoState, SunLight, HelloEngineFx, PrevCamBasis, HdrSceneFrame) tek bir mega-aggregate yerine.

- N5-prep (phase313): GizmoState aggregate (14 alan: visible + GizmoMode mode + 6 drag-bookkeeping + 5 light-drag-start + 2 cross-frame) main() locallerinden anon namespace'e lift edildi. cd::editor::AxisGizmo ayri main() lokali kaldi (kendi published state surface'i var). main() body 5261 -> 5247 (-14).
- N5 (phase314): Phase 152 gizmo overlay update_and_draw_gizmo(gizmo, gizmo_state, pending_pick, selected, selected_kind, entities, lights, scene, history, log_push, vp, cam, window, frame.extent) helper'ina cikarildi. Run 9+10 marathonunun en buyuk tek extracti (~690 satir body). Imza 13 parametre ile genis ama callsite tek satir. main() body 5247 -> 4542 (-705).
- N6A (phase315): R3 phase 227 velocity G-Buffer pass velocity_gbuffer_pass<MeshFor> helper'ina cikarildi (~104 satir body). main() body 4542 -> 4443 (-99).
- N6B (phase316): R3 bloom chain run_bloom_chain helper'ina cikarildi (~123 satir body, ic run_bloom_pass lambda dahil). W4 visual baseline korundu (1.10 threshold + 0.50 knee, radius/intensity 1.0). main() body 4443 -> 4323 (-120).
- N6C (phase317): Composite + frame-feedback snapshot begin_composite_pass helper'ina cikarildi (~285 satir body). TAA history ping-pong barriers + CompositePush fill (tonemap + AO + DOF + light shafts + atmospheric fog + camera basis for SSR + prev-cam motion blur) + prev_cam_basis/prev_vp snapshot. Helper render pass'i ACAR ama KAPATMAZ - caller ImGui'yi ayni pass icinde cizip sonra cmd.end_render_pass() cagiriyor. PrevCamBasis struct lift dahil. main() body 4323 -> 4043 (-280).
- N7 (phase318): R3 HDR + 3 G-Buffer (normal/albedo/MR) + depth render pass open + Halton(2,3) jitter VP math begin_hdr_scene_pass helper'ina cikarildi (~95 satir body). HdrSceneFrame POD geri donduruyor (vp + vp_unjittered + aspect). main() body 4043 -> 3946 (-97).
- N8 (phase319): Floor quad + ECS entity primitives row (X1D parallel-prep + serial-draw) draw_floor_and_entities<MeshFor> helper'ina cikarildi (~124 satir body). kFloorY constant caller'da kalir cunku planar shadow pass de gerek duyuyor. main() body 3946 -> 3829 (-117).

**Run 10 sonuc:**
- main.cpp: 7707 -> 7854 (+147; 9 yeni anon-namespace helper + GizmoState + PrevCamBasis + HdrSceneFrame POD + GizmoMode enum).
- **main() body: 5261 -> 3829 (-1432, 27.2% azalma)**.
- 9 yeni anon-namespace helper: update_and_draw_gizmo, velocity_gbuffer_pass<>, run_bloom_chain, begin_composite_pass, begin_hdr_scene_pass, draw_floor_and_entities<>, + 3 POD/enum.
- Yeni library promotion yok (henuz second consumer yok).
- Tests 96/96 PASS her checkpoint'te.
- Engine boots clean (her commit sonrasi smoke launch dogruandi). Renderer davranisi degismedi (visual baseline korundu).

**Combined Run 9 + Run 10 sonuc (Double Marathon):**
- main.cpp: 7811 -> 7854 (+43 toplam; 16 yeni anon-namespace helper + 5 POD + 1 enum + 1 sample-local header).
- **main() body: 5938 -> 3829 (-2109, 35.5% azalma combined)**.
- 16 yeni anon-namespace helper toplamda: draw_r_showcase_panel, upload_multi_light_ubo, resolve_sun_light, fill_prim_push_shared, draw_sky_pass, draw_planar_shadows<>, draw_shadow_map_pass<>, update_and_draw_gizmo, velocity_gbuffer_pass<>, run_bloom_chain, begin_composite_pass, begin_hdr_scene_pass, draw_floor_and_entities<>.
- 1 yeni sample-local header (HelloEngineFx.hpp).
- Tests 96/96 PASS her checkpoint'te (combined 16 commits).
- Engine boots clean tum sub-phase'lerde.

**Ship edilmedi (Run 11 territory) - structural wall:**
- Command palette setup (~846 satir, line ~5753): 52 register_command lambda'si herbiri [&] capture-list ile main()-locallarini yakaliyor. Tek helper'a cikarmak icin tum captures'i state aggregate'ine lift gerekiyor (~50 ref bundle). Bu Run 11 N10-prep + N10.
- Per-frame TLAS rebuild (~182 satir, line ~7371): SkinnedRuntime + DeferredTlas struct lift gerekiyor (su anda main()-inline). N9-prep + N9.
- Material creation chain (boot, ~430 satir line ~3923): prim + velocity + shadow + sky + composite + 7 bloom material'leri art arda yaratiyor; her birinin MaterialDesc fill + create + error check var. Yine inline-tanimli formats/bindings/attrs sabit'leri lift gerek.
- glTF auto-load (~245 satir) + glTF baseColor (~250 satir): asset_streamer / asset_gltf API'leri ile entegrasyon, kucuk parca degil tek 500-satir block.

**Run 11 onerilen oncelik sirasi:**
1. **N9-prep**: SkinnedRuntime + DeferredTlas struct'larini main()-scope'tan anon namespace'e lift et.
2. **N9**: TLAS rebuild + depth ring barrier rebuild_tlas_and_transition_depth<BlasForKind> helper'ina cikar (~180 satir).
3. **N10-prep**: cd_sample::SampleAppState aggregate tanit (palette + history + log + entities + lights + scene + selected/_kind + camera + audio_state + net_state + streamer_state). Bu Run 9'da kacindigimiz HelloEngineFrame'in motive olmus hali - command palette'in 52 ref'i bunu zorunlu kiliyor.
4. **N10**: Command palette setup register_commands(palette, app_state) helper'ina cikar (~846 satir).
5. **N11**: Material creation chain spawn_materials(device, compiler, kPrimBindings, ...) -> MaterialBundle helper'ina cikar (~430 satir).
6. Hedef: main() body 3829 -> <500.

**Library promotion candidates (refreshed)**:
- cd::sample_framework: hello_engine bootstrap (Vulkan device + window + swapchain + dockspace + IBL bake + material creation) - Run 11 N11 sonrasi when MaterialBundle abstraction is settled.
- cd::editor::command_palette_registry: SampleAppState referansi alan register_commands templated callable kabul eden helper - Run 11 N10 sonrasi.
- cd::editor::gizmo (zaten kismen var, AxisGizmo): update_and_draw_gizmo signature stabilise olunca ECS-light dual-mode varyantini kutuphaneye terfi et.

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


### Marathon Run 11 N9-N12 close-out (W8 phase321-325 + Strands B/C)

Run 11 was a triple-strand marathon (Strand A extraction, Strand B clang-tidy quality, Strand C docs quality).

**Strand A extraction (phase321-325)**:

- N9-prep (phase321): SkinnedRuntime + DeferredTlas types lifted from main()-scope to cd_sample namespace in HelloSkinned.hpp + HelloTlasRing.hpp. -24 lines.
- N9 (phase322): per-frame TLAS rebuild + depth ring barrier extracted to cd_sample::rebuild_tlas_and_transition_depth (HelloTlasRebuild.hpp) as a 5-callable template so SceneEntity / PrimitiveKind / MaterialInstance never escape main.cpp anon namespace. -149 lines.
- N10 (phase323): CPU-LBS skinned-animation per-frame step (advance, sample animation, build palette, 4-weight LBS, upload deformed verts) extracted to cd_sample::update_skinned_animation (HelloSkinnedAnim.hpp). Returns bool; false drops into the W4-F fallback turntable. -100 lines.
- N11 (phase324): audio chain boot (DSP nodes prepare + meter scoreboard + 5 s WAV ring + WASAPI pre-render + voice spin-up) bundled into cd_sample::AudioState aggregate + cd_sample::init_audio (HelloAudio.hpp). 17 reference aliases preserve main()-side call sites. -74 lines.
- N12 (phase325): R1 IBL bake graph + GPU upload (X1C boot JobGraph + IBL sampler creation) extracted to cd_sample::bake_ibl_cpu + cd_sample::upload_ibl_gpu (HelloIbl.hpp). W8-AW chrome-mirror quality parameters preserved verbatim. -122 lines.

main() body progression: 3829 -> 3805 -> 3656 -> 3556 -> 3482 -> 3360.
hello_engine total: 7854 -> 7830 -> 7681 -> 7581 -> 7507 -> 7385 -> 7381.

Strand A extractions queued for Run 12: glTF auto-load (~223 lines), glTF baseColor texture (~251 lines), click-to-pick + camera-ray helpers (~161 lines), right-mouse FPS look (~110 lines), SampleAppState aggregate + command palette setup (~846 lines), material creation chain (~430 lines). Each requires aggregate-state introduction (>10 captures); per marathon scope-down rule deferred to keep Run 11 ship-able.

**Strand B clang-tidy quality (phase326-327)**:

- B1 (phase326): full clang-tidy 21.1.0 audit against samples/engine/hello_engine/main.cpp deepest-TU sweep. Categorized 38 distinct rule violations (~2150 total instances). Top rules: readability-math-missing-parentheses 910, cppcoreguidelines-macro-usage 294, hicpp-uppercase-literal-suffix 185, portability-avoid-pragma-once 122 (deliberate project policy), readability-identifier-naming 113. Bug-class signals isolated and tagged. docs/CLANG_TIDY_AUDIT_RUN11.md captures the verdict matrix.
- B2 (phase327):
  - modernize-use-scoped-lock cleaned globally (15 lock_guard sites in 9 files: AsyncStreamer, JobGraph, JobToken, Future, ThreadPool, WorkStealingThreadPool, ProfileSpan, BufferSink, ChromeTraceSink, CsvSink). Rule promoted to WarningsAsErrors in .clang-tidy.
  - bugprone-misplaced-widening-cast cleaned in cd::asset::Primitives.hpp (10 reserve sites for sphere / cone / cylinder / torus / hemisphere; widens to size_t BEFORE multiply, defending against int*int overflow).
  - .clang-tidy policy hardened: disabled portability-avoid-pragma-once + modernize-use-std-print + cppcoreguidelines-avoid-c-arrays + readability-redundant-member-init with rationale comments.

Strand B remaining bug-class rules (bugprone-implicit-widening-of-multiplication-result, bugprone-integer-division false positive, bugprone-suspicious-stringview-data-usage, bugprone-unhandled-exception-at-new, readability-misleading-indentation, cert-flp30-c float-loop) tracked as warnings; site-by-site fixes queued for Run 12. Once cleaned the rules promote to error-status too.

**Strand C docs quality (phase328+)**:

- C1: docs audit doc docs/DOC_AUDIT_RUN11.md identifying missing ADRs (W8-AJ / W8-AN / W8-AR / W8-BA / W8-BC / W8-AY / W8-AZ), zero per-library READMEs across 188 CMakeLists.txt.
- C2: 3 highest-priority W7/W8 ADRs backfilled: ADR-20260529-W8-AJ-LTC-corner-winding.md, ADR-20260529-W8-AN-Karis-MRP.md, ADR-20260529-W8-AR-ECS-attribute-PBR.md.
- C3: 6 Tier-1 READMEs shipped: engine/README.md index + foundation/{core,math,concurrency,frame_timing,log}/README.md. Run 12 queued for the remaining ~92 libraries.
- C4: this section (STATUS Section 6 refresh).
- C5: CLAUDE.md unchanged this run (Run 11 surfaced no new project-wide patterns that warrant codification beyond what already lives in CLAUDE.md). Will revisit in Run 12.

**Tests**: 96/96 PASS across every checkpoint. Zero rendering-behaviour regressions.

**Phase numbering**: 321 (N9-prep) -> 322 (N9) -> 323 (N10) -> 324 (N11) -> 325 (N12) -> 326 (B1) -> 327 (B2) -> 328 (C-batch close-out tagged as NXZ-RUN11).

---

## 7. Calisma modu (team-lead notu)

- Bu plandaki her item icin subagent dispatch ayri brief ile baslatilir (CLAUDE.md Brief Sozlesmesi).
- Bagimsiz itemler (N4 + N3 + N5; X1 + X2; L1 + L9) paralel dispatch edilir.
- BLOCKING ajan zincirleri: safety-integration (concurrency icin zorunlu), citation-verifier (her academic atif), independent-auditor (kritik claim).
- Tag yok (feedback_no_auto_tag). Phase numarasi devam: phase303 (Run 9 N3-prep oncelikli).
- Marathon kurali: kullanici uzun maraton demedigi surece her wave kullanici onayi ile kapanir.

---

### Marathon Run 12 close-out (W8 phase329-332 + Strand C2 ADR batch)

Run 12 was a triple-strand marathon focused on (A) extraction continuation, (B) substantive clang-tidy fixes per user mandate "trivial = disable / rest = fix", and (C) ADR backfill for the four W8 phases Run 11 had deferred.

**Strand B1+B2+B3 substantive clang-tidy + policy (phase329)**:

User mandate verbatim: *"fonksiyonlara ve kutuphanelere bolme beraber tum warning fixleri yap. fixler bazizlari onemisiz olabile mesela dont use do while givi yada printf return kullanma givi hatalar kapatilabilir onun disindakiler duzeltilmis olmali"* — fix the substantive, disable the trivial.

- Disabled in `.clang-tidy` with one-line rationale (Strand B1):
  - `cert-err33-c` (user verbatim "printf return")
  - `cppcoreguidelines-avoid-do-while` (user verbatim "do-while")
  - `hicpp-uppercase-literal-suffix` (style preference, mirrors readability rule)
  - `readability-braces-around-statements` (project style on hot-path math)
  - `readability-suspicious-call-argument` (FP-prone on math/shader arg shuffles)
- Fixed site-by-site in main.cpp + engine/ (Strand B2, 10 sites):
  - `bugprone-misplaced-widening-cast` (1): main.cpp:1557 spot-cone rim
  - `bugprone-unhandled-exception-at-new` (1): WorkStealingThreadPool.hpp:192 — `noexcept` spawn_detached's throwing `new` would call `std::terminate`; now uses `new(std::nothrow)` + graceful false return + coroutine counter rollback.
  - `readability-misleading-indentation` (2): Decal.hpp + Primitives.hpp — brace outer fors.
  - `bugprone-implicit-widening-of-multiplication-result` (3): Upload.hpp, EditHistory.hpp, main.cpp:698 (WAV byte-rate).
  - `bugprone-integer-division` (1): Gtao.hpp:86 — replaced `static_cast<float>(size/2)` with `size * 0.5F`.
  - `bugprone-suspicious-stringview-data-usage` (1): main.cpp:1033 — route through `std::string{sv}` before `c_str()`.
  - `cert-flp30-c` (2): IBL IrradianceConvolution outer + inner float-counter loops kept verbatim under `NOLINTNEXTLINE(cert-flp30-c)` with rationale; CLAUDE.md marathon rule "DON'T regenerate IBL bake" locks the loop count to the exact float-counter form W8-AW chrome-mirror was calibrated against.
- Promoted to WarningsAsErrors (Strand B3): all 7 above categories alongside the existing `modernize-use-scoped-lock`. Zero diagnostics in promoted categories against `main.cpp` compile_commands.json sweep.

`docs/CLANG_TIDY_AUDIT_RUN11.md` extended with the Run 12 phase B1+B3 close-out section listing every disable rationale + every fix site + the WarningsAsErrors list.

**Strand C2 four W8 ADRs backfilled (phase330)**:

Iglberger-format ADRs for the four W8 phases identified by Run 11 docs audit:

- `ADR-20260529-W8-AY-bake-budget-revert.md`: IBL bake budget revert from W8-AX 256² to W8-AV 128² (analytic-sky source had zero high-freq content; 4x boot cost for zero visible gain). First env-spec composite gate.
- `ADR-20260529-W8-AZ-env-spec-sun-gate.md`: tighten the composite gate to strict `clamp(sun_dir.w, 0, 1)`. Stale sun-baked cube would otherwise ghost-reflect when sun toggled off.
- `ADR-20260529-W8-BA-RT-reflection-occlusion.md`: closest-hit RT walk along the spec reflection direction; on hit, scale sky toward 0. Roughness-attenuated so mirror chrome shows silhouettes while rough metal keeps the IBL. ~0.4 ms GPU on RTX 3060 Ti class.
- `ADR-20260529-W8-BC-per-instance-albedo-SSBO.md`: per-frame 256-slot SSBO of `(albedo, emissive)` keyed by TLAS instance ID. PBR branch reads `rayQueryGetIntersectionInstanceIdEXT` and indexes the SSBO so the W8-BA silhouette shades to neighbour albedo. Same ray cost, coloured reflections. The "instance ID == SSBO slot" invariant is load-bearing; phase293 N1E extract co-located both halves of the contract in `HelloRayQuery.hpp` precisely for review.

**Strand A1+A7 extraction continuation (phase331-332)**:

- N13 (phase331): `cd_sample::SampleAppState` aggregate landing — bundles 18 free-look camera + pick locals into `FreeLookState` + `PickRequest` substructs. Name-aliases preserve every existing call site (input handlers, palette callbacks, gizmo overlay, picker). Header design note explicitly rejects the kitchen-sink god-object pattern; aggregate is scoped to the cohesive camera+input+pick cluster only. main.cpp 7381 -> 7388 (header overhead net), main() body 3360 -> 3350 (-10 from local declarations consolidated).
- N14 (phase332, Strand A7): WASD + right-mouse-look + scene_cam auto-orbit per-frame tick (~84 inline lines) lifted into `cd_sample::update_free_look_camera()` in `HelloAppState.hpp`. Preserves the `wasd_was_active_prev` function-local-static latch and the W6-F shift x2.5 / ctrl x0.25 modifiers bit-for-bit. Required a parallel touch in the picker block to recompute `wasd_active` locally (previously shared with the camera tick). main.cpp 7388 -> 7313 (-75), main() body 3350 -> 3275 (-75).

main() body progression Run 12: 3360 -> 3350 (N13) -> 3275 (N14). Net -85 lines (-2.5%).

**Strand A deferred to Run 13**: N15 materials extract (~430 lines), N16 glTF auto-load (~223 lines), N17 glTF baseColor (~251 lines), N18 picker extract (~161 lines), N19 command palette setup (~846 lines), main()<500 target. The deferred extracts each carry higher risk (deeper main()-scope dependencies — material handles passed to dozens of descriptor writes; palette captures every state by reference) and need their own dedicated focused aggregates (HelloMaterials, HelloPalette). The N13 SampleAppState foundation is in place for the picker path; future runs build on it.

**Strand C3 STATUS refresh**: this section.

**Tests**: 96/96 PASS at every Run 12 checkpoint. Zero rendering-behaviour regressions. IBL bake parameters preserved verbatim (Run 12 cert-flp30-c sites preserved under NOLINTNEXTLINE with rationale rather than algorithmic rewrite that would change `n_samples` per output texel).

**Phase numbering**: 329 (B1+B2+B3) -> 330 (C2 four ADRs) -> 331 (A1 N13 SampleAppState) -> 332 (A7 N14 free-look camera extract).

### Marathon Run 16 NF1+NF2 close-out (Marathon FINAL extraction round, phase347-348)



**Mandate**: user verbatim "bu fix hello main librarylere bulma isi cok uzadi bu turda hepsini bitir ve sonra gelecek gelistirmelere bakalim" (close out the hello-main library-split work this round; no more incremental deferrals).



**Two extractions shipped this round**:



- **NF1 (phase347)**: `cd_sample::HelloMeshes` aggregate + `boot_meshes()` factory + `destroy_meshes()` teardown landed in `samples/engine/hello_engine/HelloMeshes.hpp` (220 lines). Bundles all 8 procedural CPU meshes + matching per-mesh BLAS + glTF auto-load result + SkinnedRuntime + has_gltf_texture flag. boot_meshes() consolidates the previously-inline make_*/flatten_white/upload_mesh boilerplate + try_auto_load_gltf chain + per-mesh build_blas + the one-shot AS-build command-buffer submit. destroy_meshes() factors the matching cleanup (7 destroy_mesh + 7 destroy_acceleration_structure + gltf teardown). main.cpp 6522 -> 6395 (-127).



- **NF2 (phase348)**: `cd_sample::register_engine_gi_rhi_fx_palette_commands` free function landed in `samples/engine/hello_engine/HelloEnginePalette.hpp` (258 lines). Pulls 21 lambda-bodied palette.register_command() calls (IDs 110/111/112/113 ReSTIR/DDGI/NRC GI toggles + 120 RHI parity + 130 physics + 131 script + 140 v2.0 milestone + 80-93 engineering FX toggles) out of main(). Closure surface: palette + HelloEngineFx& + log_push functor + post_gtao::Settings + post_bloom::Settings (5 typed references). main.cpp 6395 -> 6187 (-208).



**Net Run 16**: main.cpp 6522 -> 6187 (-335 / 5.1%). main() body 2479 -> 2143 (-336 / 13.5%).



**Cumulative Run 7 -> Run 16**: main() body 7147 -> 2143 (-5004 / 70.0%). main.cpp 7800 -> 6187 (-1613 / 20.7%).



**Marathon extraction phase DECLARED DONE** (HONEST close-out per mandate).



Hard truth: the <500 main() body target stated in the Run 16 brief is **mechanically unreachable** without a `cd::sample_framework` library redesign. The remaining 2143 lines decompose as:



- ~620 lines: boot-time resource allocation (Window/Vulkan device/Renderer + ImGui + Shader + Materials + RenderTargets + Shadow + Multi-light UBO + IBL textures + glTF baseColor + Bloom mips). Each block touches ~10-20 main()-scope locals that the downstream frame loop and ImGui panels close over.

- ~520 lines: scene-mutation palette commands (Edit/Select/Transform/Scene-save/load) that close over file-local `SceneEntity` + `LightRow` + `PrimitiveKind` + `kind_name` + `kind_from_name` + entities vector + lights vector + history + scene + world. Lifting these types to a public header is a separate refactor.

- ~730 lines: frame-loop body that closes over **80+** main()-scope locals (every resource, every state struct, every settings POD, every CCT slider, every camera basis, every ImGui panel). Extracting this requires either a god-object `FrameLoopCtx` aggregate (~80 reference fields) or a real `cd::sample_framework` library that owns the lifecycle.

- ~150 lines: cd::light demo (5-entry lights vector + cluster grid) entangled with downstream Lights ImGui panel.

- ~120 lines: AsyncStreamer + Random viz + Counter table + Command palette + FX state declarations.

- ~60 lines: cleanup



**What this means in practice**:



- The hello_engine sample reached the natural ceiling of *mechanical* extract-and-replace refactoring. Every remaining inline block has 8+ closure dependencies that cannot be deduplicated without (a) lifting file-local types, OR (b) introducing a fat reference-bundle aggregate, OR (c) building a real sample framework library.

- All three paths are **architectural redesign**, not mechanical extraction. They belong to a separate planned milestone (queued in gap table as "cd::sample_framework redesign" under Medium severity).

- The 5004-line cumulative reduction (70% of original main() body) over 10 marathon runs is the upper bound this approach can deliver.



**Tests**: 96/96 PASS at NF1 + NF2 checkpoints. Zero rendering-behaviour regressions. Same IBL bake parameters, same pixel-for-pixel output.



**Build**: clang-cl ninja-debug clean at every commit. 0 warnings under -Werror with the project warning set (Wshadow + Wconversion + Wsign-conversion + Wold-style-cast + ...).



**New files**:

- `samples/engine/hello_engine/HelloMeshes.hpp` (220 lines)

- `samples/engine/hello_engine/HelloEnginePalette.hpp` (258 lines)



**Phase numbering**: 347 (NF1) -> 348 (NF2) -> 349 (NFZ STATUS refresh + close-out).



**Recommended NEXT topics** (user verbatim "sonra gelecek gelistirmelere bakalim"):

1. **X4 D3D12 parity** (Major severity, 3-4 weeks per gap table) — production-grade cross-API story; currently Vulkan-only de facto. The hello_engine groundwork (HelloMaterials + HelloRenderTargets + HelloIbl) is RHI-agnostic.

2. **X5 shader on-disk hot-reload** (Minor, 1 week) — leverages already-shipped cd::vfs + cd::shader::Compiler; would let renderer-side iteration happen without app restart.

3. **X1-FU-F secondary command buffers** (RHI rev) — true parallel command recording; unblocks real-multithread rendering claim. ADR-20260528 follow-up.

4. **Demir Kural academic pilot** — first three PDFs to MANIFEST.csv: Karis 2013 (UE4 specular), Heitz 2016 (LTC area lights), Frisvad 2012 (orthonormal basis). All three are already cited in shader source comments; MANIFEST closure is overdue.

5. **Tier-2/3 README backfill** — Run 11 Strand C shipped 6 Tier-1 + Run 13/14 C1+C2 shipped 22 more; ~70 libraries remain undocumented at README level.

6. **cd::sample_framework architectural design** (Medium, post-extraction follow-up) — the only path to push hello_engine main() body further below ~2100 lines. Scope: lifecycle-owning library that hosts the boot+frame-loop scaffold so sample main() becomes pure scene authoring.
