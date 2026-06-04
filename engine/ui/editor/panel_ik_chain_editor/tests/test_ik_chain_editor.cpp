// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_ik_chain_editor/tests/test_ik_chain_editor.cpp
//
// phase710 — unit tests for cd::editor::panel::ik_chain_editor::IkChainEditor.
// phase743 — additional tests for set_camera() VP projection + drag API.
//
// All tests are headless (no RHI, no ImGui).  We verify:
//   1.  DefaultCtor            — starts with no chain, no result, no selection.
//   2.  SetChainClearsSelection — set_chain resets any prior selection.
//   3.  DrawEmitsNoCommandsWithoutChain
//                              — draw() without a bound chain emits only the
//                                background quad (1 command).
//   4.  DrawEmitsCommandsProportionalToJoints
//                              — more joints => more draw commands.
//   5.  SimulateClickSelectsNearestJoint
//                              — click near a joint's projected position selects
//                                that joint index.
//   6.  SimulateClickOutsideBoundsIgnored
//                              — click outside panel leaves selection unchanged.
//   7.  ConvergedResultIndicatorEmitsCommands
//                              — binding a converged IkResult causes additional
//                                quads vs no-result case.
//   8.  NonConvergedResultIndicatorEmitsCommands
//                              — binding a non-converged IkResult also produces
//                                additional quads.
//   9.  NullChainAfterSetDetaches
//                              — set_chain(nullptr) detaches; draw() emits only
//                                background quad and selected_joint is nullopt.
//   10. SetCameraDoesNotCrash  — set_camera() + draw() with a VP matrix completes
//                                without crash and emits at least the background
//                                + separator quads (2+ draw commands).
//   11. ClearCameraRevertsTo2D — clear_camera() after set_camera() reverts to the
//                                same vertex count as the 2D baseline.
//   12. SetChainMutableBinds   — set_chain_mutable() makes the mutable chain
//                                visible to draw() (same command count as const).
//   13. BeginTargetDragReturnsFalseWhenNoMutableChain
//                              — begin_target_drag() returns false when only the
//                                const chain is bound.
//   14. BeginTargetDragReturnsFalseWhenFarFromTarget
//                              — begin_target_drag() returns false for a click
//                                far from the target marker.
//   15. DragCycleUpdatesMutableTarget
//                              — begin → update → end cycle writes a different
//                                end_effector_target into the mutable chain.
//   16. IsDraggingTargetState  — is_dragging_target() tracks begin/end drag state.
// =============================================================================
#include <cd/editor/panel_ik_chain_editor/IkChainEditor.hpp>

#include <cd/animation/ik/Ik.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace ike = cd::editor::panel::ik_chain_editor;
namespace aik = cd::animation::ik;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

/// Build a simple 3-joint straight-line chain along +X.
[[nodiscard]] aik::IkChain make_line_chain(int num_joints = 3,
                                            float segment_len = 1.0F) noexcept
{
    aik::IkChain chain;
    for (int i = 0; i < num_joints; ++i)
    {
        aik::Joint joint;
        joint.name   = "joint_" + std::to_string(i);
        // First joint at world origin; subsequent joints offset along +X.
        joint.local_position = (i == 0)
            ? std::array<float, 3>{ 0.0F, 0.0F, 0.0F }
            : std::array<float, 3>{ segment_len, 0.0F, 0.0F };
        joint.length = segment_len;
        chain.joints.push_back(joint);
    }
    chain.end_effector_target = { static_cast<float>(num_joints) * segment_len,
                                   0.0F, 0.0F };
    return chain;
}

/// Build a converged IkResult with `num_joints` rotations.
[[nodiscard]] aik::IkResult make_converged_result(int num_joints) noexcept
{
    aik::IkResult result;
    result.converged       = true;
    result.iterations_used = 3U;
    result.final_distance  = 0.0001F;
    for (int i = 0; i < num_joints; ++i)
        result.solved_rotations.push_back({ 0.0F, 0.0F, 0.0F, 1.0F });
    return result;
}

/// Build a non-converged IkResult.
[[nodiscard]] aik::IkResult make_failed_result(int num_joints) noexcept
{
    aik::IkResult result;
    result.converged       = false;
    result.iterations_used = 16U;
    result.final_distance  = 5.0F;
    for (int i = 0; i < num_joints; ++i)
        result.solved_rotations.push_back({ 0.0F, 0.0F, 0.0F, 1.0F });
    return result;
}

/// Returns a standard 400×600 panel bounds.
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
TEST(IkChainEditorPanel, DefaultCtor)
{
    const ike::IkChainEditor editor;

    // No selection on construction.
    EXPECT_FALSE(editor.selected_joint().has_value());
}

