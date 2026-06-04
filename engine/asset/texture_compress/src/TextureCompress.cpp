// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_compress/src/TextureCompress.cpp
// Phase 650 — cd::asset::texture_compress implementation (Sprint-1)
//
// Sprint-1: BC1 stub encoder + BC1 decoder (for analyze()).
// BC1 block layout (8 bytes per 4×4 block):
//   [0..1] color0 (RGB565 little-endian)
//   [2..3] color1 (RGB565 little-endian)
//   [4..7] 2-bit per-texel indices (16 texels, packed LSB-first)
//
// Endpoint selection: naive per-block min/max channel-wise in RGB565 space.
// Sprint-2: replace encode_bc1_block() with PCA-based endpoint search.
// =============================================================================

#include <cd/asset/texture_compress/TextureCompress.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cd::asset::texture_compress
{

// ============================================================================
// Internal helpers
// ============================================================================

namespace
{

// ---- RGB565 packing / unpacking --------------------------------------------

/// Pack 8-bit R,G,B into a 16-bit RGB565 value (5/6/5 bits).
[[nodiscard]] constexpr std::uint16_t pack_rgb565(std::uint8_t r,
                                                   std::uint8_t g,
                                                   std::uint8_t b) noexcept
{
    const auto r5 = static_cast<std::uint16_t>(r >> 3U);
    const auto g6 = static_cast<std::uint16_t>(g >> 2U);
    const auto b5 = static_cast<std::uint16_t>(b >> 3U);
    return static_cast<std::uint16_t>((r5 << 11U) | (g6 << 5U) | b5);
}

/// Unpack a 16-bit RGB565 value into 8-bit R,G,B (upscaled to [0,255]).
struct Rgb8 { std::uint8_t r, g, b; };

[[nodiscard]] constexpr Rgb8 unpack_rgb565(std::uint16_t c) noexcept
{
    const auto r5 = static_cast<std::uint8_t>((c >> 11U) & 0x1FU);
    const auto g6 = static_cast<std::uint8_t>((c >>  5U) & 0x3FU);
    const auto b5 = static_cast<std::uint8_t>( c          & 0x1FU);
    // Upscale: replicate upper bits into the low bits.
    return { static_cast<std::uint8_t>((r5 << 3U) | (r5 >> 2U)),
             static_cast<std::uint8_t>((g6 << 2U) | (g6 >> 4U)),
             static_cast<std::uint8_t>((b5 << 3U) | (b5 >> 2U)) };
}

// ---- Write little-endian uint16 / uint32 -----------------------------------

void write_u16_le(std::uint8_t* dst, std::uint16_t v) noexcept
{
    dst[0] = static_cast<std::uint8_t>(v & 0xFFU);
    dst[1] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}

void write_u32_le(std::uint8_t* dst, std::uint32_t v) noexcept
{
    dst[0] = static_cast<std::uint8_t>(v & 0xFFU);
    dst[1] = static_cast<std::uint8_t>((v >>  8U) & 0xFFU);
    dst[2] = static_cast<std::uint8_t>((v >> 16U) & 0xFFU);
    dst[3] = static_cast<std::uint8_t>((v >> 24U) & 0xFFU);
}

[[nodiscard]] std::uint16_t read_u16_le(const std::uint8_t* src) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(src[0]) |
        (static_cast<std::uint16_t>(src[1]) << 8U));
}

// ---- BC1 block encoder (Sprint-1: min/max endpoint picker) -----------------

