# ADR-20261112-source2-clean-modular-architecture: Valve Source 2 Esinli Temiz Modüler Mimari, Sıfır Kod Tekrarı ve Godot 4 / Hammer 5 Stüdyo UI/UX Yeniden Yapılandırması

- **Status**: Accepted
- **Date**: 2026-11-12
- **Layer Rank**: Level 5 (`apps/common`, `apps/editor`, `apps/player`), Level 2 (`engine/asset/obj`), Level 3 (`engine/render/render2d`, `engine/ui`)
- **Subsystems**: `cd::app_common`, `cd_editor_lib`, `cd_app_editor`, `cd_player`, `cd::asset_obj`, `cd::render2d`

***

## 1. Bağlam ve Mevcut Yapısal Sorunların Kök Neden Analizi (Context & Root-Cause Diagnosis)

`apps/editor/` ve `apps/player/` katmanları üzerinde gerçekleştirilen adli kod denetimi, alt-seviye `engine/` kütüphanelerinin temiz DAG yapısına (`check_layer_dag.py`: `0` ihlal) rağmen uygulama ve editör katmanında beş kritik mimari bozulma biriktiğini ortaya koymuştur:

1. **Özel Tamamlanmamış PIMPL Tuzağı (`StudioApp::EngineState`)**:
   - `apps/editor/StudioApp.hpp` içinde `private: struct EngineState;` olarak ileri-bildirilen yapı yalnızca `apps/editor/main.cpp:3752` satırında (112+ alanlı dev blok) tanımlanmıştır.
   - Başka hiçbir başlık veya `.cpp` çeviri birimi `EngineState` tipini somut olarak göremediği için 45 başlık dosyası `template <typename StateT, typename LogFn>` ördek-tipli (duck-typed) şablonlara dönüşmüştür.
2. **Sözcüksel `#include` Sırası Bağımlılığı ve Sıkışmış Tipler**:
   - `PrimitiveKind`, `SceneEntity`, `SelKind`, `LightRow`, `GizmoMode`, `GizmoState` ve `PrevCamBasis` tipleri `apps/editor/main.cpp:259-391` aralığında iki `#include` bloğunun tam ortasında küresel kapsamda tanımlanmıştır.
   - Bu yapı derleme sırasını kırılganlaştırmış ve tüm editör kodunun (`~34,000+` satır) tek bir seri `main.cpp.obj` içinde derlenmesine yol açmıştır.
3. **Gizli Başlık İçi Tekillikler ve Oyuncak Deney Yükleri**:
   - Özel `EngineState` engelini aşmak için `StudioModifierClasses.hpp` içinde `studio_world()` ve `Studio2DWorkspace.hpp` içinde `studio_2d()` fonksiyon-içi statik tekillikleri oluşturulmuştur.
   - Ayrıca `EngineState` içine ticari oyun geliştirme iş akışına hizmet etmeyen yapay ağ simülasyonu (`net_throttle`, `net_snapbuf`), yapay `sleep_for(120ms)` beklemeli akış testi (`streamer_tracked`), PCG32 rastgele sayı histogramları (`Histogram`) ve `4x4` sanal doku deneme tabloları (`vt_table`) eklenmiştir.
4. **Çoklu Noktada Tekrarlanan Yardımcı Kodlar**:
   - Kamera baz vektörleri (`fwd`, `right`, `up`), ışın-küre/düzlem kesişimi ve kuaterniyon-Euler dönüşümleri 7 farklı noktada kopyalanmıştır.
   - Vulkan takas-zinciri (`swapchain`) okuma ve PNG yazma kodu `apps/common/GoldenCapture.hpp`, `apps/editor/main.cpp:6021-6135` ve `apps/player/main.cpp:71-181` (el yapımı sıkıştırmasız PNG/CRC32 yazıcısı + 5x7 piksel font tablosu) arasında üç kez tekrarlanmıştır.
   - `StudioCustomMeshLoader.hpp`, motorda zaten bulunan `cd::asset_obj` (`cd::asset::obj::load_obj`) yerine `try / catch (...)` içeren mükerrer bir `.obj` ayrıştırıcısı barındırmaktadır.
   - Bağımsız oyun paketleme kodu `StudioSceneBuilder.hpp` ve `Studio2DWorkspace.hpp` arasında ikiye bölünmüştür.
