# ADR-20260522 — Wave 6: CVarBridge + JSON microbench

## Bağlam

Wave 5 sonu: `cd::asset_json` standalone parser + serializer. Wave 6
(13:25–13:45) iki küçük genişletme:

1. **`cd/asset_json/CVarBridge.hpp`** — header-only adapter:
   `cd::core::CVarRegistry` ↔ JSON Object Value.
2. **`hello_bench`** + 2 yeni JSON microbench (parse, serialize).

Bu adım `cd::config`'in (mevcut binary CVAR format'ı) yanına insan-okunaklı
JSON config dosyaları tutma yolunu açar. Game'lerin "settings.json"
modellerine ve developer overrides'a doğal yol.

## Karar

### CVarBridge

```cpp
cd::core::CVarRegistry reg;
// ... reg.set(...) ile populate ...
auto json = cd::asset_json::to_json(reg);              // Object Value
auto text = cd::asset_json::serialize(json, true);     // pretty string

// reload:
auto parsed = cd::asset_json::parse(text);
cd::asset_json::from_json(*parsed, reg);
```

Type-mapping kuralları header'da yorum olarak yazıldı:
- bool ↔ JSON bool
- int64 → JSON number (existing CVar i64 ise load'da geri coerce)
- double ↔ JSON number
- string ↔ JSON string
- null → leave-unchanged sentinel (silent skip in from_json)

Lexicographic key sıralaması output'ta deterministik (git-diff friendly).

4 yeni test (toplam 17) → tüm asset_json suite hâlâ tek `cd_test_asset_json`
binary'sinde geçer.

### hello_bench JSON benchmarks

`asset_json_parse_S` (~70 char input): Debug 15.3 µs/op.
`asset_json_serialize_S` (same tree): Debug 5.5 µs/op.

These act as a regression watch: if a future Json.cpp tweak slows the
parser by 2x, the sample's wall time grows visibly and we notice in
the next session.

## Reddedilen alternatifler

- **JSON $type tag (verbose payload):** Her CVar `{"v": ..., "$t":"i64"}`
  formatında verilebilirdi → round-trip lossless. Ama insan-readable
  ölmü (config dosyası "msaa: 4" değil "msaa: {v:4, $t:i64}" oluyor).
  Mevcut yaklaşım existing-CVar-type-fallback round-trip'i çözüyor.
- **YAML output:** Daha güzel okunur, ama bir parser daha sürdürmek
  istemiyoruz.
- **`from_json` strict mode (unknown key → error):** Şu an silent insert.
  v2'de optional flag olarak eklenebilir.

## Sonuçlar

| Metric | Wave 5 sonu | Wave 6 sonu |
|---|---|---|
| Engine libraries | 48 | **48** (CVarBridge.hpp, asset_json'a iç-eklenti) |
| Samples | 20 | **20** |
| ctest binaries | 50 | **50** |
| Tests in cd_test_asset_json | 13 | **17** (+4 CVarBridge tests) |
| hello_bench benchmark count | 4 | **6** (+JSON parse, +JSON serialize) |

JSON parse/serialize artık reproducible bir performance baseline. Sonraki
tweaklar bu sayılara karşı karşılaştırılabilir.

## Açık sorular

- **JSON in cd::config:** API: `cd::config::load_json(path, registry)` —
  zarif bir alias `cd::asset_json::from_json(parse(load(path)), registry)`.
  v7'de eklenebilir.
- **Schema validation:** "msaa must be 1/2/4/8" gibi kısıtlar şu an
  yok. v3 — JSON Schema subset desteği.
- **Streaming write:** Çok büyük save dosyaları için (`std::ostream&`'a
  yazma). Şu an `std::string` döner. v2.
