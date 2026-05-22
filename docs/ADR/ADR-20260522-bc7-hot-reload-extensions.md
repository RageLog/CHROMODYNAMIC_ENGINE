# ADR-20260522 — BC7 texture compression + shader hot-reload

## Bağlam

Oturum sonu polish: iki bağımsız ama küçük genişletme. Tek tek ADR
gereksiz; sıkıştırılmış format.

## Karar

### BC7 sıkıştırma (W8.3)

`cd::asset_image::compress_bc7(rgba, w, h, quality)` API'si eklendi:
- `Bc7Quality::kFast` (uber_level=0, ~5 ms/MP)
- `Bc7Quality::kBalanced` (uber_level=2, ~25 ms/MP — default)
- `Bc7Quality::kHigh` (uber_level=4, ~50 ms/MP — shipping cook)

Encoder: **bc7enc** (richgel999, MIT). FetchContent ile `master`
branch'inden çekilir. Upstream `bc7enc.exe` demo target'ı
`EXCLUDE_FROM_ALL=TRUE` ile karantinaya alındı (libm linkage gerektiriyor,
Windows lld-link fatal eror veriyor). bc7enc.c'nin tüm warning'leri
source-file properties ile suppress (vendor TU, `-w` ile global susturma).

API:
```cpp
auto rgba = cd::asset_image::load_image("texture.png");
auto bc7 = cd::asset_image::compress_bc7(rgba->rgba, rgba->width, rgba->height);
// bc7->data ⊕ block_w/block_h → VK_FORMAT_BC7_UNORM_BLOCK upload
```

Non-multiple-of-4 boyutlar zero-padded (v1 limitation; v2'de edge-extend
yapılacak). Mipmap chain caller sorumluluğu.

6 birim test (4 compiler ✓):
- 4×4 tek blok roundtrip
- 16×16 = 4×4 blok grid count
- 5×3 → rounded-up 2×1
- Fast vs High preset farklı çıktı verir
- Sıfır boyut reddediyor
- Kısa rgba buffer reddediyor

### Hot-reload sample (W9.1 finalize)

`samples/hello_hot_reload/` — FileWatcher + CachedCompiler birlikte:
1. Startup'ta temp dir'a default fragment shader yaz
2. FileWatcher.add(path)
3. Her frame poll() — değişiklik var mı?
4. Var ise read_text → CachedCompiler.compile → Material::create →
   renderer.wait_idle → eski Material::~Material → yeni Material yerleş
5. Hata olursa eski Material kullanılmaya devam (typo demo'yu çökertmez)

Headless mode otomatik bir kez kAltFs yazıyor → CI'de reload yolu
mekanik olarak test ediliyor (`--headless 5` → "1 live reload(s)" log
satırı).

## Reddedilen alternatifler

* **bc7enc_rdo** (rate-distortion variant): Repo state stabil değil
  (commit pin bulamadık). Plain bc7enc yeterli — RDO opt-in v2.
* **AMD Compressonator:** Cross-vendor + GUI + ~80 MB. Engine için
  aşırı. CLI integration heavy.
* **NVTT (NVIDIA Texture Tools):** Closed-source binary distribution.
  Ship-able license sorunlu.
* **Hot-reload native (ReadDirectoryChangesW / inotify):** Polling 60
  Hz'de < 1 ms; native API per-OS karmaşıklığı katlamayı haklı
  çıkarmıyor. (Ayrıca FileWatcher API'si polling odaklı; native event
  source ekleseydik aynı interface'i tutardık.)

## Sonuçlar

* Sample sayısı 14 → 15 (+ hello_hot_reload)
* Library sayısı 11 → 11 (bc7 mevcut asset_image'a eklendi, yeni lib
  değil)
* Test sayısı 44 → 45 (+ cd_test_bc7 binary; gerçek bireysel test
  sayısı ~85+ → 91)
* Tüm 4 compiler 45/45 yeşil
* Asset pipeline artık BC7 cook için hazır — `cd_cook_mesh` v2'de
  textured glTF input alıp `.cdmesh` + sidecar `.cdmesh.tex0.bc7`
  yazacak.

## Açık sorular

* BC7 stand-alone CLI tool gerekli mi (`cd_cook_texture`)? Yoksa
  `cd_cook_mesh` v2'ye gömülü mü gelmeli? Asset graph'a göre karar
  verilecek.
* `.shader_cache/` mevcut SPIR-V cache + VkPipelineCache yanı sıra
  `.texture_cache/` (cooked .bc7 blobları) eklenmeli mi? Per-app data
  dir konvansiyonu hâlâ açık (ADR-shader-pipeline-caches'in son
  paragrafı).
