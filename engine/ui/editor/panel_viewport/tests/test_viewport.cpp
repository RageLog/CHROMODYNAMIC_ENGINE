// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_viewport/tests/test_viewport.cpp
//
// phase546 — unit tests for cd::editor::panel::viewport::Viewport.
//
// All tests are headless (no RHI device, no GPU). We verify:
//   * DefaultCtorHasNullTexture      — current_texture() is null by default.
//   * SetTexturePersists             — set/get round-trip works correctly.
//   * ClearTextureWithNull           — passing null handle clears the state.
//   * DrawNoTextureEmitsBackground   — draw() with null handle emits solid quad.
//   * DrawWithTextureEmitsMoreCmds   — draw() with valid handle emits textured quad.
//   * DrawZeroBoundsDoesNotCrash     — draw() on zero-size rect is safe.
//   * DrawOnlyBackgroundForInvalid   — bounds.is_valid()==false stops after bg.
// =============================================================================
#include <cd/editor/panel_viewport/Viewport.hpp>

#include <cd/core/Handle.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace vp = cd::editor::panel::viewport;

// ---------------------------------------------------------------------------
// TEST(ViewportPanel, DefaultCtorHasNullTexture)
// ---------------------------------------------------------------------------
TEST(ViewportPanel, DefaultCtorHasNullTexture)
{
    const vp::Viewport panel;
    EXPECT_FALSE(panel.current_texture().is_valid());
}

// ---------------------------------------------------------------------------
// TEST(ViewportPanel, SetTexturePersists)
// ---------------------------------------------------------------------------
TEST(ViewportPanel, SetTexturePersists)
{
    vp::Viewport panel;

    // Construct a non-null handle with index=1, generation=1.
    const cd::rhi::TextureHandle h { static_cast<cd::rhi::TextureHandle::index_type>(1),
                                     static_cast<cd::rhi::TextureHandle::generation_type>(1) };
    panel.set_scene_texture(h);
    EXPECT_TRUE(panel.current_texture().is_valid());
    EXPECT_EQ(panel.current_texture(), h);
}

// ---------------------------------------------------------------------------
// TEST(ViewportPanel, ClearTextureWithNull)
// ---------------------------------------------------------------------------
TEST(ViewportPanel, ClearTextureWithNull)
{
    vp::Viewport panel;
    const cd::rhi::TextureHandle h { static_cast<cd::rhi::TextureHandle::index_type>(2),
                                     static_cast<cd::rhi::TextureHandle::generation_type>(1) };
    panel.set_scene_texture(h);
    EXPECT_TRUE(panel.current_texture().is_valid());

    // Clear by passing the null (default-constructed) handle.
    panel.set_scene_texture(cd::rhi::TextureHandle {});
    EXPECT_FALSE(panel.current_texture().is_valid());
}

// ---------------------------------------------------------------------------
// TEST(ViewportPanel, DrawNoTextureEmitsBackground)
// ---------------------------------------------------------------------------
TEST(ViewportPanel, DrawNoTextureEmitsBackground)
{
    vp::Viewport panel;  // no texture set
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 800.0F, 600.0F };

    batcher.begin_frame();
    panel.draw(batcher, theme, bounds);

    // Must emit at least 1 command (background quad).
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
    // Should emit exactly 1 command (no texture = no textured_quad overlay).
    EXPECT_EQ(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(ViewportPanel, DrawWithTextureEmitsMoreCmds)
// ---------------------------------------------------------------------------
TEST(ViewportPanel, DrawWithTextureEmitsMoreCmds)
{
    vp::Viewport panel;
    const cd::rhi::TextureHandle h { static_cast<cd::rhi::TextureHandle::index_type>(5),
                                     static_cast<cd::rhi::TextureHandle::generation_type>(1) };
    panel.set_scene_texture(h);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 800.0F, 600.0F };

    batcher.begin_frame();
    panel.draw(batcher, theme, bounds);

    // Background (kSolid) + textured quad (kTextured) = 2 distinct commands
    // because they carry different variant ids so the batcher opens a new
    // command segment.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(2U));
    // Both quads together: 2 quads × 4 vertices = 8 vertices minimum.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// TEST(ViewportPanel, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(ViewportPanel, DrawZeroBoundsDoesNotCrash)
{
    vp::Viewport panel;
    const cd::rhi::TextureHandle h { static_cast<cd::rhi::TextureHandle::index_type>(3),
                                     static_cast<cd::rhi::TextureHandle::generation_type>(1) };
    panel.set_scene_texture(h);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero_bounds { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    // Must not crash — only the background quad is emitted.
    panel.draw(batcher, theme, zero_bounds);

    // bounds.is_valid() == false → returns after the background quad.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// TEST(ViewportPanel, DrawOnlyBackgroundForInvalidBounds)
//
// When bounds.is_valid() == false the panel must NOT emit the kTextured quad
// overlay — so vertex count after drawing must be strictly less than it would
// be if both quads were emitted (2 quads = 8 vertices minimum).
// ---------------------------------------------------------------------------
TEST(ViewportPanel, DrawOnlyBackgroundForInvalidBounds)
{
    vp::Viewport panel;
    const cd::rhi::TextureHandle h { static_cast<cd::rhi::TextureHandle::index_type>(7),
                                     static_cast<cd::rhi::TextureHandle::generation_type>(2) };
    panel.set_scene_texture(h);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    // Negative-size bounds — is_valid() returns false.
    const cd::ui::widgets::Rect   bad_bounds { 10.0F, 10.0F, -5.0F, -5.0F };

    batcher.begin_frame();
    panel.draw(batcher, theme, bad_bounds);

    // The textured_quad overlay must NOT be emitted (early-out after background).
    // Valid-bounds draw emits 2 quads = at least 8 vertices; invalid must be < 8.
    EXPECT_LT(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
