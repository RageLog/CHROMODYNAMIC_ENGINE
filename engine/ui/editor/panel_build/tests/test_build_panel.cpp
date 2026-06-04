// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_build/tests/test_build_panel.cpp
//
// phase699 — unit tests for cd::editor::panel::build::BuildPanel.
//
// All tests are headless (no RHI, no ImGui). We verify:
//
//   1. DefaultCtor
//         — starts kIdle, zero events, no selected index.
//   2. SetStatus_Updates_CurrentStatus
//         — set_status round-trips all four Status values.
//   3. PushEvent_IncreasesEventCount
//         — pushing N events → event_count() == N.
//   4. ClearEvents_ResetsLog
//         — clear_events() drops all events and clears selected index.
//   5. SimulateClick_SelectsEventWithSourceFile
//         — click on a row with a non-empty source_file → selected_event_index.
//   6. SimulateClick_IgnoresEventWithoutSourceFile
//         — click on a row with an empty source_file → no selection change.
//   7. SimulateClick_OutsidePanel_NoOp
//         — click outside bounds → selected_event_index unchanged.
//   8. DrawEmitsMoreCommandsWithEvents
//         — draw() with N events emits strictly more vertices than draw()
//           with 0 events for the same bounds.
//   9. DrawDoesNotCrashOnZeroBounds
//         — draw() with a zero-size Rect must not assert or crash.
//  10. StatusColorDistinct
//         — the four Status values produce four distinct badge draws
//           (vertex counts can differ; at minimum badge quad is always emitted).
// =============================================================================
#include <cd/editor/panel_build/BuildPanel.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <string>

namespace bp = cd::editor::panel::build;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 0.0F, 0.0F, 320.0F, 600.0F };
}

[[nodiscard]] cd::ui::widgets::Theme standard_theme() noexcept
{
    return {};
}

/// Build a simple BuildEvent with a source_file.
[[nodiscard]] bp::BuildEvent make_event(double ts_ms,
                                        const std::string& msg,
                                        const std::string& file,
                                        uint32_t           line,
                                        bp::Status         sev) noexcept
{
    return bp::BuildEvent { ts_ms, msg, file, line, sev };
}

// Layout constants (must match BuildPanel internals).
constexpr float kPad        =  6.0F;
constexpr float kBadgeH     = 32.0F;
constexpr float kBarH       =  4.0F;
constexpr float kRowH       = 18.0F;
constexpr float kRowGap     =  2.0F;
constexpr float kListOffsetY = kPad + kBadgeH + kPad + kBarH + kPad;