/// Encode one 4×4 block of RGBA8 texels into 8 bytes of BC1 data.
///
/// Sprint-1 algorithm:
///   1. Find min and max for each channel (R,G,B) across the 16 texels.
///   2. Pack min as color1 (RGB565), max as color0 (RGB565).
///   3. For each texel, compute the lerp index [0..3] by projecting the texel
///      onto the (color1..color0) axis in 565 space.
///
/// color0 > color1 selects the 4-color mode (no transparent texels in BC1).
void encode_bc1_block(const std::uint8_t* block_rgba,  // 16 texels × 4 bytes
                      std::uint8_t*       out_8)        // 8 bytes output
    noexcept
{
    // --- Step 1: find per-channel min / max (8-bit space) -------------------
    std::uint8_t min_r = 255U, min_g = 255U, min_b = 255U;
    std::uint8_t max_r = 0U,   max_g = 0U,   max_b = 0U;

    for (int t = 0; t < 16; ++t)
    {
        const std::uint8_t r = block_rgba[static_cast<std::size_t>(t) * 4U + 0U];
        const std::uint8_t g = block_rgba[static_cast<std::size_t>(t) * 4U + 1U];
        const std::uint8_t b = block_rgba[static_cast<std::size_t>(t) * 4U + 2U];
        min_r = std::min(min_r, r);  min_g = std::min(min_g, g);  min_b = std::min(min_b, b);
        max_r = std::max(max_r, r);  max_g = std::max(max_g, g);  max_b = std::max(max_b, b);
    }

    // --- Step 2: pack endpoints in RGB565 -----------------------------------
    // color0 = max endpoint (higher 565 value → 4-color mode when c0 > c1)
    // color1 = min endpoint
    const std::uint16_t color0 = pack_rgb565(max_r, max_g, max_b);
    const std::uint16_t color1 = pack_rgb565(min_r, min_g, min_b);

    // Ensure color0 >= color1 (4-color opaque mode). If they are equal,
    // both encode to index 0 which references color0 — correct.
    const std::uint16_t c0 = (color0 >= color1) ? color0 : color1;
    const std::uint16_t c1 = (color0 >= color1) ? color1 : color0;

    // --- Step 3: compute per-texel 2-bit indices ----------------------------
    // 4-color mode palette:
    //   idx 0 → c0
    //   idx 1 → c1
    //   idx 2 → (2*c0 + c1) / 3
    //   idx 3 → (c0 + 2*c1) / 3

    const Rgb8 p0 = unpack_rgb565(c0);
    const Rgb8 p1 = unpack_rgb565(c1);

    // Palette entries (in 8-bit space for distance computation).
    const std::array<Rgb8, 4> palette = { p0, p1,
        Rgb8{ static_cast<std::uint8_t>((2U * p0.r + p1.r) / 3U),
              static_cast<std::uint8_t>((2U * p0.g + p1.g) / 3U),
              static_cast<std::uint8_t>((2U * p0.b + p1.b) / 3U) },
        Rgb8{ static_cast<std::uint8_t>((p0.r + 2U * p1.r) / 3U),
              static_cast<std::uint8_t>((p0.g + 2U * p1.g) / 3U),
              static_cast<std::uint8_t>((p0.b + 2U * p1.b) / 3U) } };

    std::uint32_t indices = 0U;

    for (int t = 0; t < 16; ++t)
    {
        const std::uint8_t tr = block_rgba[static_cast<std::size_t>(t) * 4U + 0U];
        const std::uint8_t tg = block_rgba[static_cast<std::size_t>(t) * 4U + 1U];
        const std::uint8_t tb = block_rgba[static_cast<std::size_t>(t) * 4U + 2U];

        // Find nearest palette entry (L2 squared distance in RGB space).
        std::uint32_t best_dist = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t best_idx  = 0U;

        for (std::uint32_t i = 0U; i < 4U; ++i)
        {
            const auto dr = static_cast<std::int32_t>(tr) -
                            static_cast<std::int32_t>(palette[i].r);
            const auto dg = static_cast<std::int32_t>(tg) -
                            static_cast<std::int32_t>(palette[i].g);
            const auto db = static_cast<std::int32_t>(tb) -
                            static_cast<std::int32_t>(palette[i].b);
            const auto dist = static_cast<std::uint32_t>(dr*dr + dg*dg + db*db);
            if (dist < best_dist)
            {
                best_dist = dist;
                best_idx  = i;
            }
        }

        indices |= (best_idx << static_cast<std::uint32_t>(t * 2));
    }

    // --- Step 4: write the 8-byte BC1 block ---------------------------------
    write_u16_le(out_8 + 0, c0);
    write_u16_le(out_8 + 2, c1);
    write_u32_le(out_8 + 4, indices);
}

// ---- BC1 block decoder (for analyze()) -------------------------------------

