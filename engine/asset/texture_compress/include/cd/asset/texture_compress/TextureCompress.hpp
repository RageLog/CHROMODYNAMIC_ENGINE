// =============================================================================
// CHROMODYNAMIC — cd/asset/texture_compress/TextureCompress.hpp
// Phase 650 — cd::asset::texture_compress (Sprint-1: BC1 stub encoder)
//
// Offline texture compression pipeline: compress raw RGBA8 pixels into
// GPU-native block-compressed formats (BC1/BC3/BC5/BC7 for desktop,
// ASTC for mobile).
//
// Sprint-1: API surface + BC1 stub encoder (naive min/max endpoint picker).
// Sprint-2: real BC1 PCA + BC3/BC5/BC7 via bc7enc + ASTC via astc-encoder.
//
// Moment: game ships with 8 GB → 2 GB texture footprint; mobile build runs on
// devices without 8 GB VRAM.
//
// Namespace: cd::asset::texture_compress
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cd::asset::texture_compress
{

// ---- Format -----------------------------------------------------------------

/// Supported block-compressed texture formats.
enum class Format : std::uint8_t
{
    kBC1,      ///< BC1 (DXT1) — RGB / RGB+1-bit-alpha, 4 bpp
    kBC3,      ///< BC3 (DXT5) — RGBA, 8 bpp
    kBC5,      ///< BC5 (RGTC2) — two-channel (normal maps), 8 bpp
    kBC7,      ///< BC7 (BPTC) — high-quality RGBA, 8 bpp
    kAstc4x4,  ///< ASTC 4×4 — variable bpp (~8 bpp for RGBA LDR)
    kAstc8x8,  ///< ASTC 8×8 — lower bpp, mobile high-efficiency
};

// ---- CompressedTexture ------------------------------------------------------

/// Output of a successful encode() call.
///
/// `blob` contains the raw block-compressed bytes in linear mip order:
///   mip 0 (full-res), mip 1 (half-res), ... if generate_mips was requested.
/// Dimensions refer to the base mip level.
struct CompressedTexture
{
    Format                      format {};
    std::uint32_t               width  {};
    std::uint32_t               height {};
    std::vector<std::uint8_t>   blob   {};
};

// ---- EncodeOptions ----------------------------------------------------------

/// Options passed to encode().
struct EncodeOptions
{
    Format        target        { Format::kBC1 };  ///< Desired output format
    std::uint8_t  quality       { 128 };           ///< 0 = fastest, 255 = best quality
    bool          generate_mips { false };         ///< Generate full mip chain
};

// ---- CompressionStats -------------------------------------------------------

/// Statistics reported by analyze().
struct CompressionStats
{
    std::uint64_t input_bytes  {};   ///< Raw RGBA8 input byte count
    std::uint64_t output_bytes {};   ///< Compressed blob byte count
    double        ratio        {};   ///< input_bytes / output_bytes (>1 = savings)
    double        rmse         {};   ///< Root-mean-square error vs original
};

// ---- Public API -------------------------------------------------------------

/// Compress raw RGBA8 pixels into a block-compressed format.
///
/// Preconditions (returns nullopt on violation):
///   - rgba8_pixels.size() == width * height * 4
///   - width and height are multiples of the format's block size (4 for BC*, 4
///     or 8 for ASTC), each >= 4
///   - Sprint-1: only Format::kBC1 is implemented; other formats return nullopt.
///
/// Sprint-1 BC1 encoder: naive per-block min/max endpoint picker. Produces
/// correct-shape output (8 bytes per 4×4 block) but is not quality-optimal.
/// Sprint-2 will replace the inner loop with full PCA-based endpoint search.
///
/// @param rgba8_pixels  RGBA8 input, row-major, no padding.
/// @param width         Image width in pixels (must be a multiple of 4, >= 4).
/// @param height        Image height in pixels (must be a multiple of 4, >= 4).
/// @param options       Encoding options (target format, quality, mip flags).
/// @return              Compressed texture on success, nullopt on error.
[[nodiscard]] std::optional<CompressedTexture>
encode(std::span<const std::uint8_t> rgba8_pixels,
       std::uint32_t                 width,
       std::uint32_t                 height,
       const EncodeOptions&          options);

/// Compute compression statistics for a previously encoded texture.
///
/// Decompresses `compressed.blob` (Sprint-1: BC1 only) and compares it to
/// the original RGBA8 input to compute RMSE and the compression ratio.
///
/// Preconditions (returns nullopt on violation):
///   - rgba8_pixels.size() == compressed.width * compressed.height * 4
///   - compressed.format == Format::kBC1 (Sprint-1 limitation)
///
/// @param rgba8_pixels  Original RGBA8 input used to produce `compressed`.
/// @param compressed    The CompressedTexture returned by encode().
/// @return              CompressionStats on success, nullopt on error.
[[nodiscard]] std::optional<CompressionStats>
analyze(std::span<const std::uint8_t> rgba8_pixels,
        const CompressedTexture&      compressed);

}  // namespace cd::asset::texture_compress
