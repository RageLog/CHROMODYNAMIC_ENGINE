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
