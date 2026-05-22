# ADR-20260522 — Offline asset pipeline (cd_cook_mesh)

## Bağlam

Runtime'da .obj / .gltf parse cold-start maliyeti yaratıyor
(tinygltf ~50-200 ms/MB, obj parser benzeri). Production game launch'ta
bu fark her assete tekrar tekrar ödenir. Ayrıca malformed asset'ler
ancak player önünde yakalanır.

## Karar

**`tools/cook_mesh/`** — CLI executable (`cd_cook_mesh`). Workflow:

```
mesh.obj  ─┐
mesh.gltf ─┼──► cd_cook_mesh -i <input> -o <output> ──► mesh.cdmesh ──► runtime
mesh.glb  ─┘                                            (fread + memcpy
                                                         straight to GPU)
```

Tool C++23 exe, link'i:
* `cd::asset_obj` — OBJ parse
* `cd::asset_gltf` — glTF/glb parse
* `cd::asset_cdmesh` — cdmesh write
* `cd::core` + `cd::math` — temel

Format detection extension'dan (`.obj` / `.gltf` / `.glb`). glTF v1
cooker sadece İLK instance/primitive'i yazıyor — multi-mesh ve LOD
chain v2 hedefi.

CMake `tools/` ağacı `CD_ENABLE_TOOLS=ON` (default) cache flag'i ile
gated. `cd_add_sample` değil çünkü tools interactive demo değil; ayrı
folder + namespace.

## Reddedilen alternatifler

* **CMake post-build script** (her engine build'inde cook çağır):
  Geliştirici sürekli .obj düzenliyorsa rebuild loop'a girer. Cook
  tool ayrı bir çağrı olarak kalıyor; build tool'u DEĞİL.
* **Python script:** Python deployment kompleks; C++ tool zaten
  asset parser kütüphanelerine link'leniyor — duplicate logic riski yok.
* **Cook tool engine'in içine gömülmüş (cd::runtime'da bir API):**
  Production runtime'ı parse kütüphanelerine bağımlı kılar — cold-start
  cost zaten kaçınmak istediğimiz şey. Ayrı binary doğru ayrım.

## Sonuçlar

* Yerel test: `cd_cook_mesh -i cube.obj -o cube.cdmesh -v` 8 vert × 32
  byte stride + 36 idx × 4 byte stride + 52-byte header = 452 byte
  dosya. Beklenen toplama birebir eşit.
* Asset pipeline mimari ADR'sinde (ADR-20260522-asset-loader-tier)
  öngörülen "cooker → runtime" yolu artık somut.
* Yeni format desteklemek (cd::asset_<X> + cooker if-branch) iki dosya
  ekleyerek genişler.

## Açık sorular

* **glTF multi-mesh cook:** v1 sadece ilk primitive'i yazıyor. v2 .cdmesh
  formatına çoklu-mesh chunk header ekleyecek (header.flag = bit 0
  = multi-mesh, sub-mesh tablosu).
* **Texture cook:** Şu an cdmesh sadece geometri. v2: bc7-compressed
  texture'ları yan dosya olarak (`.cdmesh.tex0` vb.) yaz.
* **Versioning:** `kFormatVersion = 1`; engine version bumpunda
  invalidation policy ne olacak? Şimdilik mesh format'ı engine
  versiyonundan ayrı versiyonlanıyor.
