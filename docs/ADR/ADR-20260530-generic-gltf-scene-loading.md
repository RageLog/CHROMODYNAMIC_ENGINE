# ADR-20260530 — Generic glTF Scene Loading (SceneNode tree + ingestion)

## Bağlam (Context)

Bugünkü durum: `cd::asset::gltf::load_gltf` çağrısı `GltfScene` POD'u dönüyor
(meshes / materials / textures / skins / animations / nodes / instances).
Bu **veri** yeterli; eksik olan **sample-side glue kodu**:

* `samples/engine/hello_engine/HelloGltf.hpp` her asset için elle yazılmış
  `try_auto_load_gltf` (Sponza) + `try_load_cesiumman_gltf` (animated chr.)
  fonksiyonları içeriyor — `parse_gltf_result` ile flat merge yapıyor, sonra
  hangi mesh, hangi rotation, hangi scale uygulanacağına `main.cpp` /
  `HelloMeshes.hpp` karar veriyor.
* `HelloMeshes.hpp` içinde `PrimitiveKind::kSponza` enum değeri ve Sponza'ya
  özgü `0.01 * scale` + identity rotation hardcoded. CesiumMan için ayrı
  Z→Y dönüşüm + `2.2` scale + skinning wire `main.cpp` içinde elle yazılıyor.
* Per-primitive material bağlama mevcut `GltfPrimRange` ile çalışıyor; ama
  her alpha-test, double-sided, sky-disable kararı sample tarafında if/else.
* Sahne ağacı (`GltfScene::nodes`) **atılıyor** — sadece `instances`
  düzleştirmesi okunuyor; gerçek `LocalTransform`/`Parent`/`Children` ECS
  kayıtları yok. Pick / gizmo / editor için sahne düğümü kavramı kaybolmuş
  durumda.

Kullanıcı geri bildirimi (verbatim): *"her load edilen cisme kod yazılıyor
olması, bunun otomatik ve generic olması... gerçek editör yazdığımızda bu
gibi şeylerle uğraşmak istemiyorum."*