/// Decode one 8-byte BC1 block into 16 RGBA8 texels (alpha always 255).
void decode_bc1_block(const std::uint8_t* in_8,       // 8 bytes input
                      std::uint8_t*       block_rgba)  // 16 texels × 4 bytes
    noexcept
{
    const std::uint16_t c0      = read_u16_le(in_8 + 0);
    const std::uint16_t c1      = read_u16_le(in_8 + 2);
    const std::uint32_t indices =
        static_cast<std::uint32_t>(in_8[4]) |
        (static_cast<std::uint32_t>(in_8[5]) <<  8U) |
        (static_cast<std::uint32_t>(in_8[6]) << 16U) |
        (static_cast<std::uint32_t>(in_8[7]) << 24U);

    const Rgb8 p0 = unpack_rgb565(c0);
    const Rgb8 p1 = unpack_rgb565(c1);

    std::array<Rgb8, 4> palette{};

    if (c0 > c1)
    {
        // 4-color opaque mode
        palette[0] = p0;
        palette[1] = p1;
        palette[2] = { static_cast<std::uint8_t>((2U * p0.r + p1.r) / 3U),
                       static_cast<std::uint8_t>((2U * p0.g + p1.g) / 3U),
                       static_cast<std::uint8_t>((2U * p0.b + p1.b) / 3U) };
        palette[3] = { static_cast<std::uint8_t>((p0.r + 2U * p1.r) / 3U),
                       static_cast<std::uint8_t>((p0.g + 2U * p1.g) / 3U),
                       static_cast<std::uint8_t>((p0.b + 2U * p1.b) / 3U) };
    }
    else
    {
        // 3-color + transparent mode (c0 == c1 collapses to this branch safely)
        palette[0] = p0;
        palette[1] = p1;
        palette[2] = { static_cast<std::uint8_t>((p0.r + p1.r) / 2U),
                       static_cast<std::uint8_t>((p0.g + p1.g) / 2U),
                       static_cast<std::uint8_t>((p0.b + p1.b) / 2U) };
        palette[3] = { 0U, 0U, 0U };
    }

    for (int t = 0; t < 16; ++t)
    {
        const std::uint32_t idx = (indices >> static_cast<std::uint32_t>(t * 2)) & 0x3U;
        const std::size_t   off = static_cast<std::size_t>(t) * 4U;
        block_rgba[off + 0U] = palette[idx].r;
        block_rgba[off + 1U] = palette[idx].g;
        block_rgba[off + 2U] = palette[idx].b;
        block_rgba[off + 3U] = 255U;  // BC1 always opaque in 4-color mode
    }
}

// ---- Mip level helpers -----------------------------------------------------

/// Number of mip levels for a dimension.
[[nodiscard]] constexpr std::uint32_t mip_count(std::uint32_t w,
                                                 std::uint32_t h) noexcept
{
    std::uint32_t dim   = std::max(w, h);
    std::uint32_t count = 0U;
    while (dim >= 1U) { ++count; dim >>= 1U; }
    return count;
}

/// Bytes required for one BC1 mip level of given dimensions.
/// BC1 = 8 bytes per 4×4 block; dimensions padded up to multiples of 4.
[[nodiscard]] constexpr std::uint64_t bc1_mip_bytes(std::uint32_t w,
                                                     std::uint32_t h) noexcept
{
    const std::uint32_t bw = (w + 3U) / 4U;
    const std::uint32_t bh = (h + 3U) / 4U;
    return static_cast<std::uint64_t>(bw) * bh * 8ULL;
}

/// Downsample RGBA8 image to half-resolution using a 2×2 box filter.
[[nodiscard]] std::vector<std::uint8_t>
downsample_2x(const std::vector<std::uint8_t>& src,
              std::uint32_t                     w,
              std::uint32_t                     h)
{
    const std::uint32_t dw = std::max(1U, w / 2U);
    const std::uint32_t dh = std::max(1U, h / 2U);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(dw) * dh * 4U, 0U);

    for (std::uint32_t dy = 0U; dy < dh; ++dy)
    {
        for (std::uint32_t dx = 0U; dx < dw; ++dx)
        {
            // Sample 2×2 neighbourhood (clamp to edge).
            const std::uint32_t sx0 = dx * 2U;
            const std::uint32_t sy0 = dy * 2U;
            const std::uint32_t sx1 = std::min(sx0 + 1U, w - 1U);
            const std::uint32_t sy1 = std::min(sy0 + 1U, h - 1U);

            for (std::uint32_t ch = 0U; ch < 4U; ++ch)
            {
                const std::uint32_t sum =
                    static_cast<std::uint32_t>(src[(sy0 * w + sx0) * 4U + ch]) +
                    static_cast<std::uint32_t>(src[(sy0 * w + sx1) * 4U + ch]) +
                    static_cast<std::uint32_t>(src[(sy1 * w + sx0) * 4U + ch]) +
                    static_cast<std::uint32_t>(src[(sy1 * w + sx1) * 4U + ch]);
                dst[(dy * dw + dx) * 4U + ch] =
                    static_cast<std::uint8_t>(sum / 4U);
            }
        }
    }
    return dst;
}

