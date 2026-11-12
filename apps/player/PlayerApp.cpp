// =============================================================================
// CHROMODYNAMIC - apps/player/PlayerApp.cpp
//
// Standalone runtime game & showcase player implementation (ADR-20261112).
//   * Zero duplicated PNG/CRC32 encoders -> cd::app_common::FrameCapture
//   * Zero duplicated binary file readers -> cd::app_common::read_binary_file
//   * Zero duplicated 5x7 font tables    -> cd::render2d::Canvas2D::add_pixel_text
// =============================================================================
#include "PlayerApp.hpp"
#include "ShowcaseRenderer3D.hpp"

#include <cd/app_common/FileTextIo.hpp>
#include <cd/app_common/FrameCapture.hpp>
#include <cd/audio/NativeBackend.hpp>
#include <cd/element_core/Element.hpp>
#include <cd/gameplay_framework/VesselPilotDirector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/render/TextureUpload.hpp>
#include <cd/render2d/Renderer2D.hpp>
#include <cd/render2d/TextureAtlas2D.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/runtime/EngineContext.hpp>
#include <cd/runtime/EngineLoop.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <cd/world2d/GenreTemplates2D.hpp>
#include <cd/world2d/PlaytestRunner2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::player
{

namespace
{

namespace ur  = cd::ui::renderer;
namespace urr = cd::ui::renderer_rhi;
namespace r2d = cd::render2d;

constexpr std::uint64_t kHudSortKey = 0x00FF'0000'0000'0000ULL;

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

void add_clipped_pixel_text(
    r2d::Canvas2D&   canvas,
    float            x,
    float            y,
    std::string_view text,
    float            scale,
    r2d::ColorRgba8  color,
    float            max_x = 1.0e9F)
{
    const float px = std::max(1.0F, std::round(scale));
    float cx = x;
    std::size_t count = 0U;
    for (const char ch : text)
    {
        const float char_w = (ch == ' ') ? (4.0F * px) : (6.0F * px);
        if (cx + char_w > max_x)
        {
            break;
        }
        cx += char_w;
        ++count;
    }
    if (count > 0U)
    {
        canvas.add_pixel_text(x, y, text.substr(0U, count), px, color, kHudSortKey);
    }
}

[[nodiscard]] int run_showcase_slug_headless(
    cd::runtime::EngineContext& ctx,
    cd::rhi::IDevice&           dev,
    cd::rhi::TextureHandle      backbuffer,
    std::string_view            slug,
    std::uint32_t               frames_to_run)
{
    cd::runtime::EngineLoop loop(ctx, 1.0F / 60.0F);
    loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(slug));

    for (std::uint32_t f = 0U; f < frames_to_run; ++f)
    {
        const auto telemetry = loop.step_frame(1.0F / 60.0F, &dev, backbuffer);
        if (telemetry.framegraph_passes_compiled == 0U)
        {
            std::fprintf(
                stderr,
                "[cd_player] ERROR: 0 render passes on showcase '%.*s'\n",
                static_cast<int>(slug.size()),
                slug.data());
            return 1;
        }
    }

    std::printf(
        "[cd_player] Completed %u frames of stage '%.*s' (elements=%zu)\n",
        frames_to_run,
        static_cast<int>(loop.stage().name().size()),
        loop.stage().name().data(),
        loop.stage().root().children().size());
    return 0;
}

void render_showcase_hud_canvas(
    r2d::Canvas2D&                      canvas,
    std::uint32_t                       fb_w,
    std::uint32_t                       fb_h,
    PlayerUiState&                      ui,
    const cd::element_core::Stage&      stage,
    const cd::runtime::FrameStepReport& telemetry,
    bool                                gpu_3d_active,
    bool                                hd2d_mode,
    bool&                               out_stage_changed)
{
    out_stage_changed = false;
    const float w = static_cast<float>(fb_w);
    const float h = static_cast<float>(fb_h);
    constexpr float kTopBarH = 38.0F;
    const float vp_y = kTopBarH;
    const float vp_h = h - kTopBarH;
    const float horizon_y = vp_y + vp_h * 0.42F;

    canvas.clear(r2d::ViewportRect2D { 0.0F, 0.0F, w, h });

    const float cy = std::cos(ui.cam_yaw);
    const float sy = std::sin(ui.cam_yaw);
    const float cp = std::cos(ui.cam_pitch);
    const float sp = std::sin(ui.cam_pitch);
    const auto& children = stage.root().children();

    if (!gpu_3d_active)
    {
    // 1. Sky & Ground Gradient (CPU --null fallback only)
    canvas.add_gradient_rect(
        0.0F, vp_y, w, horizon_y - vp_y,
        r2d::ColorRgba8 { 14U, 22U, 38U, 255U },
        r2d::ColorRgba8 { 38U, 62U, 98U, 255U },
        false,
        kHudSortKey);
    canvas.add_solid_rect(0.0F, horizon_y, w, vp_y + vp_h - horizon_y, r2d::ColorRgba8 { 18U, 22U, 30U, 255U }, kHudSortKey);
    canvas.add_solid_rect(0.0F, horizon_y, w, 2.0F, r2d::ColorRgba8 { 72U, 118U, 185U, 220U }, kHudSortKey);

    const float sun_x = w * 0.76F;
    const float sun_y = vp_y + (horizon_y - vp_y) * 0.36F;
    canvas.add_radial_glow(sun_x, sun_y, 28.0F, r2d::ColorRgba8 { 255U, 225U, 140U, 185U }, 18, kHudSortKey);
    canvas.add_solid_rect(sun_x - 6.0F, sun_y - 6.0F, 12.0F, 12.0F, r2d::ColorRgba8 { 255U, 248U, 215U, 245U }, kHudSortKey);

    // 2. 3D Perspective Camera Projection
    const float cam_dist  = 10.5F;
    const float fov_scale = std::min(w, vp_h) * 0.72F;
    const float center_x  = w * 0.50F;
    const float center_y  = horizon_y + 18.0F;

    const auto project3d = [&](float wx, float wy, float wz, float& sx, float& sy_out, float& depth_out) noexcept -> bool {
        const float rx = wx * cy - wz * sy;
        const float rz = wx * sy + wz * cy;
        const float ry = wy * cp - rz * sp;
        const float cz = wy * sp + rz * cp + cam_dist;
        if (cz <= 0.4F)
        {
            return false;
        }
        sx        = center_x + (rx / cz) * fov_scale;
        sy_out    = center_y - (ry / cz) * fov_scale;
        depth_out = cz;
        return true;
    };

    // 3. Perspective 3D XZ Ground Grid + World Axes
    for (int g = -6; g <= 6; ++g)
    {
        const float gf = static_cast<float>(g) * 1.15F;
        float x0 = 0.0F, y0 = 0.0F, z0 = 0.0F;
        float x1 = 0.0F, y1 = 0.0F, z1 = 0.0F;
        const r2d::ColorRgba8 grid_c = (g == 0)
            ? r2d::ColorRgba8 { 70U, 105U, 155U, 210U }
            : r2d::ColorRgba8 { 34U, 42U, 56U, 165U };
        if (project3d(gf, -1.0F, -6.9F, x0, y0, z0) && project3d(gf, -1.0F, 6.9F, x1, y1, z1))
        {
            canvas.add_line(x0, y0, x1, y1, (g == 0) ? 2.0F : 1.0F, grid_c, kHudSortKey);
        }
        if (project3d(-6.9F, -1.0F, gf, x0, y0, z0) && project3d(6.9F, -1.0F, gf, x1, y1, z1))
        {
            canvas.add_line(x0, y0, x1, y1, (g == 0) ? 2.0F : 1.0F, grid_c, kHudSortKey);
        }
    }

    {
        float ax0 = 0.0F, ay0 = 0.0F, az0 = 0.0F;
        float ax1 = 0.0F, ay1 = 0.0F, az1 = 0.0F;
        if (project3d(0.0F, -1.0F, 0.0F, ax0, ay0, az0) && project3d(5.2F, -1.0F, 0.0F, ax1, ay1, az1))
        {
            canvas.add_line(ax0, ay0, ax1, ay1, 2.0F, r2d::ColorRgba8 { 225U, 75U, 75U, 225U }, kHudSortKey);
        }
        if (project3d(0.0F, -1.0F, 0.0F, ax0, ay0, az0) && project3d(0.0F, -1.0F, 5.2F, ax1, ay1, az1))
        {
            canvas.add_line(ax0, ay0, ax1, ay1, 2.0F, r2d::ColorRgba8 { 75U, 145U, 240U, 225U }, kHudSortKey);
        }
    }

    // Turntable pedestal
    {
        float px = 0.0F, py = 0.0F, pz = 0.0F;
        if (project3d(0.0F, -1.0F, 0.0F, px, py, pz))
        {
            const float pr = std::clamp(260.0F / pz, 34.0F, 95.0F);
            canvas.add_solid_rect(px - pr, py - pr * 0.24F, pr * 2.0F, pr * 0.48F, r2d::ColorRgba8 { 26U, 34U, 48U, 215U }, kHudSortKey);
            canvas.add_solid_rect(px - pr * 0.85F, py - pr * 0.18F, pr * 1.7F, pr * 0.36F, r2d::ColorRgba8 { 38U, 52U, 74U, 230U }, kHudSortKey);
            canvas.add_solid_rect(px - pr, py - pr * 0.24F, pr * 2.0F, 1.5F, r2d::ColorRgba8 { 85U, 145U, 225U, 190U }, kHudSortKey);
        }
    }

    // 4. Render 3D Stage Elements
    if (ui.selected_elem_idx >= children.size() && !children.empty())
    {
        ui.selected_elem_idx = 0U;
    }

    static constexpr std::array<std::array<float, 3>, 8> kStageSlots {{
        {  0.0F, 0.20F,  0.0F },
        { -2.9F, 1.35F, -0.8F },
        {  2.9F, 0.25F, -0.5F },
        { -2.1F, 0.00F,  2.1F },
        {  2.1F, 0.00F,  2.1F },
        { -4.2F, 0.45F,  0.5F },
        {  4.2F, 0.45F,  0.5F },
        {  0.0F, 1.40F, -2.8F },
    }};

    for (std::size_t idx = 0U; idx < children.size(); ++idx)
    {
        const auto& elem = children.at(idx);
        if (!elem)
        {
            continue;
        }
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
        if (std::abs(wx) < 0.05F && std::abs(wz) < 0.05F)
        {
            const auto& slot = kStageSlots.at(idx % kStageSlots.size());
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
        const bool is_light = (cname.find("Light") != std::string_view::npos || cname.find("Sun") != std::string_view::npos);
        const bool is_world = (cname.find("World") != std::string_view::npos || cname.find("Terrain") != std::string_view::npos ||
                               cname.find("Ocean") != std::string_view::npos || cname.find("Nav") != std::string_view::npos ||
                               cname.find("Sky") != std::string_view::npos);
        const bool is_mesh  = (cname.find("Mesh") != std::string_view::npos || cname.find("Pbr") != std::string_view::npos ||
                               cname.find("Avatar") != std::string_view::npos || cname.find("Vessel") != std::string_view::npos);
        const bool is_agent = (cname.find("Agent") != std::string_view::npos || cname.find("Pilot") != std::string_view::npos ||
                               cname.find("Synth") != std::string_view::npos);

        const float half_sz = std::clamp((idx == 0U ? 145.0F : 112.0F) / depth, 14.0F, 36.0F);

        float gx = 0.0F, gy = 0.0F, gz = 0.0F;
        if (project3d(wx, -1.0F, wz, gx, gy, gz))
        {
            canvas.add_solid_rect(gx - half_sz * 0.9F, gy - half_sz * 0.25F, half_sz * 1.8F, half_sz * 0.5F, r2d::ColorRgba8 { 6U, 9U, 14U, 155U }, kHudSortKey);
            if (wy > -0.8F)
            {
                canvas.add_line(
                    sx, sy_pt + half_sz, gx, gy, 1.0F,
                    is_light ? r2d::ColorRgba8 { 255U, 215U, 95U, 130U } : r2d::ColorRgba8 { 95U, 135U, 190U, 110U },
                    kHudSortKey);
            }
        }

        const r2d::ColorRgba8 top_c = is_light ? r2d::ColorRgba8 { 255U, 235U, 135U, 255U }
                                    : is_mesh  ? r2d::ColorRgba8 { 115U, 195U, 255U, 255U }
                                    : is_world ? r2d::ColorRgba8 { 95U, 225U, 160U, 255U }
                                    : is_agent ? r2d::ColorRgba8 { 255U, 145U, 115U, 255U }
                                               : r2d::ColorRgba8 { 185U, 155U, 255U, 255U };
        const r2d::ColorRgba8 front_c = is_light ? r2d::ColorRgba8 { 235U, 185U, 65U, 245U }
                                      : is_mesh  ? r2d::ColorRgba8 { 65U, 145U, 230U, 245U }
                                      : is_world ? r2d::ColorRgba8 { 55U, 175U, 115U, 245U }
                                      : is_agent ? r2d::ColorRgba8 { 215U, 95U, 75U, 245U }
                                                 : r2d::ColorRgba8 { 135U, 105U, 215U, 245U };
        const r2d::ColorRgba8 side_c = is_light ? r2d::ColorRgba8 { 185U, 135U, 40U, 245U }
                                     : is_mesh  ? r2d::ColorRgba8 { 40U, 95U, 170U, 245U }
                                     : is_world ? r2d::ColorRgba8 { 35U, 125U, 80U, 245U }
                                     : is_agent ? r2d::ColorRgba8 { 155U, 60U, 48U, 245U }
                                                : r2d::ColorRgba8 { 90U, 68U, 160U, 245U };

        const float iso_dx = half_sz * 0.42F;
        const float iso_dy = half_sz * 0.34F;
        canvas.add_solid_rect(sx - half_sz + iso_dx * 0.5F, sy_pt - half_sz - iso_dy, half_sz * 1.6F, iso_dy + 2.0F, top_c, kHudSortKey);
        canvas.add_solid_rect(sx - half_sz, sy_pt - half_sz, half_sz * 1.6F, half_sz * 1.6F, front_c, kHudSortKey);
        canvas.add_solid_rect(sx + half_sz * 0.6F, sy_pt - half_sz - iso_dy * 0.5F, iso_dx, half_sz * 1.6F, side_c, kHudSortKey);

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
            canvas.add_rect_outline(bx, by, bw, bh, 2.0F, r2d::ColorRgba8 { 255U, 205U, 60U, 255U }, kHudSortKey);

            const float giz_len = 46.0F;
            canvas.add_line(sx, sy_pt, sx + giz_len, sy_pt + 5.0F, 2.5F, r2d::ColorRgba8 { 245U, 75U, 75U, 255U }, kHudSortKey);
            canvas.add_pixel_text(sx + giz_len + 6.0F, sy_pt, "X", 1.0F, r2d::ColorRgba8 { 255U, 110U, 110U, 255U }, kHudSortKey);

            canvas.add_line(sx, sy_pt, sx, sy_pt - giz_len, 2.5F, r2d::ColorRgba8 { 80U, 235U, 115U, 255U }, kHudSortKey);
            canvas.add_pixel_text(sx + 6.0F, sy_pt - giz_len - 4.0F, "Y", 1.0F, r2d::ColorRgba8 { 110U, 255U, 140U, 255U }, kHudSortKey);

            canvas.add_line(sx, sy_pt, sx - giz_len * 0.65F, sy_pt + giz_len * 0.48F, 2.5F, r2d::ColorRgba8 { 85U, 160U, 255U, 255U }, kHudSortKey);
            canvas.add_pixel_text(sx - giz_len * 0.65F - 14.0F, sy_pt + giz_len * 0.48F - 4.0F, "Z", 1.0F, r2d::ColorRgba8 { 120U, 185U, 255U, 255U }, kHudSortKey);
        }

        char tag_buf[64] {};
        std::snprintf(
            tag_buf,
            sizeof(tag_buf),
            "[%zu] %.*s",
            idx + 1U,
            static_cast<int>(elem->name().size()),
            elem->name().data());
        const std::string_view tag_sv { tag_buf };
        const float lbl_w = static_cast<float>(std::min<std::size_t>(tag_sv.size(), 24U)) * 6.0F + 10.0F;
        const float lbl_x = sx - lbl_w * 0.5F;
        const float lbl_y = sy_pt - half_sz - iso_dy - 19.0F - static_cast<float>(idx % 2U) * 11.0F;
        canvas.add_solid_rect(
            lbl_x, lbl_y, lbl_w, 14.0F,
            is_selected ? r2d::ColorRgba8 { 42U, 58U, 88U, 240U } : r2d::ColorRgba8 { 16U, 22U, 32U, 215U },
            kHudSortKey);
        add_clipped_pixel_text(
            canvas, lbl_x + 5.0F, lbl_y + 3.0F, tag_sv, 1.0F,
            is_selected ? r2d::ColorRgba8 { 255U, 235U, 140U, 255U } : r2d::ColorRgba8 { 215U, 225U, 242U, 245U },
            lbl_x + lbl_w - 2.0F);
    }
    }  // end if (!gpu_3d_active)

    // 5. Top-Right 3D Orientation Rose
    {
        const float comp_cx = w - 42.0F;
        const float comp_cy = vp_y + 42.0F;
        canvas.add_solid_rect(comp_cx - 28.0F, comp_cy - 28.0F, 56.0F, 56.0F, r2d::ColorRgba8 { 14U, 18U, 26U, 205U }, kHudSortKey);
        canvas.add_line(comp_cx, comp_cy, comp_cx + cy * 18.0F, comp_cy + sy * sp * 18.0F, 2.0F, r2d::ColorRgba8 { 245U, 80U, 80U, 255U }, kHudSortKey);
        canvas.add_pixel_text(comp_cx + cy * 21.0F - 2.0F, comp_cy + sy * sp * 21.0F - 3.0F, "X", 1.0F, r2d::ColorRgba8 { 255U, 110U, 110U, 255U }, kHudSortKey);
        canvas.add_line(comp_cx, comp_cy, comp_cx, comp_cy - cp * 18.0F, 2.0F, r2d::ColorRgba8 { 85U, 235U, 115U, 255U }, kHudSortKey);
        canvas.add_pixel_text(comp_cx - 2.0F, comp_cy - cp * 22.0F - 5.0F, "Y", 1.0F, r2d::ColorRgba8 { 110U, 255U, 140U, 255U }, kHudSortKey);
        canvas.add_line(comp_cx, comp_cy, comp_cx - sy * 18.0F, comp_cy + cy * sp * 18.0F, 2.0F, r2d::ColorRgba8 { 85U, 160U, 255U, 255U }, kHudSortKey);
        canvas.add_pixel_text(comp_cx - sy * 22.0F - 2.0F, comp_cy + cy * sp * 22.0F - 3.0F, "Z", 1.0F, r2d::ColorRgba8 { 120U, 185U, 255U, 255U }, kHudSortKey);
    }

    // 6. Top Interactive Player Bar
    canvas.add_solid_rect(0.0F, 0.0F, w, kTopBarH, r2d::ColorRgba8 { 16U, 20U, 28U, 255U }, kHudSortKey);
    canvas.add_solid_rect(0.0F, kTopBarH - 2.0F, w, 2.0F, r2d::ColorRgba8 { 56U, 128U, 235U, 255U }, kHudSortKey);
    canvas.add_solid_rect(8.0F, 6.0F, 168.0F, 26.0F, r2d::ColorRgba8 { 48U, 105U, 205U, 255U }, kHudSortKey);
    canvas.add_pixel_text(16.0F, 15.0F, "CHROMODYNAMIC PLAYER", 1.0F, r2d::ColorRgba8 { 248U, 252U, 255U, 255U }, kHudSortKey);

    const auto draw_btn = [&](float bx, float bw, std::string_view label, bool active, r2d::ColorRgba8 act_c) -> bool {
        const bool hovered = (ui.mouse_x >= bx && ui.mouse_x < bx + bw && ui.mouse_y >= 6.0F && ui.mouse_y < 32.0F);
        const r2d::ColorRgba8 bg = active ? act_c : (hovered ? r2d::ColorRgba8 { 44U, 54U, 72U, 255U } : r2d::ColorRgba8 { 28U, 34U, 46U, 255U });
        canvas.add_solid_rect(bx, 6.0F, bw, 26.0F, bg, kHudSortKey);
        canvas.add_solid_rect(bx, 6.0F, bw, 1.0F, active ? r2d::ColorRgba8 { 145U, 210U, 255U, 255U } : r2d::ColorRgba8 { 58U, 70U, 92U, 255U }, kHudSortKey);
        add_clipped_pixel_text(canvas, bx + 8.0F, 15.0F, label, 1.0F, r2d::ColorRgba8 { 240U, 246U, 255U, 255U }, bx + bw - 4.0F);
        return hovered && ui.lmb_pressed;
    };

    float cx_bar = 186.0F;
    if (draw_btn(cx_bar, 62.0F, "< PREV", false, r2d::ColorRgba8 { 60U, 120U, 205U, 255U }))
    {
        ui.active_showcase_idx = (ui.active_showcase_idx + kAllShowcaseSlugs.size() - 1U) % kAllShowcaseSlugs.size();
        out_stage_changed = true;
    }
    cx_bar += 66.0F;

    char stage_lbl[96] {};
    std::snprintf(
        stage_lbl,
        sizeof(stage_lbl),
        "STAGE %02zu/12: %.*s",
        ui.active_showcase_idx + 1U,
        static_cast<int>(stage.name().size()),
        stage.name().data());
    canvas.add_solid_rect(cx_bar, 6.0F, 300.0F, 26.0F, r2d::ColorRgba8 { 22U, 28U, 38U, 255U }, kHudSortKey);
    canvas.add_solid_rect(cx_bar, 6.0F, 300.0F, 1.0F, r2d::ColorRgba8 { 75U, 130U, 210U, 255U }, kHudSortKey);
    add_clipped_pixel_text(canvas, cx_bar + 10.0F, 15.0F, stage_lbl, 1.0F, r2d::ColorRgba8 { 135U, 210U, 255U, 255U }, cx_bar + 294.0F);
    cx_bar += 304.0F;

    if (draw_btn(cx_bar, 62.0F, "NEXT >", false, r2d::ColorRgba8 { 60U, 120U, 205U, 255U }))
    {
        ui.active_showcase_idx = (ui.active_showcase_idx + 1U) % kAllShowcaseSlugs.size();
        out_stage_changed = true;
    }
    cx_bar += 74.0F;

    if (draw_btn(cx_bar, 118.0F, ui.playing ? "PLAYING (SPACE)" : "PAUSED (SPACE)", ui.playing, r2d::ColorRgba8 { 46U, 165U, 88U, 255U }))
    {
        ui.playing = !ui.playing;
    }
    cx_bar += 124.0F;

    if (draw_btn(cx_bar, 96.0F, "HUD (TAB)", ui.show_hud, r2d::ColorRgba8 { 56U, 125U, 215U, 255U }))
    {
        ui.show_hud = !ui.show_hud;
    }
    cx_bar += 102.0F;

    if (draw_btn(cx_bar, 96.0F, "RESET CAM", false, r2d::ColorRgba8 { 56U, 125U, 215U, 255U }))
    {
        ui.cam_yaw   = 0.48F;
        ui.cam_pitch = 0.34F;
    }

    canvas.add_solid_rect(w - 172.0F, 6.0F, 164.0F, 26.0F, r2d::ColorRgba8 { 24U, 58U, 42U, 255U }, kHudSortKey);
    canvas.add_pixel_text(
        w - 162.0F,
        15.0F,
        hd2d_mode ? "VULKAN HD-2D HYBRID" : "VULKAN 3D PBR LIVE",
        1.0F,
        r2d::ColorRgba8 { 145U, 245U, 175U, 255U },
        kHudSortKey);

    // 7. Optional Bottom Telemetry Strip
    if (ui.show_hud)
    {
        const float strip_h = 36.0F;
        const float strip_y = h - strip_h - 10.0F;
        canvas.add_solid_rect(12.0F, strip_y, w - 24.0F, strip_h, r2d::ColorRgba8 { 14U, 18U, 26U, 228U }, kHudSortKey);
        canvas.add_solid_rect(12.0F, strip_y, 4.0F, strip_h, r2d::ColorRgba8 { 70U, 155U, 255U, 255U }, kHudSortKey);
        char line[196] {};
        std::snprintf(
            line,
            sizeof(line),
            "STAGE: %.*s  |  PASSES: %zu  |  ELEMENTS: %zu  |  SIM: %.2fS  |  RMB ORBIT  |  TAB TOGGLE HUD",
            static_cast<int>(stage.name().size()),
            stage.name().data(),
            telemetry.framegraph_passes_compiled,
            children.size(),
            static_cast<double>(ui.sim_time));
        add_clipped_pixel_text(canvas, 24.0F, strip_y + 14.0F, line, 1.0F, r2d::ColorRgba8 { 220U, 232U, 248U, 255U }, w - 24.0F);
    }
}

}  // namespace

