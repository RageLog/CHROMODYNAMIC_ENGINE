// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer/DrawBatcher.hpp
//
// Phase 1.2 of ADR-20260530-ui-widget-library. CPU-side batcher that
// converts high-level UI draw commands (Rect / TexturedRect / Glyph /
// NinePatch) into a flat vertex / index buffer pair ready for upload.
//
// The batcher knows nothing about RHI: callers (a future
// cd::ui_renderer_rhi or the editor binary) take the produced buffers
// and issue draws. This keeps the algorithm fully testable without
// launching a graphics device.
//
// Vertex layout (matches the renderer pipeline spec from the ADR):
//   pos2 + uv2 + color4 (RGBA8) + flags (uint8 -- material variant id)
//   = 4 + 4 + 4 + 4 = 16 bytes per vertex (std140-friendly, AVX-aligned).
//
// Material variant ids (Phase 1):
//   0 = kSolid       -- uv unused; fragment = color
//   1 = kTextured    -- fragment = texture(uv) * color (premultiplied alpha)
//   2 = kGlyph       -- fragment = color * texture(uv).r  (alpha-only atlas)
//   3 = kNinePatch   -- stretchable border tile; renderer must rebind atlas
//   4 = kLinearGrad  -- two-stop gradient between two corner colors
//   5 = kBlur        -- blur sample (reads a separately-bound source target)
//
// Scissor stack: pushed scissor rects clip subsequent emissions. The
// batcher tracks one rect per draw call and segments the index buffer
// at scissor boundaries -- callers iterate `commands()` to bind the
// right scissor + texture between draws.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::ui::renderer
{

// ---- Vertex / index ------------------------------------------------------

/// One UI vertex. 16 bytes total. Color is RGBA8, packed so the std140
/// layout in the shader is `vec2 + vec2 + uint8x4 + uint8x4`.
struct Vertex
{
    float         pos_x { 0.0F };
    float         pos_y { 0.0F };
    float         uv_x  { 0.0F };
    float         uv_y  { 0.0F };
    std::uint8_t  r     { 255U };
    std::uint8_t  g     { 255U };
    std::uint8_t  b     { 255U };
    std::uint8_t  a     { 255U };
    std::uint8_t  variant { 0U };  ///< material variant id (kSolid / kTextured / ...)
    std::uint8_t  pad0    { 0U };
    std::uint8_t  pad1    { 0U };
    std::uint8_t  pad2    { 0U };
};
static_assert(sizeof(Vertex) == 24U, "Vertex size invariant (pos2=8 + uv2=8 + color4=4 + variant+3pad=4)");

/// Material variant ids. See header comment.
namespace material
{
inline constexpr std::uint8_t kSolid       = 0U;
inline constexpr std::uint8_t kTextured    = 1U;
inline constexpr std::uint8_t kGlyph       = 2U;
inline constexpr std::uint8_t kNinePatch   = 3U;
inline constexpr std::uint8_t kLinearGrad  = 4U;
inline constexpr std::uint8_t kBlur        = 5U;
}  // namespace material

/// One scissor rectangle. Pixel coordinates relative to the framebuffer
/// origin (top-left). `clip_disabled` rect = entire framebuffer.
struct ScissorRect
{
    std::int32_t  x { 0 };
    std::int32_t  y { 0 };
    std::uint32_t width  { 0xFFFFFFFFu };  ///< 0xFFFFFFFFu = "no clip"
    std::uint32_t height { 0xFFFFFFFFu };
};

/// One draw command emitted by the batcher. The renderer (RHI side)
/// binds `scissor` + `texture_slot` + the global pipeline keyed by
/// `variant`, then issues a draw of `index_count` indices starting at
/// `index_offset` (into the batcher's flat index buffer).
struct DrawCommand
{
    std::uint32_t index_offset { 0U };
    std::uint32_t index_count  { 0U };
    std::uint8_t  variant      { material::kSolid };
    std::uint32_t texture_slot { 0xFFFFFFFFu };  ///< 0xFFFFFFFFu = no texture
    ScissorRect   scissor      {};
};

// ---- Color helper --------------------------------------------------------

struct Color
{
    std::uint8_t r { 255U };
    std::uint8_t g { 255U };
    std::uint8_t b { 255U };
    std::uint8_t a { 255U };

    [[nodiscard]] static constexpr Color white()       noexcept { return { 255U, 255U, 255U, 255U }; }
    [[nodiscard]] static constexpr Color black()       noexcept { return {   0U,   0U,   0U, 255U }; }
    [[nodiscard]] static constexpr Color transparent() noexcept { return {   0U,   0U,   0U,   0U }; }
};

// ---- Glyph / UV helper ---------------------------------------------------

/// Atlas UV bundle. Returned by cd::ui::font::Font::glyph_uv -- the
/// renderer batcher accepts the same shape so callers can pipe glyph
/// info straight in without translation.
struct AtlasUv
{
    float u0 { 0.0F };
    float v0 { 0.0F };
    float u1 { 1.0F };
    float v1 { 1.0F };
};

// ---- Batcher -------------------------------------------------------------

class DrawBatcher
{
public:
    DrawBatcher() = default;

    /// Reset all buffers. Call at the start of each frame.
    void begin_frame();

    /// Push a new scissor rect; subsequent emissions clip to it. Returns
    /// the depth of the stack after push so callers can verify.
    std::uint32_t push_scissor(const ScissorRect& s);

    /// Pop the top scissor. No-op when the stack is empty.
    void pop_scissor();

    /// Emit a solid-color filled rectangle.
    void quad(float x, float y, float w, float h, Color c);

    /// Emit a textured rectangle. `texture_slot` is opaque to the
    /// batcher; the RHI tier maps it to a bound descriptor index.
    void textured_quad(float x, float y, float w, float h,
                       std::uint32_t texture_slot,
                       const AtlasUv& uv,
                       Color tint = Color::white());

    /// Emit one glyph. `texture_slot` indexes the atlas the glyph was
    /// rasterized into (cd::ui::font::Font::atlas() upload).
    void glyph(float x, float y, float w, float h,
               std::uint32_t texture_slot,
               const AtlasUv& uv,
               Color tint = Color::white());

    /// Vertex buffer ready for upload.
    [[nodiscard]] std::span<const Vertex> vertices() const noexcept
    {
        return std::span<const Vertex>(vertices_.data(), vertices_.size());
    }

    /// Index buffer ready for upload.
    [[nodiscard]] std::span<const std::uint16_t> indices() const noexcept
    {
        return std::span<const std::uint16_t>(indices_.data(), indices_.size());
    }

    /// Draw commands in submission order. The renderer iterates these
    /// and binds scissor / texture / pipeline for each before draw_indexed.
    [[nodiscard]] std::span<const DrawCommand> commands() const noexcept
    {
        return std::span<const DrawCommand>(commands_.data(), commands_.size());
    }

    [[nodiscard]] std::size_t vertex_count() const noexcept { return vertices_.size(); }
    [[nodiscard]] std::size_t index_count()  const noexcept { return indices_.size(); }
    [[nodiscard]] std::size_t command_count() const noexcept { return commands_.size(); }

private:
    std::vector<Vertex>         vertices_;
    std::vector<std::uint16_t>  indices_;
    std::vector<DrawCommand>    commands_;
    std::vector<ScissorRect>    scissor_stack_;

    [[nodiscard]] ScissorRect current_scissor_() const noexcept;

    /// Append a quad of (v0, v1, v2, v3) winding 0-1-2-0-2-3 (two
    /// CCW triangles) and start / extend a draw command keyed by
    /// (variant, texture_slot, scissor). Same key -> merge into the
    /// previous command's index range; different key -> emit new.
    void emit_quad_(float x, float y, float w, float h,
                    std::uint8_t variant,
                    std::uint32_t texture_slot,
                    const AtlasUv& uv,
                    Color color);
};

}  // namespace cd::ui::renderer
