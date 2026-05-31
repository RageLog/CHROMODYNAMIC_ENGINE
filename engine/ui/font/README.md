# cd::ui_font

**Purpose**: TTF/OTF rasterizer + glyph atlas for the cd::ui renderer. Phase 1.1 of ADR-20260530-ui-widget-library shipped a stb_truetype-based alpha rasterizer; Phase 4 (T2.4) layers a FreeType outline backend + HarfBuzz shaper + signed-distance-field atlas mode on top while keeping the stb path as a zero-dependency fallback.

**Namespace**: `cd::ui::font`.

**Headers**: `cd/ui/font/Font.hpp`.

**Primary types**:
- `cd::ui::font::Font` -- one rasterized face at one pixel size. PIMPL'd: the backend headers (stb_truetype.h, FreeType, HarfBuzz) stay in the .cpp TUs.
- `cd::ui::font::AtlasBitmap` -- single-channel 8-bit pixel buffer + dimensions. Coverage when `atlas_mode() == kAlpha`, unsigned SDF (zero at 128) when `kMsdf`.
- `cd::ui::font::GlyphInfo` -- per-glyph atlas UVs + size + bearing + advance.
- `cd::ui::font::ShapedGlyph` -- one shaped glyph (glyph_id + advance + offset) emitted by `Font::shape()`.
- `cd::ui::font::Backend` -- `kAuto` (default), `kStb`, `kFreeType`.
- `cd::ui::font::AtlasMode` -- `kAlpha` (default), `kMsdf`.

**Backend matrix (compile-time)**:
- `CD_UI_FONT_HAVE_FREETYPE` -- FreeType outline rasterizer + MSDF atlas mode available.
- `CD_UI_FONT_HAVE_HARFBUZZ` -- HarfBuzz `Font::shape()` available (implies FreeType).
- Neither defined -- stb_truetype-only, `Font::shape()` returns identity 1:1 mapping.

**Resolution tiers** (mirrors `cd::spirv_cross_glue`):
1. **vcpkg** -- `find_package(Freetype CONFIG)` + `find_package(harfbuzz CONFIG)`. The `vcpkg.json` manifest now lists `freetype >= 2.13.0` and `harfbuzz >= 8.0.0`.
2. **FetchContent** -- enabled via `-DCD_UI_FONT_ENABLE_FETCHCONTENT_FT=ON` at configure time. Pulls freetype `VER-2-13-3`. Disabled by default to keep clean configures fast.
3. **stb_truetype-only** -- when neither tier resolves, the library still builds and the Phase 1.1 tests still pass.

**Pipeline**:
1. (Optional) `Font::select_backend(Backend::kFreeType)` -- pick a backend up front. `kAuto` resolves at load time to FreeType when available, stb otherwise.
2. (Optional) `Font::select_atlas_mode(AtlasMode::kMsdf)` -- switch the atlas format BEFORE the first `rasterize_range`.
3. `Font::load_ttf_in_memory(bytes)` -- parses the SFNT header (FT or stb depending on backend).
4. `Font::rasterize_range(first, last, pixel_size, max_dim)` -- runs one bin-pack pass over the codepoint range. Can be called multiple times.
5. `Font::glyph_uv(codepoint)` -- atlas slot for sampling.
6. `Font::shape(text_utf8, locale)` -- HarfBuzz BiDi + ligature shaping (real BiDi/script substitution when HB is available; identity 1:1 mapping otherwise).
7. `Font::atlas()` -- alpha or SDF bitmap for upload to a single-channel texture.
8. `Font::sdf_zero()` -- the pixel value (128) the renderer should treat as the SDF boundary; 0 in alpha mode.

**MSDF**: the Phase 4 SDF generator is a hand-rolled 8-SSED Euclidean distance approximation operating per-glyph on the source alpha bitmap. msdfgen would give multi-channel anti-aliasing for sharp corners; the single-channel approach buys us scale-invariant rendering without another vendored dependency. The renderer should sample with bilinear filtering and the standard `smoothstep(0.5 - 1/spread, 0.5 + 1/spread, sample)` hint.

**Out of Phase 4** (queued for Phase 5+):
- True msdfgen multi-channel SDF (sharp-corner preservation).
- Variable fonts (FT supports them via `FT_Var_*`; not yet wired).
- LRU eviction for dynamic glyph mix.
- ICU BiDi pre-pass when scripts mix in a single string (HB handles per-run direction; full Unicode BiDi reorder needs ICU).
- HarfBuzz when not present in vcpkg manifest CI (see "scope-down" below).

**Scope-down policy**: when vcpkg resolves FreeType but NOT HarfBuzz (the common Tier-2 FetchContent outcome, since hb depends on ft and ordering is fragile), the library compiles with only `CD_UI_FONT_HAVE_FREETYPE` set. `Font::shape()` then degrades to the identity fallback. The HB-only tests `ShapeArabicReversesCodepointOrderToVisualLtr` and `ShapeFiLigatureMergesWhenSupported` skip cleanly.

**Usage**:
```cpp
#include <cd/ui/font/Font.hpp>

cd::ui::font::Font f;
f.select_backend(cd::ui::font::Backend::kFreeType);   // or kStb, kAuto
f.select_atlas_mode(cd::ui::font::AtlasMode::kMsdf);  // optional

auto ttf = fs_read_bytes("assets/Inter-Regular.ttf");
if (!f.load_ttf(ttf)) { /* error */ }

f.rasterize_range(0x0020U, 0x00FFU, 16.0F, 2048U);  // ASCII + Latin-1
f.rasterize_range(0x011EU, 0x011FU, 16.0F, 2048U);  // Türkçe Ğğ
f.rasterize_range(0x0600U, 0x06FFU, 16.0F, 2048U);  // Arabic

// Upload f.atlas() to a single-channel RHI texture in the renderer.
auto g = f.glyph_uv(0x41U);  // 'A'

// HB shaping (real BiDi/ligature when HB available; identity fallback otherwise).
auto glyphs = f.shape("Merhaba dünya", "tr-TR");
auto arabic = f.shape("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", "ar");  // سلام
```

**Test command**: `ctest --preset ninja-debug -R cd_test_font --output-on-failure`. Phase 4 tests gated on `Font::has_freetype()` / `Font::has_harfbuzz()` skip cleanly when the build is stb-only.

**Notes**:
- Garbage / too-small TTF blobs are rejected up-front (256-byte minimum + `stbtt_GetFontOffsetForIndex` magic check) so a malicious caller can't AV-crash either rasterizer.
- The atlas pixel format is single-channel (one byte per pixel). Renderer declares an `Format::kR8Unorm` texture; for MSDF mode it should sample with bilinear and decode via `smoothstep` around `sdf_zero()`.
- The skyline bin-pack is shared across backends. It is O(N×M) per pack which is negligible for Phase 4 glyph counts (~256 ASCII + ~20 Türkçe + ~256 Arabic + ~200 widget-icons).
- `Font::Impl` keeps a copy of the TTF bytes so the FreeType `FT_Face` (which stores a back-pointer into the buffer) and the stb `stbtt_fontinfo` (likewise) both stay valid for the `Font`'s lifetime.
