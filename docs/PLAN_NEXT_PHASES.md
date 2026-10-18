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

---

# BÖLÜM VI — SİSTEMİK DEBUG, CHROMAUI 2.1 KUSURSUZLUK & GODOT+ TAM OYNANABİLİR OYUN MOTORU MİMARİSİ (FAZ 20–22)

## FAZ 20 — Sistemik Görsel & Operasyonel Hata Ayıklama (Systematic Debug & UI/UX Polish)

- [x] **Wave 20.1 — ChromaUI 2.1 Açılır Menü, Çok-Satırlı İpucu (`SetTooltip`), Combo Sınır Kırpma, Kesişimli `PushClipRect` Yığını ve Satır-İçi Sayısal Giriş (`DragFloat` / `DragFloat3`) Düzeltmeleri**:
  - `BeginPopup` içindeki `BeginMenu` alt menülerinin yatay değil dikey (`w.is_popup`) dizilmesi; tüm açılır pencere, alt menü ve `Combo` koordinatlarının ekran sınırlarına (`[6, DisplaySize - size - 6]`) kelepçelenmesi (`ChromaUi.hpp`).
  - `ImDrawList::PushClipRect` içinde her yeni kırpma dikdörtgeninin ebeveyn pencere kırpma yığını (`clip_stack_`) ile kesiştirilmesi (intersection) ve pencere dışına taşan bileşenlerin/metinlerin alt panele sızmasının (ghosting/bleed) %100 engellenmesi.
  - `SetTooltip` içinde `\n` çok-satırlı metinlerin satır satır ölçülüp çizilmesi ve ekran dışına taşmasının önlenmesi.
  - `DragFloat` ve `DragFloat3` (Inspector Transform `Position`, `Rotation`, `Scale`) kutularına çift tıklama veya `Ctrl+Sol Tık` ile **doğrudan klavyeden sayısal değer yazma (`Inline Numeric Edit`)** desteğinin eklenmesi ve `cd_test_ui_widgets` birim testleriyle doğrulanması.
- [x] **Wave 20.2 — Sahne/Dünya/Seviye Geçişlerinde Iterator Invalidation & Use-After-Free Koruması ve Silme (`Delete`) Temizliği**:
  - `StudioWorkspacePanels.hpp`, `StudioToolbar.hpp`, `StudioPhysicsSolver.hpp` ve `StudioSceneBuilder.hpp` içinde `project()->world_names()` ve `levels_in_world()` döngüleri sürerken doğrudan `switch_to_project_level` çağrılmasının (ve `clear_levels()` nedeniyle döngüdeki referansların dangling kalmasının) ertelenmiş geçiş (`deferred_switch_idx`) ile %100 güvenli hale getirilmesi.
  - `Delete` tuşu ve `delete_selected` işleminde silinen varlığın `w.entity_modifiers`, `w.home_transforms`, `w.euler_cache_deg` ve `cd::world_container::clear_layer` kayıtlarının eksiksiz temizlenmesi ve `Ground_Floor` varlığının kazara silinmeye karşı korunması.
- [x] **Wave 20.3 — Klavye Odak Çakışması (`WantCaptureKeyboard`), Kilitli/Gizli Nesne Seçimi (`Viewport Picking`) ve PIE HUD Çizim Hatasının Giderilmesi**:
  - `apps/editor/main.cpp` içinde metin kutularına (`InputText` — varlık adı, seviye adı, katman adı, arama filtresi) yazı yazarken veya PIE modundayken `W/A/S/D/Q/E`, `Space` ve `F` tuşlarının 3D editör kamerasını veya gizmo modunu değiştirmesini engelleyen `!ImGui::GetIO().WantCaptureKeyboard && !s.pie_playing` kapısının eklenmesi.
  - 3D Viewport raycast seçiminde (`ehits`) gizli (`!ent.visible`), kilitli (`ent.locked`) ve `Ground_Floor (Static Collider)` varlıklarının yanlışlıkla seçilmesinin ve Gizmo ile sürüklenmesinin engellenmesi.
  - `s.pie_playing` (`F5` Play-In-Editor) aktifken `draw_clean_studio_workspace_panels` (ve dolayısıyla fizik çözücüsü ile `draw_ingame_hud_workspace`) fonksiyonunun (`main.cpp` `else` bloğu yüzünden) atlanması hatasının giderilerek canlı oyun döngüsünün ve oyun-içi HUD'un (`Crosshair`, `HP/Shield/Energy`, `Objectives`, `Score`, `Minimap`) PIE modunda tam ekran çalışmasının sağlanması.

## FAZ 21 — Godot+ Oyun Yapım Çatısı: Oyun-İçi Davranış Modifiers, Hiyerarşi Göz/Kilit Kontrolleri ve Pre-PIE Sahne Snapshot/Restore

- [x] **Wave 21.1 — Godot+ Oyun Mantığı ve Etkileşim Bileşenleri (`KinematicMoverModifier`, `TriggerVolumeModifier`, `PatrolWaypointModifier`)**:
  - `StudioModifierClasses.hpp` ve `StudioPhysicsSolver.hpp` içine gerçek oyun mekanikleri inşa etmek için 3 yeni C++23 oyun bileşeni:
    1. **`KinematicMoverModifier` (`MOV`)**: Hareketli platformlar, asansörler, sarkaçlar ve dönen engeller için eksenel sinüzoidal salınım (`amplitude`, `frequency_hz`, `phase`) + sürekli açısal dönüş (`angular_deg_per_sec`) ve üzerindeki oyuncuyu/nesneleri taşıma (`carry_riders`).
    2. **`TriggerVolumeModifier` (`TRG`)**: Oyuncu veya fiziksel nesne kesişiminde tetiklenen oyun mantığı sensörü: `Collectible (+Score & Dissolve)`, `JumpPad (+Y Velocity Boost)`, `Hazard Zone (-HP Damage & Knockback)`, `Checkpoint / Heal (+HP/Shield)`, `Level Goal Beacon (Victory Trigger)`.
    3. **`PatrolWaypointModifier` (`AI`)**: Otonom devriye gezen düşman/NPC/drone davranışı (`patrol_span`, `move_speed`, `chase_player`).
- [x] **Wave 21.2 — Pre-PIE Sahne Durumu Kaydetme / Geri Yükleme (`Snapshot & Restore`) ve Godot Tarzı Hiyerarşi Satır Kontrolleri**:
  - `F5` / `[> PLAY]` ile oyun başlatıldığında (`enter_pie_play_mode`) sahnedeki tüm varlıkların ve oyun durumunun hazır hale getirilmesi; `F8` / `ESC` / `[# STOP]` ile durdurulduğunda (`exit_pie_play_mode`) çalışma zamanında fırlatılan topların (`CannonBall_*`) silinmesi, toplanan enerji çekirdeklerinin yeniden görünür yapılması ve tüm fizik konumlarının düzenleme öncesi haline kusursuzca geri döndürülmesi.
  - Sol `Scene` (`Hierarchy & Entities`) ağacındaki her varlık satırına Godot tarzı hızlı **Görünürlük (`V` / `-`)** ve **Kilit (`U` / `L`)** satır-içi düğmelerinin ve `[MOV]`, `[TRG]`, `[AI]` rozetlerinin eklenmesi.

