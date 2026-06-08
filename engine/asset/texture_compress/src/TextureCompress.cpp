// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_compress/src/TextureCompress.cpp
// Phase 650 (Sprint-1) + Phase 750 (Sprint-2)
//
// Sprint-1: BC1 stub encoder + BC1 decoder (for analyze()).
// Sprint-2: real BC7 via bc7enc_rdo + real ASTC via ARM astc-encoder.
//           Both are optional: guarded by CD_TC_HAS_BC7ENC / CD_TC_HAS_ASTCENC.
//
// BC1 block layout (8 bytes per 4×4 block):
//   [0..1] color0 (RGB565 little-endian)
//   [2..3] color1 (RGB565 little-endian)
//   [4..7] 2-bit per-texel indices (16 texels, packed LSB-first)
//
// BC7 block layout: 16 bytes per 4×4 block (handled by bc7enc_rdo).
// ASTC block layout: 16 bytes per block (handled by ARM astc-encoder).
// =============================================================================

#include <cd/asset/texture_compress/TextureCompress.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// ---- Optional BC7 encoder (bc7enc_rdo) ------------------------------------
#if CD_TC_HAS_BC7ENC
#  include <bc7enc.h>
#endif

// ---- Optional ASTC encoder (ARM astc-encoder) ------------------------------
#if CD_TC_HAS_ASTCENC
#  include <astcenc.h>
#endif

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
void encode_bc1_block(const std::uint8_t* block_rgba,  // 16 texels × 4 bytes
                      std::uint8_t*       out_8)        // 8 bytes output
    noexcept
{
    // --- Step 1: find per-channel min / max (8-bit space) -------------------
    std::uint8_t min_r = 255U;
    std::uint8_t min_g = 255U;
    std::uint8_t min_b = 255U;
    std::uint8_t max_r = 0U;
    std::uint8_t max_g = 0U;
    std::uint8_t max_b = 0U;

    for (int t = 0; t < 16; ++t)
    {
        const std::uint8_t r = block_rgba[static_cast<std::size_t>(t) * 4U + 0U];
        const std::uint8_t g = block_rgba[static_cast<std::size_t>(t) * 4U + 1U];
        const std::uint8_t b = block_rgba[static_cast<std::size_t>(t) * 4U + 2U];
        min_r = std::min(min_r, r);  min_g = std::min(min_g, g);  min_b = std::min(min_b, b);
        max_r = std::max(max_r, r);  max_g = std::max(max_g, g);  max_b = std::max(max_b, b);
    }

    // --- Step 2: pack endpoints in RGB565 -----------------------------------
    const std::uint16_t color0 = pack_rgb565(max_r, max_g, max_b);
    const std::uint16_t color1 = pack_rgb565(min_r, min_g, min_b);

    const std::uint16_t c0 = (color0 >= color1) ? color0 : color1;
    const std::uint16_t c1 = (color0 >= color1) ? color1 : color0;

    // --- Step 3: compute per-texel 2-bit indices ----------------------------
    const Rgb8 p0 = unpack_rgb565(c0);
    const Rgb8 p1 = unpack_rgb565(c1);

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
        // 3-color + transparent mode
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
        block_rgba[off + 3U] = 255U;
    }
}

// ---- Mip level helpers -----------------------------------------------------

/// Number of mip levels for given dimensions.
[[nodiscard]] constexpr std::uint32_t mip_count(std::uint32_t w,
                                                 std::uint32_t h) noexcept
{
    std::uint32_t dim   = std::max(w, h);
    std::uint32_t count = 0U;
    while (dim >= 1U) { ++count; dim >>= 1U; }
    return count;
}

/// Bytes required for one BC1 mip level of given dimensions.
[[nodiscard]] constexpr std::uint64_t bc1_mip_bytes(std::uint32_t w,
                                                     std::uint32_t h) noexcept
{
    const std::uint32_t bw = (w + 3U) / 4U;
    const std::uint32_t bh = (h + 3U) / 4U;
    return static_cast<std::uint64_t>(bw) * bh * 8ULL;
}

