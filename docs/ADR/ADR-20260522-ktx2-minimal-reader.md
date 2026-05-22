# ADR-20260522 — KTX2 minimal runtime reader (cd::asset_ktx2)

## Bağlam

Engine'in iki paralel texture pipeline'ı oluştu:

* **cdtex** (cd::asset_cdtex): engine-internal BC7 format. cd_cook_texture
  PNG → .cdtex. Hızlı, minimum overhead. `hello_textured_cooked` sample
  bunu kullanıyor.
* **KTX2**: industry interchange format. NVIDIA Texture Tools, AMD
  Compressonator, toktx (Khronos), Microsoft DirectXTex, Basis Universal
  toolchain hepsi KTX2 üretiyor. glTF 2.0 `KHR_texture_basisu` uzantısı
  KTX2'yi kullanır.

cdtex sahip olduğumuz workflow için ideal ama dış content'i kabul etmek
istediğimizde KTX2 zorunlu. Bu ADR minimal bir KTX2 runtime reader
ekledi.

## Karar

Yeni library `cd::asset_ktx2` (engine/asset_ktx2/). Sorumluluk:

* Tek-2D-texture KTX2 dosyalarını okur (single face, single layer)
* `vkFormat` short-list: RGBA8 Unorm/SRGB, BGRA8 Unorm/SRGB, BC7
  Unorm/SRGB
* Multi-mip OK; mip 0 = base level (KTX2 spec v2 invariant)
* `supercompressionScheme == 0` (no Basis / Zstd) — basisu transcoder
  v2 hedefi
* Tüm parse hand-rolled (dependency: cd::core only)
* 8 birim test, hepsi 4 compiler altında ✓

API:

```cpp
auto r = cd::asset_ktx2::load("texture.ktx2");
if (r.has_value()) {
  for (auto& m : r->mips) {
    upload_to_gpu(m.bytes, m.width, m.height, r->format);
  }
}
```

Hata domain'i `cdtex` ile aynı şema:
* `kFileNotFound`, `kIoError`, `kMagicMismatch`, `kCorrupt`
* `kUnsupportedFormat` — vkFormat short-list'in dışında (kullanıcı
  cdtex'e cook etsin)
* `kUnsupportedSupercompression` — Basis/Zstd geleceğin işi

## Reddedilen alternatifler

* **libktx (Khronos reference):** ~1500 SLOC + libdeflate + Basis +
  vulkan-headers transitive. Bundling ~30 MB, vendor-managed. Engine'in
  cd::asset_image (stb_image), cd::asset_obj (hand-rolled), cd::asset_gltf
  (tinygltf) DAG pattern'i ile uyumsuz.
* **KTX1:** Deprecated by Khronos, glTF 2.0 KHR_texture_basisu yalnızca
  KTX2 destekliyor. KTX1 reader yazmak teknik borç.
* **Basis Universal direct:** "Transcoder-then-runtime-encode" pattern
  güçlü ama transcoder boyutu büyük (~150KB binary cost) ve cd::asset_ktx2
  v1 zaten BC7-only senaryoyu çözüyor. Basis v2'de.

## Sonuçlar

* Yeni library: `cd::asset_ktx2` (45 → 45 — aslında bu 45'inci).
* Test sayısı 46 → 47 (`cd_test_asset_ktx2` 8 test).
* Tüm 4 compiler (MSVC, clang-cl, GCC 15, LLVM Clang) 47/47 yeşil.
* Yeni asset pipeline DAG:
  ```
   .png/.jpg/.hdr ──┐
                    ├──► cd_cook_texture ──► .cdtex (engine internal)
   .ktx2 ───────────┴──► cd::asset_ktx2 ──► Ktx2 (runtime, RAM)
  ```

## Açık sorular

* Basis Universal transcoder entegrasyonu (v2) — third-party header
  pile (~1MB), MIT, performant. Cost/benefit analizi gerekli.
* KTX2 cooker (cd_cook_ktx2) ihtiyacı var mı? toktx zaten standalone CLI;
  bizim engine-internal cook için cdtex var. KTX2 daha çok import path.
* `cd::asset_gltf` KHR_texture_basisu extension parser — glTF içinde
  embed edilmiş KTX2'yi açabilmek için. v2 önceliği.
