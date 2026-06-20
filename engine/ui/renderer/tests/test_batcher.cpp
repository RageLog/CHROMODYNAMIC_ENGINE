// =============================================================================
// CHROMODYNAMIC — cd::ui::renderer DrawBatcher tests
//
// Validates the CPU-side batching algorithm without any RHI dependency.
// Coverage:
//   * Quad emits 4 verts + 6 indices
//   * Multiple quads with identical state merge into one DrawCommand
//   * Different texture / variant / scissor splits the command stream
//   * Scissor push/pop affects subsequent emissions
//   * Empty quads (w<=0 or h<=0) are no-ops
//   * Vertex layout invariants (pos/uv/color)
//   * begin_frame resets all state
//   * Empty batch (zero counts before any emit)
//   * Single quad full vertex layout (all 4 corners, all UV corners)
//   * Solid vs textured batch split + sentinel texture_slot
//   * Scissor two-level nesting push/pop
//   * Zero-size scissor is recorded (not silently dropped at CPU level)
//   * Adjacent-merge index_offset and accumulated index_count
//   * Non-merge split commands carry correct independent index_offset values
//   * u16 vertex limit: quads past kMaxVertices are silently dropped;
//     at_vertex_limit() signals saturation; begin_frame() resets
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <gtest/gtest.h>

#include <cstdint>

namespace ur = cd::ui::renderer;

TEST(DrawBatcher, QuadEmitsFourVertsSixIndices)
{
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(10.0F, 20.0F, 30.0F, 40.0F, ur::Color { 200U, 100U, 50U, 255U });

    EXPECT_EQ(b.vertex_count(), 4U);
    EXPECT_EQ(b.index_count(),  6U);
    EXPECT_EQ(b.command_count(), 1U);

    const auto verts = b.vertices();
    EXPECT_FLOAT_EQ(verts[0].pos_x, 10.0F);
    EXPECT_FLOAT_EQ(verts[0].pos_y, 20.0F);
    EXPECT_FLOAT_EQ(verts[2].pos_x, 40.0F);
    EXPECT_FLOAT_EQ(verts[2].pos_y, 60.0F);
    EXPECT_EQ(verts[0].r, 200U);
    EXPECT_EQ(verts[0].variant, ur::material::kSolid);
}

TEST(DrawBatcher, AdjacentSolidQuadsMergeIntoOneCommand)
{
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    b.quad(20.0F, 0.0F, 10.0F, 10.0F, ur::Color::black());
    b.quad(40.0F, 0.0F, 10.0F, 10.0F, ur::Color { 50U, 60U, 70U, 80U });

    EXPECT_EQ(b.vertex_count(), 12U);
    EXPECT_EQ(b.index_count(),  18U);
    EXPECT_EQ(b.command_count(), 1U);
    EXPECT_EQ(b.commands()[0].index_count, 18U);
}

TEST(DrawBatcher, TexturedQuadEmitsTexturedCommand)
{
    ur::DrawBatcher b;
    b.begin_frame();
    ur::AtlasUv uv { 0.0F, 0.0F, 0.5F, 0.5F };
    b.textured_quad(0.0F, 0.0F, 32.0F, 32.0F, 7U, uv);

    ASSERT_EQ(b.command_count(), 1U);
    EXPECT_EQ(b.commands()[0].variant, ur::material::kTextured);
    EXPECT_EQ(b.commands()[0].texture_slot, 7U);

    const auto v = b.vertices();
    EXPECT_FLOAT_EQ(v[0].uv_x, 0.0F);
    EXPECT_FLOAT_EQ(v[2].uv_x, 0.5F);
    EXPECT_FLOAT_EQ(v[2].uv_y, 0.5F);
}

TEST(DrawBatcher, DifferentVariantsSplitCommands)
{
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    ur::AtlasUv uv {};
    b.textured_quad(10.0F, 0.0F, 10.0F, 10.0F, 0U, uv);
    b.quad(20.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());

    EXPECT_EQ(b.command_count(), 3U);
    EXPECT_EQ(b.commands()[0].variant, ur::material::kSolid);
    EXPECT_EQ(b.commands()[1].variant, ur::material::kTextured);
    EXPECT_EQ(b.commands()[2].variant, ur::material::kSolid);
}

TEST(DrawBatcher, DifferentTextureSlotsSplitCommands)
{
    ur::DrawBatcher b;
    b.begin_frame();
    ur::AtlasUv uv {};
    b.textured_quad(0.0F, 0.0F, 10.0F, 10.0F, 1U, uv);
    b.textured_quad(20.0F, 0.0F, 10.0F, 10.0F, 2U, uv);  // different slot

    EXPECT_EQ(b.command_count(), 2U);
}

