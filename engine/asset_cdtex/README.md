# cd::asset_cdtex

**Purpose**: Runtime reader for .cdtex (Chromodynamic Texture), the proprietary BC7 texture format produced by tools/cook_texture. Enables offline texture compression into a runtime-friendly format with mipmap chains, layer support, and metadata.

**Namespace**: `cd::asset_cdtex`.

**Public Headers**:
- `cd/asset_cdtex/CdTex.hpp` — .cdtex format parser; mip descriptors, BC7 block access.
- `cd/asset_cdtex/AssetLoader.hpp` — async .cdtex asset loader interface.

**Primary Types**:
- `CdTexHeader` — .cdtex file header (width, height, format, mip count, compression level).
- `CdTexImage` — in-memory .cdtex texture (header + mip/layer BC7 block data).
- `CdTexLoader` — async loader; file queue + worker threads.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_asset_cdtex
ctest --preset ninja-debug -R asset_cdtex --output-on-failure
```

**Dependencies**: cd::core.

**Format Rationale**:
- **Why Not KTX2?** KTX2 is standard but requires runtime supercompression (zstd) decompression. .cdtex skips this (files are pre-compressed offline) for faster load times.
- **Tool Chain**: tools/cook_texture (offline) — source HDR → BC7 blocks → .cdtex file.
- **Runtime**: cd::asset_cdtex reads .cdtex → GPU texture upload.

**Format Structure**:
```
Header (64 bytes)
[ Mip 0 BC7 blocks ]
[ Mip 1 BC7 blocks ]
...
[ Metadata / foot ]
```

**Notes**:
- Hand-rolled binary parser; no external codec dependencies.
- All mipmaps pre-computed offline (no GPU mip generation needed).
- Metadata includes source HDR file hash for validation.
- Designed for fast mmap (direct block offset calculation).
