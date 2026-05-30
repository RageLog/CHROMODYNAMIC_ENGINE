# cd::ui_font

**Purpose**: TTF/OTF rasterizer + glyph atlas for the cd::ui renderer. Phase 1.1 of ADR-20260530-ui-widget-library. Foundation: `stb_truetype.h` (single-header MIT, sibling of `stb_image` already vendored by `cd::asset_image`). Phase 4 swaps the backend to `FreeType + HarfBuzz` per ADR-009 for proper BiDi / script shaping; the public API stays.

**Namespace**: `cd::ui::font`.

**Headers**: `cd/ui/font/Font.hpp`.

**Primary types**:
- `cd::ui::font::Font` -- one rasterized face at one pixel size. PIMPL'd: `stb_truetype.h` stays in the .cpp.
- `cd::ui::font::AtlasBitmap` -- single-channel 8-bit alpha pixel buffer + dimensions. Renderer wraps this in a single-channel RHI texture.
- `cd::ui::font::GlyphInfo` -- per-glyph atlas UVs + size + bearing + advance.

**Pipeline**:
1. Caller loads a TTF blob (cd::vfs / fs read).
2. `Font::load_ttf_in_memory(bytes)` parses the SFNT header.
3. `Font::rasterize_range(first, last, pixel_size, max_dim)` runs one bin-pack pass over the codepoint range, growing the atlas on demand. Can be called multiple times to add scattered ranges (e.g. ASCII + Türkçe diakritik).
4. `Font::glyph_uv(codepoint)` returns the atlas slot (or `nullopt` for missing glyphs -- renderer falls back to `?`).
5. `Font::atlas()` exposes the alpha bitmap for upload to a single-channel texture.
6. Optional `Font::kerning(a, b)` for proportional pairs.

**Phase 1 scope**:
- Skyline bin-pack (in-house, ~120 lines). Single page, no LRU eviction.
- Up to 2048×2048 default atlas.
- ASCII + Latin-1 + Türkçe diakritik tested.
- Kerning lookup via `stbtt_GetCodepointKernAdvance`.

**Out of Phase 1** (Phase 4 per ADR-009):
- MSDF (signed-distance field) glyphs.
- HarfBuzz shaping (Arabic / Devanagari / ligatures).
- BiDi reorder.
- Variable fonts.
- LRU eviction for dynamic glyph mix.

**Usage**:
```cpp
#include <cd/ui/font/Font.hpp>

cd::ui::font::Font f;
auto ttf = fs_read_bytes("assets/Inter-Regular.ttf");
if (!f.load_ttf_in_memory(ttf)) { /* error */ }

// ASCII + Latin-1.
f.rasterize_range(0x0020U, 0x00FFU, 16.0F, 2048U);
// Türkçe extras (Latin Extended-A).
f.rasterize_range(0x011EU, 0x011FU, 16.0F, 2048U);  // Ğğ
f.rasterize_range(0x015EU, 0x015FU, 16.0F, 2048U);  // Şş

// Upload f.atlas() to a single-channel RHI texture in the renderer.
auto g = f.glyph_uv(0x41U);  // 'A'
```

**Test command**: `ctest --preset ninja-debug -R cd_test_font --output-on-failure`. Skips the rasterization smoke test gracefully when no system font (Arial / DejaVuSans / Helvetica) is available on the host.

**Notes**:
- The header reuses the `nothings/stb` repo already fetched by `cd::asset_image` (`stb_image_SOURCE_DIR`). A defensive `FetchContent_GetProperties` re-declares it when ui_font builds standalone.
- Garbage / too-small TTF blobs are rejected up-front (256-byte minimum + `stbtt_GetFontOffsetForIndex` magic check) so a malicious caller can't AV-crash the stb_truetype table walker.
- The atlas pixel format is single-channel alpha (one byte per pixel). The renderer is expected to declare a Format::kR8Unorm texture and tint in the fragment shader; do NOT pre-expand to RGBA here.
- Skyline bin-pack: O(N×M) per pack (N glyphs × M skyline segments). For Phase 1 glyph counts (~256 ASCII + ~20 Türkçe + ~200 widget-icons) the cost is negligible.
