// =============================================================================
// CHROMODYNAMIC — cd/render2d/SpriteBatch2D.hpp
// FAZ 34 (ADR-20261111) — Sprite2D, FlipbookAnimator2D, NineSlice2D, Crisp 5x7
// Vector/Pixel Font & RHI-Agnostic Canvas2D Command Stream.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/render2d/Camera2D.hpp>
#include <cd/render2d/Lighting2D.hpp>
#include <cd/render2d/SortKey2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::render2d
{

struct UvRect2D
{
    float u0 { 0.0F };
    float v0 { 0.0F };
    float u1 { 1.0F };
    float v1 { 1.0F };
};

enum class SpriteVisualStyle : std::uint8_t
{
    kFlatQuad       = 0,
    kCharacterHero  = 1,
    kCollectibleGem = 2,
    kJumpPad        = 3,
    kPatrolDrone    = 4,
    kMovingPlatform = 5,
    kGoalPortal     = 6,
    kCrate          = 7,
};

struct Sprite2D
{
    cd::math::Vec2f   position { 0.0F, 0.0F };
    cd::math::Vec2f   size { 1.0F, 1.0F };
    cd::math::Vec2f   pivot { 0.5F, 0.5F };
    float             rotation_rad { 0.0F };
    UvRect2D          uv {};
    ColorRgba8        tint { 255U, 255U, 255U, 255U };
    std::uint32_t     texture_slot { 0xFFFFFFFFU };
    std::uint8_t      layer { 100U };
    std::int16_t      z_index { 0 };
    bool              y_sort { true };
    bool              flip_x { false };
    bool              flip_y { false };
    bool              lit { true };
    SpriteVisualStyle style { SpriteVisualStyle::kFlatQuad };
    std::uint32_t     anim_frame_index { 0U };
    std::uint32_t     normal_texture_slot { 0xFFFFFFFFU };
    std::uint32_t     emissive_texture_slot { 0xFFFFFFFFU };
    ColorRgba8        emissive_tint { 0U, 0U, 0U, 255U };
    float             emissive_intensity { 0.0F };
    cd::math::Vec3f   surface_normal { 0.0F, 0.0F, 1.0F };
};

struct FlipbookFrame2D
{
    UvRect2D uv {};
    float    duration_sec { 0.10F };
};

struct FlipbookClip2D
{
    std::string                  name {};
    std::vector<FlipbookFrame2D> frames {};
    bool                         loop { true };
};

class FlipbookAnimator2D
{
public:
    void add_clip(FlipbookClip2D clip)
    {
        for (auto& existing : clips_)
        {
            if (existing.name == clip.name)
            {
                existing = std::move(clip);
                return;
            }
        }
        clips_.push_back(std::move(clip));
    }

    bool play(std::string_view clip_name, bool restart_if_same = false) noexcept
    {
        for (std::size_t i = 0U; i < clips_.size(); ++i)
        {
            if (clips_.at(i).name == clip_name)
            {
                if (active_clip_ != static_cast<int>(i) || restart_if_same)
                {
                    active_clip_  = static_cast<int>(i);
                    active_frame_ = 0U;
                    elapsed_      = 0.0F;
                }
                return true;
            }
        }
        return false;
    }

    void step(float dt) noexcept
    {
        if (dt <= 0.0F || active_clip_ < 0 || static_cast<std::size_t>(active_clip_) >= clips_.size())
        {
            return;
        }
        const auto& clip = clips_.at(static_cast<std::size_t>(active_clip_));
        if (clip.frames.empty())
        {
            return;
        }

        elapsed_ += dt;
        while (active_frame_ < clip.frames.size())
        {
            const float dur = std::max(0.005F, clip.frames.at(active_frame_).duration_sec);
            if (elapsed_ < dur)
            {
                break;
            }
            elapsed_ -= dur;
            if (active_frame_ + 1U < clip.frames.size())
            {
                ++active_frame_;
            }
            else if (clip.loop)
            {
                active_frame_ = 0U;
            }
            else
            {
                elapsed_ = 0.0F;
                break;
            }
        }
    }

    [[nodiscard]] UvRect2D current_uv() const noexcept
    {
        if (active_clip_ < 0 || static_cast<std::size_t>(active_clip_) >= clips_.size())
        {
            return {};
        }
        const auto& clip = clips_.at(static_cast<std::size_t>(active_clip_));
        if (clip.frames.empty())
        {
            return {};
        }
        return clip.frames.at(std::min<std::size_t>(active_frame_, clip.frames.size() - 1U)).uv;
    }

    [[nodiscard]] std::uint32_t current_frame_index() const noexcept { return active_frame_; }

    [[nodiscard]] std::string_view active_clip_name() const noexcept
    {
        if (active_clip_ < 0 || static_cast<std::size_t>(active_clip_) >= clips_.size())
        {
            return {};
        }
        return clips_.at(static_cast<std::size_t>(active_clip_)).name;
    }

    [[nodiscard]] std::span<const FlipbookClip2D> clips() const noexcept { return clips_; }

private:
    std::vector<FlipbookClip2D> clips_ {};
    int                         active_clip_ { -1 };
    std::uint32_t               active_frame_ { 0U };
    float                       elapsed_ { 0.0F };
};

enum class CanvasCmdKind2D : std::uint8_t
{
    kSolidQuad    = 0,
    kGradientQuad = 1,
    kTexturedQuad = 2,
    kQuadPoly     = 3,
    kTriangleGrad = 4,
};

struct CanvasCmd2D
{
    CanvasCmdKind2D kind { CanvasCmdKind2D::kSolidQuad };
    std::uint64_t   sort_key { 0ULL };
    float           x0 { 0.0F };
    float           y0 { 0.0F };
    float           x1 { 0.0F };
    float           y1 { 0.0F };
    float           x2 { 0.0F };
    float           y2 { 0.0F };
    float           x3 { 0.0F };
    float           y3 { 0.0F };
    UvRect2D        uv {};
    std::uint32_t   texture_slot { 0xFFFFFFFFU };
    ColorRgba8      c0 { 255U, 255U, 255U, 255U };
    ColorRgba8      c1 { 255U, 255U, 255U, 255U };
    ColorRgba8      c2 { 255U, 255U, 255U, 255U };
    bool            horizontal_grad { false };
};

/// RHI-independent 2D command buffer that sorts by 64-bit SortKey2D and replays
/// into any GPU/CPU DrawBatcher (`cd::ui::renderer::DrawBatcher`).
class Canvas2D
{
public:
    void clear(ViewportRect2D viewport = {}) noexcept
    {
        commands_.clear();
        viewport_ = viewport;
    }

    [[nodiscard]] constexpr const ViewportRect2D& viewport() const noexcept { return viewport_; }
    [[nodiscard]] std::span<const CanvasCmd2D> commands() const noexcept { return commands_; }

    void add_solid_rect(float x, float y, float w, float h, ColorRgba8 c, std::uint64_t key)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
            w <= 0.0F || h <= 0.0F || c.a == 0U)
        {
            return;
        }
        CanvasCmd2D cmd {};
        cmd.kind     = CanvasCmdKind2D::kSolidQuad;
        cmd.sort_key = key;
        cmd.x0       = x;
        cmd.y0       = y;
        cmd.x1       = w;
        cmd.y1       = h;
        cmd.c0       = c;
        commands_.push_back(cmd);
    }

    void add_gradient_rect(
        float x, float y, float w, float h,
        ColorRgba8 c_start, ColorRgba8 c_end,
        bool horizontal, std::uint64_t key)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
            w <= 0.0F || h <= 0.0F)
        {
            return;
        }
        CanvasCmd2D cmd {};
        cmd.kind            = CanvasCmdKind2D::kGradientQuad;
        cmd.sort_key        = key;
        cmd.x0              = x;
        cmd.y0              = y;
        cmd.x1              = w;
        cmd.y1              = h;
        cmd.c0              = c_start;
        cmd.c1              = c_end;
        cmd.horizontal_grad = horizontal;
        commands_.push_back(cmd);
    }

    void add_textured_rect(
        float x, float y, float w, float h,
        std::uint32_t tex_slot, const UvRect2D& uv, ColorRgba8 tint, std::uint64_t key)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
            w <= 0.0F || h <= 0.0F || tint.a == 0U)
        {
            return;
        }
        CanvasCmd2D cmd {};
        cmd.kind         = CanvasCmdKind2D::kTexturedQuad;
        cmd.sort_key     = key;
        cmd.x0           = x;
        cmd.y0           = y;
        cmd.x1           = w;
        cmd.y1           = h;
        cmd.uv           = uv;
        cmd.texture_slot = tex_slot;
        cmd.c0           = tint;
        commands_.push_back(cmd);
    }

    void add_line(
        float x0, float y0, float x1, float y1,
        float thickness, ColorRgba8 c, std::uint64_t key)
    {
        if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1) ||
            !std::isfinite(thickness) || thickness <= 0.0F || c.a == 0U)
        {
            return;
        }
        const float dx  = x1 - x0;
        const float dy  = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (!std::isfinite(len) || len <= 0.05F)
        {
            return;
        }
        const float nx = (-dy / len) * (thickness * 0.5F);
        const float ny = (dx / len) * (thickness * 0.5F);

        CanvasCmd2D cmd {};
        cmd.kind     = CanvasCmdKind2D::kQuadPoly;
        cmd.sort_key = key;
        cmd.x0       = x0 + nx;
        cmd.y0       = y0 + ny;
        cmd.x1       = x1 + nx;
        cmd.y1       = y1 + ny;
        cmd.x2       = x1 - nx;
        cmd.y2       = y1 - ny;
        cmd.x3       = x0 - nx;
        cmd.y3       = y0 - ny;
        cmd.c0       = c;
        commands_.push_back(cmd);
    }

    void add_rect_outline(
        float x, float y, float w, float h,
        float thickness, ColorRgba8 c, std::uint64_t key)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) ||
            !std::isfinite(thickness) || w <= 0.0F || h <= 0.0F || thickness <= 0.0F || c.a == 0U)
        {
            return;
        }
        add_solid_rect(x, y, w, thickness, c, key);
        add_solid_rect(x, y + h - thickness, w, thickness, c, key);
        add_solid_rect(x, y, thickness, h, c, key);
        add_solid_rect(x + w - thickness, y, thickness, h, c, key);
    }

    void add_radial_glow(
        float cx, float cy, float radius_px,
        ColorRgba8 center_col, int segments, std::uint64_t key)
    {
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius_px) ||
            radius_px <= 1.0F || center_col.a == 0U)
        {
            return;
        }
        const int   n     = std::clamp(segments, 8, 36);
        const ColorRgba8 outer_col { center_col.r, center_col.g, center_col.b, 0U };
        constexpr float  kTwoPi = 6.283185307F;
        for (int i = 0; i < n; ++i)
        {
            const float a0 = (static_cast<float>(i) / static_cast<float>(n)) * kTwoPi;
            const float a1 = (static_cast<float>(i + 1) / static_cast<float>(n)) * kTwoPi;
            CanvasCmd2D cmd {};
            cmd.kind     = CanvasCmdKind2D::kTriangleGrad;
            cmd.sort_key = key;
            cmd.x0       = cx;
            cmd.y0       = cy;
            cmd.x1       = cx + std::cos(a0) * radius_px;
            cmd.y1       = cy + std::sin(a0) * radius_px;
            cmd.x2       = cx + std::cos(a1) * radius_px;
            cmd.y2       = cy + std::sin(a1) * radius_px;
            cmd.c0       = center_col;
            cmd.c1       = outer_col;
            cmd.c2       = outer_col;
            commands_.push_back(cmd);
        }
    }

    /// Built-in crisp 5x7 pixel-font text emitter (works without external font atlas).
    void add_pixel_text(
        float x, float y, std::string_view text,
        float pixel_scale, ColorRgba8 color, std::uint64_t key);

    void sort_commands();

    /// Emits all sorted commands into `cd::ui::renderer::DrawBatcher` (or any compatible batcher)
    /// clipped to `viewport_`.
    template <typename BatcherT, typename ColorT, typename ScissorT, typename UvT>
    void replay_into(BatcherT& batcher) const
    {
        if (viewport_.width <= 1.0F || viewport_.height <= 1.0F)
        {
            return;
        }
        ScissorT sc {};
        sc.x      = static_cast<std::int32_t>( std::max(0.0F, std::floor(viewport_.x)) );
        sc.y      = static_cast<std::int32_t>( std::max(0.0F, std::floor(viewport_.y)) );
        sc.width  = static_cast<std::uint32_t>( std::max(1.0F, std::ceil(viewport_.width)) );
        sc.height = static_cast<std::uint32_t>( std::max(1.0F, std::ceil(viewport_.height)) );
        (void)batcher.push_scissor(sc);

        const auto to_col = [](ColorRgba8 c) constexpr noexcept -> ColorT {
            return ColorT { c.r, c.g, c.b, c.a };
        };

        for (const auto& cmd : commands_)
        {
            switch (cmd.kind)
            {
                case CanvasCmdKind2D::kSolidQuad:
                    batcher.quad(cmd.x0, cmd.y0, cmd.x1, cmd.y1, to_col(cmd.c0));
                    break;
                case CanvasCmdKind2D::kGradientQuad:
                    batcher.linear_gradient(
                        cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                        to_col(cmd.c0), to_col(cmd.c1), cmd.horizontal_grad);
                    break;
                case CanvasCmdKind2D::kTexturedQuad:
                {
                    UvT uv { cmd.uv.u0, cmd.uv.v0, cmd.uv.u1, cmd.uv.v1 };
                    batcher.textured_quad(cmd.x0, cmd.y0, cmd.x1, cmd.y1, cmd.texture_slot, uv, to_col(cmd.c0));
                    break;
                }
                case CanvasCmdKind2D::kQuadPoly:
                    batcher.quad_poly(
                        cmd.x0, cmd.y0, cmd.x1, cmd.y1,
                        cmd.x2, cmd.y2, cmd.x3, cmd.y3,
                        to_col(cmd.c0));
                    break;
                case CanvasCmdKind2D::kTriangleGrad:
                    batcher.triangle_grad(
                        cmd.x0, cmd.y0, to_col(cmd.c0),
                        cmd.x1, cmd.y1, to_col(cmd.c1),
                        cmd.x2, cmd.y2, to_col(cmd.c2));
                    break;
            }
        }

        batcher.pop_scissor();
    }

private:
    ViewportRect2D           viewport_ {};
    std::vector<CanvasCmd2D> commands_ {};
};

/// Emits a stylized or textured `Sprite2D` (with frustum culling + 2D dynamic lighting) into `Canvas2D`.
void emit_sprite_2d(
    Canvas2D&                    canvas,
    const Camera2D&              camera,
    const LightingEnvironment2D& lighting,
    const Sprite2D&              sprite);

}  // namespace cd::render2d