TEST(DrawBatcher, ScissorPushSplitsCommand)
{
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    b.push_scissor(ur::ScissorRect { 5, 5, 50U, 50U });
    b.quad(20.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    b.pop_scissor();
    b.quad(40.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());

    EXPECT_EQ(b.command_count(), 3U);
    EXPECT_EQ(b.commands()[1].scissor.x, 5);
    EXPECT_EQ(b.commands()[1].scissor.width, 50U);
    // Third command (post-pop) returns to no-clip.
    EXPECT_EQ(b.commands()[2].scissor.width, 0xFFFFFFFFu);
}

TEST(DrawBatcher, EmptyQuadIsNoOp)
{
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(0.0F, 0.0F, 0.0F, 50.0F, ur::Color::white());   // zero width
    b.quad(0.0F, 0.0F, 50.0F, 0.0F, ur::Color::white());   // zero height
    b.quad(0.0F, 0.0F, -5.0F, 5.0F, ur::Color::white());   // negative

    EXPECT_EQ(b.vertex_count(), 0U);
    EXPECT_EQ(b.index_count(),  0U);
    EXPECT_EQ(b.command_count(), 0U);
}

TEST(DrawBatcher, BeginFrameClearsState)
{
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    b.push_scissor(ur::ScissorRect { 1, 2, 3U, 4U });

    b.begin_frame();
    EXPECT_EQ(b.vertex_count(), 0U);
    EXPECT_EQ(b.index_count(),  0U);
    EXPECT_EQ(b.command_count(), 0U);

    // Scissor stack should also be cleared (next emit should have no clip).
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    ASSERT_EQ(b.command_count(), 1U);
    EXPECT_EQ(b.commands()[0].scissor.width, 0xFFFFFFFFu);
}

TEST(DrawBatcher, GlyphHasGlyphVariant)
{
    ur::DrawBatcher b;
    b.begin_frame();
    ur::AtlasUv uv { 0.1F, 0.1F, 0.2F, 0.2F };
    b.glyph(100.0F, 200.0F, 12.0F, 16.0F, 3U, uv,
            ur::Color { 0U, 0U, 0U, 255U });

    ASSERT_EQ(b.command_count(), 1U);
    EXPECT_EQ(b.commands()[0].variant, ur::material::kGlyph);
    EXPECT_EQ(b.commands()[0].texture_slot, 3U);

    const auto v = b.vertices();
    EXPECT_EQ(v[0].variant, ur::material::kGlyph);
    EXPECT_FLOAT_EQ(v[0].uv_x, 0.1F);
    EXPECT_FLOAT_EQ(v[2].uv_x, 0.2F);
}

TEST(DrawBatcher, IndexWindingMatchesCcw)
{
    ur::DrawBatcher b;
    b.begin_frame();
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());

    const auto idx = b.indices();
    ASSERT_EQ(idx.size(), 6U);
    // (0,1,2) first triangle, (0,2,3) second; verts are TL, TR, BR, BL.
    EXPECT_EQ(idx[0], 0);
    EXPECT_EQ(idx[1], 1);
    EXPECT_EQ(idx[2], 2);
    EXPECT_EQ(idx[3], 0);
    EXPECT_EQ(idx[4], 2);
    EXPECT_EQ(idx[5], 3);
}

// ---------------------------------------------------------------------------
// New depth-coverage tests (10 additional)
// ---------------------------------------------------------------------------

// 11. Empty batch: begin_frame() with no emits yields all-zero counts.
TEST(DrawBatcher, EmptyBatchAfterBeginFrame)
{
    // Arrange
    ur::DrawBatcher b;

    // Act
    b.begin_frame();

    // Assert
    EXPECT_EQ(b.vertex_count(),  0U);
    EXPECT_EQ(b.index_count(),   0U);
    EXPECT_EQ(b.command_count(), 0U);
    EXPECT_TRUE(b.vertices().empty());
    EXPECT_TRUE(b.indices().empty());
    EXPECT_TRUE(b.commands().empty());
    EXPECT_FALSE(b.at_vertex_limit());
}

