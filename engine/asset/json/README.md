# cd::asset_json

**Purpose**: Hand-rolled JSON subset parser. Provides fast, exception-free JSON document loading and traversal without pulling a heavy third-party JSON library (e.g., RapidJSON, nlohmann/json). Used by asset loaders (glTF, metadata) and configuration layers (cvars).

**Namespace**: `cd::asset_json`.

**Public Headers**:
- `cd/asset_json/Json.hpp` — parser, DOM tree, array/object navigation.
- `cd/asset_json/AssetLoader.hpp` — JSON asset loader interface (file → Dom).
- `cd/asset_json/CVarBridge.hpp` — JSON-to-cvar bridge (load config from .json files).

**Primary Types**:
- `JsonValue` — variant-like type (null, bool, number, string, array, object).
- `JsonDocument` — parsed JSON DOM; root access + error handling.
- `JsonArray` — array iteration + indexed access.
- `JsonObject` — object field lookup + field enumeration.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_asset_json
ctest --preset ninja-debug -R asset_json --output-on-failure
```

**Dependencies**: cd::core.

**Subset Rationale**:
- Parsed subset: objects (key-value), arrays, strings, numbers, booleans, null.
- Not supported: JSON schema validation, JSON-Pointer, JSON-Patch.
- Performance: single-pass parser, no allocations per token (strings point into source buffer).

**Usage**:
```cpp
#include <cd/asset_json/Json.hpp>
auto doc = cd::asset_json::parse_file("config.json");
if (doc.root().is_object()) {
  const auto width = doc.root()["width"].as_int();
}
```

**Notes**:
- Validates UTF-8 at parse time; invalid UTF-8 is an error.
- Large JSON documents (>10MB) are not recommended (single-pass, no streaming).
- Used by cd::asset_gltf as the glTF JSON parser.
