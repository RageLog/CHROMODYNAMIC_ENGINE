// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_light_editor/tests/test_light_editor.cpp
//
// phase664 — unit tests for cd::editor::panel::light_editor::LightEditor.
//
// All tests are headless (no RHI, no ImGui).  We verify:
//   1. DefaultCtor            — starts with empty selection, empty light list.
//   2. SetLightsRoundTrip     — set_lights copies data; count reflected in draw.
//   3. DrawEmitsCommandsProportionalToLightCount
//                             — draw() emits more commands with more lights.
//   4. SetGridRoundTrip       — set_grid stores the config; stats rendered.
//   5. SimulateClickSelectsCorrectRow
//                             — click within a row selects that row index.
//   6. SimulateClickOutsideBoundsIgnored
//                             — click outside panel leaves selection unchanged.
//   7. SelectionInvalidatedWhenLightCountShrinks
//                             — selection is cleared if set_lights shrinks the
//                                list below the current selection index.
// =============================================================================
#include <cd/editor/panel_light_editor/LightEditor.hpp>

#include <cd/render/lighting_clusters/LightingClusters.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace le  = cd::editor::panel::light_editor;
namespace lc  = cd::render::lighting_clusters;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

/// Build a simple PointLight at the given position.
[[nodiscard]] lc::PointLight make_light(float x, float y, float z,
                                        float radius    = 5.0F,
                                        float intensity = 100.0F) noexcept
{
    lc::PointLight pl;
    pl.position  = { x, y, z };
    pl.radius    = radius;
    pl.intensity = intensity;
    pl.color     = { 1.0F, 0.8F, 0.4F };
    return pl;
}

/// Returns a standard 400×600 bounds rectangle.
[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 0.0F, 0.0F, 400.0F, 600.0F };
}

/// Returns a default-constructed theme.
[[nodiscard]] cd::ui::widgets::Theme standard_theme() noexcept
{
    return {};
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultCtor
// ---------------------------------------------------------------------------
TEST(LightEditorPanel, DefaultCtor)
{
    const le::LightEditor editor;

    // No selection on construction.
    EXPECT_FALSE(editor.selected_light().has_value());
}

// ---------------------------------------------------------------------------
// TEST 2 — SetLightsRoundTrip
// ---------------------------------------------------------------------------
TEST(LightEditorPanel, SetLightsRoundTrip)
{
    le::LightEditor editor;

    std::vector<lc::PointLight> lights;
    lights.push_back(make_light(1.0F, 2.0F, 3.0F));
    lights.push_back(make_light(4.0F, 5.0F, 6.0F));
    lights.push_back(make_light(7.0F, 8.0F, 9.0F));

    editor.set_lights(lights);

    // No selection yet.
    EXPECT_FALSE(editor.selected_light().has_value());

    // Simulate clicking the second row and confirm index is 1.
    const cd::ui::widgets::Rect bounds = standard_bounds();
    // Row-top for index 1: kHeaderH + 1 * (kRowH + 2) = 16 + 24 = 40,
    // click in the centre of that row.
    constexpr float kHeaderH = 6.0F + 4.0F + 6.0F;    // kPad + kBarH + kPad
    constexpr float kRowH    = 22.0F;
    constexpr float kRowStep = kRowH + 2.0F;
    const float     row1_y   = bounds.y + kHeaderH + 1.0F * kRowStep + kRowH * 0.5F;

    editor.simulate_click(50.0F, row1_y, bounds);
    ASSERT_TRUE(editor.selected_light().has_value());
    EXPECT_EQ(*editor.selected_light(), static_cast<std::size_t>(1));
}

// ---------------------------------------------------------------------------
// TEST 3 — DrawEmitsCommandsProportionalToLightCount
// ---------------------------------------------------------------------------
TEST(LightEditorPanel, DrawEmitsCommandsProportionalToLightCount)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();

    // 0 lights — at minimum: background quad emits >= 4 vertices.
    le::LightEditor editor_empty;
    cd::ui::renderer::DrawBatcher batcher_empty;
    batcher_empty.begin_frame();
    editor_empty.draw(batcher_empty, theme, bounds);
    const std::size_t verts_empty = batcher_empty.vertex_count();
    EXPECT_GE(verts_empty, static_cast<std::size_t>(4U));

    // 5 lights — each row emits multiple quads (swatch + 5 strips + 5 fills),
    // so total vertex count must be strictly greater than with 0 lights.
    le::LightEditor editor_five;
    std::vector<lc::PointLight> lights;
    lights.reserve(5U);
    for (int i = 0; i < 5; ++i)
        lights.push_back(make_light(static_cast<float>(i), 0.0F, 0.0F));
    editor_five.set_lights(lights);

    cd::ui::renderer::DrawBatcher batcher_five;
    batcher_five.begin_frame();
    editor_five.draw(batcher_five, theme, bounds);
    const std::size_t verts_five = batcher_five.vertex_count();

    EXPECT_GT(verts_five, verts_empty);
}

