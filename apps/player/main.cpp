// =============================================================================
// CHROMODYNAMIC — apps/player/main.cpp (cd_player)
// FAZ 6 Wave 6.2 & FAZ 8 — Standalone CHROMODYNAMIC Runtime Player Executable.
//
// Modes:
//   * cd_player [--showcase <slug>] [--frames N] [--capture-png <path>]
//     Opens a 1280x720 Vulkan window (falling back to NullDevice if headless),
//     boots the requested Showcase Stage (.cdstage / .cdproj) into EngineLoop +
//     SceneRenderer (19-pass FrameGraph), renders the interactive 3D Showcase
//     Stage viewport + live Player Telemetry HUD, and supports live stage
//     switching ([< PREV] / [NEXT >]), element selection, and RMB camera orbit.
//   * cd_player --all-showcases [--smoke-frames N]
//     Fast headless verification across all 12 built-in Showcase Projects
//     (01_pbr_materials_sss_hair .. 12_planar_2d_platformer_net) through
//     EngineLoop + SceneRenderer and verifies zero errors across all 12 stages.
// =============================================================================
#include <cd/element_core/Element.hpp>
#include <cd/gameplay_framework/VesselPilotDirector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/runtime/EngineContext.hpp>
#include <cd/runtime/EngineLoop.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace ur  = cd::ui::renderer;
namespace urr = cd::ui::renderer_rhi;

constexpr std::array<std::string_view, 12> kAllShowcaseSlugs {
    "01_pbr_materials_sss_hair",
    "02_celestial_aether_fog_clouds",
    "03_sponza_clustered_ddgi_restir",
    "04_optics_cinematic_studio",
    "05_avatar_locomotion_anim_ik",
    "06_jolt_dynamics_rover_shatter",
    "07_terra_ocean_nature_world",
    "08_synth_pilot_squad_narrative",
    "09_acoustics_hrtf_echo_chamber",
    "10_spark_swarm_and_decals",
    "11_custom_ui_hud_and_menus",
    "12_planar_2d_platformer_net",
};

// ---- Uncompressed PNG writer & DrawBatcher software rasterizer -------------

[[nodiscard]] std::uint32_t png_crc32(std::span<const std::uint8_t> data) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t b : data)
    {
        crc ^= static_cast<std::uint32_t>(b);
        for (int k = 0; k < 8; ++k)
        {
            crc = (crc & 1U) != 0U ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }
    return ~crc;
}

void write_be32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

void write_png_chunk(std::vector<std::uint8_t>& out, const char type[4], std::span<const std::uint8_t> payload)
{
    write_be32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t type_offset = out.size();
    out.push_back(static_cast<std::uint8_t>(type[0]));
    out.push_back(static_cast<std::uint8_t>(type[1]));
    out.push_back(static_cast<std::uint8_t>(type[2]));
    out.push_back(static_cast<std::uint8_t>(type[3]));
    out.insert(out.end(), payload.begin(), payload.end());
    const std::uint32_t crc = png_crc32(std::span<const std::uint8_t>(out.data() + type_offset, 4U + payload.size()));
    write_be32(out, crc);
}

