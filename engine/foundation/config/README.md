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

// Save all CVars to file
cd::config::save_cvars(registry, "engine.cfg");

// Load back
cd::config::load_cvars(registry, "engine.cfg");
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
- Per entry: type-tag (u8) + key_len (u32) + key + payload (varies by type)
- Entries sorted by key for reproducibility

## Notes
- Interface library
- Sprint S2.8 — ADR-017 P4
- Sorted-key output enables git-friendly configuration tracking
- Type-safe (bool/int64/double/string at persistence layer)

## References
- ADR-017 — DtForHil pattern salvage (CVar bridge)
- ADR-013 + ADR-017 — Foundation architecture
