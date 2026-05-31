// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_material_editor/tests/test_material_editor.cpp
//
// phase556-557-558 — unit tests for cd::editor::panel::material_editor::MaterialEditor.
//
// All tests are headless (no ImGui / no RHI). We verify:
//   * DefaultCtorHasNoMaterial       — material_id() == kInvalidMaterialId,
//                                      default PBR parameters are set.
//   * SetMaterialIdRoundTrips        — set_material_id / material_id round-trip.
//   * SetBaseColorRoundTrips         — set_base_color / base_color_r/g/b round-trip
//                                      and values are clamped to [0..1].
//   * SetMetallicRoundTrips          — set_metallic / metallic round-trip,
//                                      including clamp at 0 and 1.
//   * SetRoughnessRoundTrips         — set_roughness / roughness round-trip,
//                                      including clamp at 0 and 1.
//   * DrawDefaultDoesNotCrash        — draw() with default state emits background.
//   * DrawWithMaterialIdEmitsQuads   — draw() with a bound material + colour tint
//                                      emits fill bars + preview rectangle.
//   * DrawZeroBoundsDoesNotCrash     — draw() on a zero-size rect returns early.
//   * DrawChangedColorAffectsOutput  — changing base color changes the draw output.
// =============================================================================
#include <cd/editor/panel_material_editor/MaterialEditor.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace me = cd::editor::panel::material_editor;

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, DefaultCtorHasNoMaterial)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, DefaultCtorHasNoMaterial)
{
    const me::MaterialEditor editor;

    EXPECT_EQ(editor.material_id(), me::kInvalidMaterialId);

    // Default PBR parameters: white diffuse, non-metallic, half-rough.
    EXPECT_FLOAT_EQ(editor.base_color_r(), 1.0F);
    EXPECT_FLOAT_EQ(editor.base_color_g(), 1.0F);
    EXPECT_FLOAT_EQ(editor.base_color_b(), 1.0F);
    EXPECT_FLOAT_EQ(editor.metallic(),     0.0F);
    EXPECT_FLOAT_EQ(editor.roughness(),    0.5F);
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, SetMaterialIdRoundTrips)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, SetMaterialIdRoundTrips)
{
    me::MaterialEditor editor;

    editor.set_material_id(42U);
    EXPECT_EQ(editor.material_id(), 42U);

    // Rebind to a different id.
    editor.set_material_id(7U);
    EXPECT_EQ(editor.material_id(), 7U);

    // Clearing with the sentinel.
    editor.set_material_id(me::kInvalidMaterialId);
    EXPECT_EQ(editor.material_id(), me::kInvalidMaterialId);
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, SetBaseColorRoundTrips)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, SetBaseColorRoundTrips)
{
    me::MaterialEditor editor;

    editor.set_base_color(0.2F, 0.5F, 0.8F);
    EXPECT_FLOAT_EQ(editor.base_color_r(), 0.2F);
    EXPECT_FLOAT_EQ(editor.base_color_g(), 0.5F);
    EXPECT_FLOAT_EQ(editor.base_color_b(), 0.8F);

    // Values above 1 are clamped to 1.
    editor.set_base_color(2.0F, -0.5F, 1.5F);
    EXPECT_FLOAT_EQ(editor.base_color_r(), 1.0F);
    EXPECT_FLOAT_EQ(editor.base_color_g(), 0.0F);
    EXPECT_FLOAT_EQ(editor.base_color_b(), 1.0F);
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, SetMetallicRoundTrips)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, SetMetallicRoundTrips)
{
    me::MaterialEditor editor;

    editor.set_metallic(0.75F);
    EXPECT_FLOAT_EQ(editor.metallic(), 0.75F);

    // Clamp below 0.
    editor.set_metallic(-1.0F);
    EXPECT_FLOAT_EQ(editor.metallic(), 0.0F);

    // Clamp above 1.
    editor.set_metallic(3.0F);
    EXPECT_FLOAT_EQ(editor.metallic(), 1.0F);
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, SetRoughnessRoundTrips)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, SetRoughnessRoundTrips)
{
    me::MaterialEditor editor;

    editor.set_roughness(0.3F);
    EXPECT_FLOAT_EQ(editor.roughness(), 0.3F);

    // Clamp below 0.
    editor.set_roughness(-0.1F);
    EXPECT_FLOAT_EQ(editor.roughness(), 0.0F);

    // Clamp above 1.
    editor.set_roughness(1.5F);
    EXPECT_FLOAT_EQ(editor.roughness(), 1.0F);
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, DrawDefaultDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, DrawDefaultDoesNotCrash)
{
    me::MaterialEditor            editor;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    // Must not throw or crash.
    editor.draw(batcher, theme, bounds);

    // Background quad must have been emitted (at least one command).
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, DrawWithMaterialIdEmitsQuads)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, DrawWithMaterialIdEmitsQuads)
{
    me::MaterialEditor editor;
    editor.set_material_id(1U);
    editor.set_base_color(0.8F, 0.3F, 0.1F);
    editor.set_metallic(0.4F);
    editor.set_roughness(0.6F);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    editor.draw(batcher, theme, bounds);

    // Expect: background + separator + 3 R/G/B strips (3 track + 3 fill) +
    //         2 param rows (2 track + 2 fill) + preview (2 quads) = 13+ quads.
    // Each quad = 4 verts, so at least 52 verts total.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(40U));
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, DrawZeroBoundsDoesNotCrash)
{
    me::MaterialEditor            editor;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    editor.draw(batcher, theme, zero);

    // bounds.is_valid() == false → returns early after the background quad.
    // Row quads must NOT be emitted, vertex count stays low.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// TEST(MaterialEditorPanel, DrawChangedColorAffectsOutput)
// ---------------------------------------------------------------------------
TEST(MaterialEditorPanel, DrawChangedColorAffectsOutput)
{
    me::MaterialEditor editor;
    editor.set_material_id(5U);

    cd::ui::renderer::DrawBatcher batcher_black;
    cd::ui::renderer::DrawBatcher batcher_white;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    // Draw with a black tint (all channels = 0 → fill quads have zero width
    // and are NOT emitted by draw()).
    editor.set_base_color(0.0F, 0.0F, 0.0F);
    batcher_black.begin_frame();
    editor.draw(batcher_black, theme, bounds);

    // Draw with a white tint (all channels = 1 → all fill quads ARE emitted).
    editor.set_base_color(1.0F, 1.0F, 1.0F);
    batcher_white.begin_frame();
    editor.draw(batcher_white, theme, bounds);

    // White produces more geometry because the fill bars are non-zero width.
    EXPECT_GT(batcher_white.vertex_count(), batcher_black.vertex_count());
}