// ---------------------------------------------------------------------------
// TEST 2 — SetChainClearsSelection
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, SetChainClearsSelection)
{
    ike::IkChainEditor editor;
    const auto chain = make_line_chain(3);

    // Bind chain and simulate a click to establish selection.
    // 3-joint chain along X (0,0)->(1,0)->(2,0), target (3,0).
    // Content area: x=[6..394], y=[16..594]. range_x=3, range_y=1.
    // Root joint: panel_x = 6 + (0/3)*388 = 6,  panel_y = 16 + 1*578 = 594.
    // Click at (6, 594) — within kJointRadius (10) of the root joint.
    editor.set_chain(&chain);
    const cd::ui::widgets::Rect bounds = standard_bounds();
    editor.simulate_click(6.0F, 594.0F, bounds);
    ASSERT_TRUE(editor.selected_joint().has_value());

    // Re-binding the chain must reset selection.
    editor.set_chain(&chain);
    EXPECT_FALSE(editor.selected_joint().has_value());
}

// ---------------------------------------------------------------------------
// TEST 3 — DrawEmitsNoCommandsWithoutChain
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, DrawEmitsNoCommandsWithoutChain)
{
    ike::IkChainEditor editor;  // No chain bound.

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    editor.draw(batcher, standard_theme(), standard_bounds());

    // Only the background quad should be emitted (1 command, 4 vertices).
    EXPECT_EQ(batcher.command_count(), static_cast<std::size_t>(1U));
    EXPECT_GE(batcher.vertex_count(),  static_cast<std::size_t>(4U));
}

// ---------------------------------------------------------------------------
// TEST 4 — DrawEmitsCommandsProportionalToJoints
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, DrawEmitsCommandsProportionalToJoints)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();

    // 2-joint chain.
    ike::IkChainEditor editor_two;
    const auto chain_two = make_line_chain(2);
    editor_two.set_chain(&chain_two);

    cd::ui::renderer::DrawBatcher batcher_two;
    batcher_two.begin_frame();
    editor_two.draw(batcher_two, theme, bounds);
    const std::size_t verts_two = batcher_two.vertex_count();

    // 5-joint chain — more joints => more circles and bone lines => more
    // vertices (draw commands may merge when same material, so use vertex_count).
    ike::IkChainEditor editor_five;
    const auto chain_five = make_line_chain(5);
    editor_five.set_chain(&chain_five);

    cd::ui::renderer::DrawBatcher batcher_five;
    batcher_five.begin_frame();
    editor_five.draw(batcher_five, theme, bounds);
    const std::size_t verts_five = batcher_five.vertex_count();

    EXPECT_GT(verts_five, verts_two);
}

// ---------------------------------------------------------------------------
// TEST 5 — SimulateClickSelectsNearestJoint
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, SimulateClickSelectsNearestJoint)
{
    ike::IkChainEditor editor;
    // 3-joint chain: root at (0,0), joint1 offset (1,0), joint2 offset (1,0).
    // World positions: (0,0), (1,0), (2,0). target=(3,0).
    // Content area: [6..394] x [16..594]. range_x=3, range_y=1.
    // Root:   panel_x = 6 + 0         = 6,   panel_y = 16 + 578 = 594.
    // Joint1: panel_x = 6 + 388/3     ≈ 135, panel_y = 594.
    // Joint2: panel_x = 6 + 776/3     ≈ 265, panel_y = 594.
    const auto chain = make_line_chain(3, 1.0F);
    editor.set_chain(&chain);

    const cd::ui::widgets::Rect bounds = standard_bounds();

    // Click exactly on the root joint position (index 0).
    editor.simulate_click(6.0F, 594.0F, bounds);
    ASSERT_TRUE(editor.selected_joint().has_value());
    EXPECT_EQ(*editor.selected_joint(), static_cast<std::size_t>(0U));
}

// ---------------------------------------------------------------------------
// TEST 6 — SimulateClickOutsideBoundsIgnored
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, SimulateClickOutsideBoundsIgnored)
{
    ike::IkChainEditor editor;
    const auto chain = make_line_chain(3);
    editor.set_chain(&chain);

    const cd::ui::widgets::Rect bounds = { 100.0F, 100.0F, 400.0F, 600.0F };

    // Click completely outside the panel.
    editor.simulate_click(10.0F, 10.0F, bounds);
    EXPECT_FALSE(editor.selected_joint().has_value());
}

