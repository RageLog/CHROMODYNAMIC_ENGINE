# cd::config

## Purpose
CVar (console variable) registry persistence layer. Saves and loads engine configuration (cvars) to/from binary files using cd::io, with sorted-key deterministic output for git-friendly diffs.

## Namespace
`cd::config::` — all public symbols.

## Public headers
- `Config.hpp` — CVar persistence functions (load/save)

## Primary types
- CVar registry access; error types for persistence failures
- Binary wire format with magic + version + type-tag encoding

## Usage example
```cpp
#include <cd/config/Config.hpp>
#include <cd/io/BinaryStream.hpp>
#include <fstream>

// Save all CVars to a byte buffer, then flush to disk
cd::io::BinaryWriter w;
cd::config::save(w, registry);
// (write w.data() to file via std::ofstream, cd::vfs, etc.)

// Load from a byte buffer (e.g. read from file first)
// std::vector<std::byte> bytes = ...;
cd::io::BinaryReader r { bytes };
auto result = cd::config::load(r, registry);
if (!result) {
    // result.error() carries domain + code + message
}
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_config
```

## Test
```bash
ctest --preset ninja-debug -R config
```

## Dependencies (per CMakeLists)
- `cd::core` — CVar registry + ErrorCode + Result
- `cd::io` — BinaryStream for persistence

## Wire format
- u32 magic (0x43564152 = 'CVAR')
- u32 version (1)
- u32 entry count
- Per entry: key_len (u32) + key bytes + type-tag (u8) + payload (varies by type)
  - bool: u8 (0 or 1)
  - int64: i64 LE
  - double: f64 LE
  - string: u32 len + bytes
- Entries sorted by key for reproducibility (deterministic / git-friendly)

## Notes
- Interface library
- Sprint S2.8 — ADR-017 P4
- Sorted-key output enables git-friendly configuration tracking
- Type-safe (bool/int64/double/string at persistence layer)

## References
- ADR-017 — DtForHil pattern salvage (CVar bridge)
- ADR-013 + ADR-017 — Foundation architecture
