// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_debug_viz/tests/test_debug_viz_real_textures.cpp
//
// phase735 — unit tests for the Sprint-2 real G-buffer texture overlay path
// added to cd::editor::debug_viz::DebugVizOverlay.
//
// Verifies (10 cases):
//   1. DefaultTextureIsNull              — fresh overlay has a null texture.
//   2. SetTextureRoundTrip               — set_texture / current_texture pair.
//   3. SetDepthTextureMatchesKind        — kDepth setter applies on matching kind.
//   4. SetDepthTextureIgnoredWrongKind   — kDepth setter no-op on non-depth.
//   5. SetNormalTextureMatchesKind       — kNormal setter applies on matching kind.
//   6. SetNormalTextureIgnoredWrongKind  — kNormal setter no-op on non-normal.
//   7. SetAlbedoTextureRoutesToBucket    — kAlphaBucket gets the albedo binding.
//   8. SetAlbedoTextureIgnoredWrongKind  — albedo setter no-op on non-bucket.
//   9. DrawWithTextureEmitsTexturedCmd   — draw emits a kTextured DrawCommand.
//  10. ClearTextureFallsBackToGradient   — null handle drops the textured cmd.
// =============================================================================
#include <cd/editor/panel_debug_viz/DebugViz.hpp>

#include <cd/core/Handle.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

namespace dv = cd::editor::debug_viz;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::rhi::TextureHandle make_texture(std::uint32_t index,
                                                  std::uint32_t generation = 1U)
{
    return cd::rhi::TextureHandle {
        static_cast<cd::rhi::TextureHandle::index_type>(index),
        static_cast<cd::rhi::TextureHandle::generation_type>(generation)
    };
}

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 10.0F, 10.0F, 200.0F, 150.0F };
}

/// Count commands in the batcher whose material variant matches `variant`.
[[nodiscard]] std::size_t count_variant(const cd::ui::renderer::DrawBatcher& b,
                                        std::uint8_t variant) noexcept
{
    const auto cmds = b.commands();
    return static_cast<std::size_t>(std::count_if(
        cmds.begin(), cmds.end(),
        [variant](const cd::ui::renderer::DrawCommand& c) {
            return c.variant == variant;
        }));
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultTextureIsNull
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, DefaultTextureIsNull)
{
    const dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    EXPECT_FALSE(overlay.current_texture().is_valid());
}

// ---------------------------------------------------------------------------
// TEST 2 — SetTextureRoundTrip
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, SetTextureRoundTrip)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    const auto h = make_texture(11U, 3U);

    overlay.set_texture(h);
    EXPECT_TRUE(overlay.current_texture().is_valid());
    EXPECT_EQ(overlay.current_texture(), h);

    // Null clears.
    overlay.set_texture(cd::rhi::TextureHandle {});
    EXPECT_FALSE(overlay.current_texture().is_valid());
}

// ---------------------------------------------------------------------------
// TEST 3 — SetDepthTextureMatchesKind
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, SetDepthTextureMatchesKind)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    const auto h = make_texture(42U);

    overlay.set_depth_texture(h);
    EXPECT_EQ(overlay.current_texture(), h);
}

// ---------------------------------------------------------------------------
// TEST 4 — SetDepthTextureIgnoredWrongKind
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, SetDepthTextureIgnoredWrongKind)
{
    dv::DebugVizOverlay overlay_n { dv::VizKind::kNormal };
    dv::DebugVizOverlay overlay_b { dv::VizKind::kAlphaBucket };

    const auto h = make_texture(99U);
    overlay_n.set_depth_texture(h);
    overlay_b.set_depth_texture(h);

    EXPECT_FALSE(overlay_n.current_texture().is_valid());
    EXPECT_FALSE(overlay_b.current_texture().is_valid());
}

// ---------------------------------------------------------------------------
// TEST 5 — SetNormalTextureMatchesKind
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, SetNormalTextureMatchesKind)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kNormal };
    const auto h = make_texture(7U);

    overlay.set_normal_texture(h);
    EXPECT_EQ(overlay.current_texture(), h);
}

