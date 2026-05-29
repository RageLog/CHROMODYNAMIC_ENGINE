// =============================================================================
// CHROMODYNAMIC — cd/asset/ktx2/Ktx2.hpp
//
// Minimal runtime KTX2 reader. KTX2 is the Khronos texture format used
// by tools like toktx, NVIDIA Texture Tools, and AMD Compressonator —
// the industry interchange format for compressed textures. Our cdtex
// is engine-internal; KTX2 lets us consume any third-party content.
//
// SCOPE (v1):
//   * Single 2D texture (faceCount=1, layerCount=1)
//   * No supercompression (supercompressionScheme=0)
//   * VK format known to the engine (BC7_UNORM/SRGB at minimum;
//     RGBA8 falls through as well)
//   * 1+ mip levels
//
// OUT OF SCOPE (v2):
//   * Basis Universal supercompression (would need basisu transcoder)
//   * Zstd supercompression (would need libzstd)
//   * Cubemaps, array textures, 3D textures, compressed array
//   * KTX2 metadata key/value pairs (DFD, animation, etc.)
//
// Format reference:
//   https://github.khronos.org/KTX-Specification/
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::asset::ktx2
{

namespace ktx2_errors
{
inline constexpr std::uint32_t kDomain = 0x0015;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kIoError = 2,
    kMagicMismatch = 3,
    kCorrupt = 4,
    kUnsupportedFormat = 5,            ///< KTX2 itself is OK but the vkFormat isn't on our short-list.
    kUnsupportedSupercompression = 6,  ///< Basis / Zstd not implemented in v1.
    kInvalidArgument = 7,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace ktx2_errors

/// Minimal subset of VkFormat enum values we currently accept. Maps onto
/// cd::rhi::Format at the application level. Keeping it as the raw
/// Vulkan numeric values lets the loader stay independent of cd::rhi.
enum class Ktx2VkFormat : std::uint32_t
{
    kUndefined = 0,
    kR8G8B8A8_Unorm = 37,
    kR8G8B8A8_Srgb = 43,
    kB8G8R8A8_Unorm = 44,
    kB8G8R8A8_Srgb = 50,
    kBC7_Unorm = 145,
    kBC7_Srgb = 146,
};

struct Ktx2Mip
{
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    std::vector<std::uint8_t> bytes;  ///< Tightly packed (no padding) pixel/block data for this level.
};

struct Ktx2
{
    Ktx2VkFormat format { Ktx2VkFormat::kUndefined };
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    std::vector<Ktx2Mip> mips;  ///< Always at least one entry.
};

[[nodiscard]] cd::core::Result<Ktx2> load(std::string_view path);

/// Variant for in-memory blobs (e.g. zip entries, downloads, embedded
/// asset bundles). Caller retains ownership of `bytes`.
[[nodiscard]] cd::core::Result<Ktx2> load_from_memory(std::span<const std::uint8_t> bytes);

// ---------------------------------------------------------------------------
// Write side (Phase 14.B / Wave 158)
//
// Encode an in-memory Ktx2 (one mip OR a fully-populated mip chain) into
// the v2 binary format. Same short-list of vkFormats as the reader plus
// `supercompressionScheme = 0` (no Basis / Zstd). DFD / KVD / SGD blobs
// are written as empty (length 0 offsets 0) to keep the cooker output
// minimal — the engine reader silently tolerates this because it never
// reads DFD/KVD. A compliant KTX2 reader (PVRTexTool, etc.) accepts
// empty DFD; engines that *require* DFD won't accept this output, which
// is a known marathon-scope trade-off.
//
// Returns kInvalidArgument when `tex.mips` is empty or `tex.format` is
// not on the supported short-list.
// ---------------------------------------------------------------------------
[[nodiscard]] cd::core::Result<std::vector<std::uint8_t>> encode_to_memory(const Ktx2& tex);

/// Convenience: encode to memory then write to disk. Same error space as
/// `encode_to_memory` plus `kIoError` on file write failure.
[[nodiscard]] cd::core::Result<void> write(const Ktx2& tex, std::string_view path);

}  // namespace cd::asset::ktx2
