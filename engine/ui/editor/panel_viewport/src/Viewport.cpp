// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_viewport/src/Viewport.cpp
//
// phase546 — cd::editor::panel::viewport  implementation
// =============================================================================
#include <cd/editor/panel_viewport/Viewport.hpp>

#include <cstdint>

namespace cd::editor::panel::viewport
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void Viewport::set_scene_texture(cd::rhi::TextureHandle handle) noexcept
{
    scene_texture_ = handle;
}

cd::rhi::TextureHandle Viewport::current_texture() const noexcept
{
    return scene_texture_;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void Viewport::draw(cd::ui::renderer::DrawBatcher& batcher,
                    const cd::ui::widgets::Theme&  theme,
                    const cd::ui::widgets::Rect&   bounds) const
{
    // Background fill — always emitted so the tile is visible even when no
    // texture has been bound (matches the original draw_viewport_stub intent:
    // background colour rather than surface, making the 3D area visually
    // distinct from the surrounding panel chrome).
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.background.r,
                     theme.background.g,
                     theme.background.b,
                     theme.background.a });

    if (!bounds.is_valid())
        return;

    // If a valid texture handle is bound, overlay a kTextured quad covering
    // the full bounds. UV 0..1 spans the entire panel so the scene colour
    // target is displayed at native panel resolution.
    if (scene_texture_.is_valid())
    {
        // The batcher's texture_slot is a 32-bit opaque index. The lower
        // 32 bits of the handle raw value carry the resource index — the
        // same convention used by cd::ui::renderer_rhi::Submitter when it
        // resolves descriptor slots.
        const auto raw_slot =
            static_cast<std::uint32_t>(scene_texture_.value() & 0xFFFFFFFFu);

        static constexpr cd::ui::renderer::AtlasUv kFullUv { 0.0F, 0.0F, 1.0F, 1.0F };

        batcher.textured_quad(bounds.x, bounds.y, bounds.w, bounds.h,
                              raw_slot,
                              kFullUv,
                              cd::ui::renderer::Color::white());
    }
}

}  // namespace cd::editor::panel::viewport