PlayerApp::PlayerApp(PlayerConfig config)
    : config_(std::move(config))
{
}

int PlayerApp::run()
{
    if (config_.show_help)
    {
        return 0;
    }
    if (config_.has_error)
    {
        return config_.exit_code != 0 ? config_.exit_code : 1;
    }

    cd::audio::NativeBackendResult native_audio {};
    const bool use_stage2d = config_.uses_stage2d();
    cd::world2d::Stage2D stage2d {};
    if (use_stage2d)
    {
        if (!config_.pak_path.empty())
        {
            const auto pak_res = cd::app_common::read_binary_file(config_.pak_path);
            if (!pak_res.has_value())
            {
                std::fprintf(stderr, "[cd_player] Failed to open .cdpak archive: %s\n", config_.pak_path.c_str());
                return 2;
            }
            const auto lr = stage2d.load_from_pak_bytes(std::as_bytes(std::span(*pak_res)));
            if (!lr.has_value())
            {
                std::fprintf(
                    stderr,
                    "[cd_player] Invalid .cdpak stage: %.*s\n",
                    static_cast<int>(lr.error().message.size()),
                    lr.error().message.data());
                return 2;
            }
        }
        else if (!config_.stage2d_path.empty())
        {
            const auto lr = stage2d.load_from_file(config_.stage2d_path);
            if (!lr.has_value())
            {
                std::fprintf(
                    stderr,
                    "[cd_player] Invalid .cd2d.json stage: %.*s\n",
                    static_cast<int>(lr.error().message.size()),
                    lr.error().message.data());
                return 2;
            }
        }
        else
        {
            const auto gkind = cd::world2d::parse_genre_template_kind_2d(config_.genre)
                                   .value_or(cd::world2d::GenreTemplateKind2D::kActionPlatformer);
            cd::world2d::GenreTemplates2D::apply_genre_template(stage2d, gkind);
        }

        native_audio = cd::audio::make_native_audio_backend();
        stage2d.audio_cues().set_backend(native_audio.backend.get());

        if (config_.force_headless && !config_.force_windowed && config_.capture_png_path.empty())
        {
            const std::uint32_t sim_frames = config_.frames_to_run > 0U ? config_.frames_to_run : 60U;
            const auto rep = cd::world2d::run_playtest_2d(stage2d, sim_frames, true, false);
            const std::string st_name(stage2d.name());
            std::printf(
                "[cd_player] Verified 2D stage '%s' headless (%u frames, gems=%d/%d, score=%d, victory=%d)\n",
                st_name.c_str(),
                rep.frames_simulated,
                rep.gems_collected,
                rep.total_gems,
                rep.score,
                rep.victory ? 1 : 0);
            std::fflush(stdout);
            stage2d.audio_cues().set_backend(nullptr);
            return 0;
        }
        stage2d.enter_play();
    }

    cd::runtime::EngineContextConfig ctx_cfg {};
    ctx_cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx(ctx_cfg);

    // Fast headless verification path for --all-showcases or --smoke-frames / --null
    if (!use_stage2d && (config_.run_all_showcases || (config_.force_headless && !config_.force_windowed && config_.capture_png_path.empty())))
    {
        const std::uint32_t headless_frames = config_.frames_to_run > 0U ? config_.frames_to_run : 5U;
        cd::rhi::NullDevice dev;
        cd::rhi::TextureDesc back_desc {};
        back_desc.extent = { 1280U, 720U, 1U };
        back_desc.format = cd::rhi::Format::kRGBA8Unorm;
        back_desc.usage  = cd::rhi::TextureUsage::kColorAttachment;
        auto back_tex = dev.create_texture(back_desc);
        const auto back_handle = back_tex.value_or(cd::rhi::TextureHandle {});

        if (config_.run_all_showcases)
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

        return run_showcase_slug_headless(ctx, dev, back_handle, config_.showcase_slug, headless_frames);
    }

    std::uint32_t frames_to_run = config_.frames_to_run;
    if (!config_.capture_png_path.empty() && frames_to_run == 0U)
    {
        frames_to_run = 4U;
    }

    std::unique_ptr<cd::platform::IWindow> window;
    std::unique_ptr<cd::rhi::IDevice>      device;
    std::optional<cd::render::Renderer>    renderer;
    bool using_null = config_.force_headless && !config_.force_windowed;

    if (!using_null)
    {
        const std::string win_title = use_stage2d
            ? ("CHROMODYNAMIC - " + std::string(stage2d.name()))
            : std::string("CHROMODYNAMIC - Standalone Stage Player");
        cd::platform::WindowDesc wd {};
        wd.title  = win_title.c_str();
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

    const auto starter_atlas = cd::render2d::TextureAtlas2D::generate_procedural_starter_atlas(32U);
    cd::render::GpuTexture2D atlas_gpu {};
    cd::rhi::SamplerHandle   atlas_sampler {};
    if (!using_null)
    {
        atlas_gpu = cd::render::upload_texture_2d_rgba8(
            *device,
            starter_atlas.pixels().data(),
            starter_atlas.width(),
            starter_atlas.height());

        cd::rhi::SamplerDesc smp_desc {};
        smp_desc.mag_filter  = cd::rhi::SamplerFilter::kNearest;
        smp_desc.min_filter  = cd::rhi::SamplerFilter::kNearest;
        smp_desc.mipmap_mode = cd::rhi::SamplerMipmapMode::kNearest;
        smp_desc.address_u   = cd::rhi::SamplerAddressMode::kClampToEdge;
        smp_desc.address_v   = cd::rhi::SamplerAddressMode::kClampToEdge;
        smp_desc.address_w   = cd::rhi::SamplerAddressMode::kClampToEdge;
        auto smp_r = device->create_sampler(smp_desc);
        if (smp_r.has_value())
        {
            atlas_sampler = *smp_r;
        }

        if (atlas_gpu.view.is_valid() && atlas_sampler.is_valid())
        {
            sci.atlas_view    = atlas_gpu.view;
            sci.atlas_sampler = atlas_sampler;
            if (use_stage2d)
            {
                stage2d.bind_starter_atlas(0U, 32U);
            }
        }
    }

    auto sub_r = urr::Submitter::create_with_inline_shader(*device, sci);
    if (!sub_r.has_value())
    {
        std::fprintf(stderr, "[cd_player] Submitter creation failed.\n");
        return 2;
    }
    auto& submitter = *sub_r;
    if (atlas_gpu.view.is_valid() && atlas_sampler.is_valid())
    {
        submitter.set_texture_slot(0U, atlas_gpu.view, atlas_sampler);
    }

    const bool is_hd2d_mode = (config_.stage_mode == "HD2D" || config_.stage_mode == "hd2d");
    ShowcaseRenderer3D showcase_3d {};
    if (!using_null && !use_stage2d && window)
    {
        (void)showcase_3d.init(
            *device,
            cd::rhi::Format::kBGRA8Unorm,
            { std::max(1U, window->width()), std::max(1U, window->height()) },
            atlas_gpu.view,
            atlas_sampler,
            starter_atlas);
    }

    PlayerUiState ui {};
    for (std::size_t idx = 0U; idx < kAllShowcaseSlugs.size(); ++idx)
    {
        if (kAllShowcaseSlugs.at(idx) == config_.showcase_slug)
        {
            ui.active_showcase_idx = idx;
            break;
        }
    }

    cd::runtime::EngineLoop loop(ctx, 1.0F / 60.0F);
    loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs.at(ui.active_showcase_idx)));

    cd::rhi::NullDevice sim_null_dev;
    cd::rhi::TextureDesc back_desc {};
    back_desc.extent = { 1280U, 720U, 1U };
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.usage  = cd::rhi::TextureUsage::kColorAttachment;
    auto back_tex = sim_null_dev.create_texture(back_desc);
    const auto back_handle = back_tex.value_or(cd::rhi::TextureHandle {});

    ur::DrawBatcher batcher;
    r2d::Canvas2D   canvas2d;
    r2d::Canvas2D   hud_canvas;
    bool key_2d_left             = false;
    bool key_2d_right            = false;
    bool key_2d_up               = false;
    bool key_2d_down             = false;
    bool key_2d_jump             = false;
    bool key_2d_jump_pressed     = false;
    bool key_2d_fire_pressed     = false;
    bool key_2d_fire_held        = false;
    bool key_2d_interact_pressed = false;
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64U);

    std::uint32_t frame_idx = 0U;
    const std::uint32_t capture_target_frame = (frames_to_run > 0U ? frames_to_run : 3U);

    while (true)
    {
        if (frames_to_run > 0U && frame_idx >= frames_to_run)
        {
            break;
        }

        ui.lmb_pressed          = false;
        ui.prev_mouse_x         = ui.mouse_x;
        ui.prev_mouse_y         = ui.mouse_y;
        key_2d_jump_pressed     = false;
        key_2d_fire_pressed     = false;
        key_2d_interact_pressed = false;

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
                if (ev.kind == cd::platform::OSEventKind::kResize)
                {
                    if (renderer && ev.width > 0 && ev.height > 0)
                    {
                        (void)renderer->recreate_swapchain({ ev.width, ev.height });
                    }
                    continue;
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
                        ui.lmb_pressed      = true;
                        key_2d_fire_pressed = true;
                        key_2d_fire_held    = true;
                    }
                    else if (ev.mouse_button == cd::platform::MouseButton::kRight)
                    {
                        ui.rmb_down = true;
                    }
                }
                else if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp)
                {
                    if (ev.mouse_button == cd::platform::MouseButton::kLeft)
                    {
                        key_2d_fire_held = false;
                    }
                    else if (ev.mouse_button == cd::platform::MouseButton::kRight)
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
                    if (ev.key == cd::platform::KeyCode::kA || ev.key == cd::platform::KeyCode::kLeft)
                    {
                        key_2d_left = true;
                    }
                    if (ev.key == cd::platform::KeyCode::kD || ev.key == cd::platform::KeyCode::kRight)
                    {
                        key_2d_right = true;
                    }
                    if (ev.key == cd::platform::KeyCode::kW || ev.key == cd::platform::KeyCode::kUp)
                    {
                        key_2d_up = true;
                        if (!key_2d_jump)
                        {
                            key_2d_jump_pressed = true;
                        }
                        key_2d_jump = true;
                    }
                    if (ev.key == cd::platform::KeyCode::kS || ev.key == cd::platform::KeyCode::kDown)
                    {
                        key_2d_down = true;
                    }
                    if (ev.key == cd::platform::KeyCode::kSpace)
                    {
                        if (!key_2d_jump)
                        {
                            key_2d_jump_pressed = true;
                        }
                        key_2d_jump         = true;
                        key_2d_fire_pressed = true;
                        key_2d_fire_held    = true;
                    }
                    if (ev.key == cd::platform::KeyCode::kE || ev.key == cd::platform::KeyCode::kF)
                    {
                        key_2d_interact_pressed = true;
                    }
                    if (!use_stage2d)
                    {
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
                            loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs.at(ui.active_showcase_idx)));
                        }
                        else if (ev.key == cd::platform::KeyCode::kLeft)
                        {
                            ui.active_showcase_idx = (ui.active_showcase_idx + kAllShowcaseSlugs.size() - 1U) % kAllShowcaseSlugs.size();
                            loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs.at(ui.active_showcase_idx)));
                        }
                    }
                    else if (ev.key == cd::platform::KeyCode::kTab)
                    {
                        ui.show_hud = !ui.show_hud;
                    }
                    else if (ev.key == cd::platform::KeyCode::kR)
                    {
                        stage2d.exit_play();
                        stage2d.enter_play();
                    }
                }
                else if (ev.kind == cd::platform::OSEventKind::kKeyUp)
                {
                    if (ev.key == cd::platform::KeyCode::kA || ev.key == cd::platform::KeyCode::kLeft)
                    {
                        key_2d_left = false;
                    }
                    if (ev.key == cd::platform::KeyCode::kD || ev.key == cd::platform::KeyCode::kRight)
                    {
                        key_2d_right = false;
                    }
                    if (ev.key == cd::platform::KeyCode::kW || ev.key == cd::platform::KeyCode::kUp)
                    {
                        key_2d_up   = false;
                        key_2d_jump = false;
                    }
                    if (ev.key == cd::platform::KeyCode::kS || ev.key == cd::platform::KeyCode::kDown)
                    {
                        key_2d_down = false;
                    }
                    if (ev.key == cd::platform::KeyCode::kSpace)
                    {
                        key_2d_jump      = false;
                        key_2d_fire_held = false;
                    }
                }
            }
            if (close_requested)
            {
                break;
            }
        }

        const std::uint32_t fb_w = window ? std::max(1U, window->width()) : 1280U;
        const std::uint32_t fb_h = window ? std::max(1U, window->height()) : 720U;

        batcher.begin_frame();
        if (use_stage2d)
        {
            cd::world2d::PlayerInput2D p2d {};
            if (config_.autopilot_2d)
            {
                p2d = cd::world2d::compute_autopilot_input_2d(stage2d);
            }
            else
            {
                p2d.move_x           = (key_2d_right ? 1.0F : 0.0F) - (key_2d_left ? 1.0F : 0.0F);
                p2d.move_y           = (key_2d_up ? 1.0F : 0.0F) - (key_2d_down ? 1.0F : 0.0F);
                p2d.jump_held        = key_2d_jump;
                p2d.jump_pressed     = key_2d_jump_pressed;
                p2d.fire_pressed     = key_2d_fire_pressed;
                p2d.fire_held        = key_2d_fire_held;
                p2d.interact_pressed = key_2d_interact_pressed;

                const float aim_dx   = ui.mouse_x - (static_cast<float>(fb_w) * 0.5F);
                const float aim_dy   = (static_cast<float>(fb_h) * 0.5F) - ui.mouse_y;
                p2d.aim_angle_rad    = std::atan2(aim_dy, aim_dx);
            }
            stage2d.step_play(1.0F / 60.0F, p2d);

            r2d::Render2DDebugOptions dbg2d {};
            dbg2d.show_grid         = false;
            dbg2d.show_camera_frame = false;
            dbg2d.show_light_gizmos = false;
            const r2d::ViewportRect2D vp2d { 0.0F, 0.0F, static_cast<float>(fb_w), static_cast<float>(fb_h) };
            stage2d.render(canvas2d, vp2d, dbg2d);
            canvas2d.replay_into<ur::DrawBatcher, ur::Color, ur::ScissorRect, ur::AtlasUv>(batcher);

            if (ui.show_hud)
            {
                hud_canvas.clear(vp2d);
                const auto& h2d = stage2d.hud();
                const auto ruleset = stage2d.arcade_ruleset();
                if (ruleset == cd::world2d::ArcadeRuleset2D::kPongCourt)
                {
                    const float hud_w = std::min(static_cast<float>(fb_w) - 32.0F, 520.0F);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, hud_w, 32.0F, r2d::ColorRgba8 { 10U, 15U, 26U, 224U }, kHudSortKey);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, 4.0F, 32.0F, r2d::ColorRgba8 { 32U, 232U, 255U, 255U }, kHudSortKey);
                    char hud_line[256] {};
                    std::snprintf(
                        hud_line,
                        sizeof(hud_line),
                        "NEON CYBER PONG  |  RALLY %d  |  SCORE %d  |  W/S MOVE  |  R REMATCH",
                        h2d.rally_count,
                        h2d.score);
                    add_clipped_pixel_text(
                        hud_canvas,
                        28.0F,
                        24.0F,
                        hud_line,
                        1.0F,
                        r2d::ColorRgba8 { 225U, 244U, 255U, 255U },
                        16.0F + hud_w - 10.0F);

                    // Center-court digital arcade scoreboard
                    const float sb_w = 186.0F;
                    const float sb_x = static_cast<float>(fb_w) * 0.5F - sb_w * 0.5F;
                    hud_canvas.add_solid_rect(sb_x, 12.0F, sb_w, 42.0F, r2d::ColorRgba8 { 10U, 14U, 25U, 232U }, kHudSortKey);
                    hud_canvas.add_rect_outline(sb_x, 12.0F, sb_w, 42.0F, 2.0F, r2d::ColorRgba8 { 65U, 215U, 255U, 220U }, kHudSortKey);
                    char score_str[64] {};
                    std::snprintf(score_str, sizeof(score_str), "P1 %d : %d CPU", h2d.gems_collected, h2d.opponent_score);
                    add_clipped_pixel_text(
                        hud_canvas,
                        sb_x + 18.0F,
                        24.0F,
                        score_str,
                        1.5F,
                        r2d::ColorRgba8 { 255U, 240U, 160U, 255U },
                        sb_x + sb_w - 8.0F);

                    if (h2d.victory || (h2d.total_gems > 0 && h2d.opponent_score >= h2d.total_gems))
                    {
                        const float bw = 420.0F;
                        const float bx = static_cast<float>(fb_w) * 0.5F - bw * 0.5F;
                        const float by = static_cast<float>(fb_h) * 0.5F - 24.0F;
                        hud_canvas.add_solid_rect(bx, by, bw, 46.0F, r2d::ColorRgba8 { 12U, 20U, 36U, 240U }, kHudSortKey);
                        hud_canvas.add_rect_outline(bx, by, bw, 46.0F, 2.0F, r2d::ColorRgba8 { 75U, 245U, 180U, 255U }, kHudSortKey);
                        add_clipped_pixel_text(
                            hud_canvas,
                            bx + 22.0F,
                            by + 16.0F,
                            h2d.victory ? "PLAYER 1 WINS THE MATCH!  (PRESS R FOR REMATCH)"
                                        : "CPU WINS THE MATCH!  (PRESS R FOR REMATCH)",
                            1.2F,
                            r2d::ColorRgba8 { 110U, 255U, 195U, 255U },
                            bx + bw - 12.0F);
                    }
                }
                else if (ruleset == cd::world2d::ArcadeRuleset2D::kBridgeDemolition)
                {
                    const float hud_w = std::min(static_cast<float>(fb_w) - 32.0F, 790.0F);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, hud_w, 34.0F, r2d::ColorRgba8 { 14U, 16U, 26U, 228U }, kHudSortKey);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, 4.0F, 34.0F, r2d::ColorRgba8 { 255U, 168U, 48U, 255U }, kHudSortKey);
                    char hud_line[256] {};
                    std::snprintf(
                        hud_line,
                        sizeof(hud_line),
                        "BREAK THE BRIDGES  |  SPANS SHATTERED %d/%d  |  SCORE %d  |  COMBO %d  |  A/D MOVE STRIKER  |  R RESET%s",
                        h2d.gems_collected,
                        h2d.total_gems,
                        h2d.score,
                        h2d.rally_count,
                        h2d.victory ? "  |  CANYON CLEARED!" : "");
                    add_clipped_pixel_text(
                        hud_canvas,
                        28.0F,
                        25.0F,
                        hud_line,
                        1.0F,
                        r2d::ColorRgba8 { 255U, 238U, 210U, 255U },
                        16.0F + hud_w - 10.0F);
                }
                else if (stage2d.entities().empty())
                {
                    const float hud_w = std::min(static_cast<float>(fb_w) - 32.0F, 680.0F);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, hud_w, 34.0F, r2d::ColorRgba8 { 12U, 17U, 26U, 224U }, kHudSortKey);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, 4.0F, 34.0F, r2d::ColorRgba8 { 110U, 205U, 255U, 255U }, kHudSortKey);
                    add_clipped_pixel_text(
                        hud_canvas,
                        28.0F,
                        25.0F,
                        "CHROMODYNAMIC EMPTY 2D PROJECT  |  0 ENTITIES  |  READY FOR AUTHORING IN STUDIO",
                        1.0F,
                        r2d::ColorRgba8 { 225U, 240U, 255U, 255U },
                        16.0F + hud_w - 10.0F);
                }
                else
                {
                    const float hud_w = std::min(static_cast<float>(fb_w) - 32.0F, 680.0F);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, hud_w, 34.0F, r2d::ColorRgba8 { 12U, 17U, 26U, 224U }, kHudSortKey);
                    hud_canvas.add_solid_rect(16.0F, 14.0F, 4.0F, 34.0F, r2d::ColorRgba8 { 65U, 235U, 165U, 255U }, kHudSortKey);
                    const std::string st_name(stage2d.name());
                    char hud_line[256] {};
                    std::snprintf(
                        hud_line,
                        sizeof(hud_line),
                        "CHROMODYNAMIC 2D PLAYER  |  %s  |  HP %d  |  SCORE %d  |  GEMS %d/%d%s",
                        st_name.c_str(),
                        h2d.player_hp,
                        h2d.score,
                        h2d.gems_collected,
                        h2d.total_gems,
                        h2d.victory ? "  |  VICTORY!" : "");
                    add_clipped_pixel_text(
                        hud_canvas,
                        28.0F,
                        25.0F,
                        hud_line,
                        1.0F,
                        r2d::ColorRgba8 { 230U, 242U, 255U, 255U },
                        16.0F + hud_w - 10.0F);
                }
                hud_canvas.replay_into<ur::DrawBatcher, ur::Color, ur::ScissorRect, ur::AtlasUv>(batcher);
            }
        }
        else
        {
            if (ui.rmb_down)
            {
                const float dx = ui.mouse_x - ui.prev_mouse_x;
                const float dy = ui.mouse_y - ui.prev_mouse_y;
                ui.cam_yaw += dx * 0.0075F;
                ui.cam_pitch = std::clamp(ui.cam_pitch + dy * 0.0065F, 0.08F, 1.15F);
            }

            if (ui.playing)
            {
                ui.sim_time += (1.0F / 60.0F);
            }
            const auto telemetry = loop.step_frame(1.0F / 60.0F, &sim_null_dev, back_handle);

            bool stage_changed = false;
            render_showcase_hud_canvas(
                hud_canvas,
                fb_w,
                fb_h,
                ui,
                loop.stage(),
                telemetry,
                showcase_3d.is_valid(),
                is_hd2d_mode,
                stage_changed);
            hud_canvas.replay_into<ur::DrawBatcher, ur::Color, ur::ScissorRect, ur::AtlasUv>(batcher);
            if (stage_changed)
            {
                loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(kAllShowcaseSlugs.at(ui.active_showcase_idx)));
            }
        }

        (void)submitter.upload(batcher);

        const bool capture_this_frame = (!config_.capture_png_path.empty() && frame_idx + 1U == capture_target_frame);
        bool captured_via_gpu         = false;

        if (!using_null && renderer)
        {
            auto frame_r = renderer->begin_frame();
            if (!frame_r.has_value() &&
                frame_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                if (window && window->width() > 0 && window->height() > 0)
                {
                    (void)renderer->recreate_swapchain({ window->width(), window->height() });
                }
            }
            else if (frame_r.has_value())
            {
                auto& frame = *frame_r;
                auto& cmd   = *frame.command_buffer;

                bool rendered_3d_pass = false;
                if (!use_stage2d && showcase_3d.is_valid())
                {
                    const ShowcaseCamera3D cam3d {
                        .yaw_rad   = ui.cam_yaw,
                        .pitch_rad = ui.cam_pitch,
                        .distance  = 11.2F,
                        .sim_time  = ui.sim_time,
                    };
                    rendered_3d_pass = showcase_3d.record(
                        cmd, frame.swapchain_image_view, frame.extent, loop.stage(), cam3d, is_hd2d_mode);
                }

                std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach { cd::rhi::ColorAttachmentInfo {
                    .view        = frame.swapchain_image_view,
                    .load_op     = rendered_3d_pass ? cd::rhi::LoadOp::kLoad : cd::rhi::LoadOp::kClear,
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

                cd::app_common::SwapchainReadbackState readback {};
                if (capture_this_frame)
                {
                    (void)cd::app_common::schedule_swapchain_readback(
                        *device, cmd, frame.swapchain_image, frame.extent, readback);
                }

                auto end_r = renderer->end_frame();
                if (!end_r.has_value() &&
                    end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
                {
                    if (window && window->width() > 0 && window->height() > 0)
                    {
                        (void)renderer->recreate_swapchain({ window->width(), window->height() });
                    }
                }

                if (readback.armed)
                {
                    device->wait_idle();
                    if (cd::app_common::finish_swapchain_readback_to_png(*device, readback, config_.capture_png_path).has_value())
                    {
                        std::printf(
                            "[cd_player] captured GPU swapchain PNG (%ux%u) -> %s\n",
                            frame.extent.width,
                            frame.extent.height,
                            config_.capture_png_path.c_str());
                        std::fflush(stdout);
                        captured_via_gpu = true;
                    }
                }
            }
        }

        if (capture_this_frame && !captured_via_gpu)
        {
            if (cd::app_common::rasterize_draw_batcher_to_png(
                    batcher, fb_w, fb_h, { 12U, 16U, 24U, 255U }, config_.capture_png_path)
                    .has_value())
            {
                std::printf(
                    "[cd_player] captured frame PNG (%ux%u) -> %s\n",
                    fb_w,
                    fb_h,
                    config_.capture_png_path.c_str());
                std::fflush(stdout);
            }
        }

        ++frame_idx;
    }

    if (device)
    {
        device->wait_idle();
        showcase_3d.destroy();
        submitter.destroy();
        if (atlas_sampler.is_valid())
        {
            device->destroy_sampler(atlas_sampler);
        }
        if (atlas_gpu.view.is_valid())
        {
            device->destroy_texture_view(atlas_gpu.view);
        }
        if (atlas_gpu.image.is_valid())
        {
            device->destroy_texture(atlas_gpu.image);
        }
    }
    if (use_stage2d)
    {
        stage2d.audio_cues().set_backend(nullptr);
    }
    const std::string_view reported_stage_name = use_stage2d ? stage2d.name() : loop.stage().name();
    const std::size_t reported_elements = use_stage2d ? stage2d.entities().size() : loop.stage().root().children().size();
    std::printf(
        "[cd_player] Completed %u frames of stage '%.*s' (elements=%zu, vertices=%zu)\n",
        frame_idx,
        static_cast<int>(reported_stage_name.size()),
        reported_stage_name.data(),
        reported_elements,
        batcher.vertex_count());
    return 0;
}

}  // namespace cd::player
