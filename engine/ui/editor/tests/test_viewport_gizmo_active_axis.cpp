// =============================================================================
// CHROMODYNAMIC — test_viewport_gizmo_active_axis.cpp
// Phase 725 — active-axis highlight on TransformGizmo.
//
// Three focused cases:
//   1. Default state: active_axis() == kNone.
//   2. Clicking X sets active_axis kX and axis_x button colour is
//      accent_warning gold (r > g, r > b, g > b — a good proxy for gold).
//   3. Esc (clear_active_axis) resets to kNone.
// =============================================================================
#include <cd/ecs/World.hpp>
#include <cd/editor/AxisGizmo.hpp>
#include <cd/editor/ui/EditorWidgets.hpp>
#include <cd/scene/Scene.hpp>
#include <gtest/gtest.h>
#include <algorithm>

namespace
{

// ---- Helpers -----------------------------------------------------------------

/// Build a scene + entity and return a wired TransformGizmo.
struct GizmoFixture
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    cd::ecs::Entity entity { scene.create_node() };
    cd::editor::ui::TransformGizmo gizmo { 1.0F };

    GizmoFixture()
    {
        gizmo.set_target(&scene, entity);
    }
};

// Collect draw commands and find the first kRect command whose colour
// appears "gold-like": r >= 0.9, g >= 0.5, b <= 0.4.
[[nodiscard]] bool draw_contains_accent_warning(const cd::editor::ui::TransformGizmo& g)
{
    std::vector<cd::ui::DrawCommand> cmds;
    g.collect_draw_commands(cmds);
    return std::ranges::any_of(cmds, [](const auto& c)
    {
        return c.kind == cd::ui::DrawKind::kRect &&
               c.color.r >= 0.9F &&
               c.color.g >= 0.5F &&
               c.color.b <= 0.4F;
    });
}

// ---- Test cases --------------------------------------------------------------

TEST(ViewportGizmoActiveAxis, DefaultIsNone)
{
    // Arrange
    GizmoFixture f;

    // Act — nothing

    // Assert
    EXPECT_EQ(f.gizmo.active_axis(), cd::editor::GizmoAxis::kNone);
}

TEST(ViewportGizmoActiveAxis, ClickXEmitsAccentWarningColor)
{
    // Arrange
    GizmoFixture f;

    // Act
    f.gizmo.click_axis(0);  // X

    // Assert: active_axis is kX
    EXPECT_EQ(f.gizmo.active_axis(), cd::editor::GizmoAxis::kX);

    // Assert: the draw list contains at least one rect in accent_warning gold
    // (the active button background).
    EXPECT_TRUE(draw_contains_accent_warning(f.gizmo));
}

TEST(ViewportGizmoActiveAxis, EscClearsActiveAxis)
{
    // Arrange
    GizmoFixture f;
    f.gizmo.click_axis(0);  // X becomes active
    ASSERT_EQ(f.gizmo.active_axis(), cd::editor::GizmoAxis::kX);

    // Act — simulate Esc
    f.gizmo.clear_active_axis();

    // Assert
    EXPECT_EQ(f.gizmo.active_axis(), cd::editor::GizmoAxis::kNone);

    // After clear, no accent_warning gold should appear in the draw list.
    EXPECT_FALSE(draw_contains_accent_warning(f.gizmo));
}

}  // namespace
