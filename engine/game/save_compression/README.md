# cd::game::save_compression

Byte-stream compression for save-game blobs. Sits at the bottom of
the gameplay DAG so non-save callers (replay headers, telemetry
snapshots, network-spool batches) can use it without pulling in
slot-management machinery.

## Naming rationale

The library is called `save_compression`, **not** `save_v2`, because
it does **not** replace `cd::game::save::SaveSystem`. Callers
compress their byte blob *before* passing it to `SaveSystem::save()`
and decompress *after* `SaveSystem::load()` returns. The split keeps
I/O and codec concerns orthogonal and enables a codec swap without
touching slot-management code.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase661` | RLE encoder / decoder for save-game streams. |
| 2 | `phase749` | LZ4 path via vcpkg or FetchContent fallback. |

## LZ4 3-tier resolution

```
Tier 1: vcpkg     find_package(lz4 CONFIG)            — canonical CI path
Tier 2: FetchContent github.com/lz4/lz4 tag v1.10.0
Tier 3: absent   CD_SAVE_COMPRESSION_HAS_LZ4=0        — compress_lz4 /
                                                       decompress_lz4
                                                       compiled out;
                                                       compress_rle remains
                                                       operative.
```

The pattern matches `cd::asset::texture_compress` (bc7enc) and
`cd::audio::spatial` (openal-soft) — every optional third-party
codec follows the same 3-tier convention.

## Public surface

```cpp
namespace cd::game::save_compression {

// RLE — always available, no external dep.
std::vector<std::byte>           compress_rle(std::span<const std::byte>);
std::vector<std::byte>           decompress_rle(std::span<const std::byte>);

#if CD_SAVE_COMPRESSION_HAS_LZ4
// LZ4 — only when the dep resolves.
std::vector<std::byte>           compress_lz4(std::span<const std::byte>);
std::vector<std::byte>           decompress_lz4(std::span<const std::byte>);
#endif

}
```

## Codec selection guide

| Use case                | Codec | Why                                  |
|-------------------------|-------|--------------------------------------|
| Default save slot       | LZ4   | ~2–4× ratio, fast enough for hot path. |
| Large save (Skyrim-tier)| LZ4   | RLE will be too slow on 50 MB blobs.  |
| Save-on-suspend snapshot| RLE   | Tiny payload, RLE keeps zero deps.    |
| Telemetry batch         | LZ4   | Reuse the codec already linked.       |

When LZ4 is unavailable at build time, the library still compiles
and the RLE path remains operative — so a bare-bones environment
(e.g. an embedded test cell without network access) can still
exercise the save pipeline end-to-end.

## RLE format

```
[1 B  run_length] [1 B  byte_value]
```

Bytes that don't run (length 1) emit `[01, byte]` — a 2-byte cost.
Useful only when the input has natural runs (sparse save slot,
zero-padded blocks, region-grid encoded world state). For random
binary, LZ4 strictly dominates; the RLE path is the always-available
fallback.

## Dependencies

* `cd::core` — `Defines.hpp`.
* `lz4::lz4_static` (or `lz4::lz4`) — Sprint-2 PRIVATE link,
  **optional** via the 3-tier resolution above.

## See also

* `cd::game::save` — `SaveSystem` (slot management; consumes a
  compressed blob produced by this library).