// ---------------------------------------------------------------------------
// TEST 6 — SetNormalTextureIgnoredWrongKind
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, SetNormalTextureIgnoredWrongKind)
{
    dv::DebugVizOverlay overlay_d { dv::VizKind::kDepth };
    dv::DebugVizOverlay overlay_b { dv::VizKind::kAlphaBucket };

    const auto h = make_texture(123U);
    overlay_d.set_normal_texture(h);
    overlay_b.set_normal_texture(h);

    EXPECT_FALSE(overlay_d.current_texture().is_valid());
    EXPECT_FALSE(overlay_b.current_texture().is_valid());
}

// ---------------------------------------------------------------------------
// TEST 7 — SetAlbedoTextureRoutesToBucket
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, SetAlbedoTextureRoutesToBucket)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kAlphaBucket };
    const auto h = make_texture(31U);

    overlay.set_albedo_texture(h);
    EXPECT_EQ(overlay.current_texture(), h);
}

// ---------------------------------------------------------------------------
// TEST 8 — SetAlbedoTextureIgnoredWrongKind
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, SetAlbedoTextureIgnoredWrongKind)
{
    dv::DebugVizOverlay overlay_d { dv::VizKind::kDepth };
    dv::DebugVizOverlay overlay_n { dv::VizKind::kNormal };

    const auto h = make_texture(64U);
    overlay_d.set_albedo_texture(h);
    overlay_n.set_albedo_texture(h);

    EXPECT_FALSE(overlay_d.current_texture().is_valid());
    EXPECT_FALSE(overlay_n.current_texture().is_valid());
}

// ---------------------------------------------------------------------------
// TEST 9 — DrawWithTextureEmitsTexturedCmd
//
// When a valid texture handle is bound, the overlay must emit at least one
// DrawCommand with material::kTextured. The texture_slot on that command
// must be the lower 32 bits of the handle's raw value (mirroring the
// cd::editor::panel::viewport::Viewport convention).
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, DrawWithTextureEmitsTexturedCmd)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    const auto h = make_texture(57U, 4U);
    overlay.set_texture(h);

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    overlay.draw(batcher, standard_bounds(), cd::ui::widgets::Theme {});

    // At least one kTextured command emitted (the G-buffer tile).
    EXPECT_GE(count_variant(batcher, cd::ui::renderer::material::kTextured),
              static_cast<std::size_t>(1U));

    // Verify the texture_slot carries the lower 32 bits of the handle value.
    const auto cmds = batcher.commands();
    const auto expected_slot =
        static_cast<std::uint32_t>(h.value() & 0xFFFFFFFFu);
    bool slot_match = false;
    for (const auto& c : cmds)
    {
        if (c.variant == cd::ui::renderer::material::kTextured &&
            c.texture_slot == expected_slot)
        {
            slot_match = true;
            break;
        }
    }
    EXPECT_TRUE(slot_match);
}

// ---------------------------------------------------------------------------
// TEST 10 — ClearTextureFallsBackToGradient
//
// After clearing the texture binding, draw() must NOT emit a kTextured
// command — the overlay returns to the Sprint-1 gradient placeholder.
// ---------------------------------------------------------------------------
TEST(DebugVizRealTexture, ClearTextureFallsBackToGradient)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kNormal };
    overlay.set_normal_texture(make_texture(8U));

    // Re-clear.
    overlay.set_texture(cd::rhi::TextureHandle {});
    EXPECT_FALSE(overlay.current_texture().is_valid());

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    overlay.draw(batcher, standard_bounds(), cd::ui::widgets::Theme {});

    // No kTextured command — only kSolid quads (background, border, header,
    // gradient bands, sparkline).
    EXPECT_EQ(count_variant(batcher, cd::ui::renderer::material::kTextured),
              static_cast<std::size_t>(0U));
    EXPECT_GT(batcher.command_count(), static_cast<std::size_t>(0U));
}