// ---------------------------------------------------------------------------
// TEST 7 — ConvergedResultIndicatorEmitsCommands
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, ConvergedResultIndicatorEmitsCommands)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();
    const auto chain  = make_line_chain(3);
    const auto result = make_converged_result(3);

    // Without result.
    ike::IkChainEditor editor_no_result;
    editor_no_result.set_chain(&chain);
    cd::ui::renderer::DrawBatcher batcher_no_result;
    batcher_no_result.begin_frame();
    editor_no_result.draw(batcher_no_result, theme, bounds);
    const std::size_t verts_no_result = batcher_no_result.vertex_count();

    // With converged result — green check indicator adds quads => more vertices.
    ike::IkChainEditor editor_with_result;
    editor_with_result.set_chain(&chain);
    editor_with_result.set_last_result(&result);
    cd::ui::renderer::DrawBatcher batcher_with_result;
    batcher_with_result.begin_frame();
    editor_with_result.draw(batcher_with_result, theme, bounds);
    const std::size_t verts_with_result = batcher_with_result.vertex_count();

    // Convergence indicator adds at least one extra quad (4 vertices).
    EXPECT_GT(verts_with_result, verts_no_result);
}

// ---------------------------------------------------------------------------
// TEST 8 — NonConvergedResultIndicatorEmitsCommands
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, NonConvergedResultIndicatorEmitsCommands)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();
    const auto chain  = make_line_chain(3);
    const auto result = make_failed_result(3);

    ike::IkChainEditor editor_no_result;
    editor_no_result.set_chain(&chain);
    cd::ui::renderer::DrawBatcher batcher_no_result;
    batcher_no_result.begin_frame();
    editor_no_result.draw(batcher_no_result, theme, bounds);
    const std::size_t verts_no_result = batcher_no_result.vertex_count();

    // With failed result — red X indicator adds quads => more vertices.
    ike::IkChainEditor editor_failed;
    editor_failed.set_chain(&chain);
    editor_failed.set_last_result(&result);
    cd::ui::renderer::DrawBatcher batcher_failed;
    batcher_failed.begin_frame();
    editor_failed.draw(batcher_failed, theme, bounds);
    const std::size_t verts_failed = batcher_failed.vertex_count();

    // Divergence indicator adds at least one extra quad (4 vertices).
    EXPECT_GT(verts_failed, verts_no_result);
}

// ---------------------------------------------------------------------------
// TEST 9 — NullChainAfterSetDetaches
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, NullChainAfterSetDetaches)
{
    ike::IkChainEditor editor;
    const auto chain = make_line_chain(3);

    editor.set_chain(&chain);
    EXPECT_NO_THROW({
        cd::ui::renderer::DrawBatcher batcher;
        batcher.begin_frame();
        editor.draw(batcher, standard_theme(), standard_bounds());
    });

    // Detach.
    editor.set_chain(nullptr);
    EXPECT_FALSE(editor.selected_joint().has_value());

    // Draw after detach must emit only the background quad (no crash).
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    EXPECT_NO_THROW(editor.draw(batcher, standard_theme(), standard_bounds()));
    EXPECT_EQ(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST 10 — SetCameraDoesNotCrash
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, SetCameraDoesNotCrash)
{
    ike::IkChainEditor editor;
    const auto chain = make_line_chain(3);
    editor.set_chain(&chain);

    // A simple orthographic-style VP matrix (identity-ish, just maps world to
    // NDC with a scale): column-major identity.
    // clang-format off
    const std::array<float, 16> identity_vp {
        1.0F, 0.0F, 0.0F, 0.0F,  // col 0
        0.0F, 1.0F, 0.0F, 0.0F,  // col 1
        0.0F, 0.0F, 1.0F, 0.0F,  // col 2
        0.0F, 0.0F, 0.0F, 1.0F   // col 3
    };
    // clang-format on
    EXPECT_NO_THROW(editor.set_camera(identity_vp));

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    EXPECT_NO_THROW(editor.draw(batcher, standard_theme(), standard_bounds()));

    // At minimum: background quad + separator bar + joints + lines + target marker
    // each contribute 4 vertices (the batcher may merge draw commands, so check
    // vertex_count rather than command_count).
    // A 3-joint chain contributes: background + separator + 2 bone lines +
    // 3 joint circles + 2 target arms = 9 quads = 36 vertices minimum.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(36U));
}

// ---------------------------------------------------------------------------
// TEST 11 — ClearCameraRevertsTo2D
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, ClearCameraRevertsTo2D)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();
    const auto chain = make_line_chain(3);

    // Baseline: 2D mode.
    ike::IkChainEditor editor_2d;
    editor_2d.set_chain(&chain);
    cd::ui::renderer::DrawBatcher b2d;
    b2d.begin_frame();
    editor_2d.draw(b2d, theme, bounds);
    const std::size_t verts_2d = b2d.vertex_count();

    // Set camera then clear it — should revert to same 2D vertex count.
    ike::IkChainEditor editor_clear;
    editor_clear.set_chain(&chain);
    // clang-format off
    editor_clear.set_camera({
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    });
    // clang-format on
    editor_clear.clear_camera();

    cd::ui::renderer::DrawBatcher b_clear;
    b_clear.begin_frame();
    editor_clear.draw(b_clear, theme, bounds);

    EXPECT_EQ(b_clear.vertex_count(), verts_2d);
}