// 12. Single quad: verify all four corner positions and all four UV corners.
TEST(DrawBatcher, SingleQuadAllFourVertexPositions)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();

    // Act  (x=5, y=10, w=20, h=30)
    b.quad(5.0F, 10.0F, 20.0F, 30.0F, ur::Color::white());

    // Assert — winding: TL(0) TR(1) BR(2) BL(3)
    const auto v = b.vertices();
    ASSERT_EQ(v.size(), 4U);

    // top-left
    EXPECT_FLOAT_EQ(v[0].pos_x,  5.0F);
    EXPECT_FLOAT_EQ(v[0].pos_y, 10.0F);
    // top-right
    EXPECT_FLOAT_EQ(v[1].pos_x, 25.0F);   // 5+20
    EXPECT_FLOAT_EQ(v[1].pos_y, 10.0F);
    // bottom-right
    EXPECT_FLOAT_EQ(v[2].pos_x, 25.0F);
    EXPECT_FLOAT_EQ(v[2].pos_y, 40.0F);   // 10+30
    // bottom-left
    EXPECT_FLOAT_EQ(v[3].pos_x,  5.0F);
    EXPECT_FLOAT_EQ(v[3].pos_y, 40.0F);
}

// 13. Single textured quad: verify all four UV corners are laid out correctly.
TEST(DrawBatcher, SingleQuadAllFourVertexUVs)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();
    const ur::AtlasUv uv { 0.25F, 0.5F, 0.75F, 1.0F };

    // Act
    b.textured_quad(0.0F, 0.0F, 10.0F, 10.0F, 0U, uv);

    // Assert — winding: TL(0) TR(1) BR(2) BL(3)
    const auto v = b.vertices();
    ASSERT_EQ(v.size(), 4U);

    EXPECT_FLOAT_EQ(v[0].uv_x, 0.25F);  // TL: u0
    EXPECT_FLOAT_EQ(v[0].uv_y, 0.5F);   // TL: v0
    EXPECT_FLOAT_EQ(v[1].uv_x, 0.75F);  // TR: u1
    EXPECT_FLOAT_EQ(v[1].uv_y, 0.5F);   // TR: v0
    EXPECT_FLOAT_EQ(v[2].uv_x, 0.75F);  // BR: u1
    EXPECT_FLOAT_EQ(v[2].uv_y, 1.0F);   // BR: v1
    EXPECT_FLOAT_EQ(v[3].uv_x, 0.25F);  // BL: u0
    EXPECT_FLOAT_EQ(v[3].uv_y, 1.0F);   // BL: v1
}

// 14. Solid quad texture_slot must always be 0xFFFFFFFFu (sentinel "no texture").
TEST(DrawBatcher, SolidQuadHasNoTextureSentinel)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();

    // Act
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());

    // Assert
    ASSERT_EQ(b.command_count(), 1U);
    EXPECT_EQ(b.commands()[0].texture_slot, 0xFFFFFFFFu);
    EXPECT_EQ(b.commands()[0].variant, ur::material::kSolid);
}

// 15. Solid then textured must split — and verify each command's texture_slot.
TEST(DrawBatcher, SolidThenTexturedSplitsAndPreservesSlots)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();
    const ur::AtlasUv uv {};

    // Act
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());         // solid
    b.textured_quad(20.0F, 0.0F, 10.0F, 10.0F, 5U, uv);          // textured slot=5

    // Assert — two commands
    ASSERT_EQ(b.command_count(), 2U);
    EXPECT_EQ(b.commands()[0].variant,      ur::material::kSolid);
    EXPECT_EQ(b.commands()[0].texture_slot, 0xFFFFFFFFu);
    EXPECT_EQ(b.commands()[1].variant,      ur::material::kTextured);
    EXPECT_EQ(b.commands()[1].texture_slot, 5U);
}

// 16. Two-level scissor nesting: push A then B, verify B; pop to A; pop to default.
TEST(DrawBatcher, ScissorTwoLevelNesting)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();
    const ur::ScissorRect sA { 10, 20, 100U, 200U };
    const ur::ScissorRect sB {  5, 15,  30U,  40U };

    // Act
    const auto depth1 = b.push_scissor(sA);
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());  // cmd0 with sA

    const auto depth2 = b.push_scissor(sB);
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());  // cmd1 with sB

    b.pop_scissor();                                          // back to sA
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());  // cmd2 with sA

    b.pop_scissor();                                          // back to default
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());  // cmd3 with default

    // Assert — 4 commands (sA, sB, sA, default all differ)
    EXPECT_EQ(depth1, 1U);
    EXPECT_EQ(depth2, 2U);
    ASSERT_EQ(b.command_count(), 4U);

    // sA
    EXPECT_EQ(b.commands()[0].scissor.x,      10);
    EXPECT_EQ(b.commands()[0].scissor.width,  100U);
    // sB
    EXPECT_EQ(b.commands()[1].scissor.x,      5);
    EXPECT_EQ(b.commands()[1].scissor.width,  30U);
    // back to sA
    EXPECT_EQ(b.commands()[2].scissor.x,      10);
    EXPECT_EQ(b.commands()[2].scissor.width,  100U);
    // default (no-clip sentinel)
    EXPECT_EQ(b.commands()[3].scissor.width,  0xFFFFFFFFu);
}

