# ADR-20260522 — Wave 8: Scene enumeration helpers + file-level JSON config helpers

## Bağlam

Wave 7 sonu: `cd::scene::Serializer` JSON output/input verir, ancak
scene içeriğini iterate etmek yine sadece `World::for_each<T>` üzerinden
mümkündü. Editör UI / asset inspector / debugger view'leri için
"sadece root'lar", "depth-first subtree walk", "children of node X"
gibi natural primitive'ler eksikti.

Aynı dalgada `cd::asset_json::CVarBridge.hpp`'e dosya-tabanlı yardımcılar
eklendi (`save_to`, `load_into`) — uygulamanın settings.json'ı diskten
okuma/yazma akışını tek satıra düşürür.

## Karar

### `cd::scene::Scene` enum API genişlemesi (header-only, kütüphane sınırı değişmedi)

```cpp
template <class Fn> void for_each_node(Fn) const;   // her LocalTransform sahibi
template <class Fn> void for_each_root(Fn) const;   // Parent yoksa
template <class Fn> void for_each_descendant(Entity root, Fn) const;  // DFS, depth bilgisi
[[nodiscard]] const Children* children_of(Entity e) const;
```

DFS recursion 256 derinlikte cap'li (cycle yok — attach() reddediyor —
malformed data'ya karşı son siper).

### `cd::asset_json::CVarBridge.hpp` dosya yardımcıları

```cpp
cd::asset_json::save_to(path, registry, pretty=true);    // text → disk
cd::asset_json::load_into(path, registry, max_depth=64); // disk → registry
```

İçeride `parse` + `from_json` zinciri; hata kategorileri JSON parser
domain'inden gelir.

### `hello_bench` — yedinci microbench

`scene_serialize_16` — 16-node bir scene'i JSON Value'ya serialize eder.
Debug: ~347 µs/op (allocator-heavy std::map'in maliyeti). Reproducible
baseline; future optimization regression watch.

## Reddedilen alternatifler

* **`std::ranges`-based view (Scene::nodes() → range):** Şık ama ECS
  storage'ın iteration model'i ranges'i destekleyecek şekilde değil
  henüz. callback-based for_each pattern'i yeterli ve mevcut diğer
  cd::ecs / cd::asset / cd::scene API'lere uyumlu.
* **Children listesi cache'i:** for_each_descendant Children kullanıyor
  zaten — extra cache değil, mevcut data structure kullanılıyor.
* **`cd::config::load_json` (cd::config içine ekle):** cd::config'in
  CMakeLists'ine `cd::asset_json` dep'i eklemek dependency direction
  ihlali olurdu (config foundation tier, asset_json asset tier). Helper
  asset_json içinde durmak daha doğru — kullanıcı her ikisini de link'lerse
  `load_into`'ya erişir.

## Sonuçlar

| Metric | Wave 7 sonu | Wave 8 sonu |
|---|---|---|
| Engine libraries | 48 | **48** |
| Samples | 21 | **21** |
| ctest binaries | 50 | **50** |
| Tests in cd_test_scene | 24 | **27** (+for_each_node/root/descendant) |
| Tests in cd_test_asset_json | 17 | **19** (+save_to/load_into round-trip & error) |
| hello_bench benchmark count | 6 | **7** (+scene_serialize_16) |

Çekirdek sayılar değişmiyor (test binary sayısı 50, lib 48 sabit) ama
kapsama derinleşiyor — iç-test sayıları 67 → 73. Tüm
ninja-base / clangcl-win / llvm-win 50/50 yeşil.

## Açık sorular

* **for_each<Parent>**: Filter-based "her node with Parent" query daha
  efficient olabilir ECS storage allow ederse — şu an for_each<LocalTransform>
  + Parent? null check yapıyoruz. ECS storage v2'de query refresh
  geldiğinde tekrar bak.
* **Iterator-based interface:** range-for syntax istenirse bir
  `class SceneNodeView` ile yapılır. v2.
* **Save/load JSON to vfs (not direct fstream):** `cd::asset_json::save_to`
  şu an `std::ofstream` kullanır; `cd::vfs::VirtualFileSystem` yazma
  yolunu da kullanabilmek için overload eklenir.