## FAZ 22 — Tam Oynanabilir 3D Aksiyon-Platform Oyunu (`World 6`) ve Uçtan Uca Doğrulama

- [x] **Wave 22.1 — `World 6: Playable Games & Interactive Templates` ve Seviye `6.1 Cyber-Core Action Platformer (Playable Game)` (`template_id = 13`)**:
  - `StudioSceneBuilder.hpp` içine doğrudan editörde `F5` (veya `--pie-play`) ile baştan sona oynanabilen, kontrol edilebilir `Player_Hero_CesiumMan` (`WASD`/`Ok Tuşları` + `Space` Zıplama), hareketli kinematik köprüler (`KinematicMoverModifier`), zıplama rampaları (`JumpPad_Trigger`), 4 toplanabilir enerji çekirdeği (`Collectible_EnergyCore`), lazer tehlike bariyeri (`Hazard_LaserBarrier`), devriye gezen yapay zeka dronu (`Patrol_SynthBot`) ve bitiş portalı (`Goal_Beacon`) içeren tam oyun seviyesinin eklenmesi.
  - Canlı oyun HUD'unun (`draw_ingame_hud_workspace`) oyuncu sağlığı (`player_hp`), kalkanı (`player_shield`), toplanan enerji çekirdeği skoru (`game_score`, `collected_cores / total_cores_in_level`), çakışmasız (de-overlapped) 3D dünya-uzayı etiketleri ve zafer/oyun-bitti durumlarıyla gerçek zamanlı beslenmesi.
- [x] **Wave 22.2 — Birim Testleri (`cd_test_ui_widgets`, `cd_test_world_container`) ve Gerçek Vulkan 1.3 GPU Görsel Doğrulaması**:
  - Tüm yeni ChromaUI 2.1 özelliklerinin ve çoklu-dünya/seviye yapısının birim testleriyle %100 yeşil doğrulanması.
  - `editor.exe` ile hem Studio düzenleme modunda (`build/ui_game_studio_6_1.png`) hem de `6.1 Cyber-Core Action Platformer` (`--template 13 --pie-play`, `build/ui_game_pie_6_1.png`) canlı oyun modunda gerçek GPU ekran görüntülerinin alınıp görsel olarak denetlenmesi.

---

# BÖLÜM VII — DERİN SİSTEMİK HATA AYIKLAMA (9 KRİTİK BUG), KAYIPSIZ `.cdscene.json` SERİLEŞTİRME & GODOT+ KULLANIŞLILIK MİMARİSİ (FAZ 23–25)

## FAZ 23 — Derin Sistemik Fizik, Çarpışma, Gizmo, Undo/Redo, UTF-8 ve Klavye/UI Hata Ayıklaması

- [x] **Wave 23.1 — Çift `F5`/`F8` PIE Toggle Çakışması, `enter_pie_play_mode` Konum Ezilmesi ve `JumpPad_Trigger` Başlangıç Çakışması Düzeltmeleri**:
  - **Çift `F5` / `F8` Toggle İptali (`main.cpp`, `StudioToolbar.hpp`)**: `StudioToolbar.hpp` içinde `F5` ile `enter_pie_play_mode` çağrıldıktan hemen sonra aynı karede `main.cpp` içindeki eski `ImGui::IsKeyPressed(ImGuiKey_F5)` bloğunun `s.pie_playing = !s.pie_playing` yaparak PIE modunu anında geri kapatması hatasının giderilmesi (`main.cpp` içindeki mükerrer F5/F8 bloğunun kaldırılması).
  - **PIE Başlangıcında Düzenlenen Konumların Korunması (`StudioPhysicsSolver.hpp`)**: `enter_pie_play_mode` başında `reset_physics_to_home_positions` çağrılarak kullanıcının Edit modunda Gizmo veya Inspector ile yaptığı konum/rotasyon değişikliklerinin ezilmesi hatasının giderilmesi; bunun yerine mevcut sahne dönüşümlerinin `record_entity_home_transform` ile kaydedilerek PIE sonunda (`exit_pie_play_mode`) tam olarak düzenleme anındaki duruma dönülmesi.
  - **`JumpPad_Trigger` Başlangıç Çakışması (`StudioSceneBuilder.hpp`)**: `JumpPad_Trigger` spawn edildiğinde kendisiyle aynı koordinatta ikinci bir `Dynamic_Sphere` oluşturup aynı karede üst üste binmesi kusurunun giderilmesi; kürenin fırlatma rampasının üstünde (`y + 0.90m`) temiz şekilde doğması.
- [x] **Wave 23.2 — `CapsuleColliderModifier` & `SphereColliderModifier` Dünya-Y Dik Ekseni, Dar-Faz (`Narrowphase`) Çarpışma ve Gizmo Undo/Redo Senkronizasyonu**:
  - **Skinned Karakter (`CesiumMan` `-90 deg X` glTF) Kapsül Çarpışma Hatası (`StudioPhysicsSolver.hpp`)**: `CesiumMan.glb` modelinin yerel `-90 deg X` rotasyonu nedeniyle `compute_entity_world_obb` içinde kapsül yarı-yüksekliğinin yerel Y (dünya Z) ekseni boyunca hesaplanması ve `Player_Hero_CesiumMan` karakterinin zemin içinden (`y = -0.58`) düşmesi hatasının, `CapsuleColliderModifier` ve `SphereColliderModifier` için dünya-uzayı dik (`World +Y`) yarıçap/yarı-yükseklik hesabı ve `solve_entity_pair_narrowphase` içinde `CapsuleColliderModifier` desteğinin eklenmesiyle %100 çözülmesi (`Player_Hero_CesiumMan` zeminde tam `y = 0.00` üzerinde durur).
  - **3D Gizmo Üst-Menü Çizim Katmanı (`GetBackgroundDrawList`) ve Undo/Redo Senkronizasyonu (`main.cpp`)**: `update_and_draw_gizmo`, `draw_selection_outline_overlay` ve `draw_light_markers_overlay` çizimlerinin `GetForegroundDrawList()` yerine `GetBackgroundDrawList()` üzerine alınarak üst açılır menülerin (`Create 3D Object`, `Projects & Worlds` vb.) üstünü kapatmasının engellenmesi; Gizmo sürüklemesi bittiğinde veya `Ctrl+Z` / `Ctrl+Y` (`Undo` / `Redo`) yapıldığında `record_entity_home_transform` ve `euler_cache_deg` tablolarının güncellenmesi.
