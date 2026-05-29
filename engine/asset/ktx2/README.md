# cd::asset_ktx2

**Purpose**: Minimal KTX2 (Khronos Texture 2.0) runtime reader. Supports BC7 and RGBA8 uncompressed textures; no supercompression (zstd/zlib). Hand-rolled binary parser with no external dependencies.

**Namespace**: `cd::asset_ktx2`.

**Public Headers**:
- `cd/asset_ktx2/Ktx2.hpp` — KTX2 container parser; texture metadata (dimensions, format, mip levels, layers).
- `cd/asset_ktx2/AssetLoader.hpp` — async KTX2 asset loader interface.

**Primary Types**:
- `Ktx2Header` — parsed KTX2 file header (width, height, depth, format, mip count, layer count).
- `Ktx2Image` — in-memory KTX2 texture (header + mip/layer data).
- `Ktx2Loader` — async loader; file queue + worker threads.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_asset_ktx2
ctest --preset ninja-debug -R asset_ktx2 --output-on-failure
```

**Dependencies**: cd::core.

**Format Support**:
- **Uncompressed**: RGBA8 (sRGB or linear).
- **Compressed**: BC7 (high-quality DXT; RGB or RGBA with alpha).
- **No Supercompression**: (zstd/zlib not unpacked; load raw compressed blocks).

**Mipmap Support**: Full mip chain support; individual mip access via mip_offset().

**Notes**:
- KTX2 is the modern standard for game engine texture containers (standard by Khronos).
- No memory allocation during parsing; header bytes determine layout.
- Supercompression (zstd) would require vendoring the codec; Phase 2 may add it.
- Most game assets are shipped as .ktx2 (offline cooked from HDR sources via tools/).
