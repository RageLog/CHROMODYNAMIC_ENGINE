# ADR-20260522 — Asset-loader tier (glTF + OBJ + image + cdmesh)

## Bağlam (Context)

Phase 3 başında engine'in tek ithal yolu yoktu — `cd::asset` INTERFACE bir
header (IAssetLoader / IAsset) sunuyor ama hiçbir somut decoder yoktu.
hello_triangle / hello_cube hardcoded vertex array kullanıyor, hello_gltf
tasarımı geliyordu ve PBR materyal ile texture'lar için gerçek bir
import path lazımdı.

Karar konusu: hangi format(lar)? Hangi kütüphane(ler)? Hangi DAG?

## Karar (Decision)

Dört bağımsız asset-loader kütüphanesi:

| Kütüphane           | Sorumluluk                    | Bağımlılık                      |
|---------------------|-------------------------------|----------------------------------|
| `cd::asset`         | INTERFACE — IAsset/IAssetLoader trait'ler | cd::core + cd::vfs    |
| `cd::asset_image`   | PNG/JPG/TGA/BMP/HDR decode    | stb_image (FetchContent, SYSTEM PRIVATE) |
| `cd::asset_obj`     | Wavefront .obj parse          | cd::core + cd::math (hand-rolled, **no** tinyobjloader) |
| `cd::asset_gltf`    | glTF 2.0 + .glb (node hierarchy + instances) | tinygltf (FetchContent, SYSTEM PRIVATE) |
| `cd::asset_cdmesh`  | Cooked binary mesh format     | cd::core + cd::math (hand-rolled binary IO) |

Her biri standalone — bir kullanıcı yalnızca obj istiyorsa stb_image veya
tinygltf'i build'e dahil etmek zorunda DEĞİL.

## Reddedilen alternatifler

* **Tek "cd::asset" mega-kütüphane:** Her format için bağımlılık zinciri
  ekler (stb_image + tinygltf + JSON + libktx + ... = ~30 MB build).
  Modularity Principle (memory: `feedback_modularity_library_oriented.md`)
  ihlali.
* **Assimp:** ~60 format desteği sunar ama 30+ MB binary, kendi RTTI'si,
  giant CMake. Engine için aşırı. Disqualifying: Q1 derginin tekrar
  edilebilirliği için tek bir devasa external dep istenmiyor (CLAUDE.md
  §11 reproducibility).
* **tinyobjloader (header-only) for OBJ:** Kullanılabilir ama dependency
  chain ekler. OBJ formatı ~150 satır parser ile karşılanabilir (mevcut
  `parse_obj` ~250 satır). Sıfır transitive dep, full kontrol.
* **FlatBuffers / Capnproto for cdmesh:** Schema disiplini güzel ama
  cdmesh "load and blit" hot-path — vtable dispatch en hızlı yol değil.
  Hand-rolled binary header (52 bytes) + iki tightly-packed blob, single
  fread + memcpy. Versiyonlama u32, mismatch'te kFormatMismatch döner.

## Sonuçlar (Consequences)

* `hello_gltf` artık `.glb` yükler, node hierarchy'yi çözer, her instance
  için world matrix hesaplar, base-color texture binding'i yapar.
* Asset library DAG = 4 kütüphane × 2 test binary = 8 yeni test target.
  Toplam test sayısı 36 → 44.
* tinygltf'in stb_image bundled kopyası `cd::asset_image`'in standalone
  stb FetchContent'i ile çakışmaz — tinygltf SYSTEM PRIVATE include
  altında saklı, `cd::asset_image` kendi STB'sini ayrı klonlar.
* Yeni format eklemek = yeni `cd::asset_<fmt>` kütüphanesi. Şu an
  pipeline: KTX2 (W14.3) ve glTF-skin (W8.x v3) gelecek sprintlere.

## Açık sorular

* Asset cache invalidation: dosya mtime mi, içerik hash mi? cdmesh
  cooker'ı ileride glTF/OBJ'i girdi alıp .cdmesh üretecek — invalidation
  policy ayrı bir ADR konusu.
* glTF skinned meshes / animations — `cd::asset_gltf` v3 kapsamı.