5. **UI/UX Karmaşası ve Katman Sızıntısı**:
   - Editörün üst şerit araç çubuğu (`Ribbon Toolbar`) fizik çözücü başlığı olan `StudioPhysicsSolver.hpp:1357-1576` içine yazılmıştır.
   - Sol sahne panelinde 12 sıkışık oluşturma düğmesi, satır başı kırmızı `[X]` silme tuzakları ve sahne ağacından kopuk ayrı ışık listesi; orta görünüm alanında havada asılı 18 akademik tanı grafiği; alt panelde ise 4 bölmeli karmaşık `Disk -> Active Context` ara aşaması kullanıcı deneyimini zorlaştırmaktadır.

***

## 2. Valve Source 2 Referans Modeli ve Ticari İlerleme Hedefleri (Decision & Architecture)

Ticari oyun motoru hedeflerimiz kademeli olarak **1. Godot 4 (hız, sadelik, anında sahne ağacı) -> 2. Unity 6 (bileşen/şema iş akışı ve doğrudan varlık tarayıcısı) -> 3. Unreal Engine 5 (bindless Vulkan 1.3 PBR, donanım ışın izleme ve ileri fizik)** sırasını izler. Yazılım ve araç mimarisinde birincil yapısal rol modelimiz **Valve Source 2** mimarisidir:

| Valve Source 2 Katmanı | CHROMODYNAMIC C++23 Karşılığı | Sorumluluk ve Sınır Kontratı |
| :-- | :-- | :-- |
| **`tier0` / `tier1`** | `cd::core`, `cd::math`, `cd::mem`, `cd::concurrency`, `cd::platform`, `cd::vfs`, `cd::log` (`L0-L1`) | Bellek güvenliği, `Result<T>` (`std::expected`), iş-çalmalı paralel görev çizgesi ve işletim sistemi soyutlaması. |
| **`schemasystem`** | `cd::schema` (`L0`) | Öz-betimleyici tip/alan yansıması, otomatik Inspector üretimi ve JSON Schema Draft 2020-12 dışa aktarımı (`--dump-schema`). |
| **`resourcesystem`** | `cd::asset`, `cd::asset_gltf`, `cd::asset_obj`, `cd::asset_image`, `cd::asset_ktx2`, `cd::asset_pak` (`L2`) | Tekil ve kanonik varlık yükleyicileri (`.gltf`, `.obj`, `.png`, `.ktx2`, `.aseprite`, `.ldtk`) ve `CDPK` (`.cdpak`) arşivleme. |
| **`rendersystem` & `worldrenderer`** | `cd::rhi*`, `cd::material`, `cd::render`, `cd::scene_renderer`, `cd::render2d` (`L2-L3`) | Vulkan 1.3 dinamik çizim, bindless doku dizileri, BLAS/TLAS ışın sorguları, 2D Y-sort/Tilemap/Işık ve PBR/CSM/Bloom/TAA geçişleri. |
| **`vphysics2` (Rubikon)** | `cd::physics` (`v2d`, `v3d`, `UnifiedPhysicsEngine`) (`L2`) | 2D yumuşak-adımlı platform fiziği ve 3D katı gövde, aktif ragdoll, araç ve kumaş simülasyonu. |
| **`engine2` & `app_common`** | `cd::runtime` + `cd::app_common` (`L4-L5`) | Birleşik CLI/UTF-8 dosya G/Ç (`FileTextIo`), kanonik 3D kamera/ışın matematiği (`CameraMath3D`), tekil PNG/Swapchain yakalayıcı (`FrameCapture`) ve `.cdpak` paketleyici (`PackageExporter`). |
| **`toolframework2` / Hammer 5** | `cd_editor_lib` (`apps/editor/{core,render,scene,physics,pipeline,ui,workspace_2d}/`) (`L5`) | Somut C++23 veri tipleri (`StudioTypes`, `StudioConfig`, `StudioLogRing`), paralel derlenen `.cpp` modülleri ve `< 80` satırlık ince `apps/editor/main.cpp` giriş noktası. |

***

## 3. Somut Yeniden Yapılandırma Kuralları (Concrete Contracts)

1. **`cd::app_common` Derlenen Statik Kütüphanesi (`apps/common/`)**:
   - `FileTextIo`: UTF-8 BOM temizlemeli metin okuma ve atomik dosya yazma.
   - `CameraMath3D`: Tekil `CameraBasis3D`, ekran-ışın izdüşümü, dünya-ekran dönüşümü, kuaterniyon-Euler dönüşümleri ve ızgara yakalama (`snap`) matematiği.
   - `FrameCapture`: Vulkan takas-zinciri BGRA8 -> RGBA8 geri okuma + `cd::asset::image::write_png_rgba` kaydı + yazılım `DrawBatcher` PNG dökümü. `apps/editor/main.cpp` ve `apps/player/main.cpp` içindeki mükerrer PNG kodları tamamen silinir.
   - `PackageExporter`: Tekil `.cdpak` + `cd_player.exe` + çalışma zamanı DLL paketleyicisi.