- [x] **Wave 23.3 — ChromaUI UTF-8 Em-Dash (`—`) Normalizasyonu, Açılır Menü Fare Tekerleği (`MouseWheel`) Yalıtımı ve Satır-İçi Sayısal Giriş (`WantCaptureKeyboard`)**:
  - **`visible_label` UTF-8 Normalizasyonu (`ChromaUi.hpp`)**: 5x7 ASCII bitmap fontta `?` olarak görünen çok-baytlı UTF-8 tire (`—`, `–`), tırnak ve ok dizilerinin `visible_label()` içinde temiz ASCII karşılıklarına (`--`, `-`, `->`, `*`) dönüştürülmesi.
  - **Popup / Menu Açıkken Arka Pencere Kaydırma Koruması (`ChromaUi.hpp`)**: `detail::is_occluded_by_popup` kontrolünün `End()` ve `EndChild()` içindeki `MouseWheel` ve scrollbar sürükleme bloklarına eklenerek açık menü/combo üzerinde tekerlek çevrildiğinde alttaki panelin kaymasının önlenmesi.
  - **Inline Numeric Edit `WantCaptureKeyboard` (`ChromaUi.hpp`)**: `DragFloat` / `DragFloat3` satır-içi sayısal yazma (`inline_edit_id != 0`) aktifken `io.WantCaptureKeyboard = true` atanarak rakam/back-space yazarken sahne kısayollarının tetiklenmesinin engellenmesi.

## FAZ 24 — Kayıpsız (`.cdscene.json`) Sahne Serileştirme, CLI `--scene` Yükleyici ve Tam Bağımsız Oyun Paketi (`F9`)

- [x] **Wave 24.1 — Tüm 10 Bileşen Sınıfı (`ModifierStack`), Özel Mesh/Doku ve Hiyerarşi İçin Kayıpsız `.cdscene.json` Kayıt/Yükleme (`StudioSceneBuilder.hpp`)**:
  - `save_full_studio_scene` ve `load_full_studio_scene` içinde tüm varlık alanlarının (`visible`, `locked`, `use_texture`, `custom_texture_id`, `custom_mesh_path`, `custom_texture_path`, `layer`), ebeveyn-çocuk sahne hiyerarşisinin (`parent_index`), tüm 10 varlık bileşeninin (`PbrMaterialModifier` Turquin/Iridescence/TextureSlot dahil, `RigidBodyModifier`, `BoxColliderModifier`, `SphereColliderModifier`, `CapsuleColliderModifier`, `LightEmitterModifier`, `CharacterControllerModifier`, `KinematicMoverModifier`, `TriggerVolumeModifier`, `PatrolWaypointModifier`), 4 dünya ortam bileşeninin (`FogModifier`, `BloomLensModifier`, `TonemapCameraModifier`, `SkyAtmosphereModifier`) ve sahne ışıklarının eksiksiz JSON serileştirilmesi ve geri yüklenmesi.
- [x] **Wave 24.2 — CLI `--scene <path>` Başlatma Bayrağı ve Bağımsız Oyun Paketi (`RunGame.bat` / `RunGame.sh`) Doğrudan Sahne Bağlantısı (`main.cpp`, `StudioSceneBuilder.hpp`)**:
  - `apps/editor/main.cpp` içine `--scene <path>` / `--load-scene <path>` komut satırı bayrağının eklenmesi.
  - `export_standalone_game_package` (`F9`) tarafından üretilen `RunGame.bat` ve `RunGame.sh` başlatıcılarının dışa aktarılan `main_level.cdscene.json` dosyasını `--scene "%~dp0main_level.cdscene.json" --pie-play` ile doğrudan yükleyerek kullanıcının tasarladığı sahneyi birebir çalıştırması.
  - `duplicate_entity_with_modifiers` (`Ctrl+D`) ve `spawn_entity_with_modifiers` içinde yeni varlıkların aktif katmana (`w.active_layer_name`) atanması ve tüm modifier sınıflarının (`KinematicMoverModifier`, `TriggerVolumeModifier`, `PatrolWaypointModifier` dahil) eksiksiz kopyalanması.

## FAZ 25 — Godot+ Ergonomi: Sabitlenmiş (`Pinned`) Hiyerarşi/Inspector Kaydırma Bölgeleri, Sıkıştırılmış `Studio Workbench` ve Çoklu-Dünya Başlangıç Senkronizasyonu

- [x] **Wave 25.1 — Başlangıç Aktif Seviye (`Active Level`) Senkronizasyonu ve `.cdproject` 6-Dünya / 14-Seviye Otomatik Yükseltmesi (`StudioSceneBuilder.hpp`, `main.cpp`)**:
  - `populate_master_showcase_project` ve `init_clean_studio_workspace` içindeki seviye sayısı eşiğinin `>= 14U` olarak güncellenmesi, eski `< 14` seviyeli `.cdproject` dosyaları yükseltilirken önceki aktif seviye adının (`prev_active_name`) korunması ve otomatik olarak 6 Dünya, 14 Seviyeli (`World 6: 6.1 Cyber-Core Action Platformer` dahil) `ChromodynamicMasterShowcase` yapısına yükseltilip diske kaydedilmesi.
  - `apps/editor/main.cpp` (`s.frame_idx == 1u`) içinde CLI `--template` veya `--scene` verilmediğinde `s.cd_world.active_level_ptr()->template_id()` değerinin kontrol edilerek aktif seviyenin (`template_id != 0` ise) 3D varlıklarının otomatik yüklenmesi; ayrıca `Console & Physics` sekmesindeki `Clean Ultra-FPS` düğmesinin `load_studio_interactive_template(s, 0, log_push)` üzerinden geçirilerek üst Ribbon Toolbar, `Scene` paneli ve 3D Viewport'un her zaman %100 senkronize kalması.