// ---------------------------------------------------------------------------
// TEST 4 — SetGridRoundTrip
// ---------------------------------------------------------------------------
TEST(LightEditorPanel, SetGridRoundTrip)
{
    le::LightEditor editor;

    const lc::ClusterGrid grid { 32U, 18U, 48U, 0.2F, 5000.0F };
    editor.set_grid(grid);

    // draw() must not crash with a custom grid.
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme  = standard_theme();
    const cd::ui::widgets::Rect   bounds = standard_bounds();

    batcher.begin_frame();
    EXPECT_NO_THROW(editor.draw(batcher, theme, bounds));

    // At minimum the background + separator + stats bars should emit quads.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
    EXPECT_GE(batcher.vertex_count(),  static_cast<std::size_t>(4U));
}

// ---------------------------------------------------------------------------
// TEST 5 — SimulateClickSelectsCorrectRow
// ---------------------------------------------------------------------------
TEST(LightEditorPanel, SimulateClickSelectsCorrectRow)
{
    le::LightEditor editor;

    std::vector<lc::PointLight> lights;
    lights.reserve(10U);
    for (int i = 0; i < 10; ++i)
        lights.push_back(make_light(static_cast<float>(i) * 10.0F, 0.0F, 0.0F));
    editor.set_lights(lights);

    const cd::ui::widgets::Rect bounds = standard_bounds();

    constexpr float kHeaderH = 6.0F + 4.0F + 6.0F;
    constexpr float kRowH    = 22.0F;
    constexpr float kRowStep = kRowH + 2.0F;

    // Click each row in order and verify selection.
    for (std::size_t i = 0; i < 10U; ++i)
    {
        const float click_y = bounds.y + kHeaderH
                              + static_cast<float>(i) * kRowStep
                              + kRowH * 0.5F;
        editor.simulate_click(50.0F, click_y, bounds);
        ASSERT_TRUE(editor.selected_light().has_value()) << "row " << i;
        EXPECT_EQ(*editor.selected_light(), i) << "row " << i;
    }
}

// ---------------------------------------------------------------------------
// TEST 6 — SimulateClickOutsideBoundsIgnored
// ---------------------------------------------------------------------------
TEST(LightEditorPanel, SimulateClickOutsideBoundsIgnored)
{
    le::LightEditor editor;

    std::vector<lc::PointLight> lights;
    lights.push_back(make_light(0.0F, 0.0F, 0.0F));
    editor.set_lights(lights);

    const cd::ui::widgets::Rect bounds = { 100.0F, 100.0F, 400.0F, 600.0F };

    // Click completely outside the panel.
    editor.simulate_click(10.0F, 10.0F, bounds);
    EXPECT_FALSE(editor.selected_light().has_value());
}

// ---------------------------------------------------------------------------
// TEST 7 — SelectionInvalidatedWhenLightCountShrinks
// ---------------------------------------------------------------------------
TEST(LightEditorPanel, SelectionInvalidatedWhenLightCountShrinks)
{
    le::LightEditor editor;

    // Set 5 lights and select index 4.
    std::vector<lc::PointLight> lights5;
    lights5.reserve(5U);
    for (int i = 0; i < 5; ++i)
        lights5.push_back(make_light(static_cast<float>(i), 0.0F, 0.0F));
    editor.set_lights(lights5);

    const cd::ui::widgets::Rect bounds = standard_bounds();
    constexpr float kHeaderH = 6.0F + 4.0F + 6.0F;
    constexpr float kRowH    = 22.0F;
    constexpr float kRowStep = kRowH + 2.0F;
    const float     click_y4 = bounds.y + kHeaderH + 4.0F * kRowStep + kRowH * 0.5F;
    editor.simulate_click(50.0F, click_y4, bounds);

    ASSERT_TRUE(editor.selected_light().has_value());
    EXPECT_EQ(*editor.selected_light(), static_cast<std::size_t>(4));

    // Shrink to 3 lights — index 4 is out of range.
    std::vector<lc::PointLight> lights3;
    lights3.reserve(3U);
    for (int i = 0; i < 3; ++i)
        lights3.push_back(make_light(static_cast<float>(i), 0.0F, 0.0F));
    editor.set_lights(lights3);

    EXPECT_FALSE(editor.selected_light().has_value());
}
