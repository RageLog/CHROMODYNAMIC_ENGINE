// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_scene_palette/tests/test_scene_palette.cpp
//
// phase717 — unit tests for cd::editor::panel::scene_palette::ScenePalette.
//
// All tests are headless (no RHI, no ImGui).  We verify:
//
//   1. DefaultCtor
//         — starts with no selection (nullopt) and no palette pointer.
//   2. SelectedTokenNulloptWithNoPalette
//         — selected_token() is nullopt after default construction.
//   3. SimulateClickSelectsToken
//         — simulate_click on the first swatch cell sets selected_token()
//           to "background" (the first token in the grid).
//   4. SimulateClickOutsidePanelNoSelection
//         — simulate_click outside the panel bounds does not change selection.
//   5. SimulateClickDifferentSwatches
//         — clicking swatch 0 then swatch 1 updates selected_token()
//           to the corresponding token names.
//   6. SetPaletteNullDoesNotCrashDraw
//         — draw() with a default-constructed Theme and a null set_palette()
//           pointer emits at least the background quad.
//   7. DrawEmitsCommandsForAllTokens
//         — draw() with a valid theme emits more vertices than a draw() with
//           zero-size bounds (verifies per-token quads are issued).
//   8. DrawWithSelectionEmitsMoreQuads
//         — draw() after clicking a swatch emits more quads than before any
//           click (the selection highlight adds 4 border quads).
// =============================================================================
#include <cd/editor/panel_scene_palette/ScenePalette.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace sp = cd::editor::panel::scene_palette;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 0.0F, 0.0F, 400.0F, 600.0F };
}

[[nodiscard]] cd::ui::widgets::Rect zero_bounds() noexcept
{
    return { 0.0F, 0.0F, 0.0F, 0.0F };
}

[[nodiscard]] cd::ui::widgets::Theme standard_theme() noexcept
{
    return {};
}

// Compute the centre of swatch `i` inside standard_bounds().
// Mirrors ScenePalette::swatch_rect() layout constants.
constexpr float kPad    =  8.0F;
constexpr float kCols   =  3.0F;
constexpr float kSwatchH = 52.0F;
constexpr float kGap    =  4.0F;

[[nodiscard]] float swatch_cx(std::size_t i) noexcept
{
    const auto bounds  = standard_bounds();
    const float cell_w = (bounds.w - 2.0F * kPad - (kCols - 1.0F) * kGap) / kCols;
    const float col    = static_cast<float>(i % 3U);
    return bounds.x + kPad + col * (cell_w + kGap) + cell_w * 0.5F;
}

[[nodiscard]] float swatch_cy(std::size_t i) noexcept
{
    const auto bounds = standard_bounds();
    const float row   = static_cast<float>(i / 3U);
    return bounds.y + kPad + row * (kSwatchH + kGap) + kSwatchH * 0.5F;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultCtor
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, DefaultCtor)
{
    const sp::ScenePalette panel;
    EXPECT_FALSE(panel.selected_token().has_value());
}

// ---------------------------------------------------------------------------
// TEST 2 — SelectedTokenNulloptWithNoPalette
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, SelectedTokenNulloptWithNoPalette)
{
    sp::ScenePalette panel;
    panel.set_palette(nullptr);
    EXPECT_FALSE(panel.selected_token().has_value());
}

// ---------------------------------------------------------------------------
// TEST 3 — SimulateClickSelectsToken
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, SimulateClickSelectsToken)
{
    sp::ScenePalette panel;
    const auto bounds = standard_bounds();

    // Click the centre of swatch 0 ("background").
    panel.simulate_click(swatch_cx(0U), swatch_cy(0U), bounds);

    ASSERT_TRUE(panel.selected_token().has_value());
    EXPECT_EQ(panel.selected_token().value(), "background");
}

