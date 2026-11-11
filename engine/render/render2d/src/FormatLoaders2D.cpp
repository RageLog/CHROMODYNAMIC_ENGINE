// =============================================================================
// CHROMODYNAMIC — cd/render2d/FormatLoaders2D.cpp
// FAZ 34 (ADR-20261111) — Direct `.aseprite` Binary & `.ldtk` JSON Loaders.
// =============================================================================
#include <cd/render2d/FormatLoaders2D.hpp>

#include <cd/asset/json/Json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cd::render2d
{

namespace
{

constexpr std::uint32_t kFormatLoaderDomain = 0x002B;

[[nodiscard]] std::uint16_t read_le16(std::span<const std::uint8_t> b, std::size_t off) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(b[off]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[off + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t read_le32(std::span<const std::uint8_t> b, std::size_t off) noexcept
{
    return static_cast<std::uint32_t>(b[off]) |
           (static_cast<std::uint32_t>(b[off + 1U]) << 8U) |
           (static_cast<std::uint32_t>(b[off + 2U]) << 16U) |
           (static_cast<std::uint32_t>(b[off + 3U]) << 24U);
}

void write_le16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
}

void write_le32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}

}  // namespace

std::vector<FlipbookClip2D> AsepriteDocumentInfo2D::build_horizontal_strip_clips() const
{
    std::vector<FlipbookClip2D> clips;
    const std::uint16_t total = std::max<std::uint16_t>(1U, frame_count);

    const auto make_frame = [&](std::uint16_t idx) -> FlipbookFrame2D {
        const float u0  = static_cast<float>(idx) / static_cast<float>(total);
        const float u1  = static_cast<float>(idx + 1U) / static_cast<float>(total);
        const float ms  = (idx < frame_durations_ms.size() && frame_durations_ms.at(idx) > 0U)
            ? static_cast<float>(frame_durations_ms.at(idx))
            : 100.0F;
        return FlipbookFrame2D {
            .uv           = { u0, 0.0F, u1, 1.0F },
            .duration_sec = ms * 0.001F,
        };
    };

    if (tags.empty())
    {
        FlipbookClip2D default_clip {};
        default_clip.name = "Default";
        default_clip.loop = true;
        for (std::uint16_t f = 0U; f < total; ++f)
        {
            default_clip.frames.push_back(make_frame(f));
        }
        clips.push_back(std::move(default_clip));
        return clips;
    }

    for (const auto& tag : tags)
    {
        FlipbookClip2D clip {};
        clip.name = tag.name.empty() ? "Tag" : tag.name;
        clip.loop = true;
        const std::uint16_t f0 = std::min<std::uint16_t>(tag.from_frame, static_cast<std::uint16_t>(total - 1U));
        const std::uint16_t f1 = std::min<std::uint16_t>(std::max(f0, tag.to_frame), static_cast<std::uint16_t>(total - 1U));
        if (tag.direction == 1U)
        {
            for (int f = static_cast<int>(f1); f >= static_cast<int>(f0); --f)
            {
                clip.frames.push_back(make_frame(static_cast<std::uint16_t>(f)));
            }
        }
        else
        {
            for (std::uint16_t f = f0; f <= f1; ++f)
            {
                clip.frames.push_back(make_frame(f));
            }
            if (tag.direction == 2U && f1 > f0 + 1U)
            {
                for (int f = static_cast<int>(f1) - 1; f > static_cast<int>(f0); --f)
                {
                    clip.frames.push_back(make_frame(static_cast<std::uint16_t>(f)));
                }
            }
        }
        clips.push_back(std::move(clip));
    }
    return clips;
}

cd::core::Result<AsepriteDocumentInfo2D> parse_aseprite_binary(std::span<const std::uint8_t> bytes)
{
    // Aseprite header is exactly 128 bytes with magic 0xA5E0 at offset 4.
    if (bytes.size() < 128U)
    {
        return cd::core::fail(kFormatLoaderDomain, 1U, "aseprite buffer smaller than 128-byte header");
    }

    const std::uint16_t magic = read_le16(bytes, 4U);
    if (magic != 0xA5E0U)
    {
        return cd::core::fail(kFormatLoaderDomain, 2U, "aseprite header magic mismatch (expected 0xA5E0)");
    }

    AsepriteDocumentInfo2D doc {};
    doc.frame_count     = read_le16(bytes, 6U);
    doc.width           = read_le16(bytes, 8U);
    doc.height          = read_le16(bytes, 10U);
    doc.color_depth_bpp = read_le16(bytes, 12U);

    if (doc.width == 0U || doc.height == 0U || doc.frame_count == 0U)
    {
        return cd::core::fail(kFormatLoaderDomain, 3U, "aseprite header has zero dimensions or zero frames");
    }

    std::size_t cursor = 128U;
    for (std::uint16_t f = 0U; f < doc.frame_count && cursor + 16U <= bytes.size(); ++f)
    {
        const std::uint32_t frame_bytes = read_le32(bytes, cursor);
        const std::uint16_t frame_magic = read_le16(bytes, cursor + 4U);
        if (frame_magic != 0xF1FAU || frame_bytes < 16U || cursor + frame_bytes > bytes.size())
        {
            return cd::core::fail(kFormatLoaderDomain, 4U, "corrupt aseprite frame header");
        }

        const std::uint16_t old_chunks  = read_le16(bytes, cursor + 6U);
        const std::uint16_t duration_ms = read_le16(bytes, cursor + 8U);
        const std::uint32_t new_chunks  = read_le32(bytes, cursor + 12U);
        const std::uint32_t chunk_count = (new_chunks != 0U) ? new_chunks : static_cast<std::uint32_t>(old_chunks);

        doc.frame_durations_ms.push_back(duration_ms > 0U ? duration_ms : 100U);

        std::size_t chunk_off = cursor + 16U;
        const std::size_t frame_end = cursor + frame_bytes;
        for (std::uint32_t ci = 0U; ci < chunk_count && chunk_off + 6U <= frame_end; ++ci)
        {
            const std::uint32_t chunk_size = read_le32(bytes, chunk_off);
            const std::uint16_t chunk_type = read_le16(bytes, chunk_off + 4U);
            if (chunk_size < 6U || chunk_off + chunk_size > frame_end)
            {
                break;
            }

            // 0x2018 = Frame Tags Chunk
            if (chunk_type == 0x2018U && chunk_size >= 16U)
            {
                const std::uint16_t tag_count = read_le16(bytes, chunk_off + 6U);
                std::size_t         tag_off   = chunk_off + 16U;  // 6 header + 2 count + 8 reserved
                for (std::uint16_t ti = 0U; ti < tag_count && tag_off + 19U <= chunk_off + chunk_size; ++ti)
                {
                    const std::uint16_t from_f   = read_le16(bytes, tag_off);
                    const std::uint16_t to_f     = read_le16(bytes, tag_off + 2U);
                    const std::uint8_t  dir      = bytes[tag_off + 4U];
                    const std::uint16_t name_len = read_le16(bytes, tag_off + 17U);
                    tag_off += 19U;
                    if (tag_off + name_len > chunk_off + chunk_size)
                    {
                        break;
                    }
                    std::string tag_name(
                        reinterpret_cast<const char*>(bytes.data() + tag_off),
                        static_cast<std::size_t>(name_len));
                    tag_off += name_len;
                    doc.tags.push_back(AsepriteTag2D {
                        .name       = std::move(tag_name),
                        .from_frame = from_f,
                        .to_frame   = to_f,
                        .direction  = dir,
                    });
                }
            }

            chunk_off += chunk_size;
        }

        cursor = frame_end;
    }

    return doc;
}

std::vector<std::uint8_t> build_aseprite_header_and_tags_blob(
    std::uint16_t                  width,
    std::uint16_t                  height,
    std::span<const std::uint16_t> frame_durations_ms,
    std::span<const AsepriteTag2D> tags)
{
    std::vector<std::uint8_t> out(128U, 0U);
    const std::uint16_t frame_cnt = static_cast<std::uint16_t>(
        std::max<std::size_t>(1U, frame_durations_ms.size()));

    // Patch header fields (magic 0xA5E0, frames, w, h, 32bpp)
    out[4]  = 0xE0U;
    out[5]  = 0xA5U;
    out[6]  = static_cast<std::uint8_t>(frame_cnt & 0xFFU);
    out[7]  = static_cast<std::uint8_t>((frame_cnt >> 8U) & 0xFFU);
    out[8]  = static_cast<std::uint8_t>(width & 0xFFU);
    out[9]  = static_cast<std::uint8_t>((width >> 8U) & 0xFFU);
    out[10] = static_cast<std::uint8_t>(height & 0xFFU);
    out[11] = static_cast<std::uint8_t>((height >> 8U) & 0xFFU);
    out[12] = 32U;
    out[13] = 0U;

    for (std::uint16_t fi = 0U; fi < frame_cnt; ++fi)
    {
        std::vector<std::uint8_t> chunk_bytes;
        std::uint32_t             chunk_count = 0U;
        if (fi == 0U && !tags.empty())
        {
            chunk_count = 1U;
            write_le32(chunk_bytes, 0U);  // patched below
            write_le16(chunk_bytes, 0x2018U);
            write_le16(chunk_bytes, static_cast<std::uint16_t>(tags.size()));
            for (int r = 0; r < 8; ++r) chunk_bytes.push_back(0U);

            for (const auto& t : tags)
            {
                write_le16(chunk_bytes, t.from_frame);
                write_le16(chunk_bytes, t.to_frame);
                chunk_bytes.push_back(t.direction);
                for (int pad = 0; pad < 12; ++pad) chunk_bytes.push_back(0U);
                write_le16(chunk_bytes, static_cast<std::uint16_t>(t.name.size()));
                chunk_bytes.insert(chunk_bytes.end(), t.name.begin(), t.name.end());
            }
            const auto csz = static_cast<std::uint32_t>(chunk_bytes.size());
            chunk_bytes[0] = static_cast<std::uint8_t>(csz & 0xFFU);
            chunk_bytes[1] = static_cast<std::uint8_t>((csz >> 8U) & 0xFFU);
            chunk_bytes[2] = static_cast<std::uint8_t>((csz >> 16U) & 0xFFU);
            chunk_bytes[3] = static_cast<std::uint8_t>((csz >> 24U) & 0xFFU);
        }

        const std::uint16_t dur = (fi < frame_durations_ms.size()) ? frame_durations_ms[fi] : 100U;
        const auto          fsz = static_cast<std::uint32_t>(16U + chunk_bytes.size());
        write_le32(out, fsz);
        write_le16(out, 0xF1FAU);
        write_le16(out, static_cast<std::uint16_t>(chunk_count));
        write_le16(out, dur);
        write_le16(out, 0U);
        write_le32(out, chunk_count);
        out.insert(out.end(), chunk_bytes.begin(), chunk_bytes.end());
    }

    const auto total_sz = static_cast<std::uint32_t>(out.size());
    out[0]              = static_cast<std::uint8_t>(total_sz & 0xFFU);
    out[1]              = static_cast<std::uint8_t>((total_sz >> 8U) & 0xFFU);
    out[2]              = static_cast<std::uint8_t>((total_sz >> 16U) & 0xFFU);
    out[3]              = static_cast<std::uint8_t>((total_sz >> 24U) & 0xFFU);
    return out;
}

cd::core::Result<LdtkImportedLevel2D>
import_ldtk_level_json(std::string_view json_text, Tilemap2D& out_tilemap)
{
    if (json_text.size() >= 3U &&
        static_cast<unsigned char>(json_text[0]) == 0xEFU &&
        static_cast<unsigned char>(json_text[1]) == 0xBBU &&
        static_cast<unsigned char>(json_text[2]) == 0xBFU)
    {
        json_text.remove_prefix(3U);
    }
    auto parsed = cd::asset::json::parse(json_text);
    if (!parsed.has_value())
    {
        return std::unexpected(parsed.error());
    }
    const auto& root = *parsed;
    if (!root.is_object())
    {
        return cd::core::fail(kFormatLoaderDomain, 10U, "LDtk root must be a JSON object");
    }

    const cd::asset::json::Value* level_val = &root;
    if (auto levels_arr = root.at("levels"); levels_arr.has_value() && (*levels_arr)->is_array())
    {
        if ((*levels_arr)->as_array().empty())
        {
            return cd::core::fail(kFormatLoaderDomain, 11U, "LDtk levels array is empty");
        }
        level_val = &(*levels_arr)->as_array().front();
    }

    LdtkImportedLevel2D info {};
    if (auto id_v = level_val->at("identifier"); id_v.has_value() && (*id_v)->is_string())
    {
        info.level_identifier = (*id_v)->as_string();
    }
    else
    {
        info.level_identifier = "LDtk_Level_0";
    }

    int px_wid = 256;
    int px_hei = 160;
    if (auto w_v = level_val->at("pxWid"); w_v.has_value() && (*w_v)->is_number())
    {
        px_wid = std::max(16, static_cast<int>((*w_v)->as_number()));
    }
    if (auto h_v = level_val->at("pxHei"); h_v.has_value() && (*h_v)->is_number())
    {
        px_hei = std::max(16, static_cast<int>((*h_v)->as_number()));
    }

    out_tilemap.clear();

    if (auto layers_v = level_val->at("layerInstances"); layers_v.has_value() && (*layers_v)->is_array())
    {
        for (const auto& layer : (*layers_v)->as_array())
        {
            if (!layer.is_object())
            {
                continue;
            }
            int grid_sz = 16;
            if (auto gs = layer.at("__gridSize"); gs.has_value() && (*gs)->is_number())
            {
                grid_sz = std::max(1, static_cast<int>((*gs)->as_number()));
            }
            info.tile_px = grid_sz;

            int c_wid = px_wid / grid_sz;
            int c_hei = px_hei / grid_sz;
            if (auto cw = layer.at("__cWid"); cw.has_value() && (*cw)->is_number())
            {
                c_wid = std::max(1, static_cast<int>((*cw)->as_number()));
            }
            if (auto ch = layer.at("__cHei"); ch.has_value() && (*ch)->is_number())
            {
                c_hei = std::max(1, static_cast<int>((*ch)->as_number()));
            }
            info.grid_width  = std::max(info.grid_width, c_wid);
            info.grid_height = std::max(info.grid_height, c_hei);

            // 1. IntGrid CSV (`intGridCsv`: LDtk rows are top-to-bottom -> flip Y to engine Y-up!)
            if (auto csv = layer.at("intGridCsv"); csv.has_value() && (*csv)->is_array())
            {
                const auto& arr = (*csv)->as_array();
                for (std::size_t idx = 0U; idx < arr.size(); ++idx)
                {
                    if (!arr.at(idx).is_number()) continue;
                    const int val = static_cast<int>(arr.at(idx).as_number());
                    if (val <= 0) continue;
                    const int gx = static_cast<int>(idx) % c_wid;
                    const int row_top_down = static_cast<int>(idx) / c_wid;
                    const int gy = (c_hei - 1) - row_top_down;
                    out_tilemap.set_tile(gx, gy, static_cast<std::uint16_t>(std::clamp(val, 1, 4)), false);
                    ++info.imported_tiles;
                }
            }

            // 2. Explicit `gridTiles` / `autoLayerTiles`
            for (const std::string_view key : { std::string_view { "gridTiles" }, std::string_view { "autoLayerTiles" } })
            {
                if (auto gt = layer.at(key); gt.has_value() && (*gt)->is_array())
                {
                    for (const auto& t : (*gt)->as_array())
                    {
                        if (!t.is_object()) continue;
                        auto px = t.at("px");
                        if (!px.has_value() || !(*px)->is_array() || (*px)->as_array().size() < 2U) continue;
                        const int tx = static_cast<int>((*px)->as_array().at(0).as_number()) / grid_sz;
                        const int ty_top = static_cast<int>((*px)->as_array().at(1).as_number()) / grid_sz;
                        const int gy = (c_hei - 1) - ty_top;
                        std::uint16_t pal_id = 1U;
                        if (auto tv = t.at("t"); tv.has_value() && (*tv)->is_number())
                        {
                            pal_id = static_cast<std::uint16_t>(1 + (std::max(0, static_cast<int>((*tv)->as_number())) % 4));
                        }
                        out_tilemap.set_tile(tx, gy, pal_id, false);
                        ++info.imported_tiles;
                    }
                }
            }

            // 3. `entityInstances`
            if (auto ents = layer.at("entityInstances"); ents.has_value() && (*ents)->is_array())
            {
                for (const auto& ent : (*ents)->as_array())
                {
                    if (!ent.is_object()) continue;
                    LdtkEntity2D le {};
                    if (auto eid = ent.at("__identifier"); eid.has_value() && (*eid)->is_string())
                    {
                        le.identifier = (*eid)->as_string();
                    }
                    if (auto px = ent.at("px"); px.has_value() && (*px)->is_array() && (*px)->as_array().size() >= 2U)
                    {
                        const float px_x = static_cast<float>((*px)->as_array().at(0).as_number());
                        const float px_y = static_cast<float>((*px)->as_array().at(1).as_number());
                        le.world_pos.x   = (px_x / static_cast<float>(grid_sz)) * out_tilemap.tile_size();
                        le.world_pos.y   = ((static_cast<float>(px_hei) - px_y) / static_cast<float>(grid_sz)) * out_tilemap.tile_size();
                    }
                    if (auto wv = ent.at("width"); wv.has_value() && (*wv)->is_number())
                    {
                        le.size_units.x = std::max(0.25F, static_cast<float>((*wv)->as_number()) / static_cast<float>(grid_sz));
                    }
                    if (auto hv = ent.at("height"); hv.has_value() && (*hv)->is_number())
                    {
                        le.size_units.y = std::max(0.25F, static_cast<float>((*hv)->as_number()) / static_cast<float>(grid_sz));
                    }
                    info.entities.push_back(std::move(le));
                }
            }
        }
    }

    out_tilemap.rebuild_all_autotile_masks();
    return info;
}

}  // namespace cd::render2d
