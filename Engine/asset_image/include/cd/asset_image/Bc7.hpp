// =============================================================================
// CHROMODYNAMIC — cd/asset_image/Bc7.hpp
//
// BC7 (BPTC RGBA) compression. Offline cooker path:
//   load_image() → RGBA8 in RAM → compress_bc7() → BC7 block stream.
//
// BC7 is the highest-quality desktop-GPU compressed format for RGBA
// textures: 4 bytes per pixel uncompressed → 1 byte per pixel BC7
// (4×4 blocks → 16 bytes per block). Supported by every Vulkan 1.0+
// desktop GPU (VK_FORMAT_BC7_UNORM_BLOCK / SRGB).
//
// Encoder: bc7enc_rdo (richgel999, MIT). Single-header style; the impl
// macros are defined in src/Bc7Compress.cpp so consumers of this header
// don't pay the build cost.
//
// Limitations of v1:
//   * Input must be RGBA8 with width + height both ≥ 4 (BC7 block size).
//     Non-multiple-of-4 dimensions get padded with zeros — fine for
//     repeating textures, may show edge artefacts on isolated assets.
//     v2: do proper border extension.
//   * No mipmap chain generation — the caller compresses each mip
//     level independently. Hot path for cooker; runtime is responsible
//     for mip-down generation when needed.
//   * No RDO (rate-distortion optimisation). Plain bc7enc default
//     mode = "high quality" preset (~50 ms / megapixel on M-class CPUs).
//     RDO mode for storage-bound shipping builds is a v2 toggle.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace cd::asset_image
{

namespace bc7_errors
{
inline constexpr std::uint32_t kDomain = 0x0011;

enum class Code : std::uint32_t
{
    kOk = 0,
    kInvalidArgument = 1,
    kUnsupportedDimensions = 2,
    kEncoderFailed = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace bc7_errors

/// Encoder quality preset. Maps onto bc7enc's internal "uber_level" knob.
enum class Bc7Quality : std::uint8_t
{
    kFast = 0,     ///< ~5 ms / megapixel, lower visual quality. Dev builds.
    kBalanced = 1, ///< ~25 ms / mp, default. Production cooker.
    kHigh = 2,     ///< ~50 ms / mp, best PSNR. Shipping builds.
};

/// BC7-compressed payload. `data.size() == ceil(w/4) * ceil(h/4) * 16`.
/// `block_w` / `block_h` are the rounded-up block counts (input
/// dimensions are not stored — the caller is responsible).
struct Bc7Block
{
    std::vector<std::uint8_t> data;
    std::uint32_t block_w { 0 };
    std::uint32_t block_h { 0 };
};

/// Compress an RGBA8 image to BC7. `rgba.size()` must equal width * height * 4.
/// Both dimensions are rounded up to the next multiple of 4 with zero
/// padding (acceptable for tiling textures; v2 will support edge extend).
[[nodiscard]] cd::core::Result<Bc7Block> compress_bc7(
    std::span<const std::uint8_t> rgba,
    std::uint32_t width,
    std::uint32_t height,
    Bc7Quality quality = Bc7Quality::kBalanced
);

}  // namespace cd::asset_image
