# ADR-20260522 — Wave 5: cd::asset_json (hand-rolled subset parser)

## Bağlam

Wave 4 sonu: 47 lib / 19 sample / 49 ctest, tooling tamam, README + LIBRARIES
güncel. Wave 5 (12:50–13:20) bir kütüphane daha yerleşti: `cd::asset_json`.

Motivasyon: Engine'in JSON ihtiyacı yakında çoğalacak (scene save, config
profile, sidecar metadata, build manifesti). Şu an `cd::config` TOML-style;
JSON için ya nlohmann_json transitively (tinygltf üzerinden, ama internal
include path'i sızdırmak zorunda) ya da yeni dep. Her ikisi de
`cd::asset_obj` / `cd::asset_cdmesh` / `cd::asset_cdtex` / `cd::asset_ktx2`
hand-roll-pattern'ine aykırı.

## Karar

`cd::asset_json` hand-rolled, ~450 satır:
- **AST:** `cd::asset_json::Value = std::variant<Null, bool, double, string, Array, Object>`.
- **Array** = `std::vector<Value>`, **Object** = `std::map<string, Value>`
  (ordered → deterministic serialization).
- **Parse:** `parse(text, max_depth = 64) → Result<Value>`. Strict RFC 8259
  subset (NaN/Infinity reddedildi, comment yok, trailing comma yok).
  Escape sequence'lar: `\" \\ \/ \b \f \n \r \t \uXXXX` (BMP-içi).
- **Serialize:** `serialize(value, pretty = false) → string`. Pretty 2-space
  indent + newline; compact tek satır. Integer fast-path (`%lld`); float
  `%.17g` round-trip-safe.
- **Accessors:** `is_object/is_array/...`, `as_object/as_array/...`,
  `at(key)` / `at(index)` → `Result<const Value*>` (typed error: kKeyNotFound,
  kTypeMismatch).

Tests (13): parse null/true/false, integer + float + scientific, string with
escapes + Unicode escape (→ UTF-8), nested object lookup, unterminated string,
bad escape, trailing garbage, depth-limit trigger, compact + pretty
round-trip, escaped quotes/backslashes, kKeyNotFound semantics.

Sample (`hello_json`): parses scene-like JSON, prints fields, mutates the
tree (push new entity), serializes both pretty + compact, parses the
compact form, asserts structural equality with the mutated tree. Headless
(no GPU).

## Reddedilen alternatifler

- **nlohmann/json (transitively via tinygltf):** Bizim `cd::asset_gltf`
  içinde tinygltf'in vendored kopyası var. Bu yolu sızdırmak için
  ya tinygltf'in private include path'ini public yapardık (encapsulation
  ihlali) ya da bağımsız bir FetchContent çekmeliydik. İkincisi tek-amaç-
  için ~50 KLOC eklemek; bizim üst sınır CLAUDE.md §6.
- **simdjson:** SOTA performance, ama AOT compilation cost ve hot-path
  optimization bizim için signal değil — JSON sadece config / save / asset
  metadata için kullanılacak, MB/s yarışı değil.
- **rapidjson:** Production-grade ama bizim için iki sorun: (1)
  exception-policy farklı, (2) macro yoğun, (3) handler-based SAX
  API'si bizim DOM mental model'imize uymuyor.
- **JSON5 desteği (yorum + trailing comma):** Belki v2; v1 sıkı RFC 8259
  daha tahmin edilebilir.

## Sonuçlar

| Metric | Wave 4 sonu | Wave 5 sonu |
|---|---|---|
| Engine libraries | 47 | **48** (+cd::asset_json) |
| Samples | 19 | **20** (+hello_json) |
| ctest binaries | 49 | **50** |
| Headless smoke-clean | 19/19 | **20/20** |
| Compilers verified (49+ tests green) | 3/4 (clangcl, llvm-clang, msvc-ninja) | **3/4** |
| ADRs (this date) | 19 | **20** |

`cd::asset_json` artık `cd::config`, `cd::scene` (save/load), ve yeni asset
metadata sidecar pattern'i için temel kurulum sağlar — opsiyonel
bağımlılık olarak.

## Açık sorular

- **Scene → JSON serialize:** cd::scene::SceneSerializer için `cd::ecs::World`
  enumeration API'sine ihtiyaç var; bunun design'ı v2.
- **Asset sidecar metadata:** `texture.png.meta` ⊆ `{ "format": "BC7",
  "mips": true }` pattern'i; cooker'lar bunu okuyacak. v2.
- **cd::config JsonBackend:** `cd::config::load_json(path)` overlay/override
  semantikası — TOML / JSON aynı tree'yi paylaşacak.
- **Number type system:** Hâlâ tek tip `double`. `int64_t` tagged variant
  daha sonra eklenebilir (e.g. scene ID'leri için precision koru).
- **Stream parsing:** Çok büyük asset bundle metadata'sı için v2 — şu
  an tüm dosya RAM'e okunuyor.