// ---------------------------------------------------------------------------
// TEST 12 — SetChainMutableBinds
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, SetChainMutableBinds)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();

    auto chain = make_line_chain(3);

    ike::IkChainEditor editor_const;
    editor_const.set_chain(&chain);
    cd::ui::renderer::DrawBatcher b_const;
    b_const.begin_frame();
    editor_const.draw(b_const, theme, bounds);

    ike::IkChainEditor editor_mut;
    editor_mut.set_chain_mutable(&chain);
    cd::ui::renderer::DrawBatcher b_mut;
    b_mut.begin_frame();
    editor_mut.draw(b_mut, theme, bounds);

    // Mutable and const chains produce the same draw output.
    EXPECT_EQ(b_mut.vertex_count(), b_const.vertex_count());
}

// ---------------------------------------------------------------------------
// TEST 13 — BeginTargetDragReturnsFalseWhenNoMutableChain
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, BeginTargetDragReturnsFalseWhenNoMutableChain)
{
    ike::IkChainEditor editor;
    const auto chain = make_line_chain(3);
    editor.set_chain(&chain);  // read-only, no mutable

    const cd::ui::widgets::Rect bounds = standard_bounds();
    // Try to drag at the centre of the panel.
    const bool started = editor.begin_target_drag(200.0F, 300.0F, bounds);
    EXPECT_FALSE(started);
    EXPECT_FALSE(editor.is_dragging_target());
}

// ---------------------------------------------------------------------------
// TEST 14 — BeginTargetDragReturnsFalseWhenFarFromTarget
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, BeginTargetDragReturnsFalseWhenFarFromTarget)
{
    ike::IkChainEditor editor;
    auto chain = make_line_chain(3);
    editor.set_chain_mutable(&chain);

    const cd::ui::widgets::Rect bounds = standard_bounds();
    // Click in the top-left corner — far from the target projected position.
    const bool started = editor.begin_target_drag(
        bounds.x + 1.0F, bounds.y + 1.0F, bounds);
    EXPECT_FALSE(started);
}

// ---------------------------------------------------------------------------
// TEST 15 — DragCycleUpdatesMutableTarget
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, DragCycleUpdatesMutableTarget)
{
    ike::IkChainEditor editor;

    // Build a simple 3-joint chain along +X; target at (3, 0, 0).
    auto chain = make_line_chain(3, 1.0F);
    editor.set_chain_mutable(&chain);

    const cd::ui::widgets::Rect bounds = standard_bounds();

    // Determine where the target projects in 2D mode:
    // Content area: x=[6..394], y=[16..594]. range_x=3, range_y=1.
    // Target (3, 0) -> projected_x = 6 + (3/3)*388 = 394, projected_y = 16 + 578 = 594.
    // Move to that point to begin, then drag somewhere far.
    const float target_screen_x = 394.0F;
    const float target_screen_y = 594.0F;

    const auto original_target = chain.end_effector_target;

    const bool started = editor.begin_target_drag(target_screen_x, target_screen_y, bounds);
    ASSERT_TRUE(started);
    EXPECT_TRUE(editor.is_dragging_target());

    // Drag to a significantly different screen position (centre of panel).
    editor.update_target_drag(200.0F, 300.0F, bounds);

    // The end_effector_target must have changed.
    EXPECT_FALSE(
        chain.end_effector_target[0] == original_target[0] &&
        chain.end_effector_target[1] == original_target[1]
    );

    editor.end_target_drag();
    EXPECT_FALSE(editor.is_dragging_target());
}

// ---------------------------------------------------------------------------
// TEST 16 — IsDraggingTargetState
// ---------------------------------------------------------------------------
TEST(IkChainEditorPanel, IsDraggingTargetState)
{
    ike::IkChainEditor editor;
    auto chain = make_line_chain(3, 1.0F);
    editor.set_chain_mutable(&chain);

    EXPECT_FALSE(editor.is_dragging_target());

    const cd::ui::widgets::Rect bounds = standard_bounds();

    // Begin drag at the target projected position (394, 594) as computed above.
    const bool ok = editor.begin_target_drag(394.0F, 594.0F, bounds);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(editor.is_dragging_target());

    editor.end_target_drag();
    EXPECT_FALSE(editor.is_dragging_target());
}
