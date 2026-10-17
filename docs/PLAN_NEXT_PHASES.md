# CHROMODYNAMIC — Birleşik 10-Fazlı Master Plan (Zero-Bug, Özgün Element/Vessel Mimarisi, `cd::ui` & Showcase Editörü)

> **Son Güncelleme:** 2026-10-14 (Gemini 4 Argon High — Otonom Maraton Yürütme Planı)
> **Durum:** AKTİF — Kesintisiz Faz/Dalga (Wave) İlerlemesi (`verify_wave.ps1` + `checkpoint_guard.py`)
> **Süreklilik Kuralı (Auto-Continue):** Oturum veya kullanım hakkı sıfırlansa bile, yeni oturum başladığında bu dosyadaki (`docs/PLAN_NEXT_PHASES.md`) ilk `[ ]` işaretli dalgadan (Wave) sormadan doğrudan devam edilir.

---

## 0. Mimari Vizyon & Tasarım İlkeleri

1. **Parça Parça Kütüphane DNA'sı ("Pay-Only-For-What-You-Use" `cd_<lib>`)**:
   - Hiçbir zaman her şeyi birbirine bağlayan monolitik bir üst kütüphane kurulmaz. Her özellik ve sahne yapıtaşı bağımsız bir `cd_<lib>` CMake hedefi olarak yaşar, kendi birim testine (`tests/`) sahiptir ve katı yukarı-yönlü DAG (`check_layer_dag.py`) kurallarına uyar.
2. **Meşhur Motorların Gücünü Özgün CHROMODYNAMIC Kimliğinde Eritmek**:
   - **Godot'nun hafifliği ve sahne ağacı ergonomisi** + **Unreal Engine'in derin oynanış/render çatısı** + **Bevy/EnTT'nin veri yönelimli (DOD) hızı** + **Filament/bgfx'in modüler FrameGraph/RHI yapısı** tek potada eritilir.
   - **%100 Özgün CHROMODYNAMIC Terminolojisi** (başka motorlardan kopya `Node`, `Pawn`, `Actor`, `GameMode`, `WorldEnvironment` isimleri kullanılmaz):
     - **Sahne Yapıtaşları (`cd::element_core`, `cd::stage`)**: `Element` (taban yapıtaşı), `Planar2D` (2B uzamsal taban), `Spatial3D` (3B uzamsal taban), `Stage` (sahne/seviye konteyneri `.cdstage`), `Pulse<Args...>` (tip-güvenli sinyal/olay hattı).
     - **Oynanış & Kontrol Çatısı (`cd::gameplay_vessel`, `cd::gameplay_pilot`, `cd::gameplay_director`)**:
       - **`Vessel`**: Bir sürücü/beyin (`Pilot`) tarafından içine girilip yönetilebilen (`embark` / `disembark`) her türlü fiziksel gövde.
       - **`Avatar` (`: public Vessel`)**: Jolt kapsül fiziği, `Locomotion` (yürüme/koşma/zıplama/eğilme), `RiggedMesh3D`, `AnimGraph`, `IkRig` ve kamera kolu içeren insansı/karakter gövdesi.
       - **`RoverVessel` (`: public Vessel`)**: 4+ tekerlekli, süspansiyon ve vites fiziğine sahip sürülebilir araç gövdesi.
       - **`Pilot` $\rightarrow$ `HumanPilot` & `SynthPilot`**: Bir `Vessel`'ı oyuncu girdileriyle (`ActionMap` + `CameraBrain`) süren `HumanPilot` ve aynı `Vessel`'ı Behavior Tree, FSM, NavMesh ve `Squad` taktikleriyle süren yapay zeka beyni `SynthPilot`.
       - **`StageDirector` & `WorldLedger`**: Oyun kurallarını, doğma noktalarını (`SpawnBeacon3D`), `AiDirector` temposunu, görevleri ve kayıt durumunu yöneten otorite.
     - **Görsel, Işık, Atmosfer ve Optik Elementleri**:
       - `cd::elements_visual`: `SurfaceMesh3D`, `RiggedMesh3D`, `SwarmMesh3D` (GPU instanced), `ProjectionDecal3D`, `SparkSwarm3D` (GPU particles).
       - `cd::elements_radiance`: `SolarLight3D` (Directional + CSM/PCSS/Contact), `RadialLight3D` (Omni/Point), `ConeLight3D` (Spot + IES), `PanelLight3D` (LTC Area), `RadianceProbe3D` (IBL), `IrradianceField3D` (DDGI / ReSTIR GI).
       - `cd::elements_aether`: `CelestialDome3D` (Bruneton Sky + Aerial Perspective), `AetherFog3D` (GPU Froxel Volumetric Fog), `FogPocket3D` (lokal sis hacmi), `CloudStrata3D` (Volumetric Clouds), `SunBeams3D` (Light Shafts).
       - `cd::elements_optics`: `OpticsVolume3D` (Fiziksel Kamera ISO/Diyafram, Auto-Exposure, Bloom, GTAO, SSR, TAA/SMAA, Bokeh DoF, MotionBlur, AgX/ACES Tonemap + 3D LUT — global veya öncelikli bölgesel hacim).
       - `cd::elements_nature`: `TerraClipmap3D` (GPU Clipmap Terrain + Foliage), `OceanWave3D` (FFT Su/Okyanus).
       - `cd::elements_dynamics`: `AnchorBody3D` (Static), `KineticBody3D` (Rigid), `ClothSurface3D` (SoftBody), `ShatterMesh3D` (Voronoi yıkım), `RagdollChain3D`, `SensorVolume3D` (Trigger), `CollisionHull3D`, `RayProbe3D`.
       - `cd::elements_acoustics` & `cd::elements_wayfinding`: `SoundSource3D` (MIT KEMAR HRTF + Doppler), `AudioEar3D`, `EchoChamber3D` (Reverb/DSP), `NavMeshZone3D`, `WayfinderAgent3D` (A* + RVO), `SplineTrack3D`.
       - `cd::elements_planar`: `QuadSprite2D`, `Flipbook2D`, `TileCanvas2D`, `Lens2D`, `KineticBody2D`, `SensorZone2D`, `RadialLight2D`.
       - `cd::elements_ui`: `HudLayer`, `UiFrame`, `UiCommandButton`, `UiValueSlider`, `UiVectorBox`, `UiGlyphText`, `UiGaugeBar`, `UiNodeCanvas`, `UiDockWorkspace`.
3. **Grafik Motoru (`cd::rhi`) Tabanlı Özelleştirilebilir UI Kütüphanesi (`cd::ui::*`)**:
   - Hem bağımsız bir C++23 UI kütüphanesi, hem oyun içi HUD/Menü sistemi hem de Dear ImGui olmadan `CHROMODYNAMIC Editor`'ü çizen GPU hızlandırmalı UI motoru.
