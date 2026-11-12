// =============================================================================
// CHROMODYNAMIC — cd/render2d/SpriteBatch2D.cpp
// FAZ 34 (ADR-20261111) — Stylized & Textured Sprite2D Emission, Frustum
// Culling, 64-Bit Sort & Built-in 5x7 Pixel Font.
// =============================================================================
#include <cd/render2d/SpriteBatch2D.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace cd::render2d
{

namespace
{

[[nodiscard]] constexpr std::array<std::uint8_t, 7> glyph_5x7(char c) noexcept
{
    if (c >= 'a' && c <= 'z')
    {
        c = static_cast<char>(c - ('a' - 'A'));
    }
    switch (c)
    {
        case 'A': return { 0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U };
        case 'B': return { 0x1EU, 0x11U, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU };
        case 'C': return { 0x0EU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0EU };
        case 'D': return { 0x1CU, 0x12U, 0x11U, 0x11U, 0x11U, 0x12U, 0x1CU };
        case 'E': return { 0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x1FU };
        case 'F': return { 0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U };
        case 'G': return { 0x0EU, 0x11U, 0x10U, 0x17U, 0x11U, 0x11U, 0x0FU };
        case 'H': return { 0x11U, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U };
        case 'I': return { 0x0EU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU };
        case 'J': return { 0x07U, 0x02U, 0x02U, 0x02U, 0x12U, 0x12U, 0x0CU };
        case 'K': return { 0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U };
        case 'L': return { 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1FU };
        case 'M': return { 0x11U, 0x1BU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U };
        case 'N': return { 0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U, 0x11U };
        case 'O': return { 0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU };
        case 'P': return { 0x1EU, 0x11U, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U };
        case 'Q': return { 0x0EU, 0x11U, 0x11U, 0x11U, 0x15U, 0x12U, 0x0DU };
        case 'R': return { 0x1EU, 0x11U, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U };
        case 'S': return { 0x0FU, 0x10U, 0x10U, 0x0EU, 0x01U, 0x01U, 0x1EU };
        case 'T': return { 0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U };
        case 'U': return { 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU };
        case 'V': return { 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U };
        case 'W': return { 0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x1BU, 0x11U };
        case 'X': return { 0x11U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U, 0x11U };
        case 'Y': return { 0x11U, 0x11U, 0x0AU, 0x04U, 0x04U, 0x04U, 0x04U };
        case 'Z': return { 0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x1FU };
        case '0': return { 0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU };
        case '1': return { 0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU };
        case '2': return { 0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU };
        case '3': return { 0x1FU, 0x02U, 0x04U, 0x02U, 0x01U, 0x11U, 0x0EU };
        case '4': return { 0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U };
        case '5': return { 0x1FU, 0x10U, 0x1EU, 0x01U, 0x01U, 0x11U, 0x0EU };
        case '6': return { 0x06U, 0x08U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU };
        case '7': return { 0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U };
        case '8': return { 0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU };
        case '9': return { 0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x02U, 0x0CU };
        case ':': return { 0x00U, 0x04U, 0x04U, 0x00U, 0x04U, 0x04U, 0x00U };
        case '.': return { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x0CU };
        case ',': return { 0x00U, 0x00U, 0x00U, 0x00U, 0x04U, 0x04U, 0x08U };
        case '-': return { 0x00U, 0x00U, 0x00U, 0x1FU, 0x00U, 0x00U, 0x00U };
        case '_': return { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1FU };
        case '+': return { 0x00U, 0x04U, 0x04U, 0x1FU, 0x04U, 0x04U, 0x00U };
        case '/': return { 0x01U, 0x02U, 0x02U, 0x04U, 0x08U, 0x08U, 0x10U };
        case '[': return { 0x0EU, 0x08U, 0x08U, 0x08U, 0x08U, 0x08U, 0x0EU };
        case ']': return { 0x0EU, 0x02U, 0x02U, 0x02U, 0x02U, 0x02U, 0x0EU };
        case '|': return { 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U };
        case '(': return { 0x02U, 0x04U, 0x08U, 0x08U, 0x08U, 0x04U, 0x02U };
        case ')': return { 0x08U, 0x04U, 0x02U, 0x02U, 0x02U, 0x04U, 0x08U };
        case '<': return { 0x02U, 0x04U, 0x08U, 0x10U, 0x08U, 0x04U, 0x02U };
        case '>': return { 0x08U, 0x04U, 0x02U, 0x01U, 0x02U, 0x04U, 0x08U };
        case '=': return { 0x00U, 0x00U, 0x1FU, 0x00U, 0x1FU, 0x00U, 0x00U };
        case '%': return { 0x19U, 0x19U, 0x02U, 0x04U, 0x08U, 0x13U, 0x13U };
        case '#': return { 0x0AU, 0x1FU, 0x0AU, 0x0AU, 0x1FU, 0x0AU, 0x00U };
        case '?': return { 0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x00U, 0x04U };
        case '!': return { 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x00U, 0x04U };
        case '*': return { 0x00U, 0x04U, 0x15U, 0x0EU, 0x15U, 0x04U, 0x00U };
        case '&': return { 0x0CU, 0x12U, 0x14U, 0x08U, 0x15U, 0x12U, 0x0DU };
        case '\'': return { 0x04U, 0x04U, 0x08U, 0x00U, 0x00U, 0x00U, 0x00U };
        default:  return { 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U };
    }
}

[[nodiscard]] ColorRgba8 scale_rgb(ColorRgba8 c, float f) noexcept
{
    return ColorRgba8::from_floats(
        (static_cast<float>(c.r) / 255.0F) * f,
        (static_cast<float>(c.g) / 255.0F) * f,
        (static_cast<float>(c.b) / 255.0F) * f,
        static_cast<float>(c.a) / 255.0F);
}

}  // namespace

void Canvas2D::add_pixel_text(
    float x, float y, std::string_view text,
    float pixel_scale, ColorRgba8 color, std::uint64_t key)
{
    const float px     = std::max(1.0F, pixel_scale);
    float       cursor = x;
    for (const char ch : text)
    {
        if (ch == ' ')
        {
            cursor += 4.0F * px;
            continue;
        }
        auto rows = glyph_5x7(ch);
        for (std::size_t r = 0U; r < rows.size(); ++r)
        {
            const std::uint8_t row_bits = rows.at(r);
            if (row_bits == 0U)
            {
                continue;
            }
            int col = 0;
            while (col < 5)
            {
                if (((row_bits >> (4 - col)) & 1U) != 0U)
                {
                    int col_end = col + 1;
                    while (col_end < 5 && ((row_bits >> (4 - col_end)) & 1U) != 0U)
                    {
                        ++col_end;
                    }
                    std::uint8_t run_mask = 0U;
                    for (int k = col; k < col_end; ++k)
                    {
                        run_mask = static_cast<std::uint8_t>(run_mask | (1U << (4 - k)));
                    }
                    std::size_t r_end = r + 1U;
                    while (r_end < rows.size() && (rows.at(r_end) & run_mask) == run_mask)
                    {
                        rows.at(r_end) = static_cast<std::uint8_t>(rows.at(r_end) & ~run_mask);
                        ++r_end;
                    }
                    add_solid_rect(
                        cursor + static_cast<float>(col) * px,
                        y + static_cast<float>(r) * px,
                        static_cast<float>(col_end - col) * px,
                        static_cast<float>(r_end - r) * px,
                        color,
                        key);
                    col = col_end;
                }
                else
                {
                    ++col;
                }
            }
        }
        cursor += 6.0F * px;
    }
}

void Canvas2D::sort_commands()
{
    std::stable_sort(commands_.begin(), commands_.end(), [](const CanvasCmd2D& a, const CanvasCmd2D& b) noexcept {
        return a.sort_key < b.sort_key;
    });
}

void emit_sprite_2d(
    Canvas2D&                    canvas,
    const Camera2D&              camera,
    const LightingEnvironment2D& lighting,
    const Sprite2D&              sprite)
{
    if (sprite.size.x <= 0.0F || sprite.size.y <= 0.0F || sprite.tint.a == 0U)
    {
        return;
    }

    const float min_wx = sprite.position.x - sprite.size.x * sprite.pivot.x;
    const float max_wx = min_wx + sprite.size.x;
    const float min_wy = sprite.position.y - sprite.size.y * sprite.pivot.y;
    const float max_wy = min_wy + sprite.size.y;

    const Aabb2D sprite_bounds { .min = { min_wx, min_wy }, .max = { max_wx, max_wy } };
    const Aabb2D cam_bounds = camera.visible_world_bounds(canvas.viewport(), 0.5F);
    if (!sprite_bounds.overlaps(cam_bounds))
    {
        return;
    }

    const cd::math::Vec2f top_left_px = camera.world_to_screen({ min_wx, max_wy }, canvas.viewport());
    const cd::math::Vec2f bot_right_px = camera.world_to_screen({ max_wx, min_wy }, canvas.viewport());
    const float           sx           = top_left_px.x;
    const float           sy           = top_left_px.y;
    const float           sw           = std::max(1.0F, bot_right_px.x - top_left_px.x);
    const float           sh           = std::max(1.0F, bot_right_px.y - top_left_px.y);

    const std::uint64_t base_key = make_sort_key_2d(
        sprite.layer,
        sprite.z_index,
        min_wy,
        sprite.y_sort,
        static_cast<std::uint16_t>(sprite.texture_slot & 0xFFFFU));

    cd::math::Vec3f eff_normal = sprite.surface_normal;
    if (sprite.flip_x)
    {
        eff_normal.x = -eff_normal.x;
    }
    const ColorRgba8 lit_color = sprite.lit
        ? evaluate_lighting_2d_with_emissive(
              lighting,
              sprite.position,
              eff_normal,
              sprite.tint,
              sprite.emissive_tint,
              sprite.emissive_intensity)
        : sprite.tint;

    if (sprite.texture_slot != 0xFFFFFFFFU)
    {
        UvRect2D uv = sprite.uv;
        if (sprite.flip_x) std::swap(uv.u0, uv.u1);
        if (sprite.flip_y) std::swap(uv.v0, uv.v1);
        canvas.add_textured_rect(sx, sy, sw, sh, sprite.texture_slot, uv, lit_color, base_key);
        return;
    }

    switch (sprite.style)
    {
        case SpriteVisualStyle::kCharacterHero:
        {
            // Cyber-suit 2D platformer hero with animated stride, visor, cape highlight & outline
            const float bob    = ((sprite.anim_frame_index % 2U) == 1U) ? (sh * 0.04F) : 0.0F;
            const float head_h = sh * 0.38F;
            const float body_h = sh * 0.42F;
            const float leg_h  = sh * 0.20F;
            const ColorRgba8 dark_trim = scale_rgb(lit_color, 0.42F);
            const ColorRgba8 visor_col { 115U, 245U, 255U, 255U };

            // Legs (animated step phase)
            const float leg_w   = sw * 0.28F;
            const float stride  = ((sprite.anim_frame_index % 4U) == 1U) ? (sw * 0.14F)
                                : (((sprite.anim_frame_index % 4U) == 3U) ? (-sw * 0.14F) : 0.0F);
            canvas.add_solid_rect(sx + sw * 0.18F + stride, sy + sh - leg_h, leg_w, leg_h, dark_trim, base_key);
            canvas.add_solid_rect(sx + sw * 0.54F - stride, sy + sh - leg_h, leg_w, leg_h, dark_trim, base_key);

            // Torso & armor highlight
            canvas.add_solid_rect(sx + sw * 0.12F, sy + head_h + bob, sw * 0.76F, body_h, lit_color, base_key + 1ULL);
            canvas.add_solid_rect(sx + sw * 0.24F, sy + head_h + bob + body_h * 0.18F, sw * 0.52F, body_h * 0.28F,
                                  scale_rgb(lit_color, 1.25F), base_key + 2ULL);

            // Helmet + glowing directional visor
            canvas.add_solid_rect(sx + sw * 0.14F, sy + bob, sw * 0.72F, head_h, scale_rgb(lit_color, 1.10F), base_key + 3ULL);
            const float visor_x = sprite.flip_x ? (sx + sw * 0.16F) : (sx + sw * 0.44F);
            canvas.add_solid_rect(visor_x, sy + bob + head_h * 0.28F, sw * 0.38F, head_h * 0.34F, visor_col, base_key + 4ULL);
            break;
        }
        case SpriteVisualStyle::kCollectibleGem:
        {
            const float cx = sx + sw * 0.5F;
            const float cy = sy + sh * 0.5F;
            canvas.add_radial_glow(cx, cy, std::max(sw, sh) * 0.95F,
                                   ColorRgba8 { sprite.tint.r, sprite.tint.g, sprite.tint.b, 85U }, 12, base_key);
            CanvasCmd2D diamond {};
            diamond.kind     = CanvasCmdKind2D::kQuadPoly;
            diamond.sort_key = base_key + 2ULL;
            diamond.x0       = cx;
            diamond.y0       = sy;
            diamond.x1       = sx + sw;
            diamond.y1       = cy;
            diamond.x2       = cx;
            diamond.y2       = sy + sh;
            diamond.x3       = sx;
            diamond.y3       = cy;
            diamond.c0       = sprite.tint;
            canvas.add_line(cx, sy + sh * 0.18F, cx, sy + sh * 0.82F, std::max(2.0F, sw * 0.26F),
                            ColorRgba8 { 255U, 255U, 255U, 220U }, base_key + 3ULL);
            canvas.add_solid_rect(sx + sw * 0.22F, sy + sh * 0.22F, sw * 0.56F, sh * 0.56F, sprite.tint, base_key + 1ULL);
            break;
        }
        case SpriteVisualStyle::kJumpPad:
        {
            canvas.add_solid_rect(sx, sy + sh * 0.52F, sw, sh * 0.48F, ColorRgba8 { 42U, 50U, 68U, 255U }, base_key);
            canvas.add_solid_rect(sx + sw * 0.08F, sy + sh * 0.18F, sw * 0.84F, sh * 0.36F, sprite.tint, base_key + 1ULL);
            canvas.add_solid_rect(sx + sw * 0.20F, sy, sw * 0.60F, sh * 0.18F,
                                  ColorRgba8 { 160U, 255U, 220U, 255U }, base_key + 2ULL);
            break;
        }
        case SpriteVisualStyle::kPatrolDrone:
        {
            canvas.add_radial_glow(sx + sw * 0.5F, sy + sh * 0.5F, sw * 0.85F,
                                   ColorRgba8 { 255U, 75U, 95U, 72U }, 12, base_key);
            canvas.add_solid_rect(sx + sw * 0.10F, sy + sh * 0.18F, sw * 0.80F, sh * 0.64F, lit_color, base_key + 1ULL);
            canvas.add_solid_rect(sx, sy + sh * 0.06F, sw * 0.32F, sh * 0.12F, ColorRgba8 { 180U, 200U, 230U, 255U }, base_key + 2ULL);
            canvas.add_solid_rect(sx + sw * 0.68F, sy + sh * 0.06F, sw * 0.32F, sh * 0.12F, ColorRgba8 { 180U, 200U, 230U, 255U }, base_key + 2ULL);
            canvas.add_solid_rect(sx + sw * 0.34F, sy + sh * 0.36F, sw * 0.32F, sh * 0.28F, ColorRgba8 { 255U, 230U, 90U, 255U }, base_key + 3ULL);
            break;
        }
        case SpriteVisualStyle::kMovingPlatform:
        {
            canvas.add_solid_rect(sx, sy, sw, sh, lit_color, base_key);
            canvas.add_solid_rect(sx, sy, sw, std::max(2.0F, sh * 0.26F),
                                  ColorRgba8 { 95U, 235U, 255U, 255U }, base_key + 1ULL);
            canvas.add_rect_outline(sx, sy, sw, sh, 1.5F, scale_rgb(lit_color, 0.55F), base_key + 2ULL);
            break;
        }
        case SpriteVisualStyle::kGoalPortal:
        {
            const float cx = sx + sw * 0.5F;
            const float cy = sy + sh * 0.5F;
            canvas.add_radial_glow(cx, cy, std::max(sw, sh) * 0.90F,
                                   ColorRgba8 { 90U, 240U, 255U, 110U }, 16, base_key);
            canvas.add_solid_rect(sx + sw * 0.12F, sy, sw * 0.76F, sh, lit_color, base_key + 1ULL);
            canvas.add_solid_rect(sx + sw * 0.26F, sy + sh * 0.12F, sw * 0.48F, sh * 0.76F,
                                  ColorRgba8 { 215U, 255U, 255U, 245U }, base_key + 2ULL);
            break;
        }
        case SpriteVisualStyle::kCrate:
        case SpriteVisualStyle::kFlatQuad:
        {
            canvas.add_solid_rect(sx, sy, sw, sh, lit_color, base_key);
            canvas.add_rect_outline(sx, sy, sw, sh, std::max(1.0F, std::min(sw, sh) * 0.07F),
                                    scale_rgb(lit_color, 0.62F), base_key + 1ULL);
            if (sprite.style == SpriteVisualStyle::kCrate)
            {
                canvas.add_line(sx + 2.0F, sy + 2.0F, sx + sw - 2.0F, sy + sh - 2.0F, 2.0F,
                                scale_rgb(lit_color, 0.72F), base_key + 2ULL);
            }
            break;
        }
    }
}

}  // namespace cd::render2d
