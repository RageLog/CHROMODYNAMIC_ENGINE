# ADR-20260522 — Wave 7: SceneSerializer (cd::scene ↔ JSON)

## Bağlam

Wave 6 sonu: `cd::asset_json` + CVarBridge devrede. Wave 7 (13:45–14:00)
asset_json'ın ikinci somut müşterisi: `cd::scene::Serializer` —
scene graph'ı (LocalTransform + Parent + Children) JSON'a yazıp geri okur.

Bu paha biçilemez bir dev-experience adımı:
- Editor save/load loop için zorunlu altyapı
- Test fixture'larını JSON dosyalarından kurgula
- Diff-friendly format (git ile scene değişikliklerini görmek)
- Asset bundle'lardaki referans entity'leri text-readable tut

## Karar

Header-only `cd/scene/Serializer.hpp` (cd::asset_json'a depend eder
ama cd::scene'in mevcut DAG tier'ı içinde — engine subdirectory order
foundation → asset → render → world olduğu için world tier asset'e
depend edebilir):

### Format (version 1)
```json
{
  "version": 1,
  "nodes": [
    {
      "id": 0,
      "translation": [0, 0, 0],
      "rotation":    [0, 0, 0, 1],
      "scale":       [1, 1, 1],
      "parent": 7
    }
  ]
}
```

* `id` = Entity::id (slot index). Generation atılır — bir round-trip'ten
  sonra fresh world'de yeni slot kabuklarına yerleştirildiği için
  id_map dönülür.
* `parent` field yoksa node root. Entity.id == 0 valid bir id olduğu
  için "0 == no parent" sentinel'i kullanılamaz — bool `has_parent`
  flag struct'ı v1'in kritik bug fix'i.
* Tüm transform alanları opsiyonel (eksikse identity default kullanılır).

### API
```cpp
cd::asset_json::Value json = cd::scene::serialize_scene(scene);
auto id_map = cd::scene::deserialize_scene(scene_out, json);  // Result<IdMap>
```

### Test coverage (5 yeni test, total cd_test_scene 19 → 24)
1. Empty scene → versioned shell ile çıkar.
2. Flat scene (2 root) round-trip: hem position hem scale korunur.
3. Parent-child round-trip: 2-node attach → restore'de parent_of correct.
4. Non-object root reddedilir (kBadShape).
5. Yanlış version reddedilir (kBadVersion).

### Sample: `hello_scene_save` (yeni, 21'inci)
Headless. 1 root + 3 child scene kurar, JSON'a yazar, parse eder,
fresh world'e deserialize eder, parent linkleri ve transform field'ları
doğrular. Smoke harness'ta geçer.

## Reddedilen alternatifler

* **Children listesi de emit et (parent + children iki yönlü):** Round-trip
  için redundant; parent'tan rebuild yeterli. Diff'i temiz tutmak için
  tek-yönlü.
* **Generation'ı da emit et:** Deserialize edilen yeni world'de
  generation 1'den başlar — eski generation'ı taşımak yanıltıcı olur.
  Round-trip için sadece id+map yeterli.
* **JSON Pointer (`$ref: "#/nodes/3"`):** Daha "tip-safe" ama daha
  verbose ve human-readability bozar. Plain int id daha okunaklı.
* **`cd::ecs::WorldSerializer` (genel ECS dump):** Çekici ama component
  type erasure içermiyor. Component-bazlı opt-in serialize için
  reflection / schema gerekecek. Şimdilik scene-specific yeterli.

## Sonuçlar

| Metric | Wave 6 sonu | Wave 7 sonu |
|---|---|---|
| Engine libraries | 48 | **48** (cd::scene'e yeni header) |
| Samples | 20 | **21** (+hello_scene_save) |
| ctest binaries | 50 | **50** |
| Tests in cd_test_scene | 19 | **24** (+5 SceneSerializer) |
| Headless smoke-clean | 20/20 | **21/21** |
| ADRs (this date) | 21 | **22** |

`cd::asset_json`'un consumer count 1 → 2 oldu (hello_json + Serializer).
Header-only Serializer'ın bağımlılık maliyeti sıfır link-time, tam compile-time.

## Açık sorular

* **Other components:** Şu an sadece LocalTransform + Parent yazılıyor.
  Material/Mesh component'leri da include etmek için reflection veya
  ad-hoc per-component handler registry gerekir. v2.
* **Generation flickering:** Aynı world'de save+reload yaparsak yeni
  entity'ler oluşur (eski silinmez); load idempotent değil. v2'de
  "clear before load" opsiyonu eklenebilir.
* **Versioning policy:** v2 format'ı çıktığında v1 nasıl convert
  edilecek? `migrate_v1_to_v2` helper'ları gerekli.
* **Binary form:** JSON ascii ~250 byte/node. Production save dosyaları
  için cdscene binary format çekici. v3.
