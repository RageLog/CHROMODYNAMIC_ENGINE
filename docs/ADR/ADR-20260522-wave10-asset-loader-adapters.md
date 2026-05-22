# ADR-20260522 — Wave 10: AssetRegistry adapter loaders for wav + json

## Bağlam

Wave 4/5'te `cd::asset_wav` ve `cd::asset_json` librarylerini ekledik,
ancak `cd::asset::AssetRegistry` (mevcut central asset cache + loader
dispatch) bunlarla konuşamıyordu. Dosya `text` veya `bytes` olarak
yüklenebiliyordu ama "wav" / "json" tag'iyle yüklenip cache'lenmek
istenirse engine extension yapmak gerekiyordu.

Wave 10 (~13:20–13:25) bu eksiği kapatır.

## Karar

İki header-only adapter eklendi:

* `cd/asset_wav/AssetLoader.hpp` → `cd::asset_wav::WavAssetLoader`
  + `cd::asset_wav::WavAsset` (IAsset wrapper, tag = "wav")
* `cd/asset_json/AssetLoader.hpp` → `cd::asset_json::JsonAssetLoader`
  + `cd::asset_json::JsonAsset` (IAsset wrapper, tag = "json")

Kullanım:

```cpp
cd::vfs::VirtualFileSystem vfs;
cd::asset::AssetRegistry registry { vfs };
registry.register_loader(std::make_unique<cd::asset_wav::WavAssetLoader>());
registry.register_loader(std::make_unique<cd::asset_json::JsonAssetLoader>());

auto wav_id  = registry.load("wav",  "sfx/explosion.wav");
auto cfg_id  = registry.load("json", "data/config.json");

const auto* w = dynamic_cast<const cd::asset_wav::WavAsset*>(registry.find(wav_id));
const auto* j = dynamic_cast<const cd::asset_json::JsonAsset*>(registry.find(cfg_id));
```

Dependency strategy: cd::asset_wav ve cd::asset_json librarylerinin
**linktime** dependency'lerine `cd::asset` eklenmedi (decoder kütüphanesi
standalone kalabilsin diye). Sadece `tests/CMakeLists.txt`'lere
`cd::asset` eklendi — adapter header'ını kullanan consumer'lar ek bir
link satırına ihtiyaç duyar.

## Reddedilen alternatifler

- **`PUBLIC_DEPS cd::asset` runtime kütüphanesinde:** Tüm consumer'lar
  zorunlu olarak cd::asset'i çekerdi. cd::asset_wav'ı sadece bir tool
  / cooker'da kullanmak istenirse fazladan link cost.
- **Adapter'ı cd::asset içine yerleştir:** `cd::asset` her external
  format'ı tanımak zorunda kalır — circular DAG riski.
- **Static initialization / auto-register:** Global registrar'lar
  link-time'da çağrılmasını garanti edemezsin (static init fiasco).
  Explicit `register_loader()` çağrısı daha güvenli.

## Sonuçlar

| Metric | Wave 9 sonu | Wave 10 sonu |
|---|---|---|
| Engine libraries | 48 | **48** (header-only adapter ekstra-yok) |
| Samples | 22 | **22** |
| ctest binaries | 50 | **50** |
| Tests in cd_test_asset_wav | 7 | **9** (+WavAssetLoader 2) |
| Tests in cd_test_asset_json | 19 | **21** (+JsonAssetLoader 2) |
| ADRs (this date) | 25 | **26** |

Total marathon internal tests: ~77.

## Açık sorular

- **Asset registry sample:** `hello_asset_registry` — VFS mount +
  WavAssetLoader + JsonAssetLoader register + 2 dosya load + downcast +
  use. v11 için iyi candidate (~30 dakika).
- **Reflection-based loader discovery:** "format X için" tag → loader
  factory haritası, plugin-friendly. v2.
- **Stream / mmap-backed asset loading:** Şu an `vfs.read()` tüm
  byte'ları RAM'e döker. Büyük WAV / glb için stream loader interface
  gerekir.
