// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer/DrawBatcher.cpp
//
// CPU-side batcher impl. See header for shape and contract.
//
// Index format: u16. kMaxVertices = 65532 (largest multiple-of-4 that fits
// safely in a uint16_t index, avoiding GPU sentinel 0xFFFF).  Once the
// limit is reached, emit_quad() silently drops the quad and
// at_vertex_limit() returns true.  No auto-split: callers must
// begin_frame() and re-submit.  16K quads covers any realistic Phase 1 UI.
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::ui::renderer
{

void DrawBatcher::begin_frame()
{
    vertices_.clear();
    indices_.clear();
    commands_.clear();
    scissor_stack_.clear();
}

std::uint32_t DrawBatcher::push_scissor(const ScissorRect& s)
{
    scissor_stack_.push_back(s);
    return static_cast<std::uint32_t>(scissor_stack_.size());
}

void DrawBatcher::pop_scissor()
{
    if (!scissor_stack_.empty())
    {
        scissor_stack_.pop_back();
    }
}

ScissorRect DrawBatcher::current_scissor() const noexcept
{
    if (scissor_stack_.empty())
    {
        return ScissorRect {};  // default = "no clip"
    }
    return scissor_stack_.back();
}

namespace
{
[[nodiscard]] bool same_command(const DrawCommand& a, const DrawCommand& b) noexcept
{
    return a.variant == b.variant &&
           a.texture_slot == b.texture_slot &&
           a.scissor.x == b.scissor.x &&
           a.scissor.y == b.scissor.y &&
           a.scissor.width  == b.scissor.width &&
           a.scissor.height == b.scissor.height;
}
}  // namespace

void DrawBatcher::emit_quad_ex(float x, float y, float w, float h,
                                std::uint8_t variant,
                                std::uint32_t texture_slot,
                                const AtlasUv& uv,
                                Color c0, Color c1, Color c2, Color c3,
                                std::uint8_t pad0,
                                std::uint8_t pad1,
                                std::uint8_t pad2)
{
    // Reject empty quads up-front so the batch + index buffers stay tight.
    if (w <= 0.0F || h <= 0.0F) return;

    // Guard: u16 indices cannot address more than kMaxVertices vertices.
    // No auto-split is performed (see README / kMaxVertices rationale);
    // the caller must begin_frame() and re-emit when this limit is reached.
    if (vertices_.size() >= static_cast<std::size_t>(kMaxVertices)) return;

    const auto base = static_cast<std::uint16_t>(vertices_.size());
    // top-left, top-right, bottom-right, bottom-left
    Vertex v0 { x,     y,     uv.u0, uv.v0, c0.r, c0.g, c0.b, c0.a, variant, pad0, pad1, pad2 };
    Vertex v1 { x + w, y,     uv.u1, uv.v0, c1.r, c1.g, c1.b, c1.a, variant, pad0, pad1, pad2 };
    Vertex v2 { x + w, y + h, uv.u1, uv.v1, c2.r, c2.g, c2.b, c2.a, variant, pad0, pad1, pad2 };
    Vertex v3 { x,     y + h, uv.u0, uv.v1, c3.r, c3.g, c3.b, c3.a, variant, pad0, pad1, pad2 };
    vertices_.emplace_back(v0);
    vertices_.emplace_back(v1);
    vertices_.emplace_back(v2);
    vertices_.emplace_back(v3);

    const std::uint16_t i0 = base;
    const auto i1 = static_cast<std::uint16_t>(base + 1U);
    const auto i2 = static_cast<std::uint16_t>(base + 2U);
    const auto i3 = static_cast<std::uint16_t>(base + 3U);
    indices_.push_back(i0);
    indices_.push_back(i1);
    indices_.push_back(i2);
    indices_.push_back(i0);
    indices_.push_back(i2);
    indices_.push_back(i3);

    DrawCommand prospective {};
    prospective.variant      = variant;
    prospective.texture_slot = texture_slot;
    prospective.scissor      = current_scissor();
    prospective.index_offset = static_cast<std::uint32_t>(indices_.size() - 6U);
    prospective.index_count  = 6U;

    if (!commands_.empty() && same_command(commands_.back(), prospective))
    {
        // Merge: extend the previous command's index_count.
        commands_.back().index_count += 6U;
    }
    else
    {
        commands_.push_back(prospective);
    }
}

void DrawBatcher::emit_quad(float x, float y, float w, float h,
                             std::uint8_t variant,
                             std::uint32_t texture_slot,
                             const AtlasUv& uv,
                             Color color)
{
    emit_quad_ex(x, y, w, h, variant, texture_slot, uv, color, color, color, color);
}

void DrawBatcher::quad(float x, float y, float w, float h, Color c)
{
    AtlasUv unused {};
    emit_quad(x, y, w, h, material::kSolid, 0xFFFFFFFFu, unused, c);
}

void DrawBatcher::triangle_grad(float x0, float y0, Color c0,
                                float x1, float y1, Color c1,
                                float x2, float y2, Color c2)
{
    if (vertices_.size() + 3U > static_cast<std::size_t>(kMaxVertices)) return;

    const auto base = static_cast<std::uint16_t>(vertices_.size());
    vertices_.emplace_back(Vertex { x0, y0, 0.0F, 0.0F, c0.r, c0.g, c0.b, c0.a, material::kSolid, 0U, 0U, 0U });
    vertices_.emplace_back(Vertex { x1, y1, 0.0F, 0.0F, c1.r, c1.g, c1.b, c1.a, material::kSolid, 0U, 0U, 0U });
    vertices_.emplace_back(Vertex { x2, y2, 0.0F, 0.0F, c2.r, c2.g, c2.b, c2.a, material::kSolid, 0U, 0U, 0U });

    indices_.push_back(base);
    indices_.push_back(static_cast<std::uint16_t>(base + 1U));
    indices_.push_back(static_cast<std::uint16_t>(base + 2U));

    DrawCommand prospective {};
    prospective.variant      = material::kSolid;
    prospective.texture_slot = 0xFFFFFFFFu;
    prospective.scissor      = current_scissor();
    prospective.index_offset = static_cast<std::uint32_t>(indices_.size() - 3U);
    prospective.index_count  = 3U;

    if (!commands_.empty() && same_command(commands_.back(), prospective))
    {
        commands_.back().index_count += 3U;
    }
    else
    {
        commands_.push_back(prospective);
    }
}

void DrawBatcher::triangle(float x0, float y0,
                           float x1, float y1,
                           float x2, float y2,
                           Color c)
{
    triangle_grad(x0, y0, c, x1, y1, c, x2, y2, c);
}

void DrawBatcher::quad_poly(float x0, float y0,
                            float x1, float y1,
                            float x2, float y2,
                            float x3, float y3,
                            Color c)
{
    if (vertices_.size() + 4U > static_cast<std::size_t>(kMaxVertices)) return;

    const auto base = static_cast<std::uint16_t>(vertices_.size());
    vertices_.emplace_back(Vertex { x0, y0, 0.0F, 0.0F, c.r, c.g, c.b, c.a, material::kSolid, 0U, 0U, 0U });
    vertices_.emplace_back(Vertex { x1, y1, 0.0F, 0.0F, c.r, c.g, c.b, c.a, material::kSolid, 0U, 0U, 0U });
    vertices_.emplace_back(Vertex { x2, y2, 0.0F, 0.0F, c.r, c.g, c.b, c.a, material::kSolid, 0U, 0U, 0U });
    vertices_.emplace_back(Vertex { x3, y3, 0.0F, 0.0F, c.r, c.g, c.b, c.a, material::kSolid, 0U, 0U, 0U });

    indices_.push_back(base);
    indices_.push_back(static_cast<std::uint16_t>(base + 1U));
    indices_.push_back(static_cast<std::uint16_t>(base + 2U));
    indices_.push_back(base);
    indices_.push_back(static_cast<std::uint16_t>(base + 2U));
    indices_.push_back(static_cast<std::uint16_t>(base + 3U));

    DrawCommand prospective {};
    prospective.variant      = material::kSolid;
    prospective.texture_slot = 0xFFFFFFFFu;
    prospective.scissor      = current_scissor();
    prospective.index_offset = static_cast<std::uint32_t>(indices_.size() - 6U);
    prospective.index_count  = 6U;

    if (!commands_.empty() && same_command(commands_.back(), prospective))
    {
        commands_.back().index_count += 6U;
    }
    else
    {
        commands_.push_back(prospective);
    }
}

void DrawBatcher::textured_quad(float x, float y, float w, float h,
                                std::uint32_t texture_slot,
                                const AtlasUv& uv,
                                Color tint)
{
    emit_quad(x, y, w, h, material::kTextured, texture_slot, uv, tint);
}

void DrawBatcher::glyph(float x, float y, float w, float h,
                        std::uint32_t texture_slot,
                        const AtlasUv& uv,
                        Color tint)
{
    emit_quad(x, y, w, h, material::kGlyph, texture_slot, uv, tint);
}

void DrawBatcher::nine_patch(float x, float y, float w, float h,
                             std::uint32_t texture_slot,
                             const NinePatchDesc& desc,
                             Color tint)
{
    if (w <= 0.0F || h <= 0.0F) return;

    const float l = std::clamp(desc.left,   0.0F, w * 0.5F);
    const float r = std::clamp(desc.right,  0.0F, w * 0.5F);
    const float t = std::clamp(desc.top,    0.0F, h * 0.5F);
    const float b = std::clamp(desc.bottom, 0.0F, h * 0.5F);

    const float x0 = x;
    const float x1 = x + l;
    const float x2 = x + w - r;
    const float x3 = x + w;

    const float y0 = y;
    const float y1 = y + t;
    const float y2 = y + h - b;
    const float y3 = y + h;

    const float du = desc.outer_uv.u1 - desc.outer_uv.u0;
    const float dv = desc.outer_uv.v1 - desc.outer_uv.v0;

    const float u0 = desc.outer_uv.u0;
    const float u1 = desc.outer_uv.u0 + du * std::clamp(desc.uv_left,   0.0F, 0.5F);
    const float u2 = desc.outer_uv.u1 - du * std::clamp(desc.uv_right,  0.0F, 0.5F);
    const float u3 = desc.outer_uv.u1;

    const float v0 = desc.outer_uv.v0;
    const float v1 = desc.outer_uv.v0 + dv * std::clamp(desc.uv_top,    0.0F, 0.5F);
    const float v2 = desc.outer_uv.v1 - dv * std::clamp(desc.uv_bottom, 0.0F, 0.5F);
    const float v3 = desc.outer_uv.v1;

    const float xs[4] = { x0, x1, x2, x3 };
    const float ys[4] = { y0, y1, y2, y3 };
    const float us[4] = { u0, u1, u2, u3 };
    const float vs[4] = { v0, v1, v2, v3 };

    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            const float cw = xs[col + 1] - xs[col];
            const float ch = ys[row + 1] - ys[row];
            if (cw > 0.0F && ch > 0.0F)
            {
                const AtlasUv slice_uv { us[col], vs[row], us[col + 1], vs[row + 1] };
                emit_quad(xs[col], ys[row], cw, ch, material::kNinePatch, texture_slot, slice_uv, tint);
            }
        }
    }
}

