// =============================================================================
// CHROMODYNAMIC — cd/asset/cdtex/CdTex.hpp
//
// Runtime reader for the .cdtex BC7 texture format produced by
// tools/cook_texture. The cooker writes a 16-byte header followed by
// raw BC7 block bytes; this library reads them back so the renderer can
// upload straight into a VK_FORMAT_BC7_UNORM_BLOCK image without an
// intermediate decode pass.
//
// Format (matches tools/cook_texture/main.cpp byte-for-byte):
//
// Version 1 (single mip):
//   ┌──────────────── HEADER v1 (18 B) ────────────┐
//   │ magic[5]   "CDBC7"                            │
//   │ version    u8  = 1                            │
//   │ width      u32 (pixels)                       │
//   │ height     u32 (pixels)                       │
//   │ block_w    u16 (== ceil(width / 4))           │
//   │ block_h    u16 (== ceil(height / 4))          │
//   └───────────────────────────────────────────────┘
//   Payload: block_w * block_h * 16 bytes (BC7 blocks)
//   5 + 1 + 4 + 4 + 2 + 2 = 18.
//
// Version 2 (mip chain — cooker --mips flag):
//   ┌──────────────── HEADER v2 (19 B) ────────────┐
//   │ magic[5]   "CDBC7"                            │
//   │ version    u8  = 2                            │
//   │ width      u32 (pixels, mip 0)                │
//   │ height     u32 (pixels, mip 0)                │
//   │ block_w    u16 (mip 0)                        │
//   │ block_h    u16 (mip 0)                        │
//   │ mip_count  u8  (>= 1)                         │
//   └───────────────────────────────────────────────┘
//   Payload: mip levels concatenated in order. Each mip i has
//   ceil(width >> i / 4) * ceil(height >> i / 4) * 16 bytes.
//   The loader returns `mips` as a vector — `mips[0].blocks` is mip 0.
//
// Split into a separate library (vs. just decoding inside the sample)
// so multiple consumers — runtime, editor preview, asset-bundle packer —
// share one parser. Depends only on cd::core.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::asset::cdtex
{

namespace cdtex_errors
{
inline constexpr std::uint32_t kDomain = 0x0012;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kIoError = 2,
    kMagicMismatch = 3,
    kVersionMismatch = 4,
    kCorrupt = 5,
    kInvalidArgument = 6,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace cdtex_errors

/// Maximum supported format version. Loader accepts 1 (single mip) and 2
/// (mip chain). Older v1 files load as a 1-element `mips` vector.
inline constexpr std::uint8_t kFormatVersion = 2;

/// One mip level. mips[0] is the base level matching the file header's
/// width/height; subsequent mips halve dimensions.
struct CdTexMip
{
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    std::uint16_t block_w { 0 };
    std::uint16_t block_h { 0 };
    std::vector<std::uint8_t> blocks;
};

struct CdTex
{
    /// Logical pixel dimensions of the source image (mip 0). Vulkan
    /// image_extent = (width, height, 1).
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    /// 4×4-block grid dimensions for mip 0. Always equal to mips[0].block_w/h.
    std::uint16_t block_w { 0 };
    std::uint16_t block_h { 0 };
    /// Mip chain (always at least one entry). `mips[0]` is the base level.
    /// v1 files load with mips.size() == 1.
    std::vector<CdTexMip> mips;
    /// LEGACY: same data as mips[0].blocks. Kept so v1-era consumers don't
    /// break. New code should iterate `mips`.
    std::vector<std::uint8_t> blocks;
};

/// Load a .cdtex file from disk. Reads the entire file into RAM and
/// dispatches to `decode()` below — kept as a thin convenience around
/// `decode()` so memory-mapped / VFS-backed flows (asset registry) use
/// the same parser.
[[nodiscard]] cd::core::Result<CdTex> load(std::string_view path);

/// Decode a .cdtex byte buffer already in memory. Identical wire-format
/// semantics as load() — same kCorrupt / kMagicMismatch / kVersionMismatch
/// error codes.
[[nodiscard]] cd::core::Result<CdTex> decode(const std::uint8_t* bytes, std::size_t size);

[[nodiscard]] cd::core::Result<CdTex> load(std::string_view path);

}  // namespace cd::asset::cdtex