// ---------------------------------------------------------------------------
// TEST 4 — SimulateClickOutsidePanelNoSelection
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, SimulateClickOutsidePanelNoSelection)
{
    sp::ScenePalette panel;
    const auto bounds = standard_bounds();

    // Click well outside the panel.
    panel.simulate_click(-100.0F, -100.0F, bounds);

    EXPECT_FALSE(panel.selected_token().has_value());
}

// ---------------------------------------------------------------------------
// TEST 5 — SimulateClickDifferentSwatches
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, SimulateClickDifferentSwatches)
{
    sp::ScenePalette panel;
    const auto bounds = standard_bounds();

    // Click swatch 0 ("background").
    panel.simulate_click(swatch_cx(0U), swatch_cy(0U), bounds);
    ASSERT_TRUE(panel.selected_token().has_value());
    EXPECT_EQ(panel.selected_token().value(), "background");

    // Click swatch 1 ("surface").
    panel.simulate_click(swatch_cx(1U), swatch_cy(1U), bounds);
    ASSERT_TRUE(panel.selected_token().has_value());
    EXPECT_EQ(panel.selected_token().value(), "surface");
}

// ---------------------------------------------------------------------------
// TEST 6 — SetPaletteNullDoesNotCrashDraw
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, SetPaletteNullDoesNotCrashDraw)
{
    sp::ScenePalette panel;
    panel.set_palette(nullptr);

    const auto theme  = standard_theme();
    const auto bounds = standard_bounds();
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    // draw() uses the explicitly-passed palette, so this must not crash.
    EXPECT_NO_THROW(panel.draw(batcher, theme, bounds));

    // At minimum the background quad must be issued.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(4U));
}

// ---------------------------------------------------------------------------
// TEST 7 — DrawEmitsCommandsForAllTokens
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, DrawEmitsCommandsForAllTokens)
{
    sp::ScenePalette panel;
    const auto theme = standard_theme();

    // Draw with valid bounds — must emit many quads (1 background + 15 swatches
    // + 15 label strips = 31 quads minimum → 124 vertices).
    cd::ui::renderer::DrawBatcher batcher_full;
    batcher_full.begin_frame();
    panel.draw(batcher_full, theme, standard_bounds());
    const std::size_t verts_full = batcher_full.vertex_count();
    EXPECT_GE(verts_full, static_cast<std::size_t>(4U * 31U));

    // Draw with zero-size bounds — only the background quad, which is
    // immediately guarded by is_valid(); still at least 4 verts for the
    // background fill.
    cd::ui::renderer::DrawBatcher batcher_zero;
    batcher_zero.begin_frame();
    panel.draw(batcher_zero, theme, zero_bounds());
    const std::size_t verts_zero = batcher_zero.vertex_count();

    EXPECT_GT(verts_full, verts_zero);
}

// ---------------------------------------------------------------------------
// TEST 8 — DrawWithSelectionEmitsMoreQuads
// ---------------------------------------------------------------------------
TEST(ScenePalettePanel, DrawWithSelectionEmitsMoreQuads)
{
    const auto theme  = standard_theme();
    const auto bounds = standard_bounds();

    // Unselected draw.
    sp::ScenePalette panel_no_sel;
    cd::ui::renderer::DrawBatcher batcher_no_sel;
    batcher_no_sel.begin_frame();
    panel_no_sel.draw(batcher_no_sel, theme, bounds);
    const std::size_t verts_no_sel = batcher_no_sel.vertex_count();

    // Clicked draw — selecting swatch 0 adds 4 border quads (16 extra verts).
    sp::ScenePalette panel_sel;
    panel_sel.simulate_click(swatch_cx(0U), swatch_cy(0U), bounds);
    cd::ui::renderer::DrawBatcher batcher_sel;
    batcher_sel.begin_frame();
    panel_sel.draw(batcher_sel, theme, bounds);
    const std::size_t verts_sel = batcher_sel.vertex_count();

    // Selection adds 4 border quads = 16 additional vertices.
    EXPECT_GT(verts_sel, verts_no_sel);
    EXPECT_GE(verts_sel - verts_no_sel, static_cast<std::size_t>(16U));
}