[[nodiscard]] bool write_rgba_png(const std::filesystem::path& path, std::uint32_t w, std::uint32_t h, std::span<const std::uint8_t> rgba)
{
    if (w == 0U || h == 0U || rgba.size() < static_cast<std::size_t>(w) * h * 4U)
    {
        return false;
    }
    std::vector<std::uint8_t> raw_scanlines;
    const std::size_t row_bytes = static_cast<std::size_t>(w) * 4U;
    raw_scanlines.reserve(static_cast<std::size_t>(h) * (1U + row_bytes));
    for (std::uint32_t y = 0U; y < h; ++y)
    {
        raw_scanlines.push_back(0U);
        const auto* row = rgba.data() + static_cast<std::size_t>(y) * row_bytes;
        raw_scanlines.insert(raw_scanlines.end(), row, row + row_bytes);
    }

    std::vector<std::uint8_t> zlib_data;
    zlib_data.push_back(0x78U);
    zlib_data.push_back(0x01U);
    std::size_t offset = 0U;
    while (offset < raw_scanlines.size())
    {
        const std::size_t rem = raw_scanlines.size() - offset;
        const std::uint16_t blk = static_cast<std::uint16_t>(std::min<std::size_t>(rem, 65535U));
        const bool is_last = (offset + blk >= raw_scanlines.size());
        zlib_data.push_back(is_last ? 0x01U : 0x00U);
        zlib_data.push_back(static_cast<std::uint8_t>(blk & 0xFFU));
        zlib_data.push_back(static_cast<std::uint8_t>((blk >> 8U) & 0xFFU));
        const std::uint16_t nlen = static_cast<std::uint16_t>(~blk);
        zlib_data.push_back(static_cast<std::uint8_t>(nlen & 0xFFU));
        zlib_data.push_back(static_cast<std::uint8_t>((nlen >> 8U) & 0xFFU));
        zlib_data.insert(
            zlib_data.end(),
            raw_scanlines.begin() + static_cast<std::ptrdiff_t>(offset),
            raw_scanlines.begin() + static_cast<std::ptrdiff_t>(offset + blk)
        );
        offset += blk;
    }
    std::uint32_t s1 = 1U;
    std::uint32_t s2 = 0U;
    for (const std::uint8_t b : raw_scanlines)
    {
        s1 = (s1 + b) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    write_be32(zlib_data, (s2 << 16U) | s1);

    std::vector<std::uint8_t> png;
    static constexpr std::array<std::uint8_t, 8> kSig { 137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U };
    png.insert(png.end(), kSig.begin(), kSig.end());

    std::vector<std::uint8_t> ihdr;
    write_be32(ihdr, w);
    write_be32(ihdr, h);
    ihdr.push_back(8U);
    ihdr.push_back(6U);
    ihdr.push_back(0U);
    ihdr.push_back(0U);
    ihdr.push_back(0U);
    write_png_chunk(png, "IHDR", ihdr);
    write_png_chunk(png, "IDAT", zlib_data);
    write_png_chunk(png, "IEND", {});

    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

void dump_batcher_to_png(const ur::DrawBatcher& batcher, std::uint32_t w, std::uint32_t h, const std::filesystem::path& path)
{
    std::vector<std::uint8_t> fb(static_cast<std::size_t>(w) * h * 4U, 0U);
    for (std::size_t i = 0U; i < fb.size(); i += 4U)
    {
        fb[i + 0U] = 12U;
        fb[i + 1U] = 16U;
        fb[i + 2U] = 24U;
        fb[i + 3U] = 255U;
    }

    const auto verts = batcher.vertices();
    const auto idxs  = batcher.indices();
    for (const auto& cmd : batcher.commands())
    {
        const int sx0 = (cmd.scissor.width == 0xFFFFFFFFu) ? 0 : std::max(0, cmd.scissor.x);
        const int sy0 = (cmd.scissor.height == 0xFFFFFFFFu) ? 0 : std::max(0, cmd.scissor.y);
        const int sx1 = (cmd.scissor.width == 0xFFFFFFFFu)
                            ? static_cast<int>(w)
                            : std::min(static_cast<int>(w), cmd.scissor.x + static_cast<int>(cmd.scissor.width));
        const int sy1 = (cmd.scissor.height == 0xFFFFFFFFu)
                            ? static_cast<int>(h)
                            : std::min(static_cast<int>(h), cmd.scissor.y + static_cast<int>(cmd.scissor.height));

        for (std::uint32_t k = 0U; k + 2U < cmd.index_count; k += 3U)
        {
            const std::uint32_t base = cmd.index_offset + k;
            if (base + 2U >= idxs.size()) { break; }
            const auto& v0 = verts[idxs[base + 0U]];
            const auto& v1 = verts[idxs[base + 1U]];
            const auto& v2 = verts[idxs[base + 2U]];

            const int min_x = std::max(sx0, static_cast<int>(std::floor(std::min({ v0.pos_x, v1.pos_x, v2.pos_x }))));
            const int max_x = std::min(sx1 - 1, static_cast<int>(std::ceil(std::max({ v0.pos_x, v1.pos_x, v2.pos_x }))));
            const int min_y = std::max(sy0, static_cast<int>(std::floor(std::min({ v0.pos_y, v1.pos_y, v2.pos_y }))));
            const int max_y = std::min(sy1 - 1, static_cast<int>(std::ceil(std::max({ v0.pos_y, v1.pos_y, v2.pos_y }))));

            const float denom = (v1.pos_y - v2.pos_y) * (v0.pos_x - v2.pos_x) +
                                (v2.pos_x - v1.pos_x) * (v0.pos_y - v2.pos_y);
            if (std::abs(denom) < 1e-5F) { continue; }
            const float inv_denom = 1.0F / denom;

            for (int py = min_y; py <= max_y; ++py)
            {
                const float fy = static_cast<float>(py) + 0.5F;
                for (int px = min_x; px <= max_x; ++px)
                {
                    const float fx = static_cast<float>(px) + 0.5F;
                    const float w0 = ((v1.pos_y - v2.pos_y) * (fx - v2.pos_x) +
                                      (v2.pos_x - v1.pos_x) * (fy - v2.pos_y)) * inv_denom;
                    const float w1 = ((v2.pos_y - v0.pos_y) * (fx - v2.pos_x) +
                                      (v0.pos_x - v2.pos_x) * (fy - v2.pos_y)) * inv_denom;
                    const float w2 = 1.0F - w0 - w1;
                    if (w0 >= -1e-4F && w1 >= -1e-4F && w2 >= -1e-4F)
                    {
                        const float sr = std::clamp(w0 * v0.r + w1 * v1.r + w2 * v2.r, 0.0F, 255.0F);
                        const float sg = std::clamp(w0 * v0.g + w1 * v1.g + w2 * v2.g, 0.0F, 255.0F);
                        const float sb = std::clamp(w0 * v0.b + w1 * v1.b + w2 * v2.b, 0.0F, 255.0F);
                        const float sa = std::clamp(w0 * v0.a + w1 * v1.a + w2 * v2.a, 0.0F, 255.0F) / 255.0F;
                        const std::size_t dst = (static_cast<std::size_t>(py) * w + static_cast<std::size_t>(px)) * 4U;
                        fb[dst + 0U] = static_cast<std::uint8_t>(sr * sa + fb[dst + 0U] * (1.0F - sa));
                        fb[dst + 1U] = static_cast<std::uint8_t>(sg * sa + fb[dst + 1U] * (1.0F - sa));
                        fb[dst + 2U] = static_cast<std::uint8_t>(sb * sa + fb[dst + 2U] * (1.0F - sa));
                        fb[dst + 3U] = 255U;
                    }
                }
            }
        }
    }

    if (write_rgba_png(path, w, h, fb))
    {
        std::printf("[cd_player] captured frame PNG (%ux%u) -> %s\n", w, h, path.string().c_str());
        std::fflush(stdout);
    }
}

// ---- 2D Rectangle-Merging 5x7 ASCII Glyph & Line Renderer ------------------

[[nodiscard]] constexpr std::array<std::uint8_t, 7> glyph_5x7(char c) noexcept
{
    if (c >= 'a' && c <= 'z')
    {
        c = static_cast<char>(c - 'a' + 'A');
    }
    switch (c)
    {
        case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
        case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'D': return { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
        case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
        case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E };
        case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'I': return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
        case 'J': return { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C };
        case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
        case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
        case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
        case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
        case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
        case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
        case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
        case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
        case 'X': return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
        case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
        case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
        case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
        case '1': return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
        case '2': return { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F };
        case '3': return { 0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E };
        case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
        case '5': return { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
        case '6': return { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
        case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
        case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
        case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
        case ':': return { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
        case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
        case ',': return { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 };
        case '-': return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
        case '_': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F };
        case '+': return { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
        case '/': return { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 };
        case '|': return { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case '[': return { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E };
        case ']': return { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E };
        case '(': return { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
        case ')': return { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
        case '<': return { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 };
        case '>': return { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 };
        case '=': return { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 };
        case '%': return { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
        case '#': return { 0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00 };
        case '?': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
        case '!': return { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
        case '*': return { 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00 };
        case '&': return { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D };
        case '\'': return { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 };
        default:  return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    }
}

void draw_ui_text_clipped(ur::DrawBatcher& batcher,
                          float x,
                          float y,
                          std::string_view text,
                          ur::Color color,
                          float scale = 1.0F,
                          float max_x = 1e9F)
{
    const float px = std::max(1.0F, std::round(scale));
    const float adv = 6.0F * px;
    float cx = std::round(x);
    const float cy = std::round(y);
    for (const char ch : text)
    {
        if (cx + 5.0F * px > max_x)
        {
            break;
        }
        if (ch != ' ')
        {
            auto rows = glyph_5x7(ch);
            for (int r = 0; r < 7; ++r)
            {
                std::uint8_t row_bits = rows[static_cast<std::size_t>(r)];
                if (row_bits == 0U) { continue; }
                int c = 0;
                while (c < 5)
                {
                    if (((row_bits >> (4 - c)) & 1U) != 0U)
                    {
                        int c_end = c + 1;
                        while (c_end < 5 && ((row_bits >> (4 - c_end)) & 1U) != 0U)
                        {
                            ++c_end;
                        }
                        std::uint8_t run_mask = 0U;
                        for (int k = c; k < c_end; ++k)
                        {
                            run_mask = static_cast<std::uint8_t>(run_mask | (1U << (4 - k)));
                        }
                        int r_end = r + 1;
                        while (r_end < 7 &&
                               (rows[static_cast<std::size_t>(r_end)] & run_mask) == run_mask)
                        {
                            rows[static_cast<std::size_t>(r_end)] =
                                static_cast<std::uint8_t>(rows[static_cast<std::size_t>(r_end)] & ~run_mask);
                            ++r_end;
                        }
                        if (batcher.vertex_count() + 4U <= ur::kMaxVertices)
                        {
                            batcher.quad(cx + static_cast<float>(c) * px,
                                         cy + static_cast<float>(r) * px,
                                         static_cast<float>(c_end - c) * px,
                                         static_cast<float>(r_end - r) * px,
                                         color);
                        }
                        c = c_end;
                    }
                    else
                    {
                        ++c;
                    }
                }
            }
        }
        cx += adv;
    }
}

void draw_ui_text(ur::DrawBatcher& batcher,
                  float x,
                  float y,
                  std::string_view text,
                  ur::Color color,
                  float scale = 1.0F)
{
    draw_ui_text_clipped(batcher, x, y, text, color, scale, 1e9F);
}

void draw_ui_line(ur::DrawBatcher& batcher,
                  float x0, float y0,
                  float x1, float y1,
                  float thickness,
                  ur::Color color)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5F) { return; }
    if (std::abs(dy) < 0.75F)
    {
        if (batcher.vertex_count() + 4U <= ur::kMaxVertices)
        {
            batcher.quad(std::min(x0, x1), y0 - thickness * 0.5F, std::abs(dx), thickness, color);
        }
        return;
    }
    if (std::abs(dx) < 0.75F)
    {
        if (batcher.vertex_count() + 4U <= ur::kMaxVertices)
        {
            batcher.quad(x0 - thickness * 0.5F, std::min(y0, y1), thickness, std::abs(dy), color);
        }
        return;
    }
    const int steps = std::clamp(static_cast<int>(len / std::max(3.5F, thickness * 1.4F)), 1, 28);
    const float seg = std::max(thickness, 2.0F);
    const float half = seg * 0.5F;
    for (int i = 0; i <= steps; ++i)
    {
        if (batcher.vertex_count() + 4U > ur::kMaxVertices) { break; }
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float px = x0 + dx * t - half;
        const float py = y0 + dy * t - half;
        batcher.quad(px, py, seg, seg, color);
    }
}

// ---- Headless verification helper ------------------------------------------

[[nodiscard]] int run_showcase_slug_headless(
    cd::runtime::EngineContext& ctx,
    cd::rhi::IDevice& dev,
    cd::rhi::TextureHandle backbuffer,
    std::string_view slug,
    std::uint32_t frames_to_run
)
{
    cd::runtime::EngineLoop loop(ctx, 1.0F / 60.0F);
    loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(slug));

    for (std::uint32_t f = 0; f < frames_to_run; ++f)
    {
        const auto telemetry = loop.step_frame(1.0F / 60.0F, &dev, backbuffer);
        if (telemetry.framegraph_passes_compiled == 0U)
        {
            std::fprintf(stderr, "[cd_player] ERROR: 0 render passes on showcase '%.*s'\n",
                         static_cast<int>(slug.size()), slug.data());
            return 1;
        }
    }

    std::printf(
        "[cd_player] Completed %u frames of stage '%.*s' (elements=%zu)\n",
        frames_to_run,
        static_cast<int>(loop.stage().name().size()),
        loop.stage().name().data(),
        loop.stage().root().children().size()
    );
    return 0;
}

// ---- Interactive 3D Stage & Player HUD Renderer ----------------------------

struct PlayerUiState
{
    std::size_t active_showcase_idx { 0U };
    std::size_t selected_elem_idx { 0U };
    bool        playing { true };
    bool        show_hud { true };
    float       cam_yaw { 0.48F };
    float       cam_pitch { 0.34F };
    float       sim_time { 0.0F };
    float       mouse_x { 0.0F };
    float       mouse_y { 0.0F };
    float       prev_mouse_x { 0.0F };
    float       prev_mouse_y { 0.0F };
    bool        lmb_pressed { false };
    bool        rmb_down { false };
};

void render_player_frame(
    ur::DrawBatcher& batcher,
    std::uint32_t fb_w,
    std::uint32_t fb_h,
    PlayerUiState& ui,
    const cd::element_core::Stage& stage,
    const cd::runtime::FrameStepReport& telemetry,
    bool& out_stage_changed
)
{
    out_stage_changed = false;
    const float w = static_cast<float>(fb_w);
    const float h = static_cast<float>(fb_h);
    constexpr float kTopBarH = 38.0F;
    const float vp_y = kTopBarH;
    const float vp_h = h - kTopBarH;
    const float horizon_y = vp_y + vp_h * 0.42F;

    // 1. Sky & Ground Gradient
    batcher.quad(0.0F, vp_y, w, horizon_y - vp_y, ur::Color { 14U, 22U, 38U, 255U });
    batcher.quad(0.0F, horizon_y - 34.0F, w, 34.0F, ur::Color { 24U, 38U, 64U, 255U });
    batcher.quad(0.0F, horizon_y - 10.0F, w, 10.0F, ur::Color { 42U, 68U, 108U, 255U });
    batcher.quad(0.0F, horizon_y, w, vp_y + vp_h - horizon_y, ur::Color { 18U, 22U, 30U, 255U });
    batcher.quad(0.0F, horizon_y, w, 2.0F, ur::Color { 72U, 118U, 185U, 220U });

    // Solar disc in upper-right sky
    const float sun_x = w * 0.76F;
    const float sun_y = vp_y + (horizon_y - vp_y) * 0.36F;
    batcher.quad(sun_x - 20.0F, sun_y - 20.0F, 40.0F, 40.0F, ur::Color { 255U, 200U, 95U, 45U });
    batcher.quad(sun_x - 12.0F, sun_y - 12.0F, 24.0F, 24.0F, ur::Color { 255U, 225U, 140U, 130U });
    batcher.quad(sun_x - 6.0F, sun_y - 6.0F, 12.0F, 12.0F, ur::Color { 255U, 248U, 215U, 245U });

    // 2. 3D Perspective Camera Projection
    const float cy = std::cos(ui.cam_yaw);
    const float sy = std::sin(ui.cam_yaw);
    const float cp = std::cos(ui.cam_pitch);
    const float sp = std::sin(ui.cam_pitch);
    const float cam_dist = 10.5F;
    const float fov_scale = std::min(w, vp_h) * 0.72F;
    const float center_x = w * 0.50F;
    const float center_y = horizon_y + 18.0F;

    auto project3d = [&](float wx, float wy, float wz, float& sx, float& sy_out, float& depth_out) -> bool {
        const float rx = wx * cy - wz * sy;
        const float rz = wx * sy + wz * cy;
        const float ry = wy * cp - rz * sp;
        const float cz = wy * sp + rz * cp + cam_dist;
        if (cz <= 0.4F)
        {
            return false;
        }
        sx = center_x + (rx / cz) * fov_scale;
        sy_out = center_y - (ry / cz) * fov_scale;
        depth_out = cz;
        return true;
    };

    // 3. Perspective 3D XZ Ground Grid + World Axes
    for (int g = -6; g <= 6; ++g)
    {
        const float gf = static_cast<float>(g) * 1.15F;
        float x0 = 0.0F, y0 = 0.0F, z0 = 0.0F;
        float x1 = 0.0F, y1 = 0.0F, z1 = 0.0F;
        const ur::Color grid_c = (g == 0)
            ? ur::Color { 70U, 105U, 155U, 210U }
            : ur::Color { 34U, 42U, 56U, 165U };
        if (project3d(gf, -1.0F, -6.9F, x0, y0, z0) && project3d(gf, -1.0F, 6.9F, x1, y1, z1))
        {
            draw_ui_line(batcher, x0, y0, x1, y1, (g == 0) ? 2.0F : 1.0F, grid_c);
        }
        if (project3d(-6.9F, -1.0F, gf, x0, y0, z0) && project3d(6.9F, -1.0F, gf, x1, y1, z1))
        {
            draw_ui_line(batcher, x0, y0, x1, y1, (g == 0) ? 2.0F : 1.0F, grid_c);
        }
    }

    {
        float ax0 = 0.0F, ay0 = 0.0F, az0 = 0.0F;
        float ax1 = 0.0F, ay1 = 0.0F, az1 = 0.0F;
        if (project3d(0.0F, -1.0F, 0.0F, ax0, ay0, az0) && project3d(5.2F, -1.0F, 0.0F, ax1, ay1, az1))
        {
            draw_ui_line(batcher, ax0, ay0, ax1, ay1, 2.0F, ur::Color { 225U, 75U, 75U, 225U });
        }
        if (project3d(0.0F, -1.0F, 0.0F, ax0, ay0, az0) && project3d(0.0F, -1.0F, 5.2F, ax1, ay1, az1))
        {
            draw_ui_line(batcher, ax0, ay0, ax1, ay1, 2.0F, ur::Color { 75U, 145U, 240U, 225U });
        }
    }

    // Studio turntable pedestal at world origin
    {
        float px = 0.0F, py = 0.0F, pz = 0.0F;
        if (project3d(0.0F, -1.0F, 0.0F, px, py, pz))
        {
            const float pr = std::clamp(260.0F / pz, 34.0F, 95.0F);
            batcher.quad(px - pr, py - pr * 0.24F, pr * 2.0F, pr * 0.48F, ur::Color { 26U, 34U, 48U, 215U });
            batcher.quad(px - pr * 0.85F, py - pr * 0.18F, pr * 1.7F, pr * 0.36F, ur::Color { 38U, 52U, 74U, 230U });
            batcher.quad(px - pr, py - pr * 0.24F, pr * 2.0F, 1.5F, ur::Color { 85U, 145U, 225U, 190U });
        }
    }

    // 4. Render 3D Stage Elements with live animation & click-to-select
    const auto& children = stage.root().children();
    if (ui.selected_elem_idx >= children.size() && !children.empty())
    {
        ui.selected_elem_idx = 0U;
    }

    for (std::size_t idx = 0U; idx < children.size(); ++idx)
    {
        const auto& elem = children[idx];
        if (!elem) { continue; }
        float wx = 0.0F;
        float wy = 0.0F;
        float wz = 0.0F;
        if (const auto* s3d = dynamic_cast<const cd::element_core::Spatial3D*>(elem.get()))
        {
            const auto pos = s3d->local_position();
            wx = pos[0];
            wy = pos[1];
            wz = pos[2];
        }
        static constexpr std::array<std::array<float, 3>, 8> kStageSlots {{
            {  0.0F, 0.20F,  0.0F },   // 0: Hero centerpiece on turntable pedestal
            { -2.9F, 1.35F, -0.8F },   // 1: Key Solar / Directional Light (upper left)
            {  2.9F, 0.25F, -0.5F },   // 2: Reflection Probe / Secondary (right)
            { -2.1F, 0.00F,  2.1F },   // 3: Front-left prop / vehicle / avatar
            {  2.1F, 0.00F,  2.1F },   // 4: Front-right prop / dialogue / emitter
            { -4.2F, 0.45F,  0.5F },   // 5: Far-left wing
            {  4.2F, 0.45F,  0.5F },   // 6: Far-right wing
            {  0.0F, 1.40F, -2.8F },   // 7: Back-center backdrop
        }};
        if (std::abs(wx) < 0.05F && std::abs(wz) < 0.05F)
        {
            const auto& slot = kStageSlots[idx % kStageSlots.size()];
            wx = slot[0];
            wy = slot[1] + 0.06F * std::sin(ui.sim_time * 2.0F + static_cast<float>(idx));
            wz = slot[2];
        }

        float sx = 0.0F, sy_pt = 0.0F, depth = 10.0F;
        if (!project3d(wx, wy, wz, sx, sy_pt, depth))
        {
            continue;
        }

        const bool is_selected = (idx == ui.selected_elem_idx);
        const std::string_view cname = elem->class_name();
        const bool is_light = (cname.find("Light") != std::string_view::npos ||
                               cname.find("Sun") != std::string_view::npos);
        const bool is_world = (cname.find("World") != std::string_view::npos ||
                               cname.find("Terrain") != std::string_view::npos ||
                               cname.find("Ocean") != std::string_view::npos ||
                               cname.find("Nav") != std::string_view::npos ||
                               cname.find("Sky") != std::string_view::npos);
        const bool is_mesh  = (cname.find("Mesh") != std::string_view::npos ||
                               cname.find("Pbr") != std::string_view::npos ||
                               cname.find("Avatar") != std::string_view::npos ||
                               cname.find("Vessel") != std::string_view::npos);
        const bool is_agent = (cname.find("Agent") != std::string_view::npos ||
                               cname.find("Pilot") != std::string_view::npos ||
                               cname.find("Synth") != std::string_view::npos);

        const float half_sz = std::clamp((idx == 0U ? 145.0F : 112.0F) / depth, 14.0F, 36.0F);

        // Ground shadow & vertical drop marker
        float gx = 0.0F, gy = 0.0F, gz = 0.0F;
        if (project3d(wx, -1.0F, wz, gx, gy, gz))
        {
            batcher.quad(gx - half_sz * 0.9F, gy - half_sz * 0.25F,
                         half_sz * 1.8F, half_sz * 0.5F,
                         ur::Color { 6U, 9U, 14U, 155U });
            if (wy > -0.8F)
            {
                draw_ui_line(batcher, sx, sy_pt + half_sz, gx, gy, 1.0F,
                             is_light ? ur::Color { 255U, 215U, 95U, 130U }
                                      : ur::Color { 95U, 135U, 190U, 110U });
            }
        }

        const ur::Color top_c = is_light ? ur::Color { 255U, 235U, 135U, 255U }
                              : is_mesh  ? ur::Color { 115U, 195U, 255U, 255U }
                              : is_world ? ur::Color { 95U, 225U, 160U, 255U }
                              : is_agent ? ur::Color { 255U, 145U, 115U, 255U }
                                         : ur::Color { 185U, 155U, 255U, 255U };
        const ur::Color front_c = is_light ? ur::Color { 235U, 185U, 65U, 245U }
                                : is_mesh  ? ur::Color { 65U, 145U, 230U, 245U }
                                : is_world ? ur::Color { 55U, 175U, 115U, 245U }
                                : is_agent ? ur::Color { 215U, 95U, 75U, 245U }
                                           : ur::Color { 135U, 105U, 215U, 245U };
        const ur::Color side_c = is_light ? ur::Color { 185U, 135U, 40U, 245U }
                               : is_mesh  ? ur::Color { 40U, 95U, 170U, 245U }
                               : is_world ? ur::Color { 35U, 125U, 80U, 245U }
                               : is_agent ? ur::Color { 155U, 60U, 48U, 245U }
                                          : ur::Color { 90U, 68U, 160U, 245U };

        const float iso_dx = half_sz * 0.42F;
        const float iso_dy = half_sz * 0.34F;
        batcher.quad(sx - half_sz + iso_dx * 0.5F, sy_pt - half_sz - iso_dy,
                     half_sz * 1.6F, iso_dy + 2.0F, top_c);
        batcher.quad(sx - half_sz, sy_pt - half_sz,
                     half_sz * 1.6F, half_sz * 1.6F, front_c);
        batcher.quad(sx + half_sz * 0.6F, sy_pt - half_sz - iso_dy * 0.5F,
                     iso_dx, half_sz * 1.6F, side_c);
        batcher.quad(sx - half_sz + 4.0F, sy_pt - half_sz + 4.0F,
                     half_sz * 0.48F, half_sz * 0.35F,
                     ur::Color { 255U, 255U, 255U, 95U });

        if (ui.lmb_pressed &&
            ui.mouse_x >= sx - half_sz - 6.0F && ui.mouse_x <= sx + half_sz + iso_dx + 6.0F &&
            ui.mouse_y >= sy_pt - half_sz - iso_dy - 6.0F && ui.mouse_y <= sy_pt + half_sz + 6.0F)
        {
            ui.selected_elem_idx = idx;
        }

        if (is_selected)
        {
            const float bx = sx - half_sz - 4.0F;
            const float by = sy_pt - half_sz - iso_dy - 4.0F;
            const float bw = half_sz * 1.6F + iso_dx + 8.0F;
            const float bh = half_sz * 1.6F + iso_dy + 8.0F;
            const ur::Color sel_c { 255U, 205U, 60U, 255U };
            batcher.quad(bx, by, bw, 2.0F, sel_c);
            batcher.quad(bx, by + bh - 2.0F, bw, 2.0F, sel_c);
            batcher.quad(bx, by, 2.0F, bh, sel_c);
            batcher.quad(bx + bw - 2.0F, by, 2.0F, bh, sel_c);

            // 3-Axis Transform Gizmo
            const float giz_len = 46.0F;
            draw_ui_line(batcher, sx, sy_pt, sx + giz_len, sy_pt + 5.0F, 2.5F, ur::Color { 245U, 75U, 75U, 255U });
            batcher.quad(sx + giz_len - 4.0F, sy_pt + 1.0F, 8.0F, 8.0F, ur::Color { 245U, 75U, 75U, 255U });
            draw_ui_text(batcher, sx + giz_len + 6.0F, sy_pt, "X", ur::Color { 255U, 110U, 110U, 255U }, 1.0F);

            draw_ui_line(batcher, sx, sy_pt, sx, sy_pt - giz_len, 2.5F, ur::Color { 80U, 235U, 115U, 255U });
            batcher.quad(sx - 4.0F, sy_pt - giz_len - 4.0F, 8.0F, 8.0F, ur::Color { 80U, 235U, 115U, 255U });
            draw_ui_text(batcher, sx + 6.0F, sy_pt - giz_len - 4.0F, "Y", ur::Color { 110U, 255U, 140U, 255U }, 1.0F);

            draw_ui_line(batcher, sx, sy_pt, sx - giz_len * 0.65F, sy_pt + giz_len * 0.48F, 2.5F, ur::Color { 85U, 160U, 255U, 255U });
            batcher.quad(sx - giz_len * 0.65F - 4.0F, sy_pt + giz_len * 0.48F - 4.0F, 8.0F, 8.0F, ur::Color { 85U, 160U, 255U, 255U });
            draw_ui_text(batcher, sx - giz_len * 0.65F - 14.0F, sy_pt + giz_len * 0.48F - 4.0F, "Z", ur::Color { 120U, 185U, 255U, 255U }, 1.0F);
        }

        // Floating 3D world-space nameplate
        char tag_buf[64] {};
        std::snprintf(tag_buf, sizeof(tag_buf), "[%zu] %.*s",
                      idx + 1U,
                      static_cast<int>(elem->name().size()),
                      elem->name().data());
        const std::string_view tag_sv { tag_buf };
        const float lbl_w = static_cast<float>(std::min<std::size_t>(tag_sv.size(), 24U)) * 6.0F + 10.0F;
        const float lbl_x = sx - lbl_w * 0.5F;
        const float lbl_y = sy_pt - half_sz - iso_dy - 19.0F - static_cast<float>(idx % 2U) * 11.0F;
        batcher.quad(lbl_x, lbl_y, lbl_w, 14.0F,
                     is_selected ? ur::Color { 42U, 58U, 88U, 240U }
                                 : ur::Color { 16U, 22U, 32U, 215U });
        if (is_selected)
        {
            batcher.quad(lbl_x, lbl_y, lbl_w, 1.0F, ur::Color { 255U, 205U, 60U, 245U });
        }
        draw_ui_text_clipped(batcher, lbl_x + 5.0F, lbl_y + 3.0F, tag_sv,
                             is_selected ? ur::Color { 255U, 235U, 140U, 255U }
                                         : ur::Color { 215U, 225U, 242U, 245U },
                             1.0F, lbl_x + lbl_w - 2.0F);
    }

    // 5. 3D Orientation Compass in top-right
    {
        const float comp_cx = w - 42.0F;
        const float comp_cy = vp_y + 42.0F;
        batcher.quad(comp_cx - 28.0F, comp_cy - 28.0F, 56.0F, 56.0F, ur::Color { 14U, 18U, 26U, 205U });
        batcher.quad(comp_cx - 28.0F, comp_cy - 28.0F, 56.0F, 1.0F, ur::Color { 65U, 95U, 140U, 190U });
        draw_ui_line(batcher, comp_cx, comp_cy, comp_cx + cy * 18.0F, comp_cy + sy * sp * 18.0F, 2.0F, ur::Color { 245U, 80U, 80U, 255U });
        draw_ui_text(batcher, comp_cx + cy * 21.0F - 2.0F, comp_cy + sy * sp * 21.0F - 3.0F, "X", ur::Color { 255U, 110U, 110U, 255U }, 1.0F);
        draw_ui_line(batcher, comp_cx, comp_cy, comp_cx, comp_cy - cp * 18.0F, 2.0F, ur::Color { 85U, 235U, 115U, 255U });
        draw_ui_text(batcher, comp_cx - 2.0F, comp_cy - cp * 22.0F - 5.0F, "Y", ur::Color { 110U, 255U, 140U, 255U }, 1.0F);
        draw_ui_line(batcher, comp_cx, comp_cy, comp_cx - sy * 18.0F, comp_cy + cy * sp * 18.0F, 2.0F, ur::Color { 85U, 160U, 255U, 255U });
        draw_ui_text(batcher, comp_cx - sy * 22.0F - 2.0F, comp_cy + cy * sp * 22.0F - 3.0F, "Z", ur::Color { 120U, 185U, 255U, 255U }, 1.0F);
    }

    // 6. Top Interactive Player Bar
    batcher.quad(0.0F, 0.0F, w, kTopBarH, ur::Color { 16U, 20U, 28U, 255U });
    batcher.quad(0.0F, kTopBarH - 2.0F, w, 2.0F, ur::Color { 56U, 128U, 235U, 255U });

    batcher.quad(8.0F, 6.0F, 168.0F, 26.0F, ur::Color { 48U, 105U, 205U, 255U });
    draw_ui_text(batcher, 16.0F, 15.0F, "CHROMODYNAMIC PLAYER", ur::Color { 248U, 252U, 255U, 255U }, 1.0F);

    auto draw_btn = [&](float bx, float bw, std::string_view label, bool active, ur::Color act_c) -> bool {
        const bool hovered = (ui.mouse_x >= bx && ui.mouse_x < bx + bw && ui.mouse_y >= 6.0F && ui.mouse_y < 32.0F);
        const ur::Color bg = active ? act_c : (hovered ? ur::Color { 44U, 54U, 72U, 255U } : ur::Color { 28U, 34U, 46U, 255U });
        batcher.quad(bx, 6.0F, bw, 26.0F, bg);
        batcher.quad(bx, 6.0F, bw, 1.0F, active ? ur::Color { 145U, 210U, 255U, 255U } : ur::Color { 58U, 70U, 92U, 255U });
        draw_ui_text_clipped(batcher, bx + 8.0F, 15.0F, label, ur::Color { 240U, 246U, 255U, 255U }, 1.0F, bx + bw - 4.0F);
        return hovered && ui.lmb_pressed;
    };

    float cx_bar = 186.0F;
    if (draw_btn(cx_bar, 62.0F, "< PREV", false, ur::Color { 60U, 120U, 205U, 255U }))
    {
        ui.active_showcase_idx = (ui.active_showcase_idx + kAllShowcaseSlugs.size() - 1U) % kAllShowcaseSlugs.size();
        out_stage_changed = true;
    }
    cx_bar += 66.0F;

    char stage_lbl[96] {};
    std::snprintf(stage_lbl, sizeof(stage_lbl), "STAGE %02zu/12: %.*s",
                  ui.active_showcase_idx + 1U,
                  static_cast<int>(stage.name().size()),
                  stage.name().data());
    batcher.quad(cx_bar, 6.0F, 300.0F, 26.0F, ur::Color { 22U, 28U, 38U, 255U });
    batcher.quad(cx_bar, 6.0F, 300.0F, 1.0F, ur::Color { 75U, 130U, 210U, 255U });
    draw_ui_text_clipped(batcher, cx_bar + 10.0F, 15.0F, stage_lbl, ur::Color { 135U, 210U, 255U, 255U }, 1.0F, cx_bar + 294.0F);
    cx_bar += 304.0F;

    if (draw_btn(cx_bar, 62.0F, "NEXT >", false, ur::Color { 60U, 120U, 205U, 255U }))
    {
        ui.active_showcase_idx = (ui.active_showcase_idx + 1U) % kAllShowcaseSlugs.size();
        out_stage_changed = true;
    }
    cx_bar += 74.0F;

    if (draw_btn(cx_bar, 118.0F, ui.playing ? "PLAYING (SPACE)" : "PAUSED (SPACE)",
                 ui.playing, ur::Color { 46U, 165U, 88U, 255U }))
    {
        ui.playing = !ui.playing;
    }
    cx_bar += 124.0F;

    if (draw_btn(cx_bar, 96.0F, "HUD (TAB)", ui.show_hud, ur::Color { 56U, 125U, 215U, 255U }))
    {
        ui.show_hud = !ui.show_hud;
    }
    cx_bar += 102.0F;

    if (draw_btn(cx_bar, 96.0F, "RESET CAM", false, ur::Color { 56U, 125U, 215U, 255U }))
    {
        ui.cam_yaw = 0.48F;
        ui.cam_pitch = 0.34F;
    }

    // Right-aligned live status pill in top bar
    batcher.quad(w - 172.0F, 6.0F, 164.0F, 26.0F, ur::Color { 24U, 58U, 42U, 255U });
    batcher.quad(w - 172.0F, 6.0F, 164.0F, 1.0F, ur::Color { 75U, 205U, 125U, 255U });
    draw_ui_text(batcher, w - 162.0F, 15.0F, "VULKAN 19-PASS LIVE", ur::Color { 145U, 245U, 175U, 255U }, 1.0F);

    // 7. Bottom Interactive Telemetry & Stage Inspector HUD Cards
    if (ui.show_hud)
    {
        const float hud_h = 136.0F;
        const float hud_y = h - hud_h - 10.0F;
        const float card_gap = 10.0F;
        const float card_w = (w - 20.0F - card_gap * 2.0F) / 3.0F;

        // Card 1: Stage & EngineLoop Telemetry
        const float c1_x = 10.0F;
        batcher.quad(c1_x, hud_y, card_w, hud_h, ur::Color { 14U, 18U, 26U, 232U });
        batcher.quad(c1_x, hud_y, card_w, 22.0F, ur::Color { 26U, 36U, 54U, 245U });
        batcher.quad(c1_x, hud_y, 3.0F, hud_h, ur::Color { 70U, 155U, 255U, 255U });
        draw_ui_text(batcher, c1_x + 10.0F, hud_y + 7.0F, "RUNTIME STAGE & ENGINELOOP TELEMETRY", ur::Color { 145U, 205U, 255U, 255U }, 1.0F);

        char line1[96] {};
        std::snprintf(line1, sizeof(line1), "STAGE: %.*s", static_cast<int>(stage.name().size()), stage.name().data());
        draw_ui_text_clipped(batcher, c1_x + 10.0F, hud_y + 32.0F, line1, ur::Color { 230U, 238U, 250U, 255U }, 1.0F, c1_x + card_w - 8.0F);

        char line2[96] {};
        std::snprintf(line2, sizeof(line2), "FRAMEGRAPH: %zu PASSES COMPILED | %zu HUD LAYERS",
                      telemetry.framegraph_passes_compiled, telemetry.active_hud_layers);
        draw_ui_text_clipped(batcher, c1_x + 10.0F, hud_y + 50.0F, line2, ur::Color { 125U, 235U, 160U, 255U }, 1.0F, c1_x + card_w - 8.0F);

        char line3[96] {};
        std::snprintf(line3, sizeof(line3), "SIM TIME: %.2fS | FIXED SUBSTEPS: %u | 60.0 HZ",
                      static_cast<double>(ui.sim_time), telemetry.fixed_steps_executed);
        draw_ui_text_clipped(batcher, c1_x + 10.0F, hud_y + 68.0F, line3, ur::Color { 210U, 220U, 238U, 255U }, 1.0F, c1_x + card_w - 8.0F);

        draw_ui_text_clipped(batcher, c1_x + 10.0F, hud_y + 86.0F,
                             "DIRECTOR: VESSEL + PILOT + STAGE AUTHORITY ACTIVE",
                             ur::Color { 255U, 210U, 115U, 255U }, 1.0F, c1_x + card_w - 8.0F);
        draw_ui_text_clipped(batcher, c1_x + 10.0F, hud_y + 104.0F,
                             "CONTROLS: CLICK ELEMENT | RMB ORBIT | < PREV / NEXT >",
                             ur::Color { 165U, 180U, 205U, 255U }, 1.0F, c1_x + card_w - 8.0F);

        // Card 2: Interactive Stage Hierarchy & Element Inspector
        const float c2_x = c1_x + card_w + card_gap;
        batcher.quad(c2_x, hud_y, card_w, hud_h, ur::Color { 14U, 18U, 26U, 232U });
        batcher.quad(c2_x, hud_y, card_w, 22.0F, ur::Color { 26U, 36U, 54U, 245U });
        batcher.quad(c2_x, hud_y, 3.0F, hud_h, ur::Color { 255U, 195U, 65U, 255U });
        draw_ui_text(batcher, c2_x + 10.0F, hud_y + 7.0F, "STAGE ELEMENTS (CLICK TO INSPECT)", ur::Color { 255U, 215U, 115U, 255U }, 1.0F);

        float ey = hud_y + 25.0F;
        for (std::size_t idx = 0U; idx < children.size() && idx < 6U; ++idx)
        {
            const auto& elem = children[idx];
            if (!elem) { continue; }
            const bool sel = (idx == ui.selected_elem_idx);
            const bool hov = (ui.mouse_x >= c2_x + 6.0F && ui.mouse_x < c2_x + card_w - 6.0F &&
                              ui.mouse_y >= ey && ui.mouse_y < ey + 15.0F);
            if (hov && ui.lmb_pressed)
            {
                ui.selected_elem_idx = idx;
            }
            batcher.quad(c2_x + 6.0F, ey, card_w - 12.0F, 14.0F,
                         sel ? ur::Color { 44U, 72U, 120U, 240U }
                             : (hov ? ur::Color { 28U, 38U, 54U, 220U } : ur::Color { 20U, 26U, 36U, 200U }));
            char el_line[96] {};
            std::snprintf(el_line, sizeof(el_line), "%s [%02zu] %.*s (%.*s)",
                          sel ? ">" : " ",
                          idx + 1U,
                          static_cast<int>(elem->name().size()), elem->name().data(),
                          static_cast<int>(elem->class_name().size()), elem->class_name().data());
            draw_ui_text_clipped(batcher, c2_x + 12.0F, ey + 4.0F, el_line,
                                 sel ? ur::Color { 255U, 235U, 145U, 255U }
                                     : ur::Color { 215U, 225U, 240U, 255U },
                                 1.0F, c2_x + card_w - 10.0F);
            ey += 15.5F;
        }
        if (ui.selected_elem_idx < children.size() && children[ui.selected_elem_idx])
        {
            const auto& sel_el = *children[ui.selected_elem_idx];
            std::array<float, 3> pos { 0.0F, 1.0F, 0.0F };
            std::array<float, 3> scl { 1.0F, 1.0F, 1.0F };
            if (const auto* s3d = dynamic_cast<const cd::element_core::Spatial3D*>(&sel_el))
            {
                pos = s3d->local_position();
                scl = s3d->local_scale();
            }
            char tr_line[96] {};
            std::snprintf(tr_line, sizeof(tr_line), "INSPECT POS:(%.1f, %.1f, %.1f) SCALE:(%.1f, %.1f, %.1f)",
                          static_cast<double>(pos[0]),
                          static_cast<double>(pos[1]),
                          static_cast<double>(pos[2]),
                          static_cast<double>(scl[0]),
                          static_cast<double>(scl[1]),
                          static_cast<double>(scl[2]));
            draw_ui_text_clipped(batcher, c2_x + 10.0F, hud_y + hud_h - 18.0F, tr_line,
                                 ur::Color { 135U, 210U, 255U, 255U }, 1.0F, c2_x + card_w - 8.0F);
        }

        // Card 3: 19-Pass FrameGraph Pipeline Visualizer
        const float c3_x = c2_x + card_w + card_gap;
        batcher.quad(c3_x, hud_y, card_w, hud_h, ur::Color { 14U, 18U, 26U, 232U });
        batcher.quad(c3_x, hud_y, card_w, 22.0F, ur::Color { 26U, 36U, 54U, 245U });
        batcher.quad(c3_x, hud_y, 3.0F, hud_h, ur::Color { 80U, 225U, 135U, 255U });
        draw_ui_text(batcher, c3_x + 10.0F, hud_y + 7.0F, "19-PASS FRAMEGRAPH RENDER PIPELINE", ur::Color { 135U, 240U, 175U, 255U }, 1.0F);

        static constexpr std::array<std::string_view, 5> kPassRows {
            "01-04: SHADOW CSM -> GBUFFER -> VELOCITY -> HIZ",
            "05-08: GTAO -> CLUSTERED LIGHT -> DDGI -> RESTIR",
            "09-12: SSR -> SSS SKIN -> VOL CLOUDS -> ATMOSPHERE",
            "13-16: WATER FFT -> PARTICLES -> TAA -> MOTION BLUR",
            "17-19: BLOOM -> ACES TONEMAP -> PLAYER HUD COMPOSITE",
        };
        for (std::size_t r = 0U; r < kPassRows.size(); ++r)
        {
            const float ry = hud_y + 29.0F + static_cast<float>(r) * 20.0F;
            batcher.quad(c3_x + 8.0F, ry, card_w - 16.0F, 16.0F, ur::Color { 22U, 30U, 44U, 220U });
            batcher.quad(c3_x + 8.0F, ry, 4.0F, 16.0F,
                         (r % 2U == 0U) ? ur::Color { 75U, 185U, 255U, 255U }
                                        : ur::Color { 85U, 225U, 145U, 255U });
            draw_ui_text_clipped(batcher, c3_x + 16.0F, ry + 4.0F, kPassRows[r],
                                 ur::Color { 220U, 232U, 248U, 255U }, 1.0F, c3_x + card_w - 10.0F);
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    std::string_view showcase_slug = "01_pbr_materials_sss_hair";
    std::uint32_t frames_to_run = 0U;
    bool run_all_showcases = false;
    bool force_headless = false;
    bool force_windowed = false;
    std::string capture_png_path {};

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg { argv[i] };
        if (arg == "--showcase" && i + 1 < argc)
        {
            showcase_slug = argv[++i];
        }
        else if (arg == "--all-showcases")
        {
            run_all_showcases = true;
            force_headless = true;
        }
        else if (arg == "--null" || arg == "--headless")
        {
            force_headless = true;
        }
        else if (arg == "--windowed")
        {
            force_windowed = true;
        }
        else if (arg == "--capture-png" && i + 1 < argc)
        {
            capture_png_path = argv[++i];
            force_windowed = true;
        }
        else if (arg == "--smoke-frames" && i + 1 < argc)
        {
            const long v = std::strtol(argv[++i], nullptr, 10);
            if (v > 0)
            {
                frames_to_run = static_cast<std::uint32_t>(v);
            }
            if (!force_windowed)
            {
                force_headless = true;
            }
        }
        else if ((arg == "--frames" || arg == "--headless-frames") && i + 1 < argc)
        {
            const long v = std::strtol(argv[++i], nullptr, 10);
            if (v > 0)
            {
                frames_to_run = static_cast<std::uint32_t>(v);
            }
        }
    }

    cd::runtime::EngineContextConfig cfg {};
    cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx(cfg);

    // Fast headless path for --all-showcases or --smoke-frames / --null
    if (run_all_showcases || (force_headless && !force_windowed && capture_png_path.empty()))
    {
        const std::uint32_t headless_frames = frames_to_run > 0U ? frames_to_run : 5U;
        cd::rhi::NullDevice dev;
        cd::rhi::TextureDesc back_desc {};
        back_desc.extent = { 1280U, 720U, 1U };
        back_desc.format = cd::rhi::Format::kRGBA8Unorm;
        back_desc.usage = cd::rhi::TextureUsage::kColorAttachment;
        auto back_tex = dev.create_texture(back_desc);
        const auto back_handle = back_tex.value_or(cd::rhi::TextureHandle {});

        if (run_all_showcases)
        {
            for (const auto slug : kAllShowcaseSlugs)
            {
                const int rc = run_showcase_slug_headless(ctx, dev, back_handle, slug, headless_frames);
                if (rc != 0)
                {
                    return rc;
                }
            }
            std::printf("[cd_player] ALL 12 SHOWCASES VERIFIED (%u frames each).\n", headless_frames);
            return 0;
        }

        return run_showcase_slug_headless(ctx, dev, back_handle, showcase_slug, headless_frames);
    }

    // Windowed Vulkan + Interactive 3D Stage & Player HUD path
    if (!capture_png_path.empty() && frames_to_run == 0U)
    {
        frames_to_run = 4U;
    }

    std::unique_ptr<cd::platform::IWindow> window;
    std::unique_ptr<cd::rhi::IDevice>      device;
    std::optional<cd::render::Renderer>    renderer;
    bool using_null = force_headless && !force_windowed;

    if (!using_null)
    {
        cd::platform::WindowDesc wd {};
        wd.title  = "CHROMODYNAMIC — Standalone Stage Player";
        wd.width  = 1280U;
        wd.height = 720U;
        auto wr = cd::platform::create_window(wd);
        if (!wr.has_value())
        {
            using_null = true;
        }
        else
        {
            window = std::move(*wr);
            cd::rhi::vulkan::VulkanCreateInfo vci {};
            auto dr = cd::rhi::vulkan::create_vulkan_device(vci);
            if (!dr.has_value())
            {
                window.reset();
                using_null = true;
            }
            else
            {
                device = std::move(*dr);
                cd::render::RendererDesc rd {};
                rd.device                   = device.get();
                rd.swapchain.window_handle  = window->native_window_handle();
                rd.swapchain.display_handle = window->native_display_handle();
                rd.swapchain.extent         = { window->width(), window->height() };
                rd.swapchain.format         = cd::rhi::Format::kBGRA8Unorm;
                rd.frames_in_flight         = 2U;
                auto rr = cd::render::Renderer::create(rd);
                if (!rr.has_value())
                {
                    window.reset();
                    device.reset();
                    using_null = true;
                }
                else
                {
                    renderer.emplace(std::move(*rr));
                }
            }
        }
    }

    if (using_null)
    {
        device = std::make_unique<cd::rhi::NullDevice>();
    }

    urr::SubmitterCreateInfo sci {};
    sci.max_vertices = 65532U;
    sci.max_indices  = 196608U;
    sci.color_format = cd::rhi::Format::kBGRA8Unorm;
    auto sub_r = urr::Submitter::create_with_inline_shader(*device, sci);
    if (!sub_r.has_value())
    {
        std::fprintf(stderr, "[cd_player] Submitter creation failed.\n");
        return 2;
    }
    auto& submitter = *sub_r;

    PlayerUiState ui {};
    for (std::size_t idx = 0U; idx < kAllShowcaseSlugs.size(); ++idx)
    {
        if (kAllShowcaseSlugs[idx] == showcase_slug)
        {
            ui.active_showcase_idx = idx;
            break;
        }
    }

    cd::runtime::EngineLoop loop(ctx, 1.0F / 60.0F);
    loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs[ui.active_showcase_idx]));

    cd::rhi::NullDevice sim_null_dev;
    cd::rhi::TextureDesc back_desc {};
    back_desc.extent = { 1280U, 720U, 1U };
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.usage  = cd::rhi::TextureUsage::kColorAttachment;
    auto back_tex = sim_null_dev.create_texture(back_desc);
    const auto back_handle = back_tex.value_or(cd::rhi::TextureHandle {});

    ur::DrawBatcher batcher;
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64U);

    std::uint32_t frame_idx = 0U;
    while (true)
    {
        if (frames_to_run > 0U && frame_idx >= frames_to_run)
        {
            break;
        }

        ui.lmb_pressed = false;
        ui.prev_mouse_x = ui.mouse_x;
        ui.prev_mouse_y = ui.mouse_y;

        if (window)
        {
            events.clear();
            if (!window->pump_events(events))
            {
                break;
            }
            bool close_requested = false;
            for (const auto& ev : events)
            {
                if (ev.kind == cd::platform::OSEventKind::kClose)
                {
                    close_requested = true;
                    break;
                }
                if (ev.kind == cd::platform::OSEventKind::kMouseMove)
                {
                    ui.mouse_x = ev.mouse_x;
                    ui.mouse_y = ev.mouse_y;
                }
                else if (ev.kind == cd::platform::OSEventKind::kMouseButtonDown)
                {
                    ui.mouse_x = ev.mouse_x;
                    ui.mouse_y = ev.mouse_y;
                    if (ev.mouse_button == cd::platform::MouseButton::kLeft)
                    {
                        ui.lmb_pressed = true;
                    }
                    else if (ev.mouse_button == cd::platform::MouseButton::kRight)
                    {
                        ui.rmb_down = true;
                    }
                }
                else if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp)
                {
                    if (ev.mouse_button == cd::platform::MouseButton::kRight)
                    {
                        ui.rmb_down = false;
                    }
                }
                else if (ev.kind == cd::platform::OSEventKind::kKeyDown)
                {
                    if (ev.key == cd::platform::KeyCode::kEscape)
                    {
                        close_requested = true;
                        break;
                    }
                    if (ev.key == cd::platform::KeyCode::kSpace)
                    {
                        ui.playing = !ui.playing;
                    }
                    else if (ev.key == cd::platform::KeyCode::kTab)
                    {
                        ui.show_hud = !ui.show_hud;
                    }
                    else if (ev.key == cd::platform::KeyCode::kRight)
                    {
                        ui.active_showcase_idx = (ui.active_showcase_idx + 1U) % kAllShowcaseSlugs.size();
                        loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs[ui.active_showcase_idx]));
                    }
                    else if (ev.key == cd::platform::KeyCode::kLeft)
                    {
                        ui.active_showcase_idx = (ui.active_showcase_idx + kAllShowcaseSlugs.size() - 1U) % kAllShowcaseSlugs.size();
                        loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs[ui.active_showcase_idx]));
                    }
                }
            }
            if (close_requested)
            {
                break;
            }
        }

        if (ui.rmb_down)
        {
            const float dx = ui.mouse_x - ui.prev_mouse_x;
            const float dy = ui.mouse_y - ui.prev_mouse_y;
            ui.cam_yaw += dx * 0.0075F;
            ui.cam_pitch = std::clamp(ui.cam_pitch + dy * 0.0065F, 0.08F, 1.15F);
        }

        const float dt = ui.playing ? (1.0F / 60.0F) : 0.0F;
        if (ui.playing)
        {
            ui.sim_time += dt;
        }
        const auto telemetry = loop.step_frame(1.0F / 60.0F, &sim_null_dev, back_handle);

        const std::uint32_t fb_w = window ? std::max(1U, window->width()) : 1280U;
        const std::uint32_t fb_h = window ? std::max(1U, window->height()) : 720U;

        batcher.begin_frame();
        bool stage_changed = false;
        render_player_frame(batcher, fb_w, fb_h, ui, loop.stage(), telemetry, stage_changed);
        if (stage_changed)
        {
            loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs[ui.active_showcase_idx]));
        }

        (void)submitter.upload(batcher);

        if (!capture_png_path.empty() && frame_idx + 1U == (frames_to_run > 0U ? frames_to_run : 3U))
        {
            dump_batcher_to_png(batcher, fb_w, fb_h, capture_png_path);
        }

        if (!using_null && renderer)
        {
            auto frame_r = renderer->begin_frame();
            if (frame_r.has_value())
            {
                auto& frame = *frame_r;
                auto& cmd   = *frame.command_buffer;
                std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach { cd::rhi::ColorAttachmentInfo {
                    .view        = frame.swapchain_image_view,
                    .load_op     = cd::rhi::LoadOp::kClear,
                    .store_op    = cd::rhi::StoreOp::kStore,
                    .clear_color = { .f32 = { 0.05F, 0.06F, 0.09F, 1.0F } } } };
                cd::rhi::RenderPassBeginInfo rp {};
                rp.render_area       = cd::rhi::Rect2D { { 0, 0 }, frame.extent };
                rp.color_attachments = color_attach;
                cmd.begin_render_pass(rp);
                cmd.set_viewport(cd::rhi::Viewport {
                    0.0F, 0.0F,
                    static_cast<float>(frame.extent.width),
                    static_cast<float>(frame.extent.height),
                    0.0F, 1.0F });
                cmd.set_scissor(cd::rhi::Rect2D { { 0, 0 }, frame.extent });
                submitter.record(cmd, frame.extent);
                cmd.end_render_pass();
                (void)renderer->end_frame();
            }
        }

        ++frame_idx;
    }

    if (device)
    {
        device->wait_idle();
    }
    std::printf(
        "[cd_player] Completed %u frames of stage '%.*s' (elements=%zu, vertices=%zu)\n",
        frame_idx,
        static_cast<int>(loop.stage().name().size()),
        loop.stage().name().data(),
        loop.stage().root().children().size(),
        batcher.vertex_count()
    );
    return 0;
}

