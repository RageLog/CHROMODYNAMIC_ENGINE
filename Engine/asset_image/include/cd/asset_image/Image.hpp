// =============================================================================
// CHROMODYNAMIC — cd/asset_image/Image.hpp
//
// Stand-alone image loader for PNG / JPG / TGA / BMP / GIF (1st frame) /
// HDR (Radiance, float pixels) backed by stb_image. Kept separate from
// `cd::asset_gltf` so the engine can pull image decoding without the
// JSON / glTF parser dependency chain.
//
// Scope (v1):
//   * 8-bit RGBA output (always 4 channels, regardless of source format)
//   * Optional vertical flip (matches Vulkan's top-left UV origin vs.
//     OpenGL's bottom-left, configurable)
//   * Load from disk OR from a memory buffer
//   * 32-bit float HDR output via a separate `load_image_hdr()` entry
//     (Radiance .hdr inputs only for v1)
//
// Not in v1:
//   * KTX2 / DDS / Basis — see W14.3 (libktx integration)
//   * Mipmap generation — see W8.3 (BC7 compression pipeline)
//   * Cubemap atlassing — application-layer concern
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

namespace cd::asset_image
{

namespace image_errors
{
inline constexpr std::uint32_t kDomain = 0x000E;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kDecodeFailed = 2,
    kInvalidArgument = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace image_errors

/// 8-bit RGBA decoded image. Pixels are tightly packed (no row stride
/// padding); `rgba.size() == width * height * 4`.
struct Image
{
    std::vector<std::uint8_t> rgba;
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    /// True if the original file carried alpha (stb reports `comp == 4`).
    /// Useful for selecting a BCn variant later (BC1 vs BC3).
    bool has_alpha { false };
};

/// 32-bit float RGBA HDR image. Same packing rules as `Image` but four
/// floats per pixel. Source is Radiance (.hdr) and OpenEXR-via-stb-fallback;
/// genuine OpenEXR support requires libdeflate + EXR which we skip in v1.
struct ImageHdr
{
    std::vector<float> rgba;
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
};

/// Decode flags passed at load time. Defaults mirror the most common
/// engine consumption (Vulkan upload, top-left origin, alpha if present).
struct LoadOptions
{
    /// Flip Y at decode time. Set to `true` if your downstream UV
    /// convention expects bottom-left origin (e.g. OpenGL textures).
    /// Vulkan / D3D consumers leave this false.
    bool flip_vertical { false };
};

/// Load an image from a disk path. Format detected from file signature
/// (NOT extension), so misnamed files still decode correctly.
[[nodiscard]] cd::core::Result<Image> load_image(std::string_view path, const LoadOptions& options = {});

/// Load from an in-memory blob (e.g. a `.glb` bin chunk, a downloaded
/// asset, a zip entry). The caller retains ownership of `bytes`.
[[nodiscard]] cd::core::Result<Image>
load_image_from_memory(std::span<const std::uint8_t> bytes, const LoadOptions& options = {});

/// HDR variant — accepts Radiance .hdr inputs. Other formats are decoded
/// to 8-bit then promoted to float (lossy); prefer load_image for those.
[[nodiscard]] cd::core::Result<ImageHdr> load_image_hdr(std::string_view path, const LoadOptions& options = {});

/// 2x2 box-filter mipmap chain generator. Returns a vector of Images,
/// element 0 being the input copied unchanged and element N being a
/// 1x1 (or 1xK / Kx1 for non-square inputs) final mip.
///
/// Algorithm: each subsequent mip averages a 2x2 block of the previous
/// mip into one pixel (per-channel, with rounding via +1 in the sum).
/// Non-multiple-of-2 source dimensions are handled by clamping the
/// last row/col — the boundary samples appear twice. This is the
/// simplest correct box filter; the cooker can opt up to Kaiser or
/// Lanczos later when image quality matters.
///
/// `levels` caps the count — pass 0 to generate the full chain down to
/// 1x1. The first level is always the source mip itself.
[[nodiscard]] cd::core::Result<std::vector<Image>> generate_mips(const Image& src, std::uint32_t levels = 0);

}  // namespace cd::asset_image