// 17. Zero-size scissor is recorded in the command (not silently dropped).
//     The batcher is CPU-only; pixel-level clip is the renderer's job.
TEST(DrawBatcher, ZeroSizeScissorIsRecordedNotDropped)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();

    // Act — push a 0×0 scissor, then emit a quad
    b.push_scissor(ur::ScissorRect { 50, 50, 0U, 0U });
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    b.pop_scissor();

    // Assert — quad is emitted (batcher doesn't CPU-clip); scissor is 0×0
    EXPECT_EQ(b.vertex_count(),  4U);
    EXPECT_EQ(b.index_count(),   6U);
    ASSERT_EQ(b.command_count(), 1U);
    EXPECT_EQ(b.commands()[0].scissor.width,  0U);
    EXPECT_EQ(b.commands()[0].scissor.height, 0U);
}

// 18. Adjacent-merge: verify index_offset stays 0 and index_count accumulates.
TEST(DrawBatcher, AdjacentMergeIndexOffsetAndCount)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();

    // Act — two solid quads, should merge
    b.quad(0.0F, 0.0F, 10.0F, 10.0F, ur::Color::white());
    b.quad(20.0F, 0.0F, 10.0F, 10.0F, ur::Color::black());

    // Assert
    ASSERT_EQ(b.command_count(), 1U);
    EXPECT_EQ(b.commands()[0].index_offset, 0U);   // first quad always offset 0
    EXPECT_EQ(b.commands()[0].index_count,  12U);  // 2 quads × 6 indices
}

// 19. Non-merge (different texture) produces correct per-command index_offset.
TEST(DrawBatcher, NonMergeIncompatibleIndexOffsets)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();
    const ur::AtlasUv uv {};

    // Act — textured slot 1, then slot 2 (forces split)
    b.textured_quad(0.0F, 0.0F, 10.0F, 10.0F, 1U, uv);
    b.textured_quad(20.0F, 0.0F, 10.0F, 10.0F, 2U, uv);
    b.textured_quad(40.0F, 0.0F, 10.0F, 10.0F, 1U, uv);  // back to slot 1 (new cmd)

    // Assert
    ASSERT_EQ(b.command_count(), 3U);
    EXPECT_EQ(b.commands()[0].index_offset,  0U);   // indices [0..5]
    EXPECT_EQ(b.commands()[0].index_count,   6U);
    EXPECT_EQ(b.commands()[1].index_offset,  6U);   // indices [6..11]
    EXPECT_EQ(b.commands()[1].index_count,   6U);
    EXPECT_EQ(b.commands()[2].index_offset, 12U);   // indices [12..17]
    EXPECT_EQ(b.commands()[2].index_count,   6U);
}

// 20. u16 vertex limit: quads past kMaxVertices are silently dropped.
//     Documented behavior: no auto-split; callers must begin_frame() to flush.
TEST(DrawBatcher, U16VertexLimitDropsExcessQuads)
{
    // Arrange
    ur::DrawBatcher b;
    b.begin_frame();

    // Act — fill to exactly kMaxVertices (65532 verts = 16383 quads)
    const auto quads_to_fill = static_cast<std::uint32_t>(ur::kMaxVertices / 4U);
    for (std::uint32_t i = 0U; i < quads_to_fill; ++i)
    {
        b.quad(static_cast<float>(i), 0.0F, 1.0F, 1.0F, ur::Color::white());
    }

    // At the limit: all quads accepted
    EXPECT_EQ(b.vertex_count(), static_cast<std::size_t>(ur::kMaxVertices));
    EXPECT_TRUE(b.at_vertex_limit());

    // Try one more quad — must be silently dropped
    const auto verts_before = b.vertex_count();
    const auto idx_before   = b.index_count();
    b.quad(9999.0F, 0.0F, 1.0F, 1.0F, ur::Color::black());

    EXPECT_EQ(b.vertex_count(), verts_before);  // unchanged
    EXPECT_EQ(b.index_count(),  idx_before);    // unchanged
    EXPECT_TRUE(b.at_vertex_limit());

    // begin_frame() resets the limit
    b.begin_frame();
    EXPECT_EQ(b.vertex_count(), 0U);
    EXPECT_FALSE(b.at_vertex_limit());

    // New emits are accepted after reset
    b.quad(0.0F, 0.0F, 5.0F, 5.0F, ur::Color::white());
    EXPECT_EQ(b.vertex_count(), 4U);
}