/// ASTC: 16 bytes per block, block_dim × block_dim texels.
[[nodiscard]] constexpr std::uint64_t astc_mip_bytes(std::uint32_t w,
                                                      std::uint32_t h,
                                                      std::uint32_t block_dim) noexcept
{
    const std::uint32_t bw = (w + block_dim - 1U) / block_dim;
    const std::uint32_t bh = (h + block_dim - 1U) / block_dim;
    return static_cast<std::uint64_t>(bw) * bh * 16ULL;
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

/// Encode a single BC1 mip level and append to blob.
void encode_bc1_mip(const std::uint8_t* rgba8,
                    std::uint32_t       w,
                    std::uint32_t       h,
                    std::vector<std::uint8_t>& blob)
{
    const std::uint32_t bw = (w + 3U) / 4U;
    const std::uint32_t bh = (h + 3U) / 4U;

    std::array<std::uint8_t, 64U> block_pixels{};
    std::array<std::uint8_t,  8U> block_out{};

    for (std::uint32_t by = 0U; by < bh; ++by)
    {
        for (std::uint32_t bx = 0U; bx < bw; ++bx)
        {
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

// ---- BC7 mip encoder (Sprint-2) --------------------------------------------

#if CD_TC_HAS_BC7ENC

/// BC7: 16 bytes per 4×4 block.
[[nodiscard]] constexpr std::uint64_t bc7_mip_bytes(std::uint32_t w,
                                                     std::uint32_t h) noexcept
{
    const std::uint32_t bw = (w + 3U) / 4U;
    const std::uint32_t bh = (h + 3U) / 4U;
    return static_cast<std::uint64_t>(bw) * bh * 16ULL;
}

/// Encode one mip level of RGBA8 into BC7 (16 bytes per 4×4 block).
void encode_bc7_mip(const std::uint8_t*        rgba8,
                    std::uint32_t              w,
                    std::uint32_t              h,
                    std::vector<std::uint8_t>& blob,
                    const bc7enc_compress_block_params& params)
{
    const std::uint32_t bw = (w + 3U) / 4U;
    const std::uint32_t bh = (h + 3U) / 4U;

    std::array<std::uint8_t, 64U> block_pixels{};  // 16 texels × 4 bytes RGBA
    std::array<std::uint8_t, 16U> block_out{};     // 16 bytes BC7 output

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

            // bc7enc_compress_block() returns true if the block used the alpha
            // path, false if it used the opaque path — NOT a success/failure
            // indicator. Both outcomes produce a valid 16-byte BC7 block.
            bc7enc_compress_block(block_out.data(),
                                  block_pixels.data(),
                                  &params);

            blob.insert(blob.end(), block_out.begin(), block_out.end());
        }
    }
}

#endif  // CD_TC_HAS_BC7ENC

// ---- ASTC mip encoder (Sprint-2) -------------------------------------------

#if CD_TC_HAS_ASTCENC

/// Encode one mip level of RGBA8 into ASTC (16 bytes per block_dim×block_dim).
/// Returns false on astcenc failure.
[[nodiscard]] bool encode_astc_mip(const std::uint8_t*        rgba8,
                                    std::uint32_t              w,
                                    std::uint32_t              h,
                                    std::uint32_t              block_dim,
                                    std::vector<std::uint8_t>& blob,
                                    float                      quality)
{
    // --- Configure and allocate context ----------------------------------------
    astcenc_config cfg{};
    const astcenc_error err_cfg = astcenc_config_init(
        ASTCENC_PRF_LDR,
        block_dim, block_dim,
        1U,           // block_z = 1 (2D)
        quality,
        0U,           // flags = 0
        &cfg);
    if (err_cfg != ASTCENC_SUCCESS)
    {
        return false;
    }

    astcenc_context* ctx = nullptr;
    const astcenc_error err_alloc = astcenc_context_alloc(&cfg, 1U, &ctx);
    if (err_alloc != ASTCENC_SUCCESS || ctx == nullptr)
    {
        return false;
    }

    // --- Build astcenc_image from our flat RGBA8 buffer -------------------------
    // astcenc_image.data is void** — array of 2D slice pointers.
    // For a 2D image, dim_z = 1 and data[0] points at the row-major RGBA8 data.
    // The data is read-only during compression; the cast-away-const is safe here
    // because astcenc does not modify the input pixels.
    void* slice_ptr = const_cast<std::uint8_t*>(rgba8);  // NOLINT(cppcoreguidelines-pro-type-const-cast)

    astcenc_image img{};
    img.dim_x     = w;
    img.dim_y     = h;
    img.dim_z     = 1U;
    img.data_type = ASTCENC_TYPE_U8;
    img.data      = &slice_ptr;

    // --- Allocate output buffer --------------------------------------------------
    const std::uint32_t bw = (w + block_dim - 1U) / block_dim;
    const std::uint32_t bh = (h + block_dim - 1U) / block_dim;
    const std::size_t   out_bytes = static_cast<std::size_t>(bw) * bh * 16U;
    std::vector<std::uint8_t> out_buf(out_bytes, 0U);

    // --- Identity swizzle (RGBA → RGBA) -----------------------------------------
    const astcenc_swizzle swizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G,
                                   ASTCENC_SWZ_B, ASTCENC_SWZ_A };

    // --- Compress ---------------------------------------------------------------
    const astcenc_error err_cmp = astcenc_compress_image(
        ctx, &img, &swizzle,
        out_buf.data(), out_bytes,
        0U  // thread_index = 0 (single-threaded)
    );

    astcenc_context_free(ctx);

    if (err_cmp != ASTCENC_SUCCESS)
    {
        return false;
    }

    blob.insert(blob.end(), out_buf.begin(), out_buf.end());
    return true;
}

#endif  // CD_TC_HAS_ASTCENC

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
    // ---- Validate dimensions ------------------------------------------------
    if (width == 0U || height == 0U)
    {
        return std::nullopt;
    }

    // ---- Validate input size ------------------------------------------------
    const std::size_t expected_bytes =
        static_cast<std::size_t>(width) * height * 4U;
    if (rgba8_pixels.size() != expected_bytes)
    {
        return std::nullopt;
    }

    // ---- Dispatch by format -------------------------------------------------

    // ---- BC1 ----------------------------------------------------------------
    if (options.target == Format::kBC1)
    {
        if (width % 4U != 0U || height % 4U != 0U)
        {
            return std::nullopt;
        }

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

        encode_bc1_mip(rgba8_pixels.data(), width, height, result.blob);

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

    // ---- BC7 ----------------------------------------------------------------
    if (options.target == Format::kBC7)
    {
#if CD_TC_HAS_BC7ENC
        if (width % 4U != 0U || height % 4U != 0U)
        {
            return std::nullopt;
        }

        // Map quality [0..255] → bc7enc uber_level [0..4].
        // uber_level 0 = fastest, 4 = best. Our quality byte:
        //   0..50   → 0, 51..101 → 1, 102..152 → 2, 153..203 → 3, 204..255 → 4
        const std::uint32_t uber = static_cast<std::uint32_t>(options.quality) * 4U / 255U;

        // Initialise the lookup tables (thread-safe: idempotent after first call).
        bc7enc_compress_block_init();

        bc7enc_compress_block_params params{};
        bc7enc_compress_block_params_init(&params);
        params.m_uber_level = uber;

        const std::uint32_t num_mips =
            options.generate_mips ? mip_count(width, height) : 1U;

        CompressedTexture result;
        result.format = Format::kBC7;
        result.width  = width;
        result.height = height;

        // Estimate total bytes for reservation.
        {
            std::uint64_t total = 0ULL;
            std::uint32_t mw = width;
            std::uint32_t mh = height;
            for (std::uint32_t m = 0U; m < num_mips; ++m)
            {
                total += bc7_mip_bytes(mw, mh);
                mw = std::max(1U, mw / 2U);
                mh = std::max(1U, mh / 2U);
            }
            result.blob.reserve(static_cast<std::size_t>(total));
        }

        // Encode mip 0.
        encode_bc7_mip(rgba8_pixels.data(), width, height, result.blob, params);

        // Encode remaining mips.
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
                encode_bc7_mip(cur_mip.data(), mw, mh, result.blob, params);
                prev_mip = std::move(cur_mip);
            }
        }

        return result;
#else
        // BC7 encoder not compiled in.
        return std::nullopt;
#endif  // CD_TC_HAS_BC7ENC
    }

    // ---- ASTC 4×4 / 8×8 ----------------------------------------------------
    if (options.target == Format::kAstc4x4 || options.target == Format::kAstc8x8)
    {
#if CD_TC_HAS_ASTCENC
        const std::uint32_t block_dim =
            (options.target == Format::kAstc4x4) ? 4U : 8U;

        // ASTC does not require power-of-2 or multiple-of-block dimensions,
        // but width/height must be non-zero (checked above).

        // Map quality [0..255] → astcenc preset [FASTEST..EXHAUSTIVE].
        const float quality = ASTCENC_PRE_FASTEST +
            static_cast<float>(options.quality) *
            (ASTCENC_PRE_EXHAUSTIVE - ASTCENC_PRE_FASTEST) / 255.0F;

        const std::uint32_t num_mips =
            options.generate_mips ? mip_count(width, height) : 1U;

        CompressedTexture result;
        result.format = options.target;
        result.width  = width;
        result.height = height;

        // Estimate total bytes for reservation.
        {
            std::uint64_t total = 0ULL;
            std::uint32_t mw = width;
            std::uint32_t mh = height;
            for (std::uint32_t m = 0U; m < num_mips; ++m)
            {
                total += astc_mip_bytes(mw, mh, block_dim);
                mw = std::max(1U, mw / 2U);
                mh = std::max(1U, mh / 2U);
            }
            result.blob.reserve(static_cast<std::size_t>(total));
        }

        // Encode mip 0.
        if (!encode_astc_mip(rgba8_pixels.data(), width, height,
                             block_dim, result.blob, quality))
        {
            return std::nullopt;
        }

        // Encode remaining mips.
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
                if (!encode_astc_mip(cur_mip.data(), mw, mh,
                                    block_dim, result.blob, quality))
                {
                    return std::nullopt;
                }
                prev_mip = std::move(cur_mip);
            }
        }

        return result;
