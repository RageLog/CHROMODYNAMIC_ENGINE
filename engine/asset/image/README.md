# cd::asset_image

**Purpose**: Stand-alone image decoder and encoder. Supports PNG, JPG, TGA, BMP, GIF, HDR decoding via stb_image, and BC7 texture compression encoding via bc7enc. Kept separate from cd::asset_gltf so callers needing only image I/O avoid JSON/glTF dependencies.

**Namespace**: `cd::asset_image`.

**Vendored Dependencies**:
- `nothings/stb` (stb_image.h) — single-header image decoder (PNG, JPG, TGA, BMP, GIF, HDR).
- `richgel999/bc7enc` — single-file BC7 encoder (MIT); used for offline texture compression.

**Public Headers**:
- `cd/asset_image/Image.hpp` — image loading from file/memory; pixel format conversions.
- `cd/asset_image/AssetLoader.hpp` — async image asset loader interface.
- `cd/asset_image/Bc7.hpp` — BC7 compression (offline; real-time compression in cd::asset_image GPU pipeline).

**Primary Types**:
- `Image` — in-memory image (width, height, pixels, format); format conversions (RGBA → BC7).
- `ImageLoader` — async loader; file queue + worker threads.
- `Bc7Encoder` — BC7 compression settings + block encoder.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_asset_image
ctest --preset ninja-debug -R asset_image --output-on-failure
```

**Dependencies**: cd::core.

**Build Details**:
- stb_image headers marked SYSTEM PRIVATE (warnings suppressed).
- bc7enc.c compiled directly into the library (single .c file, no upstream CMake).
- Warnings for vendored code disabled via source-level COMPILE_OPTIONS (MSVC: /wd4100 etc.; GCC/Clang: -Wno-error=...).

**Format Support**:
- **Input**: PNG, JPG, TGA, BMP, GIF, HDR (via stb_image).
- **Output**: BC7 blocks (4×4 pixel blocks, RGB5.1 + DXT5A alpha).

**Notes**:
- BC7 is the modern GPU texture compression for quality; offline baking to .dds/.ktx2 is the expected path for game assets.
- Real-time GPU BC7 encoding (if needed) lives in a separate compute pipeline.
- Image flipping / mip-generation live in separate helpers (not in this library).
