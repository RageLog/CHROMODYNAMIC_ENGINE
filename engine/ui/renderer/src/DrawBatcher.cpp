// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer/DrawBatcher.cpp
//
// CPU-side batcher impl. See header for shape and contract.
//
// Index format: u16. A single batch supports up to 65535 vertices; once
// reached, callers should `begin_frame()` again and submit (Phase 2 will
// auto-split). 16K vertices = 4K quads which is more than enough for
// any reasonable Phase 1 UI page.
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

ScissorRect DrawBatcher::current_scissor_() const noexcept
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

void DrawBatcher::emit_quad_(float x, float y, float w, float h,
                             std::uint8_t variant,
                             std::uint32_t texture_slot,
                             const AtlasUv& uv,
                             Color color)
{
    // Reject empty quads up-front so the batch + index buffers stay tight.
    if (w <= 0.0F || h <= 0.0F) return;

    const std::uint16_t base = static_cast<std::uint16_t>(vertices_.size());
    // top-left, top-right, bottom-right, bottom-left
    Vertex v0 { x,     y,     uv.u0, uv.v0, color.r, color.g, color.b, color.a, variant, 0U, 0U, 0U };
    Vertex v1 { x + w, y,     uv.u1, uv.v0, color.r, color.g, color.b, color.a, variant, 0U, 0U, 0U };
    Vertex v2 { x + w, y + h, uv.u1, uv.v1, color.r, color.g, color.b, color.a, variant, 0U, 0U, 0U };
    Vertex v3 { x,     y + h, uv.u0, uv.v1, color.r, color.g, color.b, color.a, variant, 0U, 0U, 0U };
    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);
    vertices_.push_back(v3);

    const std::uint16_t i0 = base;
    const std::uint16_t i1 = static_cast<std::uint16_t>(base + 1U);
    const std::uint16_t i2 = static_cast<std::uint16_t>(base + 2U);
    const std::uint16_t i3 = static_cast<std::uint16_t>(base + 3U);
    indices_.push_back(i0);
    indices_.push_back(i1);
    indices_.push_back(i2);
    indices_.push_back(i0);
    indices_.push_back(i2);
    indices_.push_back(i3);

    DrawCommand prospective {};
    prospective.variant      = variant;
    prospective.texture_slot = texture_slot;
    prospective.scissor      = current_scissor_();
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

void DrawBatcher::quad(float x, float y, float w, float h, Color c)
{
    AtlasUv unused {};
    emit_quad_(x, y, w, h, material::kSolid, 0xFFFFFFFFu, unused, c);
}

void DrawBatcher::textured_quad(float x, float y, float w, float h,
                                std::uint32_t texture_slot,
                                const AtlasUv& uv,
                                Color tint)
{
    emit_quad_(x, y, w, h, material::kTextured, texture_slot, uv, tint);
}

void DrawBatcher::glyph(float x, float y, float w, float h,
                        std::uint32_t texture_slot,
                        const AtlasUv& uv,
                        Color tint)
{
    emit_quad_(x, y, w, h, material::kGlyph, texture_slot, uv, tint);
}

}  // namespace cd::ui::renderer