/// Encode a single mip level (w×h RGBA8 → BC1) and append to `blob`.
void encode_bc1_mip(const std::uint8_t* rgba8,
                    std::uint32_t       w,
                    std::uint32_t       h,
                    std::vector<std::uint8_t>& blob)
{
    // BC1: 4×4 blocks, 8 bytes each.
    const std::uint32_t bw = (w + 3U) / 4U;
    const std::uint32_t bh = (h + 3U) / 4U;

    // Scratch buffer for one 4×4 block of RGBA8 texels.
    std::array<std::uint8_t, 64U> block_pixels{};  // 16 texels × 4 bytes
    std::array<std::uint8_t,  8U> block_out{};

    for (std::uint32_t by = 0U; by < bh; ++by)
    {
        for (std::uint32_t bx = 0U; bx < bw; ++bx)
        {
            // Gather 4×4 texels (clamp to edge for non-multiple-of-4 images).
            for (std::uint32_t ty = 0U; ty < 4U; ++ty)
            {
                for (std::uint32_t tx = 0U; tx < 4U; ++tx)
                {
                    const std::uint32_t px = std::min(bx * 4U + tx, w - 1U);
                    const std::uint32_t py = std::min(by * 4U + ty, h - 1U);
                    const std::size_t   si = (static_cast<std::size_t>(py) * w + px) * 4U;
                    const std::size_t   di = (ty * 4U + tx) * 4U;
                    block_pixels[di + 0U] = rgba8[si + 0U];
                    block_pixels[di + 1U] = rgba8[si + 1U];
                    block_pixels[di + 2U] = rgba8[si + 2U];
                    block_pixels[di + 3U] = rgba8[si + 3U];
                }
            }

            encode_bc1_block(block_pixels.data(), block_out.data());

            blob.insert(blob.end(), block_out.begin(), block_out.end());
        }
    }
}

}  // anonymous namespace

// ============================================================================
// encode()
// ============================================================================

std::optional<CompressedTexture>
encode(std::span<const std::uint8_t> rgba8_pixels,
       std::uint32_t                 width,
       std::uint32_t                 height,
       const EncodeOptions&          options)
{
    // ---- Validate format (Sprint-1: BC1 only) --------------------------------
    if (options.target != Format::kBC1)
    {
        // Sprint-2 will implement remaining formats.
        return std::nullopt;
    }

    // ---- Validate dimensions ------------------------------------------------
    if (width == 0U || height == 0U)
    {
        return std::nullopt;
    }

    if (width % 4U != 0U || height % 4U != 0U)
    {
        // BC1 requires dimensions to be multiples of 4.
        return std::nullopt;
    }

    // ---- Validate input size ------------------------------------------------
    const std::size_t expected_bytes =
        static_cast<std::size_t>(width) * height * 4U;

    if (rgba8_pixels.size() != expected_bytes)
    {
        return std::nullopt;
    }

    // ---- Compute total blob size (mip 0 only or full chain) ----------------
    const std::uint32_t num_mips =
        options.generate_mips ? mip_count(width, height) : 1U;

    std::uint64_t total_bytes = 0ULL;
    {
        std::uint32_t mw = width;
        std::uint32_t mh = height;
        for (std::uint32_t m = 0U; m < num_mips; ++m)
        {
            total_bytes += bc1_mip_bytes(mw, mh);
            mw = std::max(1U, mw / 2U);
            mh = std::max(1U, mh / 2U);
        }
    }

    CompressedTexture result;
    result.format = Format::kBC1;
    result.width  = width;
    result.height = height;
    result.blob.reserve(static_cast<std::size_t>(total_bytes));

    // ---- Encode mip 0 -------------------------------------------------------
    encode_bc1_mip(rgba8_pixels.data(), width, height, result.blob);

    // ---- Encode remaining mip levels (box-filter downsample) ---------------
    if (options.generate_mips && num_mips > 1U)
    {
        std::vector<std::uint8_t> prev_mip(rgba8_pixels.begin(),
                                            rgba8_pixels.end());
        std::uint32_t mw = width;
        std::uint32_t mh = height;

        for (std::uint32_t m = 1U; m < num_mips; ++m)
        {
            std::vector<std::uint8_t> cur_mip = downsample_2x(prev_mip, mw, mh);
            mw = std::max(1U, mw / 2U);
            mh = std::max(1U, mh / 2U);
            encode_bc1_mip(cur_mip.data(), mw, mh, result.blob);
            prev_mip = std::move(cur_mip);
        }
    }

    return result;
}

