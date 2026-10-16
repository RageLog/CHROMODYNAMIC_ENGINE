# CHROMODYNAMIC 3D Studio (`editor.exe`) — Domestic Game Engine Transformation Master Plan

> **Hedef Vizyon**: `editor.exe` uygulamasını, herhangi bir `"sample"` bağımlılığı veya 2B prototip soyutlaması olmaksızın, **Godot 4 / Unreal Engine 5** ergonomisinde, **gerçek Vulkan 1.3 donanım Z-Tamponu (`Format::kD32Float`)**, **Cook-Torrance GGX PBR + IBL + Cascaded Shadow Maps (CSM) + Clustered Multi-Light**, **Sponza Atrium glTF 2.0 (Bindless)**, **GPU/CPU İskelet Animasyonu (`CesiumMan.glb`, `cd::anim`)**, **Jolt Fizik & Oyun Döngüsü (PIE)** ve **ImGui DockSpace Stüdyo Arayüzü** ile donatılmış tam teşekküllü domestik bir 3B Oyun Motoru Stüdyosuna dönüştürmek.

---

## Kök Sorun Tespiti (Neden Mevcut Görünüm Yetersizdi?)

1. **Z-Buffer (Derinlik Tamponu) Eksikliği ve İç-Dış Yüzey Girişimi**:
   - Geçici 2B `DrawBatcher` poligon izdüşümü yalnızca nesne merkezine göre sıralama yaptığı (Painter's Algorithm) ve piksel başına donanım derinlik testi (`Format::kD32Float`, `depth_test = true`, `depth_write = true`, `CompareOp::kLess`) kullanmadığı için çok parçalı cisimlerin ve kutuların arka/iç yüzeyleri ön yüzeylerin üstüne binmekteydi.
2. **Gerçek Işıklandırma, Gölge ve PBR Materyal Eksikliği**:
   - 1279 faz boyunca geliştirilen gerçek **MRT G-Buffer + HDR + Cook-Torrance GGX PBR (`PrimShader`) + True IBL (`BrdfLut`, `IrradianceConvolution`, `PrefilteredSpecular`) + Cascaded Shadow Maps (`cd::light::CascadedShadow`) + Clustered Lighting (`cd::light::ClusterGrid`) + Ray-Traced Shadows/Reflections (`VkAccelerationStructureKHR` TLAS/BLAS) + 7-Pass Karis Bloom & ACES/Reinhard Tonemapping** boru hattı `editor.exe` içinde devreye alınmamıştı.
3. **Sponza Atrium, PBR Küre Izgarası ve İskelet Animasyonu (`CesiumMan.glb`) Eksikliği**:
   - Silinen `samples/` altında yer alan **Sponza Atrium (`assets/samples/Sponza/Sponza.gltf`)**, **5×5 PBR Materyal Küre Izgarası**, **Prosedürel 3B Primitifler (`cd::asset::Primitives`)** ve **İskelet Animasyonlu Karakter (`assets/samples/CesiumMan.glb`, `cd::anim::Skeleton`, `cd::anim::Animation`)** yetenekleri özgün stüdyo modülleri olarak `apps/editor` içine taşınmamıştı.
4. **Kullanıcı Ergonomisi (Godot Benzeri Elle Kolay Kontroller)**:
   - Kullanıcının menüden veya araç çubuğundan tek tıkla 3B nesne/ışık/Sponza/karakter ekleyebileceği, 3B sahnede `AxisGizmo` (`W/E/R`) ve `SelectionOutline` ile seçip taşıyabileceği/döndürebileceği/ölçekleyebileceği, PBR materyal ve ışık parametrelerini canlı değiştirebileceği, animasyonu oynatıp timeline üzerinde scrub edebileceği ve `F5` ile anında Play-In-Editor (PIE) oyun moduna geçebileceği bütünsel bir Godot/Unreal iş akışı gerekiyordu.

---

## Uzun Dönemli Uygulama Yol Haritası (6 Fazlı Master Plan — %100 TAMAMLANDI)

### Faz 1: Gerçek Vulkan 1.3 3B PBR + Donanım Z-Buffer + Sponza + Skinned Animation Çekirdeğinin `apps/editor` İçine Özgün Modül Olarak Taşınması `[TAMAMLANDI]`
- [x] **1.1** Donanım Z-Tamponu (`Format::kD32Float`, `depth_test=true`, `depth_write=true`, `CompareOp::kLessOrEqual`) ve 4-hedefli MRT G-Buffer (`HDR RGBA16F`, `Normal RGBA16F`, `Albedo RGBA8`, `MetallicRoughness RG8`) + Velocity pass mimarisinin `apps/editor` altında özgün stüdyo modülleri olarak devreye alınması.
- [x] **1.2** Gerçek **Cook-Torrance GGX PBR + True IBL + Cascaded Shadow Maps (CSM) + Clustered Multi-Light (Directional Sun, Point, Spot, Rect-Area) + Kelvin Renk Sıcaklığı** ışıklandırma sisteminin aktif edilmesi.
- [x] **1.3** **Sponza Atrium (`Sponza.gltf` + Bindless 256-slot doku dizisi)**, **4×4 PBR Küre Izgarası** ve **İskelet Animasyonlu Karakter (`CesiumMan.glb` + `cd::anim` Dual-Quaternion Skinning)** sahnelerinin `editor.exe` içinde canlı çalışması.

### Faz 2: Godot / Unreal Ergonomisinde Stüdyo Üst Menü Çubuğu, Hızlı Oluşturma Araç Çubuğu ve Canlı Denetçi (Inspector) `[TAMAMLANDI]`
- [x] **2.1** **Godot Tarzı Ana Menü Çubuğu (`MainMenuBar`) ve Hızlı Araç Çubuğu (`StudioToolbar.hpp`)**:
  - `Scene / Create 3D`: Tek tıkla **PBR Sphere**, **Cube**, **Cylinder**, **Cone**, **Torus**, **Point Light**, **Spot Light**, **Rect Area Light** oluşturma ve silme/çoğaltma (`Duplicate` / `Delete`).
  - `Sponza & Assets`: Tek tıkla **Sponza Atrium** görünürlük/ışık önayarı, **Animated Character (CesiumMan)** animasyon oynat/durdur/hız kontrolü ve **PBR Grid** kontrolü.
  - `Camera Presets`: Tek tıkla **Overview**, **Sponza Nave**, **Character**, **PBR Grid**, **Colonnade** kamera odaklama (`Focus Selected [F]`).
  - `Transform & Gizmo`: **Move**, **Rotate**, **Scale** 3B `AxisGizmo` modları, `SelectionOutline` vurgu çerçevesi ve `EditHistory` (`Ctrl+Z` / `Ctrl+Y`) tam geri alma/yineleme.
  - `Play-In-Editor (PIE)`: **[> PLAY GAME (F5)]** ve **[# STOP GAME (F8)]** ile sahne durumunu koruyarak canlı oyun/fizik/kamera kontrolüne geçiş.

### Faz 3: Gelişmiş Sahne Yaratımı, Prefab/glTF Sürükle-Bırak ve Hiyerarşik Node3D Ağacı `[TAMAMLANDI]`
- [x] **3.1** Sahnede `.gltf` / `.glb` modellerini (`HelloGenericGltf.hpp` + `StudioDomesticPhases.hpp`) sahneye ekleme ve `hello_engine.cdscene.json` sahne dosyasına tam PBR (`is_pbr`, `metallic`, `roughness`, `use_texture`) + Işık + Kamera + PostFX + Node3D hiyerarşisiyle serileştirme (`save_full_studio_scene` / `load_full_studio_scene`).
- [x] **3.2** Ebeveyn-çocuk (`cd::scene::Scene::attach` / `detach`) hiyerarşik `Node3D` ağacı ve tek tıkla hiyerarşik Prefab üreticileri (`+ PBR Arch Prefab`, `+ Street Lamp`, `+ Physics Crate Stack (5x)`).

### Faz 4: Materyal Stüdyosu, Işıklandırma Önayarları ve Post-Processing (Bloom, GTAO, SSR, TAA, DoF) Tam Kontrolü `[TAMAMLANDI]`
- [x] **4.1** Sağ denetçide (Inspector) ve `Phase 4: Material, Light & Post-FX Studio` panelinde seçili her 3B nesne için canlı **Albedo Color Picker**, **Metallic (`0..1`)**, **Roughness (`0..1`)**, **Albedo Doku Atama** ve 8 hazır PBR materyal önayarı (`Chrome`, `24K Gold`, `Copper`, `Ceramic`, `Car Paint`, `Jade`, `Matte`, `Earth Tex`).
- [x] **4.2** Canlı **Işıklandırma Stüdyosu** (`Atrium Day`, `Golden Hour`, `Neon Night`, `3-Point`) ve **Post-Process & GI Stüdyosu** (`Clean`, `Filmic`, `Fog+Shaft`, `DDGI GI`).

### Faz 5: İskelet Animasyon Sekanslayıcı (Timeline), 3B Fizik ve Karakter Kontrolcüsü `[TAMAMLANDI]`
- [x] **5.1** Görsel **Animation Keyframe Step (`0%`, `25%`, `50%`, `75%`, `100%`) & 19-Joint Skeleton Bone Inspector**: Eklemlerin canlı `Pose` yerel konumlarını ve ebeveyn indekslerini inceleme.
- [x] **5.2** PIE (`F5`) modunda gerçek zamanlı 3B yerçekimi (`-9.81 m/s²`), zemin sıçrama (`restitution`), kutu-kutu çarpışma/yığılma fiziği, ok tuşlarıyla (`Arrow Keys`) `CesiumMan` karakter kontrolcüsü ve `F8`/`ESC` ile PIE'den çıkıldığında sahne dönüşümlerinin otomatik geri yüklenmesi (`snapshot_pre_pie_state` / `restore_pre_pie_state`).

### Faz 6: Tek Tıkla Oyun Paketleme (Standalone Game Package Export) ve Kalite Kapıları `[TAMAMLANDI]`
- [x] **6.1** Editör içinden hazırlanan sahnenin, PBR materyallerin, ışıkların ve shader'ların `File > Export Standalone Game Package (F9)` veya `[*] EXPORT STANDALONE GAME (F9)` butonu ile `build/export/ChromodynamicGame/` (`game.cdproj.json` + `scenes/main.cdscene.json` + `shaders/*.glsl` + `LAUNCH_GAME.bat`) altına tek tıkla bağımsız oyun paketi olarak dışa aktarılması.
- [x] **6.2** Deterministik GPU swapchain PNG yakalama ve CTest doğrulaması.

