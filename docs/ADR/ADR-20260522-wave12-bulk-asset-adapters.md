# ADR-20260522 — Wave 12: bulk asset-loader adapters (image / obj / ktx2)

## Bağlam

Wave 10 cd::asset_wav ve cd::asset_json için AssetLoader adapter
header'ları ekledi. Wave 11 bunları bir sample'da gösterdi. Asset
katmanının geri kalanı (cd::asset_image, cd::asset_obj, cd::asset_ktx2)
hâlâ AssetRegistry'nin dışındaydı — runtime/editor onları load etmek
isterse her loader'ı manuel sarmak zorundaydı.

Wave 12 (~14:55–15:10) bu son üç adapter'ı ekler.

## Karar

Üç yeni header-only adapter:

* `cd/asset_image/AssetLoader.hpp` → `cd::asset_image::ImageAssetLoader`
  + `cd::asset_image::ImageAsset` (tag = "image"). PNG/JPG/BMP/TGA/HDR
  v.b. — alttaki `load_image_from_memory` dispatch eder.
* `cd/asset_obj/AssetLoader.hpp` → `cd::asset_obj::ObjAssetLoader`
  + `cd::asset_obj::ObjAsset` (tag = "obj"). Bytes reinterpret olarak
  `string_view` → `parse_obj`.
* `cd/asset_ktx2/AssetLoader.hpp` → `cd::asset_ktx2::Ktx2AssetLoader`
  + `cd::asset_ktx2::Ktx2Asset` (tag = "ktx2"). `load_from_memory`.

`tests/CMakeLists.txt`'lere `cd::asset` dep eklendi. Her test binary'sine
1 yeni adapter testi (toplam +3 internal test).

## Reddedilen alternatifler

- **cdmesh / cdtex / gltf için de adapter:** Bunların `*_from_memory`
  entry point'leri yok; mevcut API ile RAM buffer'dan decode etmek
  mümkün değil. Ya bunlara `*_from_memory` eklemek ya da AssetRegistry'e
  `register_decoder_for_path(path)` pattern'i — her ikisi de v2.
- **Genel-amaçlı template adapter:** Ortaktaki kalıp 30 satır; her
  format'ın specific API'si farklı (load_from_memory vs parse vs load).
  Template'leştirmek erken.
- **Self-registering loader macros:** CD_REGISTER_LOADER(WavAssetLoader);
  pattern güzel ama static init ordering riski + global state. Explicit
  `registry.register_loader(...)` daha güvenli.

## Sonuçlar

| Metric | Wave 11 sonu | Wave 12 sonu |
|---|---|---|
| Engine libraries | 48 | **48** (header-only adapter, yeni linkage yok) |
| Samples | 23 | **23** |
| ctest binaries | 50 | **50** |
| AssetLoader adapter count | 2 (wav, json) | **5** (+image, obj, ktx2) |
| Marathon internal tests | ~77 | **~80** (+3 adapter tests) |
| ADRs (this date) | 26 | **28** |

cd::asset::AssetRegistry artık image, obj, ktx2, wav, json formatlarını
out-of-the-box destekliyor. Bu, "1 line bootstrap" akışını mümkün
kılıyor:

```cpp
registry.register_loader(std::make_unique<cd::asset_image::ImageAssetLoader>());
registry.register_loader(std::make_unique<cd::asset_obj::ObjAssetLoader>());
registry.register_loader(std::make_unique<cd::asset_ktx2::Ktx2AssetLoader>());
registry.register_loader(std::make_unique<cd::asset_wav::WavAssetLoader>());
registry.register_loader(std::make_unique<cd::asset_json::JsonAssetLoader>());
```

## Açık sorular

- **cdmesh / cdtex / gltf adapter'ları:** Her birinde `_from_memory`
  helper'ı ekleyince adapter'ları yazılır. v13.
- **path-extension → tag dispatch:** `registry.load_auto("file.png")`
  uzantıya bakıp doğru loader'ı çağırsın. v2.
- **Stream-able / chunked loader:** Mevcut interface tüm byte'ları
  RAM'e yükler. Çok büyük asset'ler için iterator-based decode v2.
