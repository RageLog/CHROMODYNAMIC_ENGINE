// =============================================================================
// CHROMODYNAMIC — cd/rhi/Format.hpp
// ADR-001 (Sprint S3.0) — pixel / vertex / index formats.
//
// Format names follow the Vulkan / GLSL convention: components, bit widths,
// numeric format (`Unorm`, `Snorm`, `Uint`, `Sint`, `Float`, `Srgb`). Block-
// compressed formats use the `BCnAlphaUnorm` style. Depth/stencil formats
// pack depth bits + stencil bits (e.g. `D24S8Uint`).
//
// Members of FormatInfo are reported in bytes/bits for use by RHI back-ends
// (Vulkan needs `VkFormat`, D3D12 needs `DXGI_FORMAT`; mapping tables live in
// the back-end target, not here).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::rhi
{

enum class Format : std::uint16_t
{
    kUndefined = 0,

    // 8-bit single channel
    kR8Unorm,
    kR8Snorm,
    kR8Uint,
    kR8Sint,

    // 8-bit dual channel
    kRG8Unorm,
    kRG8Snorm,
    kRG8Uint,
    kRG8Sint,

    // 8-bit quad channel
    kRGBA8Unorm,
    kRGBA8Snorm,
    kRGBA8Uint,
    kRGBA8Sint,
    kRGBA8Srgb,
    kBGRA8Unorm,
    kBGRA8Srgb,

    // 16-bit single
    kR16Unorm,
    kR16Snorm,
    kR16Uint,
    kR16Sint,
    kR16Float,

    // 16-bit dual
    kRG16Unorm,
    kRG16Snorm,
    kRG16Uint,
    kRG16Sint,
    kRG16Float,

    // 16-bit quad
    kRGBA16Unorm,
    kRGBA16Snorm,
    kRGBA16Uint,
    kRGBA16Sint,
    kRGBA16Float,

    // 32-bit
    kR32Uint,
    kR32Sint,
    kR32Float,
    kRG32Uint,
    kRG32Sint,
    kRG32Float,
    kRGB32Uint,
    kRGB32Sint,
    kRGB32Float,
    kRGBA32Uint,
    kRGBA32Sint,
    kRGBA32Float,

    // Packed / HDR
    kR11G11B10Float,
    kRGB10A2Unorm,
    kRGB10A2Uint,
    kRGB9E5Float,

    // Depth / stencil
    kD16Unorm,
    kD32Float,
    kD24UnormS8Uint,
    kD32FloatS8Uint,
    kS8Uint,

    // Block-compressed (Desktop)
    kBC1RGBUnorm,
    kBC1RGBSrgb,
    kBC1RGBAUnorm,
    kBC1RGBASrgb,
    kBC2Unorm,
    kBC2Srgb,
    kBC3Unorm,
    kBC3Srgb,
    kBC4Unorm,
    kBC4Snorm,
    kBC5Unorm,
    kBC5Snorm,
    kBC6HUFloat,
    kBC6HSFloat,
    kBC7Unorm,
    kBC7Srgb,

    kCount,
};

struct FormatInfo
{
    std::uint8_t bytes_per_block { 0 };  // for non-block formats, == bytes per pixel
    std::uint8_t block_width { 1 };
    std::uint8_t block_height { 1 };
    std::uint8_t channel_count { 0 };
    bool is_depth { false };
    bool is_stencil { false };
    bool is_srgb { false };
    bool is_compressed { false };
};

[[nodiscard]] constexpr FormatInfo info_of(Format f) noexcept
{
    switch (f)
    {
        // 8-bit
        case Format::kR8Unorm:
        case Format::kR8Snorm:
        case Format::kR8Uint:
        case Format::kR8Sint:
            return { 1, 1, 1, 1 };
        case Format::kRG8Unorm:
        case Format::kRG8Snorm:
        case Format::kRG8Uint:
        case Format::kRG8Sint:
            return { 2, 1, 1, 2 };
        case Format::kRGBA8Unorm:
        case Format::kRGBA8Snorm:
        case Format::kRGBA8Uint:
        case Format::kRGBA8Sint:
        case Format::kBGRA8Unorm:
            return { 4, 1, 1, 4 };
        case Format::kRGBA8Srgb:
        case Format::kBGRA8Srgb:
            return { 4, 1, 1, 4, false, false, true, false };

        // 16-bit
        case Format::kR16Unorm:
        case Format::kR16Snorm:
        case Format::kR16Uint:
        case Format::kR16Sint:
        case Format::kR16Float:
            return { 2, 1, 1, 1 };
        case Format::kRG16Unorm:
        case Format::kRG16Snorm:
        case Format::kRG16Uint:
        case Format::kRG16Sint:
        case Format::kRG16Float:
            return { 4, 1, 1, 2 };
        case Format::kRGBA16Unorm:
        case Format::kRGBA16Snorm:
        case Format::kRGBA16Uint:
        case Format::kRGBA16Sint:
        case Format::kRGBA16Float:
            return { 8, 1, 1, 4 };

        // 32-bit
        case Format::kR32Uint:
        case Format::kR32Sint:
        case Format::kR32Float:
            return { 4, 1, 1, 1 };
        case Format::kRG32Uint:
        case Format::kRG32Sint:
        case Format::kRG32Float:
            return { 8, 1, 1, 2 };
        case Format::kRGB32Uint:
        case Format::kRGB32Sint:
        case Format::kRGB32Float:
            return { 12, 1, 1, 3 };
        case Format::kRGBA32Uint:
        case Format::kRGBA32Sint:
        case Format::kRGBA32Float:
            return { 16, 1, 1, 4 };

        // Packed
        case Format::kR11G11B10Float:
        case Format::kRGB10A2Unorm:
        case Format::kRGB10A2Uint:
        case Format::kRGB9E5Float:
            return { 4, 1, 1, 3 };

        // Depth/stencil
        case Format::kD16Unorm:
            return { 2, 1, 1, 1, true, false };
        case Format::kD32Float:
            return { 4, 1, 1, 1, true, false };
        case Format::kS8Uint:
            return { 1, 1, 1, 1, false, true };
        case Format::kD24UnormS8Uint:
            return { 4, 1, 1, 2, true, true };
        case Format::kD32FloatS8Uint:
            return { 8, 1, 1, 2, true, true };

        // BC1-BC7 (4x4 blocks)
        case Format::kBC1RGBUnorm:
        case Format::kBC1RGBAUnorm:
        case Format::kBC4Unorm:
        case Format::kBC4Snorm:
            return { 8, 4, 4, 4, false, false, false, true };
        case Format::kBC1RGBSrgb:
        case Format::kBC1RGBASrgb:
            return { 8, 4, 4, 4, false, false, true, true };
        case Format::kBC2Unorm:
        case Format::kBC3Unorm:
        case Format::kBC5Unorm:
        case Format::kBC5Snorm:
        case Format::kBC6HUFloat:
        case Format::kBC6HSFloat:
        case Format::kBC7Unorm:
            return { 16, 4, 4, 4, false, false, false, true };
        case Format::kBC2Srgb:
        case Format::kBC3Srgb:
        case Format::kBC7Srgb:
            return { 16, 4, 4, 4, false, false, true, true };

        case Format::kUndefined:
        case Format::kCount:
        default:
            return {};
    }
}

[[nodiscard]] constexpr bool is_depth_format(Format f) noexcept
{
    return info_of(f).is_depth;
}

[[nodiscard]] constexpr bool is_stencil_format(Format f) noexcept
{
    return info_of(f).is_stencil;
}

[[nodiscard]] constexpr bool is_compressed_format(Format f) noexcept
{
    return info_of(f).is_compressed;
}

}  // namespace cd::rhi