#else
        // ASTC encoder not compiled in.
        return std::nullopt;
#endif  // CD_TC_HAS_ASTCENC
    }

    // ---- BC3 / BC5 (not yet implemented) ------------------------------------
    // Sprint-3: bc7enc_rdo rgbcx.h provides BC1/BC3/BC5 real encoders.
    return std::nullopt;
}

// ============================================================================
// analyze()
// ============================================================================

std::optional<CompressionStats>
analyze(std::span<const std::uint8_t> rgba8_pixels,
        const CompressedTexture&      compressed)
{
    // analyze() currently supports BC1 only (Sprint-1 + Sprint-2 scope).
    // BC7 and ASTC decoders are not bundled; adding them is Sprint-3 scope.
    if (compressed.format != Format::kBC1)
    {
        return std::nullopt;
    }

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

    const std::uint64_t mip0_bytes =
        bc1_mip_bytes(compressed.width, compressed.height);

    if (compressed.blob.size() < static_cast<std::size_t>(mip0_bytes))
    {
        return std::nullopt;
    }

    const std::uint32_t bw = (compressed.width  + 3U) / 4U;
    const std::uint32_t bh = (compressed.height + 3U) / 4U;

    std::vector<std::uint8_t> decoded(expected_input, 0U);

    std::size_t blob_offset = 0U;
    std::array<std::uint8_t, 64U> block_rgba{};

    for (std::uint32_t by = 0U; by < bh; ++by)
    {
        for (std::uint32_t bx = 0U; bx < bw; ++bx)
        {
            decode_bc1_block(compressed.blob.data() + blob_offset,
                             block_rgba.data());
            blob_offset += 8U;

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
            const auto orig  = static_cast<double>(rgba8_pixels[i * 4U + ch]);
            const auto recon = static_cast<double>(decoded[i * 4U + ch]);
            const double diff  = orig - recon;
            sse += diff * diff;
        }
    }

    const double mse  = sse / static_cast<double>(num_pixels * 3U);
    const double rmse = std::sqrt(mse);

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