- [x] **Wave 25.2 — `Studio Workbench` Kesintisiz Görünüm, `Scene` / `Inspector` Sabit Başlıklı Kaydırılabilir Alt Bölgeler ve `ChromaUi` Sürükle-Bırak / Clip-Rect Sertleştirmesi (`StudioWorkspacePanels.hpp`, `StudioPhysicsSolver.hpp`, `ChromaUi.hpp`)**:
  - `Studio Workbench` içindeki düğme yüksekliklerinin (`21px` / `19.5px` / `17px`), tablo satır aralığının (`ChromaUi.hpp` `advance_item`) ve sol dock bölme oranının (`0.70F`) optimize edilerek 6 Hızlı Sahne Şablonu, 18 Sürükle-Bırak 3D Nesne/Prefab ve 14 Modifier Sınıfının tamamının (`[CHR]`, `[MOV]`, `[TRG]`, `[AI]`, `[FOG]`, `[BLM]`, `[TMP]`, `[SKY]` dahil) hiçbir kırpılma olmadan tam görünür olmasının sağlanması.
  - `Scene` (`Hierarchy & Entities` ve `Worlds & Levels` sekmeleri) ile `Inspector` (`Entity Inspector`, `World & Fog`, `Lights & Export` sekmeleri) panellerinde içerik listelerinin `ImGui::BeginChild(...)` içine alınarak sekme çubuklarının, `Filter nodes...` / `[Save]` `[Load]` başlığının ve `[Focus(F)] [Dup(Ctrl+D)] [Floor(G)] [Delete]` hızlı eylem çubuğunun kaydırma sırasında üstte sabit (`pinned`) kalması; `BeginChild` içinde `parent.content_max_y` hesabının düzeltilerek dış pencerede mükerrer kaydırma çubuğu oluşmasının engellenmesi.
  - `ChromaUi.hpp` içinde `SliderFloat`, `DragFloat`, `DragFloat3`, `ColorEdit3`, `Combo`, `BeginCombo` ve `CurveEditor` widget'larına `detail::is_in_window_clip(w, mp)` kontrolü eklenerek `BeginChild` dışına kaydırılmış widget'ların sabitlenmiş üst başlıktaki tıklamaları yakalamasının önlenmesi; çok-satırlı `Scene` hiyerarşisinde `BeginDragDropSource` (aktif öğe kilidi + `TextUnformatted` satır-içi çizim sızıntısı önleme), `BeginDragDropTarget` (`last_item_hovered` hedef doğruluğu + vurgu çerçevesi) ve `EndChild` kenar otomatik kaydırma (edge auto-scroll) özelliklerinin tamamlanması.
- [x] **Wave 25.3 — Çift Build (`build/ide-base` & `build/ninja-base`), Birim Testleri ve Canlı GPU Ekran Görüntüsü Doğrulaması**:
  - `cd_test_ui_widgets` (10 alt senaryolu `ChromaUi_InteractiveCurveNodeDockAndChildScroll` dahil) ve `cd_test_world_container` birim testlerinin ve `cd_app_editor` (`build/ide-base` + `build/ninja-base`) derlemelerinin sıfır hata ile doğrulanması.
  - `phase1_3d_studio_workspace.png`, `studio_edit_mode_template13.png` ve `studio_pie_playable_game_live.png` ekran görüntülerinin gerçek Vulkan 1.3 GPU üzerinde yeniden üretilip görsel olarak doğrulanması.

---

# BÖLÜM VIII — UÇTAN UCA (E2E) MOTOR KULLANIMI, RENDER-ÖNCESİ FİZİK/KAMERA BORU HATTI, 3. ŞAHIS YAYLI KAMERA VE AKTİF SAVAŞ/OYNANIŞ DÖNGÜSÜ (FAZ 26)

## FAZ 26 — "Tech Demo" Hissinin Tamamen Kaldırılması: Gerçek E2E Oyun İnşası, Render-Öncesi Fizik/Kamera Senkronizasyonu ve Aktif Savaş/Platform Oynanışı

- [x] **Wave 26.1 — Render-Öncesi (`Pre-Render`) Fizik, Karakter ve Kamera Güncelleme Sıralaması (`main.cpp`, `StudioToolbar.hpp`)**:
  - `tick_studio_modifiers_and_physics` çağrısının `draw_studio_main_menu_bar` (3D sahne çiziminden *sonra* çalışan UI geçişi) içinden çıkarılarak `main.cpp` içinde `update_skinned_animation`, `rebuild_tlas_and_transition_depth` ve `begin_hdr_scene_pass` adımlarından **önceye** taşınması; böylece 3D mesh dönüşümleri, kamera `vp` matrisi, tel-kafes collider çizgileri ve 3D dünya-uzayı etiketleri arasındaki 1 karelik gecikmenin (frame lag) tamamen ortadan kaldırılması.
- [x] **Wave 26.2 — 3. Şahıs Yaylı Kamera Takibi (`X, Y, Z`), Yönlü Karakter Yönelimi (`Yaw`) ve Hıza Duyarlı İskelet Animasyonu (`StudioPhysicsSolver.hpp`, `main.cpp`)**:
  - `Player_Hero_CesiumMan` hareket ettiğinde, zıpladığında, `JumpPad_Trigger` ile havalandığında veya `KinematicMoverModifier` hareketli platformları üzerinde taşındığında kameranın hem yatay (`X, Z`) hem dikey (`Y`) eksende oyuncuyu pürüzsüz yay fiziğiyle (`std::clamp(clamped_dt * 7.5F, 0.05F, 1.0F)`) takip etmesi ve sağ-tık kamera yörünge ofsetini koruması.
  - Karakterin hareket vektörüne (`atan2(dx, dz)`) doğru yumuşakça dönmesi (`q_yaw * q_upright`) ve `CesiumMan` iskelet animasyon hızının (`pie_speed_scale`) yatay hız (`w.player_horiz_speed`) ile dinamik ölçeklenmesi (dururken nefes alma/rölanti, koşarken tam adım hızı).
- [x] **Wave 26.3 — Aktif Plazma Atışı Savaşı (`B` / Sol-Tık), Kapsül-Duyarlı Tetikleyici Çakışması ve Sahne Serileştirme Filtresi (`StudioSceneBuilder.hpp`, `StudioPhysicsSolver.hpp`)**:
  - PIE modunda `B` tuşu veya Sol-Tık ile oyuncunun göğüs hizasından en yakın aktif tehdide (`Synth_Patrol_Drone` / `Hazard_Laser`) kilitlenen yüksek hızlı plazma mermisi (`CannonBall_*`) ateşlenmesi; isabet halinde düşmanın/lazerin etkisiz hale getirilmesi (`+500 PTS` / `+250 PTS`, `w.defeated_enemies++`) ve HUD üzerinde `BOTS: %d` sayacının güncellenmesi.
  - `TriggerVolumeModifier` mesafe kontrolünün oyuncunun dik kapsül segmenti (`[p_pos.y, p_pos.y + 1.45F]`) üzerindeki en yakın noktaya göre yapılarak göğüs hizasındaki enerji çekirdeklerinin ve bitiş portalının kusursuz algılanması.
  - `save_full_studio_scene` ve `load_full_studio_scene` içinde `s.entities` dışındaki iç/yetim sahne düğümlerinin (`triangle.gltf` vb.) filtrelenmesi, varlık sırasının korunması ve kamera `eye`/`target` vektörlerinin kaydedilip yüklenmesi.