// ============================================================================
// analyze()
// ============================================================================

std::optional<CompressionStats>
analyze(std::span<const std::uint8_t> rgba8_pixels,
        const CompressedTexture&      compressed)
{
    // ---- Sprint-1: BC1 only -------------------------------------------------
    if (compressed.format != Format::kBC1)
    {
        return std::nullopt;
    }

    // ---- Validate dimensions ------------------------------------------------
    if (compressed.width == 0U || compressed.height == 0U)
    {
        return std::nullopt;
    }

    const std::size_t expected_input =
        static_cast<std::size_t>(compressed.width) * compressed.height * 4U;

    if (rgba8_pixels.size() != expected_input)
    {
        return std::nullopt;
    }

    // ---- Expected BC1 blob size for mip 0 only (or at least mip 0) ---------
    const std::uint64_t mip0_bytes =
        bc1_mip_bytes(compressed.width, compressed.height);

    if (compressed.blob.size() < static_cast<std::size_t>(mip0_bytes))
    {
        return std::nullopt;
    }

    // ---- Decode mip 0 and compute RMSE -------------------------------------
    const std::uint32_t bw = (compressed.width  + 3U) / 4U;
    const std::uint32_t bh = (compressed.height + 3U) / 4U;

    // Reconstructed RGBA8 (mip 0 only).
    std::vector<std::uint8_t> decoded(expected_input, 0U);

    std::size_t blob_offset = 0U;
    std::array<std::uint8_t, 64U> block_rgba{};  // 16 texels × 4 bytes

    for (std::uint32_t by = 0U; by < bh; ++by)
    {
        for (std::uint32_t bx = 0U; bx < bw; ++bx)
        {
            decode_bc1_block(compressed.blob.data() + blob_offset,
                             block_rgba.data());
            blob_offset += 8U;

            // Scatter decoded texels back into the decoded image buffer.
            for (std::uint32_t ty = 0U; ty < 4U; ++ty)
            {
                for (std::uint32_t tx = 0U; tx < 4U; ++tx)
                {
                    const std::uint32_t px = bx * 4U + tx;
                    const std::uint32_t py = by * 4U + ty;
                    if (px >= compressed.width || py >= compressed.height)
                    {
                        continue;
                    }
                    const std::size_t di = (static_cast<std::size_t>(py) *
                                            compressed.width + px) * 4U;
                    const std::size_t si = (ty * 4U + tx) * 4U;
                    decoded[di + 0U] = block_rgba[si + 0U];
                    decoded[di + 1U] = block_rgba[si + 1U];
                    decoded[di + 2U] = block_rgba[si + 2U];
                    decoded[di + 3U] = block_rgba[si + 3U];
                }
            }
        }
    }

    // Compute RMSE over R,G,B channels (alpha excluded — BC1 has no alpha).
    double sse = 0.0;
    const std::size_t num_pixels = static_cast<std::size_t>(compressed.width) *
                                   compressed.height;

    for (std::size_t i = 0U; i < num_pixels; ++i)
    {
        for (std::size_t ch = 0U; ch < 3U; ++ch)
        {
            const auto orig = static_cast<double>(rgba8_pixels[i * 4U + ch]);
            const auto recon = static_cast<double>(decoded[i * 4U + ch]);
            const double diff  = orig - recon;
            sse += diff * diff;
        }
    }

    const double mse  = sse / static_cast<double>(num_pixels * 3U);
    const double rmse = std::sqrt(mse);

    // ---- Build stats --------------------------------------------------------
    CompressionStats stats;
    stats.input_bytes  = static_cast<std::uint64_t>(expected_input);
    stats.output_bytes = static_cast<std::uint64_t>(compressed.blob.size());
    stats.ratio        = (stats.output_bytes > 0ULL)
                             ? static_cast<double>(stats.input_bytes) /
                               static_cast<double>(stats.output_bytes)
                             : 0.0;
    stats.rmse         = rmse;

    return stats;
}

}  // namespace cd::asset::texture_compress