void DrawBatcher::linear_gradient(float x, float y, float w, float h,
                                  Color start_color,
                                  Color end_color,
                                  bool horizontal)
{
    const AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    if (horizontal)
    {
        emit_quad_ex(x, y, w, h, material::kLinearGrad, 0xFFFFFFFFu, uv,
                     start_color, end_color, end_color, start_color, 1U, 0U, 0U);
    }
    else
    {
        emit_quad_ex(x, y, w, h, material::kLinearGrad, 0xFFFFFFFFu, uv,
                     start_color, start_color, end_color, end_color, 0U, 0U, 0U);
    }
}

void DrawBatcher::backdrop_blur(float x, float y, float w, float h,
                                float blur_radius,
                                Color tint)
{
    const AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    const auto r_byte = static_cast<std::uint8_t>(std::clamp(blur_radius, 0.0F, 255.0F));
    emit_quad_ex(x, y, w, h, material::kBlur, 0xFFFFFFFFu, uv,
                 tint, tint, tint, tint, r_byte, 0U, 0U);
}

void DrawBatcher::drop_shadow(float x, float y, float w, float h,
                              float blur_radius,
                              float spread,
                              Color shadow_color)
{
    if (w <= 0.0F || h <= 0.0F) return;
    const float expand = std::max(0.0F, blur_radius + spread);
    const AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    const auto r_byte = static_cast<std::uint8_t>(std::clamp(blur_radius, 0.0F, 255.0F));
    const auto s_byte = static_cast<std::uint8_t>(std::clamp(spread, 0.0F, 255.0F));
    emit_quad_ex(x - expand, y - expand, w + 2.0F * expand, h + 2.0F * expand,
                 material::kShadow, 0xFFFFFFFFu, uv,
                 shadow_color, shadow_color, shadow_color, shadow_color,
                 r_byte, s_byte, 0U);
}

void DrawBatcher::rounded_rect(float x, float y, float w, float h,
                               float corner_radius,
                               Color c,
                               float border_thickness)
{
    const AtlasUv uv { 0.0F, 0.0F, 1.0F, 1.0F };
    const auto r_byte = static_cast<std::uint8_t>(std::clamp(corner_radius, 0.0F, 255.0F));
    const auto b_byte = static_cast<std::uint8_t>(std::clamp(border_thickness, 0.0F, 255.0F));
    emit_quad_ex(x, y, w, h, material::kRoundedRect, 0xFFFFFFFFu, uv,
                 c, c, c, c, r_byte, b_byte, 0U);
}

}  // namespace cd::ui::renderer