- [x] **Wave 26.4 — Motor API'leriyle Uçtan Uca (E2E) Seviye İnşası, Kayıt/Yükleme ve 4-Aşamalı Canlı Oynanış Doğrulaması (`build_and_save_custom_game_level`, `--autopilot`, `--capture-gameplay-dir`)**:
  - `build_and_save_custom_game_level` ile motorun kendi sahne/varlık/doku/modifier API'leri kullanılarak 3D aksiyon-platform seviyesinin (`Arena_Marble_Pillar_West` Carrara mermer sütunu, `Goal_Citadel_Brick_Backstop` tuğla kale duvarı, karbon-fiber hareketli platformlar, `BloomLensModifier`) sıfırdan inşa edilmesi, `build/export/ChromodynamicGame/game_world.cdscene.json` olarak paketlenmesi, diskten geri yüklenmesi ve otonom/etkileşimli oynanışla (`Score=1650 | Cores=4/4 | Bots=2 | Victory=1`) bitiş portalına kadar oynanarak 4 aşamalı GPU kare yakalaması (`01_editor_built_and_loaded_scene.png` .. `04_pie_victory_goal_complete.png`) ile doğrulanması.

---

# BÖLÜM IX — ARGON ORCHESTRATOR TAM-YIĞIN GRAFİKSEL HATA ELİMİNASYONU, GERÇEK DİSK DOKU/MESH GPU YÜKLEME HATTI & DETERMINİSTİK DOĞRULAYICI (FAZ 27)

## FAZ 27 — Çoklu-Kare GPU Yarışı (Flicker) Eliminasyonu, Zemin/Gölge Hizalaması, Gerçek Disk `.jpg`/`.png`/`.gltf` Bindless Yükleyici ve `tools/argon_pipeline_runner.py`

- [x] **Wave 27.1 — Multi-Frame-in-Flight GPU Yarışı & Flicker Eliminasyonu, Zemin (`effective_floor_y`) Hizalaması ve Render-Öncesi UI/Sahne Sıralaması (`main.cpp`, `StudioTlasBuilder.hpp`, `StudioAppState.hpp`, `ChromaUi.hpp`)**:
  - **Multi-Frame-in-Flight GPU Yarışı Çözümü (`main.cpp`)**: Tekil (ring-buffer olmayan) HDR/GBuffer/Depth hedefleri, `shadow_ubo`, `lights_ubo`, `inst_mat_ssbo`, `gltf_cesium.vb` ve `prim_inst` descriptor setleri üzerinde `frames_in_flight = 3` + `vsync = false` nedeniyle oluşan CPU-yazarken-GPU-okuma titremesinin (flicker/tearing) `rd.frames_in_flight = 1`, `imgui_id.frames_in_flight = 1` ve etkileşimli modda `vsync = true` ile %100 ortadan kaldırılması; pencere yeniden boyutlandırmada (`s.needs_rebuild`) `device.wait_idle()` eklenmesi.
  - **Render-Öncesi Atomik UI & Sahne Mutasyonu (`main.cpp`)**: `ctx.new_frame()`, `draw_studio_main_menu_bar`, `DockSpace`, `draw_clean_studio_workspace_panels` ve 3D Gizmo güncellemelerinin `tlas_scene_hash`, `rebuild_tlas_and_transition_depth`, `draw_shadow_map_pass` ve `begin_hdr_scene_pass` adımlarından **önce** çalıştırılarak seviye geçişi, nesne ekleme/silme ve doku/mesh atamalarının aynı karede tüm GPU geçişlerine atomik olarak yansıması.
  - **Zemin Yüksekliği (`effective_floor_y = -0.002F`) ve Çift Gölge Çözümü (`main.cpp`, `StudioTlasBuilder.hpp`, `StudioSceneBuilder.hpp`)**: Studio modunda `kFloorY = -0.55F` ile fiziksel `Ground_Floor` (`y = 0.0F`) arasındaki `0.55m` kopukluğun ve havada asılı duran ikinci düzlemsel gölgenin (`draw_planar_shadows`) giderilmesi; `Static_Ramp` yüksekliğinin (`y = 0.52F`) zemine tam oturacak şekilde düzeltilmesi; Sponza (`template_id = 5`) dış duvarının arkasında kalan ışığın iç avlu ışıklarıyla değiştirilmesi; `sync_camera_and_invalidate_frame_history` ile seviye geçişlerinde `free_look` kamera ve TAA/TLAS geçmişinin sıfırlanması.
  - **Merkezi 3D Viewport Donanım Makaslama (`ChromaUi.hpp`)**: `ImGui::DockSpace` içinde `st.bg_dl` (`GetBackgroundDrawList()`) üzerine merkezi 3D görünüm alanı (`[lx+2, top_y+2] .. [rx-2, by-2]`) kırpma dikdörtgeni uygulanarak 3D Gizmo, seçim konturu, ışık ikonları ve `DrawWorldSpaceEntityTag` etiketlerinin yan/alt dock panellerinin arkasına taşmasının tamamen engellenmesi; `ImGui::EndTable` içinde `w.content_max_y` hesabının düzeltilmesi.
- [x] **Wave 27.2 — Klasör Ağacı / Content Browser Üzerinden Gerçek Disk Dokusu (`.png`/`.jpg`/`.tga`/`.bmp`) ve Mesh (`.gltf`/`.glb`/`TorusKnot`) GPU Yükleme Hattı (`StudioSceneBuilder.hpp`, `StudioMeshRegistry.hpp`, `PrimShader_kPrimFS.inl`, `prim.frag.glsl`)**:
  - **Gerçek Disk Dokusu Bindless GPU Yükleyicisi (`assign_texture_asset_to_entity`)**: Dosya dizininden veya Content Browser'dan seçilen gerçek disk görüntü dosyalarının (`std::filesystem::exists` + `cd::asset::image::load_image`) `create_texture_rgba8` ile GPU'ya yüklenip `s.prim_bindless_set` (`binding = 13`, slot `120..250`) içine yazılması ve `prim.frag.glsl` / `PrimShader_kPrimFS.inl` içinde `tex_mode >= 10 && tex_mode < 256` dalıyla doğrudan örneklenmesi.
  - **Gerçek Disk `.gltf`/`.glb` ve `TorusKnot` Mesh Yükleyicisi (`assign_mesh_asset_to_entity`)**: `PrimitiveKind::kTorusKnot` (`s.meshes.knot` + `s.meshes.blas_knot`) ve diskteki özel `.gltf`/`.glb` modellerinin (`cd::asset::gltf::load_scene` -> `cd::render::upload_mesh` -> `build_mesh_blas` -> `PrimitiveKind::kCustomGltf`) canlı olarak GPU'ya yüklenip hem raster hem RT gölge/yansıma geçişlerinde çizilmesi.
