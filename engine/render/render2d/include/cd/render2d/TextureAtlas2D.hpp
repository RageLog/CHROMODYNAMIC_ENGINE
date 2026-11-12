// =============================================================================
// CHROMODYNAMIC — cd/render2d/TextureAtlas2D.hpp
// Milestone 3 — Real RGBA8 GPU Texture Atlas Container with Nearest/Linear
// Filtering, UV Sub-Rectangle Mapping, Procedural Pixel-Art Synthesizer,
// Normal & Emissive Atlas Maps, and Disk/Memory Image Loaders.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/render2d/SpriteBatch2D.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::render2d
{

namespace atlas_errors
{
inline constexpr std::uint32_t kDomain = 0x0033;
enum class Code : std::uint32_t
{
    kOk           = 0,
    kFileNotFound = 1,
    kDecodeFailed = 2,
    kInvalidData  = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace atlas_errors

enum class TextureFilterMode2D : std::uint8_t
{
    kNearest = 0,  ///< Crisp pixel-art (point sampling)
    kLinear  = 1,  ///< Smooth bilinear filtering
};

struct AtlasRegion2D
{
    std::string   name {};
    std::uint32_t x { 0U };
    std::uint32_t y { 0U };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t pixel_w { 0U };
    std::uint32_t pixel_h { 0U };
    UvRect2D      uv {};
};

struct StarterAtlasBundle2D;

class TextureAtlas2D
{
public:
    TextureAtlas2D() = default;
    explicit TextureAtlas2D(
        std::uint32_t       width,
        std::uint32_t       height,
        TextureFilterMode2D filter = TextureFilterMode2D::kNearest);

    [[nodiscard]] bool is_valid() const noexcept { return width_ > 0U && height_ > 0U && !pixels_.empty(); }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] TextureFilterMode2D filter_mode() const noexcept { return filter_mode_; }
    void set_filter_mode(TextureFilterMode2D mode) noexcept { filter_mode_ = mode; }

    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept { return pixels_; }
    [[nodiscard]] std::span<const std::uint8_t> raw_rgba() const noexcept { return pixels_; }
    [[nodiscard]] std::span<std::uint8_t> mutable_pixels() noexcept { return pixels_; }

    [[nodiscard]] ColorRgba8 get_pixel(std::uint32_t x, std::uint32_t y) const noexcept;
    void set_pixel(std::uint32_t x, std::uint32_t y, ColorRgba8 c) noexcept;

    [[nodiscard]] UvRect2D compute_uv(
        std::uint32_t px,
        std::uint32_t py,
        std::uint32_t pw,
        std::uint32_t ph) const noexcept;

    UvRect2D add_region(
        std::string_view name,
        std::uint32_t    x,
        std::uint32_t    y,
        std::uint32_t    w,
        std::uint32_t    h);

    [[nodiscard]] const AtlasRegion2D* find_region(std::string_view name) const noexcept;
    [[nodiscard]] std::span<const AtlasRegion2D> regions() const noexcept { return regions_; }

    void slice_grid(std::string_view prefix, std::uint32_t tile_w, std::uint32_t tile_h);

    // Procedural Generator (Zero Disk Asset Dependency)
    static TextureAtlas2D generate_procedural_starter_atlas(std::uint32_t tile_size = 16U);
    static TextureAtlas2D generate_starter_normal_atlas(std::uint32_t tile_size = 16U);
    static TextureAtlas2D generate_starter_emissive_atlas(std::uint32_t tile_size = 16U);

    static StarterAtlasBundle2D generate_starter_bundle(std::uint32_t tile_size = 16U);

    // External Disk & Memory Loaders
    static cd::core::Result<TextureAtlas2D> load_from_file(
        std::string_view    path,
        TextureFilterMode2D filter = TextureFilterMode2D::kNearest);

    static cd::core::Result<TextureAtlas2D> load_from_memory(
        std::span<const std::uint8_t> png_bytes,
        TextureFilterMode2D           filter = TextureFilterMode2D::kNearest);

    cd::core::Result<void> load_regions_from_aseprite_json(std::string_view json_text);
    cd::core::Result<void> load_regions_from_ldtk_json(std::string_view json_text);

private:
    std::uint32_t              width_ { 0U };
    std::uint32_t              height_ { 0U };
    TextureFilterMode2D        filter_mode_ { TextureFilterMode2D::kNearest };
    std::vector<std::uint8_t>  pixels_ {};
    std::vector<AtlasRegion2D> regions_ {};
};

struct StarterAtlasBundle2D
{
    TextureAtlas2D albedo;
    TextureAtlas2D normal;
    TextureAtlas2D emissive;
};

// Convenience free functions matching Explorer 3 test suite
[[nodiscard]] inline TextureAtlas2D generate_procedural_pixel_art_atlas(std::uint32_t tile_size = 16U)
{
    return TextureAtlas2D::generate_procedural_starter_atlas(tile_size);
}

[[nodiscard]] cd::core::Result<TextureAtlas2D> load_texture_atlas_png(
    std::string_view    path,
    TextureFilterMode2D filter = TextureFilterMode2D::kNearest);

[[nodiscard]] cd::core::Result<TextureAtlas2D> load_texture_atlas_png_memory(
    std::span<const std::uint8_t> bytes,
    std::uint32_t                 width = 0U,
    std::uint32_t                 height = 0U,
    TextureFilterMode2D           filter = TextureFilterMode2D::kNearest);

}  // namespace cd::render2d