4. **Sıfır Sample Kodu (`samples/` Klasörünün Kaldırılması) & Editör-İçi Mini Showcase Projeleri (`showcases/*.cdproj`)**:
   - `samples/engine/hello_engine` (9.685 satır + 33 `Hello*.hpp`) içindeki tüm işlevsel yapılar sökülerek gerçek motor modüllerine (`SceneRenderer` Feature Pass'ları + `Element`/`Vessel` sınıfları) dönüştürülür.
   - Tüm eski sample'lar `CHROMODYNAMIC Editor` içinde açılıp düzenlenebilen ve **Play-In-Editor (PIE)** ile oynanabilen 12 adet `.cdproj` Mini Showcase Projesine dönüştürülür; ardından `samples/` klasörü tamamen kaldırılır.

---

## FAZ 1 — Sıfır-Bug Zemin, Kontrat Tamiri, Tekilleştirme ve Hijyen
*(Kaynak: Eski `PLAN_NEXT_PHASES Faz 0` + `PLAN_2026-06-11_GAPS_AND_ROADMAP`)*

- [x] **Wave 1.1 — WebGPU (`WgpuContract.hpp`) & Bilinen Mikro-Bug Düzeltmeleri**:
  - `engine/ui/renderer_webgpu/include/cd/ui/renderer_webgpu/WgpuContract.hpp`: `texture_format::*` ve `vertex_format::*` sayısal sabitlerinin gerçek Dawn `wgpu::TextureFormat` / `wgpu::VertexFormat` değerleriyle eşitlenmesi ve testleri (`Faz 0.1`).
  - `engine/world/particle_system`: Compaction sırasının `integrate + expire` sonrasına taşınması (ölen parçacığın 1 tick fazladan yaşama bug'ı — `Faz 0.4`).
  - `engine/asset/texture_streamer`: Başarısız allocation durumunda LRU bütçe koruması (`enforce_budget` sırası) + negatif birim testi (`T37`, `Faz 0.4`).
- [x] **Wave 1.2 — 3x Froxel Işık Kümeleme Tekilleştirmesi (`GAPS §3.1`)**:
  - `cd::cluster` kanonik froxel grid yapılarak `cd::lighting_clusters` ve `cd::light::ClusterGrid` içindeki kopya kümeleme matematiklerinin `cd::cluster` üzerinden delege edilmesi (`check_layer_dag.py` sıfır ihlal).
- [x] **Wave 1.3 — RHI Başlık Hijyeni & CTest %100 Tam Paket Denetimi (`Faz 0.2–0.5` & `GAPS §1`)**:
  - `cd::rhi` ve alt backend (`rhi_vulkan`, `rhi_d3d12`, `rhi_metal`, `rhi_opengl`, `rhi_webgpu`) başlıklarındaki eski *"boot-only / 50 virtuals stubbed"* yorumlarının gerçek %100 durumla güncellenmesi.
  - `build/ninja-base/bin/*_tests.exe` altındaki tüm test ikililerinin `ctest` içinde kayıtlı ve koşulsuz yeşil olduğunun doğrulanması.

---

## FAZ 2 — Grafik Motoru (`cd::rhi`) Tabanlı Özelleştirilebilir UI Kütüphanesi (`cd::ui::*`)
*(Kaynak: `ADR-20260530-ui-widget-library` Faz 1–5 + `PRODUCTION_V20_PLAN v2.0.5`)*

- [x] **Wave 2.1 — GPU UI Renderer & 6 Materyal Varyantı (`cd::ui::renderer` + `cd::ui::renderer_rhi` + `cd::material::UiVariant`)**:
  - `Submitter` içinde çoklu doku/atlas (`Multi-Atlas / Bindless Texture`) desteği ve `kSubmitterPipelineReady` kapısının tamamen açılması.
  - 6 GPU UI Materyali: (1) `Solid & SDF Rounded-Rect/Border`, (2) `Textured / Sprite / 9-Patch / Viewport RenderTarget`, (3) `MSDF / Subpixel Font Glyph`, (4) `Linear & Radial Gradient`, (5) `Backdrop Frosted-Glass Blur`, (6) `Elevation Drop-Shadow`.
- [x] **Wave 2.2 — FreeType + HarfBuzz + MSDF Font & Lokalizasyon (`cd::ui::font` P4.5 + `v2.0.5`)**:
  - MSDF + Kerning + Türkçe diakritik + Unicode + BiDi (sağdan-sola RTL layout flip) metin şekillendirme ve dil değişiminde (`cd::game::l10n`) arka planda takılmasız atlas yenileme.
- [x] **Wave 2.3 — Layout, Input, Yay Animasyonları, Tema (`ThemeV2`) ve Erişilebilirlik (`cd::ui::layout`, `input`, `animation`, `theme`, `a11y`)**:
  - `Flex` + `Grid` + `Anchor` + `Constraint` çözücüleri; Hit-test, Focus ring, Tab gezintisi, Modal yakalama, Drag & Drop, Gamepad UI navigasyonu, `CubicBezier` + `Spring` yay animasyonları ve yüksek kontrast / ekran okuyucu (`a11y`) kancaları.
- [x] **Wave 2.4 — Tam Widget Kataloğu (`cd::ui::widgets` — Editör + Oyun HUD)**:
  - Temel: `Button`, `IconButton`, `Toggle`, `Checkbox`, `Radio`, `Slider`, `NumericDragBox` (renk kodlu X/Y/Z `Vec3` editörü), `TextInput`, `Dropdown`.
  - Editör: `DockSpace` + `Splitter` + `PopoutDock`, `ScrollArea`, `VirtualListView`, `TreeView` (sürükle-bırak ebeveyn değiştirme), `PropertyGrid`, `MenuBar`, `ContextMenu`, `Modal`, `Toast`, `Tooltip`, `ColorPicker` (HSV/HDR), `CurveEditor`, `NodeGraphCanvas` (yakınlaştırılabilir düğüm-kablo tuvali), `TimelineScrubber`, `FlameChart` / `Sparkline`.
  - Oyun İçi HUD & Bildirimsel Şema: Retained + Immediate-mode HUD yardımcıları (`HealthBar`, `Minimap`, `Crosshair`, `InventoryGrid`) ve `.cdui` hot-reload.

---

## FAZ 3 — Shader Altyapısı (`cd::gluon`), Hitch-Free Hot-Reload ve VFS/Plugin Reload
*(Kaynak: `PLAN_NEXT_PHASES Faz 2.1` + `GAPS §3.2` + `PRODUCTION_V20_PLAN v2.0.6`)*

- [x] **Wave 3.1 — Hitch-Free Pipeline Hot-Reload (X5 Adım 3–4)**:
  - `wait_idle()` bloklamasını kaldıran frame-fence tabanlı `DeferredDestroyQueue` + `#include` bağımlılık grafiği (`IncludeDependencyTracker`) ile ortak `.glsl` değiştiğinde bağlı tüm shader'ların otomatik derlenmesi.
- [x] **Wave 3.2 — 76 Gömülü GLSL String'inin `cd::gluon` Altında Tekilleştirilmesi (`SL-D`)**:
  - Dağınık `kGlsl*` string'lerindeki ortak PBR, BRDF, Tonemap, Octahedral ve Froxel fonksiyonlarının `cd::gluon::ShaderLibrary` modüllerine taşınması.
- [x] **Wave 3.3 — VFS Asset & Editör Plugin DLL Hot-Reload (`v2.0.6` & `ADR-017`)**:
  - `cd::vfs` + `AsyncStreamer` üzerinden `.cdpak` / glTF / KTX2 / Lua canlı yenileme ve editör `PluginHost` DLL/`.so` sıcak yeniden yükleme.

---

## FAZ 4 — Fotogerçekçi & GPU-Driven Render Pipeline Birleştirmesi
*(Kaynak: `PLAN_NEXT_PHASES Faz 2.2` + `REALISM_ROADMAP Tier 1–4` + `ROADMAP_PHASE_2 Faz 8–9`)*

- [x] **Wave 4.1 — GPU-Driven Geometri & Sıkıştırılmış Doku Akışı (`REALISM Tier 1`)**:
  - `DrawIndexedIndirectCount` + GPU Frustum & Hi-Z Occlusion Culling (`cd::visibility`, `cd::virtual_geometry` meshlet DAG, `cd::mesh_shader`) + KTX2/BC7/ASTC `cd::staging` doku akışı.
- [x] **Wave 4.2 — Fiziksel Işık, Gölge, Atmosfer ve Hacimsel Efektler (`REALISM Tier 2` + `Faz 2.2`)**:
  - Lumen/Lux/Kelvin ışık birimleri, IES profilleri, LTC alan ışıkları (`cd::brdf_ltc`), PCSS + Contact Shadows.
  - Canlı FrameGraph pass bağlantıları: `cd::atmosphere` (Transmittance/Multi-Scatter/Sky-View + Aerial Perspective 3D LUT), `cd::volumetric_fog` (GPU Froxel Fog), `cd::volumetric_clouds`, `cd::light_shafts`.
- [x] **Wave 4.3 — Küresel Aydınlatma (GI), Yansıma ve Denoiser (`REALISM Tier 3.5` + `Faz 2.2`)**:
  - Canlı FrameGraph pass bağlantıları: `cd::post_gtao`, `cd::post_ssr`, `cd::ddgi`, `cd::restir_di`, `cd::restir_gi`, `cd::nrc`, `cd::denoise` (À-Trous SVGF) ve `cd::ibl_gpu`.
- [x] **Wave 4.4 — İleri Materyal, Su/Arazi ve Kamera PostFX Zinciri (`REALISM Tier 3.1–3.4 & Tier 4`)**:
  - `cd::brdf_sss` (deri), Kajiya-Kay Saç/Kürk, `cd::brdf_sheen_clearcoat`, POM, `cd::decal` (Deferred Decals), `cd::water` (FFT Okyanus), `cd::terrain` (GPU Clipmap Arazi + Foliage), `cd::gpu_particles`.
  - Tam Kamera & PostFX Hattı: `cd::velocity` $\rightarrow$ `cd::post_taa` / `cd::post_smaa` $\rightarrow$ `cd::post_camera` $\rightarrow$ `cd::post_exposure` $\rightarrow$ `cd::post_bloom` $\rightarrow$ `cd::post_dof` $\rightarrow$ `cd::post_motion_blur` $\rightarrow$ `cd::post_composite` (ACEScg / AgX + 3D LUT).

---

## FAZ 5 — Özgün CHROMODYNAMIC Sahne Elementleri ve Oynanış Çatısı (`Element / Stage / Vessel / Pilot`)
*(Kaynak: `ROADMAP_PHASE_2 Faz 10, 12, 13` + `PRODUCTION_V20_PLAN v2.0.4` + Özgün Mimari)*

- [x] **Wave 5.1 — Çekirdek Element Ağacı, Sinyal (`Pulse`) ve Reflection Sözleşmesi (`cd::element_core`, `cd::stage`)**:
  - `Element` (UUID, `on_mount`, `on_tick(dt)`, `on_fixed_tick(fixed_dt)`, `Pulse<Args...>`, otomatik Inspector `reflect_properties` şeması) $\rightarrow$ `Planar2D` ve `Spatial3D` (`LocalTransform`, `WorldTransform`, `Mobility`) + `Stage` (`.cdstage` serileştirme ve prefab iç-içe geçme).
- [x] **Wave 5.2 — Modüler Domain Element Kütüphaneleri (`cd::elements_*`)**:
  - `cd::elements_visual`: `SurfaceMesh3D`, `RiggedMesh3D`, `SwarmMesh3D`, `ProjectionDecal3D`, `SparkSwarm3D`, `CameraLens3D`, `SpringArm3D`.
  - `cd::elements_radiance`: `SolarLight3D`, `RadialLight3D`, `ConeLight3D`, `PanelLight3D`, `RadianceProbe3D`, `IrradianceField3D`.
  - `cd::elements_aether` & `cd::elements_optics`: `CelestialDome3D`, `AetherFog3D`, `FogPocket3D`, `CloudStrata3D`, `SunBeams3D`, `OpticsVolume3D`.
  - `cd::elements_nature`: `TerraClipmap3D`, `OceanWave3D`.
  - `cd::elements_dynamics`: `AnchorBody3D`, `KineticBody3D`, `ClothSurface3D`, `ShatterMesh3D`, `RagdollChain3D`, `SensorVolume3D`, `CollisionHull3D`, `RayProbe3D`.
  - `cd::elements_acoustics` & `cd::elements_wayfinding`: `SoundSource3D` (MIT KEMAR HRTF + Doppler), `AudioEar3D`, `EchoChamber3D`, `NavMeshZone3D`, `WayfinderAgent3D`, `SplineTrack3D`.
  - `cd::elements_planar` & `cd::elements_ui`: `QuadSprite2D`, `Flipbook2D`, `TileCanvas2D`, `Lens2D`, `KineticBody2D`, `SensorZone2D`, `RadialLight2D`, `HudLayer`, `UiFrame`, `UiCommandButton`, `UiValueSlider`, `UiVectorBox`, `UiGlyphText`, `UiGaugeBar`.
- [x] **Wave 5.3 — Oynanış Çatısı (`cd::gameplay_vessel`, `cd::gameplay_pilot`, `cd::gameplay_director`)**:
  - `Vessel` (`embark` / `disembark`) $\rightarrow$ `Avatar` (Jolt kapsül + `Locomotion` + `RiggedMesh3D` + `AnimGraph` + `IkRig` + `SpringArm3D` + `CameraLens3D`) ve `RoverVessel` (4+ tekerlekli araç).
  - `Pilot` $\rightarrow$ `HumanPilot` (`ActionMap` + `CameraBrain` + `InputRecorder` + `HudLayer`) ve `SynthPilot` (`BehaviorTree` + `StateMachine` + `WayfinderAgent3D` + `SquadCoordinator`).
  - `StageDirector`, `WorldLedger`, `SpawnBeacon3D`, `AiDirectorElement`, `QuestLedgerElement`, `DialogueElement`, `CutsceneElement`, `LuaBehaviorElement`, `NetSyncElement`.

---

## FAZ 6 — Sample Kodlarının (`hello_engine` 33 `Hello*.hpp`) Sökümü ve Birleşik `EngineLoop` / `cd_player`

- [x] **Wave 6.1 — `hello_engine` (9.685 satır) ve Diğer Sample Yapılarının Motor Kütüphanelerine Sökümü**:
  - `samples/engine/hello_engine/Hello*.hpp` içindeki tüm çalışan aydınlatma, atmosfer/sis, postfx, DDGI/rayquery, glTF/skinned anim, ses, fizik ve oynanış kurulumlarının Faz 4'teki `SceneRenderer` Feature Pass'larına ve Faz 5'teki `Element`/`Vessel` sınıflarına taşınması.
- [x] **Wave 6.2 — Deterministik `EngineLoop` Orkestratörü (`cd::runtime`) ve `apps/player` (`cd_player.exe`)**:
  - Standart kare döngüsü: `Input -> Pilot -> FixedTick (Vessel/Jolt/SensorVolume/NetReconcile) -> Tick (Stage/Lua/AnimGraph/IK/SynthPilot/Cutscene) -> Async Streaming -> SceneRenderer -> HRTF Audio -> cd::ui Render`.
  - Herhangi bir `.cdproj` / `.cdstage` projesini doğrudan çalıştıran bağımsız `cd_player` yürütücüsü.

---

## FAZ 7 — %100 `cd::ui` Tabanlı Tam Teşekküllü Editör (`apps/editor`) ve Play-In-Editor (PIE)
*(Kaynak: `ADR-20260530-editor-binary` + `ROADMAP_PHASE_2 Faz 11` + `PRODUCTION_V20_PLAN v2.0.2`)*

- [x] **Wave 7.1 — %100 `cd::ui::*` Tabanlı Editör Kabuğu (Dear ImGui Bağımlılığının Kaldırılması)**:
  - 27 editör panelinin tamamının Faz 2'deki kendi `cd::ui::renderer_rhi` + `cd::ui::widgets` (`DockSpace`, `Splitter`, `TreeView`, `PropertyGrid`, `NodeGraphCanvas`, `CurveEditor`, `ColorPicker`) altyapısıyla çalışması.
- [x] **Wave 7.2 — "+ Add Element" Kataloğu, Dinamik Inspector, 3D/2D Viewport Gizmo ve Arazi/Bitki Fırçası**:
  - Sahneye tek tıkla `AetherFog3D`, `Avatar`, `SolarLight3D`, `OpticsVolume3D`, `RoverVessel`, `TerraClipmap3D`, `UiCommandButton` ekleme; Inspector'da canlı düzenleme; GPU/Ray nesne seçimi, Translate/Rotate/Scale + Snapping Gizmo ve Undo/Redo (`EditHistory`).
- [x] **Wave 7.3 — Play-In-Editor (PIE), Görsel Graf Editörleri & GPU Frame Profiler (`v2.0.2`)**:
  - **Play / Pause / Step / Stop (PIE)**: `.cdstage` snapshot'ı alıp editör içinde `HumanPilot` + `Avatar`/`RoverVessel` ile canlı oynama ve Stop ile sahneyi geri yükleme.
  - Görsel Düğüm Editörleri (`BehaviorDesigner`, `DialogTreeEditor`, `Animator` + `IkChainEditor`, `MaterialEditor`) ve `VK_KHR_timestamp_query` / D3D12 GPU zaman damgalı **Frame Profiler** (`chrome://tracing` JSON dışa aktarımlı).

---

## FAZ 8 — `samples/` Klasörünün Komple Kaldırılması ve 12 Editör Mini Showcase Projesine (`showcases/*.cdproj`) Dönüştürülmesi

- [x] **Wave 8.1 — Editör Project Hub İçinden Açılan 12 Mini Showcase Projesi (`showcases/`)**:
  1. `01_pbr_materials_sss_hair`: 5x5 PBR küreler, Clearcoat, Sheen, SSS deri, Kajiya-Kay saç, `RadianceProbe3D`.
  2. `02_celestial_aether_fog_clouds`: `CelestialDome3D`, `AetherFog3D`, `FogPocket3D`, `CloudStrata3D`, `SunBeams3D`, gece-gündüz döngüsü.
  3. `03_sponza_clustered_ddgi_restir`: Sponza/Bistro, 256 dinamik ışık, `IrradianceField3D` (DDGI + ReSTIR + NRC), PCSS/Contact gölgeler.
  4. `04_optics_cinematic_studio`: `OpticsVolume3D` (GTAO/SSR/TAA/DoF/Bloom/MotionBlur/AgX), `CutsceneElement`, `CameraLens3D`.
  5. `05_avatar_locomotion_anim_ik`: Oynanabilir 3. şahıs `Avatar`, `HumanPilot`, CesiumMan `RiggedMesh3D`, `AnimGraph`, `IkRig` ayak basma.
  6. `06_jolt_dynamics_rover_shatter`: `KineticBody3D`, sürülebilir `RoverVessel`, `RagdollChain3D`, `ClothSurface3D`, `ShatterMesh3D` Voronoi yıkım.
  7. `07_terra_ocean_nature_world`: `TerraClipmap3D` arazi, `OceanWave3D` FFT okyanus, `SwarmMesh3D` bitki örtüsü, asenkron streaming.
  8. `08_synth_pilot_squad_narrative`: `SynthPilot`, `NavMeshZone3D` + `WayfinderAgent3D` (A* + RVO), `Squad`, `BehaviorTree`, `DialogueElement`, `AiDirectorElement`.
  9. `09_acoustics_hrtf_echo_chamber`: MIT KEMAR `HRTF` 3D ses (`SoundSource3D`), `EchoChamber3D` reverb odaları, Doppler, canlı DSP mikser zinciri.
  10. `10_spark_swarm_and_decals`: `SparkSwarm3D` (derinlik çarpışmalı GPU parçacık) ve `ProjectionDecal3D`.
  11. `11_custom_ui_hud_and_menus`: Kendi `cd::ui::*` kütüphanemizle yapılmış oyun içi `HudLayer`, sağlık barı, minimap, envanter, yay animasyonlu ayarlar menüsü ve çoklu dil (TR/EN/RTL).
  12. `12_planar_2d_platformer_net`: `Planar2D`, `QuadSprite2D`, `Flipbook2D`, `TileCanvas2D`, `RadialLight2D` + 2 oyunculu `NetSyncElement` loopback.
- [x] **Wave 8.2 — `samples/` Klasörünün Silinmesi**:
  - `hello_stress` $\rightarrow$ `tests/stress/cd_stress` ve `bench_archetype` $\rightarrow$ `tools/bench/` altına taşınır; RHI duman testleri `engine/render/rhi/tests/` içine katlanır ve `samples/` dizini (`add_subdirectory(samples)`) depodan tamamen silinir.

---

## FAZ 9 — Çapraz Platform RHI Paritesi ve Mobil/Web Hedefleri
*(Kaynak: `PLAN_NEXT_PHASES Faz 1` + `MARATHON_PLAN_NEXT Tier 0.1 & Tier 4` + `ROADMAP_PHASE_2 Faz 14`)*

- [x] **Wave 9.1 — macOS / iOS Metal RHI (`cd::rhi_metal`) Canlı GPU Doğrulaması (`Faz 1` & `T0.1`)**.
- [x] **Wave 9.2 — WebGPU (`cd::rhi_webgpu`) Dawn & Emscripten (WebAssembly) Paritesi (`T4.3`)**.
- [x] **Wave 9.3 — Android (`VK_KHR_android_surface` + NDK) ve iOS Platform Adaptörleri (`T4.1–T4.2`)**.

---

## FAZ 10 — Üretim Araçları (`cd_assetc`, Crash Reporter), Stres Kapısı, Paketleme ve Kitap
*(Kaynak: `PRODUCTION_V20_PLAN v2.0.1, v2.0.3, v2.0.7, v2.0.8` + `PLAN_NEXT_PHASES Faz 3`)*

- [x] **Wave 10.1 — `cd_assetc` Offline `.cdpak` Cooker + CMake `add_cdpack()` (`v2.0.1`)**.
- [x] **Wave 10.2 — Crash Reporter (`MiniDumpWriteDump` / Signal Backtrace + Sembolleme — `v2.0.3`)**.
- [x] **Wave 10.3 — CPack/InnoSetup Kurulum Paketi, 24s `cd_stress` Kapısı ve `docs/ENGINE_GUIDE.md` (`Faz 3` & `v2.0.7–v2.0.8`)**.

---

## FAZ 11 — ChromaUI (`cd::ui::chroma`) GPU/CPU Hibrit UI Kütüphanesi & Dear ImGui'nin Komple Kaldırılması

- [x] **Wave 11.1 — GPU/CPU Hibrit Çizim Hattı, Vektör Geometri & Font/Glyph Rasterizer (`cd::ui::renderer` + `cd::ui::renderer_rhi` + `cd::ui::ChromaUi`)**:
  - `cd::ui::renderer::DrawBatcher` + `cd::ui::renderer_rhi::Submitter` üzerinden 65.532 tepe noktalı yüksek kapasiteli GPU/CPU hibrit çizim hattı.
  - Keskin vektör/piksel-quad tabanlı yerleşik 5x7 / 7x9 ölçeklenebilir glif motoru + `cd::ui::font::Font` metrikleri, kalın çizgi (`AddLine`), çember (`AddCircle`/`AddCircleFilled`), yuvarlatılmış dikdörtgen (`AddRect`/`AddRectFilled`), gradyan ve gölge üretimi.
- [x] **Wave 11.2 — Esnek Hibrit UI Motoru (`engine/ui/widgets/include/cd/ui/ChromaUi.hpp`) — Docking, Layout, Drag & Drop, Tema ve 25+ Widget**:
  - `DockSpace` + etkileşimli panel bölücüler (Sol Palette/Hierarchy, Sağ Inspector/World, Alt Asset/Status, Üst MenuBar + Quick-Action Toolbar, Yüzen HUD pencereleri ve z-sıralı sağ-tık `Popup`/`Modal` katmanları).
  - Tam Widget Seti: `Button`, `SmallButton`, `Checkbox`, `RadioButton`, `SliderFloat`, `SliderInt`, `DragFloat`, `DragFloat3` (X/Y/Z renk şeritli sürüklenebilir sayısal kutular), `ColorEdit3` (etkileşimli renk seçici + RGB/HSV), `Combo` (açılır liste), `InputText` / `InputTextWithHint` (klavye düzenleme + imleç), `CollapsingHeader`, `TabBar`/`TabItem`, `Table` (esnek N-sütunlu ızgara), `PlotLines`, `Tooltip`, `Selectable`, `ProgressBar`.
  - Yerel Sürükle-Bırak (`BeginDragDropSource` / `AcceptDragDropPayload`) + yüzen hayalet rozet (ghost badge) ve tema motoru (*Chroma Studio Dark*, *Cyber Glass*, *Solarized Light*).
- [x] **Wave 11.3 — `apps/editor/` İçinden Dear ImGui (`cd::imgui_backend`, `<imgui.h>`) Bağımlılığının %100 Kaldırılması**:
  - `apps/editor/CMakeLists.txt` içinden `cd::imgui_backend` hedefinin tamamen çıkarılması.
  - `apps/editor/main.cpp`, `StudioToolbar.hpp`, `StudioModifierClasses.hpp`, `StudioPhysicsSolver.hpp` ve `StudioWorkspacePanels.hpp` içindeki tüm UI, pencere, menü, sürükle-bırak, 3D collider tel-kafes (wireframe) ve girdi (`OSEvent`) akışının `%100 cd::ui::ChromaUi` üzerinden çalıştırılması.
- [x] **Wave 11.4 — Birim Testleri, CTest Doğrulaması ve Gerçek Vulkan 1.3 GPU Ekran Görüntüsü Kanıtı**:
  - `cd_app_editor` hedefinin sıfır ImGui bağımlılığıyla derlenmesi, tüm editör CTest testlerinin geçmesi ve gerçek Vulkan 1.3 GPU üzerinde `studio_chromaui_clean.png` ile `studio_chromaui_physics.png` ekran görüntülerinin alınıp doğrulanması.

---

## FAZ 12 — ChromaUI 2.0: Gerçek GPU TrueType/SDF Font Atlası, UTF-8 Orantısal Tipografi & Vektör İkon Motoru

- [x] **Wave 12.1 — `Submitter` Inline Shader Doku/Atlas Bağlama Hattı (`cd::ui::renderer_rhi::Submitter`)**:
  - `Submitter::create_with_inline_shader` içinde `info.atlas_view` ve `info.atlas_sampler` geçerli olduğunda `DescriptorSetLayout` + `DescriptorSet` (`set=0, binding=0, sampler2D u_atlas`) oluşturulması; `set_texture_slot` ile çoklu doku bağlama desteği.
  - Fragment shader (`kInlineFSWithAtlas`) içinde `variant == 2u` (`kGlyph` — gerçek TrueType anti-aliased alfa/SDF örneklemesi), `variant == 1u` (`kTextured`), `variant == 3u` (`kNinePatch`), `variant == 4u` (`kLinearGrad`), `variant == 5u` (`kBlur`), `variant == 6u` (`kShadow`) ve `variant == 7u` (`kRoundedRect`) tam desteği.
- [x] **Wave 12.2 — Gerçek TrueType (`Segoe UI` / TTF) GPU Font Atlası, UTF-8 Çözümleyici ve Hassas `CalcTextSize`**:
  - `ChromaUi::Context::create` içinde `cd::ui::font::Font` üzerinden gerçek TrueType font yükleme (`rasterize_range(0x0020U, 0x017FU, 15.0F, 512U)`, ASCII + Latin-1 + Türkçe `ğüşıöçĞÜŞİÖÇ`), `512x512` RGBA8 GPU doku yüklemesi ve `Submitter` bağlantısı (TTF bulunmayan headless test ortamlarında otomatik yerleşik glif geri dönüşü).
  - Orantısal (proportional) glif metrikleri (`bearing_x`, `bearing_y`, `advance`, `kerning`) ile çalışan `ImGui::CalcTextSize`, `AddText`, `AddBezierCubic` (kalın anti-aliased Bezier eğrileri) ve prosedürel vektör ikon çizicileri (`DrawIcon`: Play, Pause, Stop, Cube, Sphere, Light, Folder, Gear, Search, Eye, Check, Warning, Node, Curve).

---

## FAZ 13 — Dinamik DockSpace Ağacı, Sekmeli (Tabbed) Paneller, Sürüklenebilir Yüzen Pencereler ve Popup/Alt-Menü Motoru

- [x] **Wave 13.1 — Dinamik DockSpace & Çok-Sekmeli (Multi-Tab) Panel Yöneticisi**:
  - Sabit pencere ismi (`"Studio Workbench"` vb.) kontrolünün yanında gerçek `DockBuilderDockWindow` tabanlı esnek dock yuvalarının (`LeftTop`, `LeftBottom`, `Right`, `Bottom`, `Center`) çalıştırılması.
  - Sürüklenebilir, sağ-alt köşeden yeniden boyutlandırılabilir (resizable), başlık çubuğundan kapatılabilir bağımsız **Floating Pencereler**.
- [x] **Wave 13.2 — Etkileşimli Kaydırma Çubukları (Scrollbar Drag) & Hatasız Popup / Alt-Menü (Submenu) Sistemi**:
  - Tüm pencerelerde ve `BeginChild` alanlarında fare ile tutulup sürüklenebilen dikey kaydırma çubuğu (`ScrollbarThumb`).
  - Dinamik içerik boyutuna göre otomatik büyüyen `BeginMenu` / `BeginPopup` / `BeginPopupContextItem`; sağ-tık menüsü içindeki alt menülerin (`BeginMenu` inside `BeginPopup`) ebeveyn menünün sağında doğru konumda açılması; dışarı tıklandığında veya `ESC` basıldığında otomatik kapanma.
  - `InputText`, `Combo`, `Button`, `Selectable`, `SliderFloat`, `DragFloat` ve `DragFloat3` hücrelerinde metin taşmasını (overflow/collision) tamamen engelleyen donanım makaslama (`PushClipRect`/`PopClipRect`) koruması.

---

## FAZ 14 — Zengin Widget Kataloğu 2.0 (Uygulama, Editör ve Araç Geliştirme İçin Tam Set)

- [x] **Wave 14.1 — Gelişmiş Metin, Sayısal Giriş, Renk Seçici (HSV/RGB Popup) ve Döner Düğme (`Knob`) Widget'ları**:
  - `InputText` / `InputTextWithHint`: Yanıp sönen imleç (blinking caret), Sol/Sağ ok, `Home`/`End`, `Backspace`/`Delete`, yatay kaydırma ve fare tıklamasıyla odaklanma desteği.
  - `SliderFloat`: Çift tıklama veya `Ctrl+Sol Tık` ile doğrudan klavyeden sayı yazma (inline numeric text input) modu!
  - `ColorEdit3`: Renk kutusuna tıklandığında açılan canlı R/G/B gradyan sürgüleri ve `#RRGGBB` Hex göstergeli popup renk seçici.
  - `ToggleSwitch` (animasyonlu hap anahtar), `RadioButton`, `KnobFloat` (dairesel açısal kontrol düğmesi), `ProgressBar`, `TreeNode` / `TreeNodeEx` / `TreePop` ve `PlotLines`.
- [x] **Wave 14.2 — Görsel Düğüm Editörü (`NodeGraph`), Eğri Editörü (`CurveEditor`), Zaman Çizelgesi (`Timeline`) ve Bildirim (`Toast`) Sistemi**:
  - `ImGui::CurveEditor`: Kontrol noktaları fareyle sürüklenebilen, ızgaralı interaktif Bezier eğri editörü.
  - `ImGui::BeginNodeGraph` / `BeginNode` / `NodePin` / `NodeLink`: Kaydırılabilir ızgara tuvali üzerinde sürüklenebilir düğümler, giriş/çıkış soketleri ve kübik Bezier bağlantı eğrileri.
  - `ImGui::TimelineScrubber`: Anahtar kare (keyframe) ve oynatma başlığı (playhead) zaman çizelgesi.
  - `ImGui::PushToast`: Sağ-alt köşede animasyonlu beliren ve kendiliğinden sönen bildirim balonları.

---

## FAZ 15 — Oyun-İçi HUD & 3D Dünya-Uzayı UI Çatısı (`cd::ui::chroma::hud`)

- [x] **Wave 15.1 — Oyun-İçi HUD Widget'ları ve Etkileşimli Envanter/Hotbar Sistemi**:
  - `DrawHealthShieldBar` (HP / Shield / Stamina barları).
  - Kuzey işaretli, görüş konili (FOV cone) ve canlı varlık radar noktalı `DrawMinimapRadar`.
  - Dinamik yayılmalı (spread) `DrawDynamicCrosshair` nişangah ve kısayol/bekleme süreli (cooldown) **Envanter & Hotbar Izgarası (`DrawInventoryHotbar`)**.
  - **3D Dünya-Uzayı Yüzen Etiketler (`DrawWorldSpaceEntityTag`)**: 3D sahnedeki nesnelerin ve karakterlerin üzerinde kamera-projeksiyonlu isim plakası, mesafe (`[m]`) ve mini sağlık çubuğu gösterimi.

---

## FAZ 16 — Bağımsız Uygulama Çatısı, Üst Şerit Araç Çubuğu & Gerçek GPU Doğrulaması

- [x] **Wave 16.1 — Üst Şerit (Ribbon Toolbar), 3 Canlı Çalışma Alanı Modu (`3D Studio` / `In-Game HUD` / `Node & App Lab`) ve 5 Tema**:
  - Üst menü altındaki `40px` boşluğu ortadan kaldıran tam genişlikli **Studio Ribbon Toolbar** (`y = 28..60`): Çalışma Alanı Seçici (`3D Studio`, `In-Game HUD`, `Node & App Lab`), Gizmo/Snap kontrolleri, Fizik oynatıcı, Kamera yer imleri ve 5 Tema (`Chroma Carbon Dark`, `Unreal Slate Pro`, `Blender Graphite`, `Cyberpunk Neon`, `High-Contrast Daylight`).
  - Bağımsız uygulama geliştiricileri için `Node & App Lab` çalışma alanı ve alt dock içinde `Console & Physics`, `Shader Node Graph`, `Timeline & Curves` sekmeli panellerinin bağlanması.
- [x] **Wave 16.2 — Kapsamlı Birim Testleri (`cd_test_ui_widgets` + `cd_test_submitter_inline`) ve Gerçek GPU Doğrulaması**:
  - `ChromaUi` tüm widget, layout, font, dock, node-graph, curve-editor ve HUD bileşenlerini doğrulayan kapsamlı birim testlerinin koşulması (`21/21` CTest %100 yeşil).
  - Gerçek Vulkan 1.3 GPU üzerinde 3 çalışma alanı (`ui_after_studio.png`, `ui_after_ingame_hud.png`, `ui_after_node_studio.png`) için ekran görüntülerinin alınıp görsel olarak sıfır taşma/hata ile doğrulanması.

---

# BÖLÜM III — TAM DONANIMLI OYUN MOTORU EDİTÖRÜ, ÇOKLU-DÜNYA/SEVİYE VİTRİNİ & ULTRA-YÜKSEK FPS MİMARİSİ (FAZ 17)

## FAZ 17 — Çoklu Proje / Çoklu Dünya (`World`) / Çoklu Seviye (`Level`) / Katman (`Layer`) Konteyneri ve Ultra-Yüksek FPS Darboğaz Eliminasyonu

- [x] **Wave 17.1 — Motor Çekirdeği `cd::world_container` (`Project` / `World` / `Level` / `Layer`) Çoklu-Dünya Genişletmesi**:
  - `engine/world/world_container/include/cd/world_container/Level.hpp`, `Project.hpp`, `ProjectIo.hpp` ve `World.hpp` içinde her `Level` nesnesine `world_name`, `description` ve `template_id` meta verilerinin eklenmesi.
  - `Project` sınıfına `active_level()`, `set_active_level()`, `world_names()` ve `levels_in_world()` sorgularının; `ProjectIo` JSON serileştiricisine `active_level`, `world`, `description` ve `template_id` alanlarının; `World` sınıfına `active_level_ptr()` erişimcisinin eklenmesi ve `cd_test_world_container` birim testleriyle %100 doğrulanması.
  - `ChromaUi` (`engine/ui/widgets/include/cd/ui/ChromaUi.hpp`) içine tam açılır liste (`ImGui::BeginCombo` / `ImGui::EndCombo`) widget çiftinin eklenmesi ve `cd_test_ui_widgets` (49/49 test) ile doğrulanması.
- [x] **Wave 17.2 — 5 Dünya (`Worlds`), 12 Seviye (`Levels`) ve 12 `.cdproj` Vitrin Kataloğunun Studio Editörüne Bağlanması**:
  - `StudioSceneBuilder.hpp`, `StudioToolbar.hpp` ve `StudioWorkspacePanels.hpp` içinde 5 Dünya (`World 1: Physics & Destruction Lab`, `World 2: Photoreal PBR & Sponza GI`, `World 3: Vehicle & Multi-Body Dynamics`, `World 4: Skinned Characters & Combat`, `World 5: Volumetric Atmosphere & Nature`) ve 12 etkileşimli 3D seviyenin (`template_id = 0..11`) inşası.
  - Üst menüye `Projects & Worlds` menüsünün, Ribbon Toolbar'a `[World/Level: ...]` hızlı geçiş açılır penceresinin, sol `Scene` dock paneline `[Hierarchy & Entities | Worlds & Levels | Layers]` sekmelerinin, sağ `Inspector` paneline canlı `Layer` açılır kutusunun ve alt dock paneline `Showcase & Project Hub (12)` + `Frame & GPU Profiler` sekmelerinin eklenmesi.
- [x] **Wave 17.3 — ~100 FPS Boş Sahne Darboğazının Kök Neden Analizi ve Ultra-Yüksek FPS Optimizasyonu**:
  - **UI Draw Call Birleştirme (`Submitter.cpp`)**: Inline UI shader hattında aynı `DescriptorSetHandle` ve `ScissorRect` paylaşan ardışık `DrawCommand`'ların tek `vkCmdDrawIndexed` çağrısında birleştirilmesi (~450 UI draw call → ~8 draw call).
  - **Statik Sahne TLAS & Descriptor Dirty-Gating (`main.cpp`, `StudioTlasCompaction.hpp`, `StudioTlasBuilder.hpp`)**: FNV-1a `tlas_scene_hash` ile sahne dönüşümleri/materyalleri değişmediğinde her kare yapılan `vkCreateAccelerationStructureKHR`, 512 KB SSBO yığın tahsisi ve 100+ `vkUpdateDescriptorSets` çağrısının atlanması.
  - **Boşta Post-FX, Gölge, Ses DSP ve RNG Döngüsü Bypass'ı (`main.cpp`, `StudioPhysicsSolver.hpp`)**: Statik karelerde CSM gölge geçişinin, TAA/MotionBlur kapalıyken `velocity_gbuffer_pass`'in, Bloom kapalıyken 7-geçişli `run_bloom_chain`'in, 3D ses/RNG görünümü kapalıyken 512-örnek DSP ve 16.384-örnek Box-Muller RNG döngülerinin atlanması; `frames_in_flight = 3` ve `swapchain.image_count = 4` ile DWM çit (fence) beklemelerinin giderilmesi.

---

# BÖLÜM IV — AKADEMİK SOTA OYUN MOTORU ARAŞTIRMASI VE MAKALE-DESTEKLİ BRDF UYGULAMASI (FAZ 18)

## FAZ 18 — SOTA Oyun Motoru Teknolojileri Akademik İncelemesi (ADR) ve Turquin (2019) + Belcour-Barla (2017) BRDF Uygulaması

- [x] **Wave 18.1 — Akademik SOTA Oyun Motoru Teknolojileri Araştırması ve Iglberger ADR Dokümantasyonu**:
  - [`docs/ADR/ADR-20261017-sota-game-engine-technologies-and-turquin-multiscatter-brdf.md`](ADR/ADR-20261017-sota-game-engine-technologies-and-turquin-multiscatter-brdf.md) içinde 5 SOTA oyun motoru teknolojisinin (1. Nanite Cluster-DAG Sanal Geometri, 2. ReSTIR DI/GI Uzay-Zamansal Rezervuar Örneklemesi, 3. Ray-Traced DDGI İrradyans Kesişim Alanları, 4. Neural Radiance Caching, 5. Turquin 2019 / Kulla-Conty 2017 Çoklu-Saçılım Mikrofaset Enerji Telafisi + Belcour-Barla 2017 İnce-Film İridesans) akademik literatür, matematiksel formülasyon, avantajlar (Pros) ve dezavantajlar (Cons) matrisiyle belgelenmesi ve `check_human_voice.py` (`PASS — 0 issues`) ile doğrulanması.
- [x] **Wave 18.2 — `cd::brdf_multiscatter` (`engine/render/brdf`) C++23 Kütüphanesi ve Birim Testleri**:
  - `engine/render/brdf/include/cd/brdf/multiscatter/TurquinMultiScatter.hpp` içinde `directional_albedo_ggx`, `average_albedo_ggx`, `fresnel_average_schlick`, `turquin_energy_compensation` (Turquin 2019 Eq. 11), `kulla_conty_multiscatter_lobe` (Kulla-Conty 2017), `belcour_barla_iridescence_fresnel` (Belcour & Barla SIGGRAPH 2017 Airy girişimi) ve `evaluate_turquin_iridescent_ggx` fonksiyonlarının gerçeklenmesi.
  - `cd_test_brdf_multiscatter` ve `cd_test_brdf` şemsiye testleriyle beyaz-fırın (white-furnace) enerji korunumu ($\forall \alpha \in [0,1],\; E(\mu,\alpha)\cdot F_{\text{ms}}(\mathbf{1}) = 1$), Helmholtz karşılıklılığı (reciprocity) ve spektral sınırların %100 doğrulanması.
- [x] **Wave 18.3 — Vulkan 1.3 Canlı PBR Shader (`prim.frag.glsl` + `PrimShader_kPrimFS.inl`) ve Studio Editör Entegrasyonu**:
  - `PbrMaterialModifier` (`StudioModifierClasses.hpp`) içine `Turquin Multi-Scatter` (`0..1`) ve `Thin-Film Iridescence (nm)` (`0..800 nm`) sürgülerinin ve `Turquin Rough Gold`, `Bismuth (380nm)`, `Soap Film (520nm)` hazır ayarlarının eklenmesi.
  - `prim.frag.glsl` ve `PrimShader_kPrimFS.inl` içinde `cd_belcour_barla_iridescence` ve `turquin_ms_comp` hesaplamalarının canlı güneş, nokta/spot/alan ışıkları ve IBL kanallarına bağlanması; Seviye `2.1 PBR 5x5 Sphere Matrix` (`template_id = 4`) üzerinde çoklu-saçılım ve ince-film iridesans kürelerinin gerçek GPU ekran görüntüsüyle doğrulanması.

---

# BÖLÜM V — KLASÖR AĞACI & İÇERİK/VARLIK TARAYICISI, DOKU/MESH ATAMA, GRAFİKSEL HATA DÜZELTMELERİ VE CLI KORUMASI (FAZ 19)

## FAZ 19 — Klasör Ağacı (`Folder Tree`) + İçerik/Varlık Tarayıcısı (`Content/Asset Browser`), Sürükle-Bırak Mesh/Texture Atama ve Grafiksel/Görsel İyileştirmeler

- [x] **Wave 19.1 — Klasör Ağacı (`Folder Tree`) ve İçerik/Varlık Tarayıcısı (`Content Browser`) Penceresi (`StudioWorkspacePanels.hpp`, `StudioModifierClasses.hpp`)**:
  - Alt Dock (`History` penceresi) içine varsayılan ilk sekme olarak **`Content Browser & Folder Tree`** panelinin eklenmesi.
  - Sol bölmede gerçek çalışma alanı dizinlerini (`assets/`, `assets/meshes/`, `assets/textures/`, `assets/samples/Sponza/`, `assets/samples/Sponza/textures/`, `assets/samples/Generic/`, `showcases/`, `apps/editor/shaders/`, `docs/ADR/`) gezen etkileşimli **Workspace Folder Tree** ve üstte `[Root: assets/]`, `[Meshes]`, `[Textures]`, `[Sponza glTF]`, `[Showcases]` hızlı ekmek kırıntısı (breadcrumb) düğmeleri.
  - Sağ bölmede `std::filesystem::directory_iterator` ile diskteki gerçek dosyaları (`.gltf`, `.glb`, `.jpg`, `.png`, `.glsl`, `.cdproj`, `.md`) ve yerleşik 3D Mesh/PBR Doku varlıklarını tarayan, arama (`Search`) ve tür filtresi (`All | Meshes | Textures | Materials`) destekleyen **4 Sütunlu Varlık Kartları Izgarası (`asset_cards_grid_4col`)**.
- [x] **Wave 19.2 — 3D Nesnelere Dosya Dizininden Doku (`Texture`) ve Mesh Atama (`StudioSceneBuilder.hpp`, `StudioPhysicsSolver.hpp`, `prim.frag.glsl`)**:
  - **Sürükle-Bırak (`CD_ASSET_MESH` / `CD_ASSET_TEXTURE`) ve 1-Tık Atama**: Varlık kartlarından herhangi bir `.mesh` / `.gltf` / `.glb` veya `.tex` / `.jpg` / `.png` / `.pbr` öğesini doğrudan **3D Sahne Görünümüne (Viewport)**, **Sol `Scene` Hiyerarşi Satırına** veya **Sağ `Inspector` -> `Mesh & Surface Texture Assets (Drop Target)`** yuvalarına sürükleyip bırakarak ya da 1-tıkla anında atama (`assign_mesh_asset_to_entity`, `assign_texture_asset_to_entity`).
  - **GPU Shader Doku Yuvaları (`prim.frag.glsl` + `PrimShader_kPrimFS.inl`)**: Her varlık için `pc.fx_params.y` üzerinden 6 farklı doku yuvası: Slot `1` (`cd_albedo_tex` GPU doku örneklemesi), Slot `2` (`UV_Checkerboard_2K` kenar-yumuşatmalı UV dama tahtası), Slot `3` (`Brick_Wall_PBR` derzli tuğla duvar), Slot `4` (`Carbon_Fiber_Twill` 2x2 dimi karbon fiber dokuma), Slot `5` (`Carrara_Marble` damarlı mermer) ve Slot `6` (`Oak_Wood_Planks` meşe ahşap kaplama).
  - **Yeni Vitrin Seviyesi (`template_id = 12` — `2.4 Asset Browser & Texture/Mesh Lab`)**: UV dama küresi, Sponza tuğla küpü, karbon fiber silindir, Carrara mermer torus, meşe ahşap koni ve altın küre içeren hazır test sahnesi.
- [x] **Wave 19.3 — Grafiksel Hataların (`Z-Fighting`, RT Yansıma Kusuru, Normal Haritası Bozulması, UI Kırpılması) ve CLI/Subagent Takılma Sorununun Giderilmesi**:
  - **Z-Fighting ve RT Yansıma Kusuru Çözümü (`main.cpp`)**: `Ground_Floor` (`y = -0.25, sy = 0.25` -> üst yüzeyi `y = 0.0`) ile `s.floor_mesh` (`y = 0.0`) arasındaki eş-düzlemli derinlik çatışmasının (Z-fighting) ve RT TLAS içinde kürelerin alt yarısını griye boyayan 20m kutu yansımasının `Ground_Floor` görsel/TLAS çizimini atlayarak (fizik çarpışmasını koruyarak) tamamen giderilmesi.
  - **Küre Yüzeyi Normal Bozulması ve Yeşil Tel-Kafes Kalabalığı Çözümü (`prim.frag.glsl`, `StudioSceneBuilder.hpp`)**: PBR vitrin kürelerinde prosedürel tümsek (bump) normal haritasının kapatılarak pürüzsüz mikrofaset/iridesans yansımalarının elde edilmesi; `show_all_collider_wires` seçeneğinin yalnızca fizik seviyelerinde (`1, 2, 3, 7`) açılması; tüm 25 kürenin kadraja tam sığması için kamera konumunun optimize edilmesi.
  - **CLI & Subagent Takılma Koruması (`main.cpp`)**: `g_studio_max_frames` ve `--frames` / `--max-frames` / `--studio-capture-png` / `--capture-png` bayrak takma adlarıyla `editor.exe` sürecinin yakalama sonrası deterministik olarak kapanmasının garanti altına alınması.