- [x] **Wave 27.3 — Argon Orchestrator Deterministik Boru Hattı Doğrulayıcısı (`tools/argon_pipeline_runner.py` & `.agents/skills/argon-orchestrator/SKILL.md`)**:
  - Zaman aşımı korumalı (`timeout_sec`) `tools/argon_pipeline_runner.py` betiği ile tek komutta Katman DAG (`check_layer_dag.py`), CMake derlemesi (`build/ide-base` & `build/ninja-base`), CTest birim testleri (`cd_test_ui_widgets`, `cd_test_world_container`, `cd_test_brdf_multiscatter`), Golden Fixture #5 piksel-birebir (`0 / 921600`) doğrulaması ve Studio seviyelerinin canlı GPU ekran görüntüsü yakalama kapısının çalıştırılması.

---

# BÖLÜM X — 250 EVRENSEL ELEMENT, VESSEL VE INLINE CAPSULE KATALOĞU: 219 GODOT DÜĞÜMÜ KARŞILIĞI + 31 NEXT-GEN SOTA YETENEĞİ (FAZ 28)

## FAZ 28 — Godot 219 Düğümünün Tamamının (`"All 219 Godot Nodes Explained In 42 Minutes !"`) CHROMODYNAMIC `Element` / `Vessel` / `TraitCapsule` Karşılıkları ve 31 Next-Gen SOTA Üstünlüğü

- [x] **Wave 28.1 — `apps/editor/StudioNodeCatalog.hpp` (219 Godot Düğümü + 31 Next-Gen SOTA = 250 Evrensel Yapı Taşı Sicili)**:
  - **219 Godot Düğümü Birebir Karşılığı (`0..218`)**:
    - `CoreSystem` (`21` düğüm, `00:00–04:01`): `Node` -> `Element`, `Viewport` -> `RenderStage`, `SubViewport` -> `EmbeddedStage`, `WorldEnvironment` -> `AtmosphereStage`, `AnimationPlayer` -> `TimelineChoreographer`, `AnimationTree` -> `BlendTreeGraph`, `AudioStreamPlayer` -> `AudioPulseEmitter`, `Timer` -> `ChronoTrigger`, `HTTPRequest` -> `HttpFetchPulse`, `MultiplayerSpawner` -> `NetVesselReplicator`, `MultiplayerSynchronizer` -> `RollbackStateSync` vb.
    - `Node2D` (`50` düğüm, `04:02–15:46`): `Node2D` -> `PlanarElement2D`, `Sprite2D` -> `SpriteQuad2D`, `AnimatedSprite2D` -> `FlipbookQuad2D`, `CanvasGroup` -> `CompositeBatchGroup2D`, `BackBufferCopy` -> `FrameCaptureCopy2D`, `GPUParticles2D` -> `ComputeParticleSwarm2D`, `StaticBody2D` -> `StaticBarrierVessel2D`, `RigidBody2D` -> `RigidKineticVessel2D`, `CharacterBody2D` -> `AvatarVessel2D`, `Area2D` -> `SensorZoneVessel2D`, `Camera2D` -> `PlanarLens2D`, `TileMap` -> `ChunkedTileGrid2D`, `LightOccluder2D` -> `SdfShadowCaster2D`, `Skeleton2D` -> `PlanarArmature2D`, `NavigationRegion2D` -> `NavMeshIsland2D`, `ParallaxBackground` -> `ParallaxStage2D` vb.
    - `Node3D` (`88` düğüm, `15:47–31:59`): `Node3D` -> `SpatialElement3D`, `MeshInstance3D` -> `SurfaceElement3D`, `MultiMeshInstance3D` -> `GpuCrowdSwarm3D`, `CSGBox3D`..`CSGCombiner3D` -> `BooleanBoxCSG3D`..`BooleanUnionAssembly3D`, `Sprite3D` -> `SpatialSpriteQuad3D`, `Decal` -> `ObbVolumeDecal3D`, `GPUParticles3D` -> `ComputeParticleSwarm3D`, `GPUParticlesAttractorBox3D`/`Sphere3D`/`VectorField3D`, `GPUParticlesCollisionSDF3D`, `OmniLight3D` -> `ClusteredPointLamp3D`, `SpotLight3D` -> `ClusteredConeLamp3D`, `DirectionalLight3D` -> `SolarCascadeLamp3D`, `VoxelGI` -> `DdgiIrradianceVolume3D`, `LightmapGI` -> `RayTracedLightmapStage3D`, `ReflectionProbe` -> `ParallaxMirrorProbe3D`, `FogVolume` -> `LocalFroxelFogBox3D`, `StaticBody3D` -> `StaticBarrierVessel3D`, `RigidBody3D` -> `RigidKineticVessel3D`, `CharacterBody3D` -> `AvatarVessel3D`, `VehicleBody3D` -> `RoverVessel3D`, `VehicleWheel3D` -> `SuspensionWheelCapsule3D`, `Area3D` -> `SensorZoneVessel3D`, `SoftBody3D` -> `XpbdSoftSurfaceVessel3D`, `BoneAttachment3D` -> `JointSocketAnchor3D`, `PhysicalBone3D` -> `RagdollBoneVessel3D`, `Joint3D` (`Pin`/`Hinge`/`Slider`/`ConeTwist`/`Generic6DOF`), `Skeleton3D` -> `SkeletalArmature3D`, `SkeletonIK3D` -> `FabrikIkCapsule3D`, `SpringArm3D` -> `SpringArmCapsule3D`, `NavigationRegion3D` -> `VoxelNavMeshIsland3D`, `NavigationAgent3D` -> `RvoSteeringCapsule3D`, `GridMap` -> `SparseVoxelGridStage3D`, `RayCast3D` -> `RaySensorCapsule3D`, `ShapeCast3D` -> `SweepSensorCapsule3D`, `XROrigin3D`/`XRCamera3D`/`XRController3D`/`XRAnchor3D` vb.
    - `ControlUI` (`60` düğüm, `32:00–42:00`): `Control` -> `ChromaControlRect`, `Button` -> `ChromaActionButton`, `ColorPicker` -> `ChromaOklchColorPicker`, `CodeEdit` -> `ChromaShaderCodeIde`, `GraphEdit` -> `ChromaBezierNodeGraph`, `GraphNode` -> `ChromaShaderGraphCard`, `RichTextLabel` -> `ChromaBbcodeRichDocument`, `Tree` -> `ChromaOutlinerTreeView`, `ItemList` -> `ChromaAssetGridView`, `TabContainer` -> `ChromaDockTabHost`, `SplitContainer` -> `ChromaResizableSplitDock`, `SubViewportContainer` vb.
  - **31 Next-Gen SOTA Beyond-Godot Yapı Taşı (`219..249`)**:
    `ReSTIRGlobalIlluminationStage3D`, `SurfelRadianceCacheStage3D`, `SvgTemporalDenoiserCapsule3D`, `TurquinMultiScatterPbrCapsule3D`, `BelcourIridescenceFilmCapsule3D`, `DualLobeSubsurfaceSkinCapsule3D`, `MarschnerHairFiberCapsule3D`, `WronskiFroxelAtmosphereStage3D`, `RayMarchedCloudscapeStage3D`, `SpectralSkySunStage3D`, `XpbdClothSheetVessel3D`, `VoronoiShatterVessel3D`, `ActiveRagdollMotorCapsule3D`, `PbfFluidSimVolume3D`, `FftOceanWaveStage3D`, `VirtualShadowClipmapStage3D`, `MeshletClusterLodElement3D`, `BindlessMaterialHeapStage`, `SmoothSdfMorphElement3D`, `HrtfAmbisonicAcousticStage3D`, `ProceduralFoleySynthCapsule`, `DeterministicRollbackStage`, `UtilityGoapBrainCapsule`, `SpatialEqsQueryCapsule3D`, `MotionMatchingPilotCapsule3D`, `FullBodyIkFootPlantCapsule3D`, `PlanetClipmapTerrainStage3D`, `GpuFoliageWindSwarm3D`, `PhysicalCameraOpticsCapsule3D`, `AgxSpectralDisplayCapsule`, `DeterministicInputReplayStage`.