**Vizyon (editor binary'sinden önce gerekli):** drag-drop bir `.gltf` /
`.glb` → engine sahne ağacını + materyalleri + skin'i + bounds'u kurar.
hello_engine **yalnızca** görsel-bug erken tespit için var; per-asset glue
kalmaması gerek.

Bu ADR Section C (editor binary, scripting) **öncesinde** kararlaştırılır
çünkü editor'ün yaslanacağı API kontratı bu.

## Karar (Decision)

İki katmanlı, üretim hedefli sahne-yükleme API'si kurulur. Mevcut
`load_gltf` / `GltfScene` API'si **dokunulmaz** (geri uyumluluk); üzerine
**generic** bir katman serilir.

### Katman 1 — `cd::asset::gltf::LoadedScene` (intermediate)

`engine/asset/include/cd/asset/gltf/SceneLoader.hpp` (yeni header,
header-only veya `.cpp` impl serbest; Architect kararı: `.hpp` kontratı
+ `.cpp` impl Developer'a verilir).

```cpp
namespace cd::asset::gltf {

enum class CoordSystem : std::uint8_t { kYUp = 0, kZUp = 1 };

enum class LoadError : std::uint32_t {
    kFileNotFound, kParseFailed, kEmptyScene, kUnsupportedFeature
};

/// Generic loaded scene. Everything needed to render ANY glTF without
/// per-asset code on the sample/editor side.
struct LoadedScene {
    struct Node {
        std::string                       name;
        cd::math::Mat4f                   local_transform { cd::math::Mat4f::identity() };
        std::vector<std::uint32_t>        children;
        std::optional<std::uint32_t>      mesh_idx;
        std::optional<std::uint32_t>      skin_idx;
        cd::math::AABB                    bounds_local {};
    };
    struct Primitive {
        cd::asset::PrimitiveMesh          mesh;          // already merged-ready
        std::uint32_t                     material_idx { 0 };
        GltfAlphaMode                     alpha_mode { GltfAlphaMode::kOpaque };
        float                             alpha_cutoff { 0.5F };
        std::vector<GltfSkinVertex>       skin_vertices; // empty if non-skinned
    };
    struct Mesh    { std::vector<Primitive> primitives; };
    struct Material {
        cd::math::Vec4f                   base_color_factor { 1,1,1,1 };
        std::optional<std::uint32_t>      base_color_tex;
        std::optional<std::uint32_t>      mr_tex;
        std::optional<std::uint32_t>      normal_tex;
        std::optional<std::uint32_t>      emissive_tex;
        std::optional<std::uint32_t>      occlusion_tex;
        float                             metallic_factor { 1.0F };
        float                             roughness_factor { 1.0F };
        cd::math::Vec3f                   emissive_factor { 0,0,0 };
        GltfAlphaMode                     alpha_mode { GltfAlphaMode::kOpaque };
        float                             alpha_cutoff { 0.5F };
        bool                              double_sided { false };
    };
    struct Texture {
        cd::asset::DecodedImage           image;         // RGBA8 decoded
        // sampler info (wrap S/T, min/mag filter) — minimal struct in v1
    };
    struct Skin {
        std::vector<std::uint32_t>        joints;        // indices into nodes
        std::vector<cd::math::Mat4f>      inverse_bind_matrices;
        std::optional<std::uint32_t>      skeleton_root;
    };
    struct Animation {
        std::string                       name;
        float                             duration_seconds { 0 };
        std::vector<GltfAnimChannel>      channels;      // reuse existing struct
        std::vector<GltfAnimSampler>      samplers;
    };

    std::vector<Node>       nodes;
    std::vector<std::uint32_t> root_nodes;
    std::vector<Mesh>       meshes;
    std::vector<Material>   materials;
    std::vector<Texture>    textures;
    std::vector<Skin>       skins;
    std::vector<Animation>  animations;

    /// Auto-detected from `asset.generator` string + bbox heuristic.
    /// glTF spec is Y-up by default; some Blender/3DS exports are Z-up.
    CoordSystem                source_coords { CoordSystem::kYUp };

    /// Engine-ready world transform: axis-convert + a scale derived from
    /// bbox so the asset lands roughly in the [-5..+5] meter range no
    /// matter what unit the exporter used. Caller MAY override but the
    /// default produces a sensible view with zero per-asset code.
    cd::math::Mat4f            suggested_world_xform { cd::math::Mat4f::identity() };

    /// World-space AABB after applying suggested_world_xform.
    cd::math::AABB             bounds_world {};
};

[[nodiscard]] auto load_scene(std::filesystem::path glb_or_gltf_path)
    -> std::expected<LoadedScene, LoadError>;

}  // namespace cd::asset::gltf
```

**Anahtar invariantlar:**

1. `LoadedScene` **referans-içermez** — tüm veri owning. ECS / RHI ingest
   sonrası serbest bırakılabilir.
2. `suggested_world_xform` deterministik: `(source_coords, bbox_size)`
   → axis-convert × uniform_scale(target_extent / max_bbox_edge).
   Editor "frame selected" davranışı bu kontratla uyumlu.
3. `material_idx` ve `mesh_idx` daima `< vector.size()` (validate at load).
4. `nodes[i].children` indeksleri **forward-only** (DAG değil tree); cycle
   yasak — yükleyici reddeder (`kUnsupportedFeature`).

### Katman 2 — `cd::render::scene::ingest_gltf_scene` (engine-side)

`engine/render/render/include/cd/render/SceneIngest.hpp` (yeni header).

```cpp
namespace cd::render::scene {

struct IngestOptions {
    cd::math::Mat4f world_xform { cd::math::Mat4f::identity() };  // override
    bool            use_suggested_xform { true };                  // default
    bool            create_ecs_nodes     { true };
    bool            wire_skinning        { true };
    bool            upload_textures      { true };
};

struct IngestResult {
    cd::ecs::Entity                root_entity {};
    std::vector<cd::ecs::Entity>   node_entities;   // index-aligned with LoadedScene::nodes
    std::vector<cd::render::GpuMesh> gpu_meshes;     // index-aligned with LoadedScene::meshes
    std::vector<cd::rhi::TextureHandle> gpu_textures;
    std::vector<cd::rhi::TextureViewHandle> gpu_texture_views;
    cd::math::AABB                 bounds_world {};
};

[[nodiscard]] auto ingest_gltf_scene(cd::rhi::IDevice&                device,
                                     cd::ecs::World&                  world,
                                     cd::scene::Scene&                scene,
                                     const cd::asset::gltf::LoadedScene& src,
                                     IngestOptions opts = {})
    -> std::expected<IngestResult, cd::core::ErrorCode>;

}  // namespace cd::render::scene
```

**Sorumluluk haritası (Developer için):**

* Tüm `LoadedScene::meshes[*].primitives[*].mesh`'i `cd::render::upload_mesh`
  ile GPU'ya yükler.
* Tüm `textures[*].image`'i `device.create_texture` + view ile yükler;
  sampler glTF wrap/filter'a göre.
* Her `LoadedScene::Node` için `cd::ecs::World::create()` + Scene'e
  `LocalTransform { decompose(node.local_transform) }` + Parent/Children
  bileşenleri yazar.
* Mesh-li node'larda `Renderable { gpu_mesh_idx, material_indices_per_prim }`
  bileşeni; skin-li node'larda ek `SkinRef { skin_idx }`.
* `IngestResult::root_entity` tek bir gizli wrapper root altında tüm
  `src.root_nodes` toplar — sample / editor tek bir entity üzerinden
  delete edebilir.

### Sample / editor kullanım kontratı

```cpp
// Önceden (hardcoded per-asset):
//   try_auto_load_gltf(...);
//   model_scale = 0.01F; model_rotation = identity;
//   ... HelloMeshes::PrimitiveKind::kSponza ...

// Sonra (generic):
auto sponza  = cd::asset::gltf::load_scene("assets/samples/Sponza/Sponza.gltf");
auto cesium  = cd::asset::gltf::load_scene("assets/samples/CesiumMan.glb");
auto r1 = cd::render::scene::ingest_gltf_scene(device, world, scene, *sponza);
auto r2 = cd::render::scene::ingest_gltf_scene(device, world, scene, *cesium);
// per-asset rotation/scale/alpha-mode/skin yok; her şey LoadedScene'den.
```

## Reddedilen alternatifler

1. **Mevcut `GltfScene` + sample-side enum yaklaşımını korumak.** Şu anki
   durum. N asset'e ölçeklenmez; editor binary'sini doğrudan engelliyor;
   kullanıcı şikayetinin tam hedefi.
2. **External library kullan (USD-runtime, glTF-Sample-Viewer).** USD
   tooling 100 MB+, transit dep ağacı; glTF-Sample-Viewer Khronos
   reference impl runtime için fazla bağımlı (own GL/WebGL bootstrap).
   ADR-006 + ADR-20260522-asset-loader-tier hattını ihlal eder. Reddedildi.
3. **glTF'i bake-time format'a derle (cd::asset_cdmesh + cdtex genişlet).**
   Üretim için *istenecek* (Phase 2); ama editor'de drag-drop hot-load
   kapanır — geliştirici workflow regresyonu. Future optimization olarak
   açık tutuluyor, bu round'da değil.
4. **Sahne ağacını ECS'e değil, ayrı `SceneGraph` struct'ına yazmak.**
   ADR-004 (ECS-scene mimari) + `cd::scene::Scene` zaten ECS üzerine bina
   edilmiş hiyerarşi sahibi — paralel sahne ağacı duplikasyon olur. Reddedildi.
5. **Yükleme sırasında doğrudan ECS'e yazmak (Katman 1 yok, sadece Katman 2).**
   `LoadedScene` POD'unu kaldırmak test edilebilirliği bozar (asset round-trip
   test'leri GPU'suz yapılamıyor olur) ve cook-tool (Phase 2 derlenmiş
   format) için intermediate representation kalmamış olur. Reddedildi.

## Sonuçlar (Consequences)

**Olumlu:**

* Editor binary herhangi bir glTF'i drag-drop yükleyebilir; per-asset glue
  kod yok.
* hello_engine `HelloGltf.hpp` ~250 satırdan ~30 satıra iner (sadece
  `load_scene` + `ingest_gltf_scene` çağrısı).
* `HelloMeshes::PrimitiveKind::kSponza` silinir; Sponza artık özel değil.
* Skin auto-wire; `cesium_skinned` özel-alanı kalkar.
* Per-primitive material auto-bind; `prim_ranges` yapısı `IngestResult`
  içine taşınır, sample tarafında inşa edilmez.
* Editor + future scripting layer ortak API kullanır (sample/editor parity).

**Olumsuz / migration yükü:**

* `HelloGltf.hpp` + `HelloMeshes.hpp` + `main.cpp` Sponza/CesiumMan yolları
  yeni API'ye taşınmalı. Tahmini ~150 satır net azalış.
* Mevcut testler (test_gltf_loader) `GltfScene`'i kullanmaya devam eder;
  yeni `load_scene` için **yeni** test eklenir (mevcut testleri bozmaz).
* `cd::asset::DecodedImage` yoksa (mevcut: `GltfTexture::rgba + width +
  height`) küçük bir tip uyumu PR'i gerekecek; Architect öneri: mevcut
  `GltfTexture` struct'ını `LoadedScene::Texture::image` alanı olarak
  reuse et — yeni tip icat etmeyiz.

**Geri uyumluluk:**

* `load_gltf` + `GltfScene` **kaldırılmaz**. Yeni kod `load_scene` kullanır;
  eski test'ler/cook-tool yolları aynen çalışır. Phase 2'de eski API
  deprecate edilebilir (ayrı ADR).

## Implementation plan

### Phase 1 (bu wave, ~3 gün, Developer'a verilir):

* P1.1 — `SceneLoader.hpp` + `SceneLoader.cpp` (`load_scene` impl —
  içte `load_gltf` çağırır, axis detect + bbox-driven `suggested_world_xform`
  hesaplar, `GltfScene` → `LoadedScene` map'ler).
* P1.2 — `SceneIngest.hpp` + `SceneIngest.cpp` (`ingest_gltf_scene` impl
  — `upload_mesh` × N, texture upload × N, ECS entity × N, Renderable/SkinRef
  bileşeni).
* P1.3 — `samples/engine/hello_engine` migration: `HelloGltf.hpp`
  şişkinliğini söker, `try_auto_load_gltf` / `try_load_cesiumman_gltf`
  yerine generic 2 satırlık çağrı. `HelloMeshes::kSponza` silinir.
  `HelloSkinned.hpp` glue'su `IngestResult::node_entities[skin_root]`
  üzerinden çalışır.
* P1.4 — yeni gtest: `test_scene_ingest.cpp` — minimal in-memory glTF
  (procedurally üretilmiş tek-mesh + tek-skin) → `load_scene` → mock device
  ile ingest → 1 root entity + N children + bounds doğrulama. Test 109/109
  → 110/110.

**Kanıt:** `cmake --build --preset ninja-debug` temiz; `ctest --preset
ninja-debug --output-on-failure` PASS; Sponza + CesiumMan görselde aynı
(no rendering regression).

### Phase 2 (gelecek wave, ayrı ADR ile aç):

* Animation playback otomatik wire (LoadedScene::animations[*] → cd::anim
  AnimationPlayer component'ı).
* PBR material extension'ları (clearcoat, sheen, anisotropy, KHR_*).
* Morph target.
* Cook-tool entegrasyonu: `LoadedScene` → `.cdscene` binary; runtime
  yalnız binary'i okur (drag-drop fallback olarak `load_scene` mevcut kalır).
* KHR_lights_punctual: `LoadedScene::Light` + ECS Light component auto-wire.

## Paralel görsel-bug onarımı ile uyum

Kullanıcı aynı brief'te 7 görsel sorun rapor etti. Bunlar bu ADR'nin
**dışında** kalır ama ADR'nin sunduğu generic-load API ortaya çıkınca
hangileri sample-side, hangileri shader/RHI-side temizlenmeli — özet:

| Sorun                              | Katman          | Notlar                                            |
|------------------------------------|-----------------|----------------------------------------------------|
| Sponza'da siyah artefakt           | Shader / depth  | 0.01 scale → near-plane z-fight; `ingest_gltf_scene` `suggested_world_xform` 1.0 scale'e taşırsa otomatik düzelir. |
| Karakter yanlış (yanlış aktör)     | Sample          | CesiumMan slot artık `load_scene("CesiumMan.glb")` ile garanti; yanlış fallback kalkar. |
| Sun shadow iç mekânda yok          | Shadow map      | Cascade config / resolution, ADR dışı; ayrı CSM PR. |
| Vejetasyon uzakta kırılır          | Sample camera   | `far_z` ayarı + LOD; `IngestResult::bounds_world` cam-far seçimini besleyebilir (helper). |
| Tüm ışıklar kapalıyken sahne görünür | Shader (kPrimFS) | IBL gate sun-off flag; ADR dışı, shader fix. |
| Metal PBR yansıma env göstermiyor   | Shader / IBL    | spec env sampler binding; ADR dışı. |
| Gölgeler genel olarak sorunlu      | Multiple        | CSM + alpha-test gölgesi (MASK vs BLEND distinction); D-F3 phase388 üzerine yeni alpha-mode pass'i gerekiyor. `LoadedScene::Material::alpha_mode` bu ayrımı zaten taşıyor; shader-side kullanılacak. |

Bu görsel-bug grubu paralel Developer ajanlarına (her biri ayrı dosya/
katman) dağıtılır; ADR'nin generic-load yolu açıldığında alpha-mode +
bounds + per-prim material doğru veriyle besleneceği için bazıları
"ucuza" çözülecek (Sponza black artifact + vegetation MASK).

## İlgili ADR'ler

* ADR-006 — Asset pipeline (intermediate-then-cook prensibi)
* ADR-20260522-asset-loader-tier — `cd::asset_gltf` library boundary
* ADR-004 — ECS-scene mimari (LocalTransform / Parent / Children)
* ADR-002 — Renderer mimari (GpuMesh, MeshUpload helper'ı)
* ADR-012 — Editor mekaniği (drag-drop entry-point hedefi)