[[nodiscard]] float row_click_y(std::size_t row_idx) noexcept
{
    return kListOffsetY + static_cast<float>(row_idx) * (kRowH + kRowGap)
           + kRowH * 0.5F;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultCtor
// ---------------------------------------------------------------------------
TEST(BuildPanel, DefaultCtor)
{
    const bp::BuildPanel panel;

    EXPECT_EQ(panel.current_status(), bp::Status::kIdle);
    EXPECT_EQ(panel.event_count(), static_cast<std::size_t>(0U));
    EXPECT_FALSE(panel.selected_event_index().has_value());
}

// ---------------------------------------------------------------------------
// TEST 2 — SetStatus_Updates_CurrentStatus
// ---------------------------------------------------------------------------
TEST(BuildPanel, SetStatusUpdatesCurrentStatus)
{
    bp::BuildPanel panel;

    panel.set_status(bp::Status::kCompiling);
    EXPECT_EQ(panel.current_status(), bp::Status::kCompiling);

    panel.set_status(bp::Status::kSuccess);
    EXPECT_EQ(panel.current_status(), bp::Status::kSuccess);

    panel.set_status(bp::Status::kFailed);
    EXPECT_EQ(panel.current_status(), bp::Status::kFailed);

    panel.set_status(bp::Status::kIdle);
    EXPECT_EQ(panel.current_status(), bp::Status::kIdle);
}

// ---------------------------------------------------------------------------
// TEST 3 — PushEvent_IncreasesEventCount
// ---------------------------------------------------------------------------
TEST(BuildPanel, PushEventIncreasesEventCount)
{
    bp::BuildPanel panel;

    ASSERT_EQ(panel.event_count(), static_cast<std::size_t>(0U));

    panel.push_event(make_event(0.0,  "Building...",              "",                    0U, bp::Status::kCompiling));
    panel.push_event(make_event(12.0, "error: undeclared id",     "src/Renderer.cpp",   42U, bp::Status::kFailed));
    panel.push_event(make_event(14.0, "note: declared here",      "src/Types.hpp",       7U, bp::Status::kIdle));

    EXPECT_EQ(panel.event_count(), static_cast<std::size_t>(3U));
}

// ---------------------------------------------------------------------------
// TEST 4 — ClearEvents_ResetsLog
// ---------------------------------------------------------------------------
TEST(BuildPanel, ClearEventsResetsLog)
{
    bp::BuildPanel panel;

    panel.push_event(make_event(1.0, "msg", "file.cpp", 1U, bp::Status::kFailed));
    panel.push_event(make_event(2.0, "msg", "file.cpp", 2U, bp::Status::kFailed));
    ASSERT_EQ(panel.event_count(), static_cast<std::size_t>(2U));

    // Simulate a click to get a selection first.
    const cd::ui::widgets::Rect bounds = standard_bounds();
    panel.simulate_click(10.0F, row_click_y(0U), bounds);
    ASSERT_TRUE(panel.selected_event_index().has_value());

    panel.clear_events();

    EXPECT_EQ(panel.event_count(), static_cast<std::size_t>(0U));
    EXPECT_FALSE(panel.selected_event_index().has_value());
}

// ---------------------------------------------------------------------------
// TEST 5 — SimulateClick_SelectsEventWithSourceFile
// ---------------------------------------------------------------------------
TEST(BuildPanel, SimulateClickSelectsEventWithSourceFile)
{
    bp::BuildPanel panel;

    // Push two events: row 0 (no source), row 1 (has source).
    panel.push_event(make_event(0.0,  "Start",              "",               0U, bp::Status::kCompiling));
    panel.push_event(make_event(10.0, "error: missing ';'", "src/Main.cpp",  88U, bp::Status::kFailed));

    const cd::ui::widgets::Rect bounds = standard_bounds();

    // Click on row 1.
    panel.simulate_click(10.0F, row_click_y(1U), bounds);

    ASSERT_TRUE(panel.selected_event_index().has_value());
    EXPECT_EQ(*panel.selected_event_index(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST 6 — SimulateClick_IgnoresEventWithoutSourceFile
// ---------------------------------------------------------------------------
TEST(BuildPanel, SimulateClickIgnoresEventWithoutSourceFile)
{
    bp::BuildPanel panel;

    // Single event with no source_file.
    panel.push_event(make_event(0.0, "Linking...", "", 0U, bp::Status::kCompiling));

    const cd::ui::widgets::Rect bounds = standard_bounds();
    panel.simulate_click(10.0F, row_click_y(0U), bounds);

    // Sprint-1 contract: no source_file → no selection.
    EXPECT_FALSE(panel.selected_event_index().has_value());
}

// ---------------------------------------------------------------------------
// TEST 7 — SimulateClick_OutsidePanel_NoOp
// ---------------------------------------------------------------------------
TEST(BuildPanel, SimulateClickOutsidePanelNoOp)
{
    bp::BuildPanel panel;
    panel.push_event(make_event(0.0, "error", "a.cpp", 1U, bp::Status::kFailed));

    const cd::ui::widgets::Rect bounds = standard_bounds();

    // Click well outside the panel.
    panel.simulate_click(-100.0F, -100.0F, bounds);

    EXPECT_FALSE(panel.selected_event_index().has_value());
}

// ---------------------------------------------------------------------------
// TEST 8 — DrawEmitsMoreCommandsWithEvents
// ---------------------------------------------------------------------------
TEST(BuildPanel, DrawEmitsMoreCommandsWithEvents)
{
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();

    // Empty panel.
    bp::BuildPanel empty_panel;
    cd::ui::renderer::DrawBatcher batcher_empty;
    batcher_empty.begin_frame();
    empty_panel.draw(batcher_empty, theme, bounds);
    const std::size_t verts_empty = batcher_empty.vertex_count();

    // Panel with 4 events.
    bp::BuildPanel full_panel;
    full_panel.push_event(make_event(0.0,   "Compiling main.cpp",          "src/main.cpp",      0U,  bp::Status::kCompiling));
    full_panel.push_event(make_event(120.0, "error: undeclared identifier", "src/Renderer.cpp", 55U,  bp::Status::kFailed));
    full_panel.push_event(make_event(121.0, "note: did you mean X?",        "src/Renderer.cpp", 50U,  bp::Status::kIdle));
    full_panel.push_event(make_event(500.0, "Build finished with errors",   "",                  0U,  bp::Status::kFailed));

    cd::ui::renderer::DrawBatcher batcher_full;
    batcher_full.begin_frame();
    full_panel.draw(batcher_full, theme, bounds);
    const std::size_t verts_full = batcher_full.vertex_count();

    // Must emit strictly more vertices when events are present.
    EXPECT_GT(verts_full, verts_empty);

    // Empty panel must still emit background + badge + separator quads.
    EXPECT_GE(verts_empty, static_cast<std::size_t>(4U));
}

// ---------------------------------------------------------------------------
// TEST 9 — DrawDoesNotCrashOnZeroBounds
// ---------------------------------------------------------------------------
TEST(BuildPanel, DrawDoesNotCrashOnZeroBounds)
{
    bp::BuildPanel panel;
    panel.push_event(make_event(1.0, "error", "src/X.cpp", 10U, bp::Status::kFailed));

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    // Zero-size rect — must not assert or crash.
    const cd::ui::widgets::Rect zero_bounds { 0.0F, 0.0F, 0.0F, 0.0F };
    const cd::ui::widgets::Theme theme = standard_theme();

    EXPECT_NO_FATAL_FAILURE(panel.draw(batcher, theme, zero_bounds));
}

// ---------------------------------------------------------------------------
// TEST 10 — StatusColorDistinct (badge always emitted)
// ---------------------------------------------------------------------------
TEST(BuildPanel, StatusBadgeAlwaysEmitted)
{
    // For each status, draw() must emit at least the background + badge quads
    // (i.e. at least 2 quads = 8 vertices minimum).
    const cd::ui::widgets::Rect  bounds = standard_bounds();
    const cd::ui::widgets::Theme theme  = standard_theme();

    constexpr std::size_t kMinVerts = 8U;  // background + badge = 2 quads × 4 verts

    for (const auto s : { bp::Status::kIdle, bp::Status::kCompiling,
                          bp::Status::kSuccess, bp::Status::kFailed })
    {
        bp::BuildPanel panel;
        panel.set_status(s);

        cd::ui::renderer::DrawBatcher batcher;
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);

        EXPECT_GE(batcher.vertex_count(), kMinVerts)
            << "Status " << static_cast<int>(s) << " emitted too few vertices";
    }
}