- [x] **Wave 28.2 — 3-Yollu Kompozisyon (`SpawnRootElement`, `NestAsChildElement`, `FoldAsInlineCapsule`), `UniversalNodeModifier` ve Seviye `6.2 Universal 250-Node Showcase` (`template_id = 14`)**:
  - Godot'nun derin alt-düğüm ağacı şişkinliğini (`CharacterBody3D -> CollisionShape3D -> MeshInstance3D -> SpringArm3D -> Camera3D`) ortadan kaldıran 3-modlu CHROMODYNAMIC kompozisyon mimarisinin (`[+ Spawn Root Element/Vessel]`, `[+ Nest as Child Element]`, `[+ Fold as Inline Capsule]`) hem `StudioSceneBuilder.hpp` (`spawn_or_compose_studio_catalog_node`) hem `StudioNodeCatalog.hpp` (`apply_catalog_node_to_studio_state`) içine entegre edilmesi.
  - Alt Dock (`History` -> `"250-Node Catalog (Godot+SOTA)"` sekmesi), `Node & App Lab` çalışma alanı, `Create 3D Object` üst menüsü ve `Scene -> Hierarchy & Entities` (`[+ Node (250)]` düğmesi + `[CAPSULE]` / `[CAPxN]` rozeti) bağlantılarının kurulması.
  - 15. Seviye olarak `6.2 Universal 250-Node Showcase (Godot 219 + SOTA 31)` (`template_id = 14`) sahnesinin ve `cd_test_splash_audio_cue` içindeki 250 düğüm birim testlerinin (`HasExact219GodotAnd31SotaMaking250Total`, `FindsAllIconicGodot219AndNextGenSota31Nodes`, `SpawnsNestsFoldsAndSurvivesJsonRoundTripForAll250Nodes`) tamamlanması.

---

# BÖLÜM XI — 250 SOMUT C++23 SINIF HİYERARŞİSİ, ZAMANSIZ SOURCE 2 / UE5 / GODOT 4 / BLENDER 4 / HAZEL UI/UX DEVRİMİ & 33-MODÜLLÜ ROCKSTAR 2D+3D BİRLEŞİK FİZİK MOTORU (FAZ 29–32)

## FAZ 29 — 250 Ayrı Somut C++23 Sınıfı (`cd::scene::constructs`) ve `ElementCapsuleTree` Nested/Fold/Unfold Mimarisi

- [x] **Wave 29.1 — `Element / Vessel / Pilot / Nested Trait Capsule` Mimarisi (`ADR-20261018` & `ElementCapsuleTree.hpp`)**:
  - [`docs/ADR/ADR-20261018-element-vessel-nested-capsule-architecture.md`](ADR/ADR-20261018-element-vessel-nested-capsule-architecture.md) ve `engine/world/scene/include/cd/scene/ElementCapsuleTree.hpp` içinde döngü-korumalı `nest_child`, `add_inline_capsule`, `unfold_capsule_to_nested_child`, `fold_nested_child_into_capsule`, `embark_pilot` ve `propagate_hierarchy` fonksiyonlarının gerçeklenmesi.
- [x] **Wave 29.2 — 250 Somut, Örneklendirilebilir C++23 Sınıfı (`engine/world/scene/include/cd/scene/constructs/*.hpp`)**:
  - `IChromaConstruct.hpp`, `CoreAndPlanar2DConstructs.hpp` (71 `class ... final`), `Spatial3DVisualAndCsgConstructs.hpp` (44 `class ... final`), `Spatial3DVesselPhysicsAndGiConstructs.hpp` (44 `class ... final`), `ControlUiAndNextGenSotaConstructs.hpp` (91 `class ... final`) ve `ChromaConstructRegistry.hpp` (`instantiate_chroma_construct(id)` + `instantiate_chroma_construct_by_name(name)`) ile 250 sınıfın polimorfik olarak örneklendirilmesi ve `cd_test_scene` ile %100 doğrulanması.

## FAZ 30 — Zamansız (Timeless) Source 2 + UE 5.4 + Godot 4.3 + Blender 4.2 + Hazel Engine UI/UX Modernizasyonu

- [x] **Wave 30.1 — 7 Motorlu UI/UX Benchmark Raporu (`docs/UX_UI_MASTER_BENCHMARK_AND_DESIGN.md`) ve `ChromaUi` Modernizasyonu**:
  - Valve Source / Source 2 (Hammer 5), Unreal Engine 5.4, Godot 4.3, Blender 4.2, Hazel Engine, Unity 6 ve Remedy Northlight özellik matrisinin çıkarılması.
  - `engine/ui/widgets/include/cd/ui/ChromaUi.hpp` içinde yüksek kontrastlı `Chroma Carbon Slate` teması, birleşik menü + şerit araç çubuğu (`Unified Header`), aktif sekme/düğme alt-vurgu çizgileri (accent underlines) ve en altta Blender 4.2 / Source 2 tarzı bağlamsal **Status Bar** şeridi (`22px`).
- [x] **Wave 30.2 — Sol `Outliner | Place | Stages` Paneli, 3D Viewport İçi Source 2 / Blender Overlay Kontrolleri ve Temiz Hiyerarşi Satırları**:
  - Sol paneldeki ikiye bölünmüş sıkışık gökkuşağı düğmelerinin kaldırılarak tam boy `Outliner (Scene) | Place & Toolbox | Worlds & Stages` sekmeli yapıya geçilmesi.
  - 3D Viewport üst-sol köşesine Source 2 Hammer / Blender tarzı `[Stage: ...] [Lit | Colliders | Focus] [+ Quick Add]` yarı-saydam haplarının ve üst-sağ köşesine interaktif **3D Eksen Gülü (`+X / +Y / +Z` Orientation Gizmo)** + `[Persp | Top | Front | Right]` kamera düğmelerinin eklenmesi.
  - Alt `250 Constructs` sekmesinde aksiyon düğmelerinin (`[+ Spawn Root]`, `[+ Nest Child]`, `[+ Fold Capsule]`) sağ kartın en üstüne sabitlenerek metin kırpılmalarının %100 giderilmesi.