2. **İnce Giriş Noktaları (`< 80` Satır `main.cpp`) ve Paralel Modüller**:
   - `apps/editor/main.cpp` ve `apps/player/main.cpp` dosyaları yalnızca CLI ayrıştırması ve uygulama yaşam döngüsü başlatmasından sorumlu `< 80` satırlık saf giriş noktalarına indirgenir.
   - `apps/editor/` altındaki kodlar `core/`, `render/`, `scene/`, `physics/`, `pipeline/`, `ui/` ve `workspace_2d/` alt dizinlerine ayrılır ve `cd_editor_lib` statik kütüphanesi olarak Ninja üzerinde paralel derlenir.
   - `StudioCustomMeshLoader` kendi özel `.obj` ayrıştırıcısını silerek doğrudan `cd::asset::obj::load_obj` (`cd::asset_obj`) çağırır.
3. **Temiz Hammer 5 + Godot 4 Stüdyo UI/UX Düzeni**:
   - Fizik çözücü içindeki mükerrer şerit araç çubuğu kaldırılarak tek satırlık birleşik Üst Çubuk (`File | Edit | Create | View | Project | Help`, ortada `[ 2D World ] [ 3D World ] [ Shader / Graph ]`, sağda `[ Play (F5) ] / [ Stop (Esc) ]`, `[ Package .cdpak (F9) ]` ve sade FPS rozeti) oluşturulur.
   - Sol `Scene` paneli: Tekil `[ + Add Node ]` açılır menüsü, arama filtresi ve ışıkların da birinci sınıf düğüm olduğu birleşik sahne ağacı (satır başı kırmızı `[X]` tuzakları ve 12'li sıkışık düğme dizisi kaldırılır).
   - Orta Görünüm Alanı (`Viewport`): Sol üstte sade Source 2 hapları (`[Lit PBR]`, `[Persp]`, `[Move W | Rotate E | Scale R]`, `[Snap]`, `[Gizmos]`) ve sağ üstte etkileşimli 3D X/Y/Z Yönelim Gülü; 18 akademik tanı grafiği varsayılan görünümde kapatılarak `Gizmos -> Diagnostic Probes` altına alınır.
   - Sağ `Inspector`: Katlanabilir temiz bileşen kartları (`Transform`, `Mesh & Material`, `Physics`, `Light`, `[ + Add Component ]`); keyfi `Tilt -25` / `Yaw +45` ve mükerrer işletim sistemi diyalog düğmeleri kaldırılır.
   - Alt `Project & Tools` Rıhtımı: 4 bölmeli `Active Context` darboğazı yerine doğrudan sürükle-bırak destekli standart 2 bölmeli (`Klasör Ağacı + Varlık Izgarası`) `Assets & FileSystem` tarayıcısı, sahte demo düğmelerinden arındırılmış `Console`, `Profiler` ve `AI CommandBus` sekmeleri.

***

## 4. Sonuçlar ve Doğrulama Kapıları (Consequences & Verification Gates)

- **Olumlu Etkiler**:
  - `apps/editor/main.cpp` (`6,455 -> < 80` satır) ve `apps/player/main.cpp` (`1,516 -> < 80` satır) monolitleri ortadan kalkar; alt sistemler birbirinden bağımsız derlenir ve birim test edilebilir hale gelir.
  - Kod tekrarı (PNG yazıcı, kamera matematiği, `.obj` ayrıştırıcı, `.cdpak` dışa aktarıcı, 5x7 piksel font tablosu) sıfırlanır.
  - Hem insan geliştiriciler hem AI ajanları için gürültüsüz, sezgisel bir Godot 4 / Source 2 stüdyo arayüzü sağlanır.
- **Doğrulama Kapıları**:
  1. `check_layer_dag.py --engine-root engine`: `violations=0 PASS`.
  2. `check_human_voice.py`: Yeni ve güncellenen mimari belgelerde sıfır AI klişesi / em-dash ihlali (`PASS`).
  3. `ctest`: `cd_test_app_common`, `cd_test_schema`, `cd_test_render2d`, `cd_test_world2d`, `cd_test_splash_audio_cue`, `cd_test_input_recorder_editor_binding`, `cd_editor_headless_pie_smoke` ve `cd_player_all_showcases_smoke` testlerinin tamamı `%100 PASS`.
  4. Canlı GPU Görsel Doğrulama (`visual-quality-loop`): Yenilenen 3D World, 2D World, PIE ve Project Hub ekran görüntülerinin piksel düzeyinde incelenerek doğrulanması.