## FAZ 31 — 32-Modüllü Rockstar (RAGE / Euphoria + Jolt + Source 2 Rubikon + Box2D v3 + XPBD) 2D & 3D Birleşik Fizik Motoru

- [x] **Wave 31.1 — Fizik Motoru Mimari Karar Kaydı (`ADR-20261018`) ve `core/` Katmanı (7 Modül)**:
  - [`docs/ADR/ADR-20261018-rockstar-2d-3d-modular-physics-engine.md`](ADR/ADR-20261018-rockstar-2d-3d-modular-physics-engine.md) ve `engine/world/physics/include/cd/physics/core/` altında 7 bağımsız modül: `PhysicsTypes.hpp`, `CollisionFilterMatrix.hpp`, `SpatialAabb2D.hpp`, `SpatialAabb3D.hpp`, `DynamicBvh2D.hpp`, `DynamicBvh3D.hpp`, `IslandBuilder.hpp`.
- [x] **Wave 31.2 — `v2d/` Box2D v3 /Erin Catto Soft-Step 2D Fizik Motoru Katmanı (9 Modül)**:
  - `engine/world/physics/include/cd/physics/v2d/` altında 9 bağımsız modül: `Shapes2D.hpp`, `ContactManifold2D.hpp`, `Narrowphase2D.hpp` (SAT + 2-point incident clipping), `SweepCcd2D.hpp` (bilateral TOI CCD), `RigidBody2D.hpp`, `SoftStepSolver2D.hpp`, `Joints2D.hpp` (`Distance`, `Revolute`, `Prismatic`, `Weld`, `Wheel`), `CharacterController2D.hpp` (`move_and_slide` + coyote time), `PhysicsWorld2D.hpp`.
- [x] **Wave 31.3 — `v3d/` Jolt / Source 2 Rubikon 6-DOF 3D Fizik Motoru Katmanı (10 Modül)**:
  - `engine/world/physics/include/cd/physics/v3d/` altında 10 bağımsız modül: `Shapes3D.hpp`, `ContactManifold3D.hpp`, `GjkEpa3D.hpp` (3D GJK + Expanding Polytope Algorithm), `Narrowphase3D.hpp` (15-axis OBB SAT + analytic primitives + GJK/EPA fallback), `SweepCcd3D.hpp`, `RigidBody3D.hpp` (6-DOF world inertia tensor + gyroscopic torque), `TgsContactSolver3D.hpp` (Temporal Gauss-Seidel + 2-tangent friction cone), `Joints3D.hpp` (`Fixed`, `BallSocket`, `Hinge`, `Slider`, `ConeTwist`, `SixDofSpring`), `CharacterMover3D.hpp`, `PhysicsWorld3D.hpp`.
- [x] **Wave 31.4 — `rockstar/` İleri Simülasyon Katmanı (5 Modül) ve `UnifiedPhysicsEngine.hpp` (#32)**:
  - `engine/world/physics/include/cd/physics/rockstar/` altında 5 ileri simülasyon modülü:
    1. `ActiveRagdollEuphoria3D.hpp`: NaturalMotion Euphoria tarzı 14-kemikli aktif ragdoll, PD kas sürücüleri, balistik darbe sersemlemesi (`stagger`), Capture Point ($\mathbf{r}_{\text{cp}} = \mathbf{r}_{\text{com}} + \dot{\mathbf{r}}_{\text{com}}\sqrt{h/g}$) denge kurtarma adımı ve düşerken kolları uzatma (`brace_for_impact`) refleksi.
    2. `PacejkaVehicleDynamics3D.hpp`: Pacejka Magic Formula lastik modeli, sürtünme elipsi, 4 tekerlekli süspansiyon + ön/arka anti-roll bar, 6-ileri şanzıman + LSD diferansiyel ve 4x3x2 **Şasi Çarpışma Deformasyonu (`Chassis Deformation Grid`)**.
    3. `FractureDestruction3D.hpp`: Voronoi / Gerilme Çizgesi (`StructuralBond3D`) kırılma ve temel bağlantısını kaybeden adaların (`detached`) serbest enkaz parçalarına ayrılması.
    4. `BuoyancyFluid3D.hpp`: Gerstner dalga yüzeyi + çoklu-prob Archimedes kaldırma kuvveti ve kuadratik hidrodinamik sürükleme.
    5. `XpbdClothSoftBody3D.hpp`: Macklin et al. XPBD kumaş, halat ve basınçlı yumuşak gövde çözücüsü.
  - `UnifiedPhysicsEngine.hpp` (#32) şemsiye orkestratörü ve `cd_test_physics` (106 test) + `cd_test_physics_vehicle` (18 test) + `cd_test_physics_soft_body` (29 test) = **153 fizik birim testinin** %100 yeşil doğrulanması.

## FAZ 32 — `RockstarPhysicsWorldAdapter` (#33) `IPhysicsWorld` Köprüsü ve Etkileşimli Seviye `6.3 Rockstar 2D+3D Physics & Euphoria/Pacejka Arena` (`template_id = 15`)

- [x] **Wave 32.1 — `RockstarPhysicsWorldAdapter.hpp` (#33) ve `StudioSceneBuilder` Entegrasyonu**:
  - `engine/world/physics/include/cd/physics/RockstarPhysicsWorldAdapter.hpp` içinde `cd::physics::IPhysicsWorld` arayüzünün `UnifiedPhysicsEngine` / `v3d::PhysicsWorld3D` üzerinde gerçeklenmesi (`make_rockstar_physics_world()`) ve `init_clean_studio_workspace` içine bağlanması.
- [x] **Wave 32.2 — 16. Vitrin Seviyesi: `6.3 Rockstar 2D+3D Physics & Euphoria/Pacejka Arena` (`template_id = 15`) ve Canlı GPU Doğrulaması**:
  - `World 6: Playable Games & Interactive Templates` altına 16. Seviye olarak `6.3 Rockstar 2D+3D Physics & Euphoria/Pacejka Arena` (`template_id = 15`) eklenmesi: `Euphoria_ActiveRagdoll_Avatar`, `Pacejka_Vehicle_Rover3D`, `Voronoi_Fracture_Chunk_1..3`, `XPBD_ClothSoftBody_Banner3D`, `Archimedes_Buoyancy_Vessel3D` ve alt `Console & Physics` panelinde canlı `[Impulse Euphoria]` / `[Shatter Voronoi]` kontrolleri.

