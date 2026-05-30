// =============================================================================
// CHROMODYNAMIC -- cd::ui::widgets concrete widget catalog tests
//
// Phase 2.2 coverage per ADR-20260530-ui-widget-library. Tests are
// CPU-only -- no font is loaded, no GPU is touched. The `draw` path is
// exercised against a fresh `DrawBatcher` per case to validate that
// emissions happen without a font (textless background-only path) and
// that no allocator / index-buffer assertion trips.
//
// One TEST per state-machine concern. State transitions are the focus
// of this suite; pixel-perfect rendering is verified separately by the
// renderer's own golden-image tests.
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace w = cd::ui::widgets;
namespace r = cd::ui::renderer;

namespace
{

constexpr w::Rect kRect100 { 10.0F, 10.0F, 100.0F, 30.0F };

[[nodiscard]] w::InputState make_input(float mx, float my,
                                       bool down, bool pressed, bool released,
                                       bool focused = false)
{
    w::InputState in;
    in.pointer.mouse_x       = mx;
    in.pointer.mouse_y       = my;
    in.pointer.left_down     = down;
    in.pointer.left_pressed  = pressed;
    in.pointer.left_released = released;
    in.focused               = focused;
    return in;
}

[[nodiscard]] w::InputState make_key_input(w::KeySignal s,
                                           std::uint32_t cp,
                                           bool focused = true)
{
    w::InputState in;
    in.focused = focused;
    // Caller must keep the storage alive for the duration of tick().
    // The static here is a scratch slot reused across calls -- tests do
    // not overlap so the lifetime is fine.
    thread_local w::KeyInput scratch;
    scratch.signal    = s;
    scratch.codepoint = cp;
    in.keys = std::span<const w::KeyInput>(&scratch, 1U);
    return in;
}

}  // namespace

// =============================================================================
// Button
// =============================================================================

TEST(UiWidgetsButton, ClickFiresOnReleaseInside)
{
    int clicks = 0;
    w::Button btn { "OK", [&] { ++clicks; } };
    btn.set_rect(kRect100);

    // Press inside.
    EXPECT_FALSE(btn.tick(make_input(50.0F, 25.0F, true,  true,  false)));
    EXPECT_TRUE(btn.state().pressed);
    EXPECT_TRUE(btn.state().hovered);
    EXPECT_EQ(clicks, 0);

    // Release inside -> click.
    EXPECT_TRUE(btn.tick(make_input(50.0F, 25.0F, false, false, true)));
    EXPECT_FALSE(btn.state().pressed);
    EXPECT_EQ(clicks, 1);
}

TEST(UiWidgetsButton, DragOutsideThenReleaseDoesNotClick)
{
    int clicks = 0;
    w::Button btn { "OK", [&] { ++clicks; } };
    btn.set_rect(kRect100);

    // Press inside.
    EXPECT_FALSE(btn.tick(make_input(50.0F, 25.0F, true, true, false)));
    EXPECT_TRUE(btn.state().pressed);

    // Drag outside (still down).
    EXPECT_FALSE(btn.tick(make_input(500.0F, 500.0F, true, false, false)));
    EXPECT_FALSE(btn.state().pressed);
    EXPECT_FALSE(btn.state().hovered);

    // Release outside -- no click.
    EXPECT_FALSE(btn.tick(make_input(500.0F, 500.0F, false, false, true)));
    EXPECT_EQ(clicks, 0);
}

TEST(UiWidgetsButton, EnterKeyActivatesWhenFocused)
{
    int clicks = 0;
    w::Button btn { "OK", [&] { ++clicks; } };
    btn.set_rect(kRect100);
    EXPECT_TRUE(btn.tick(make_key_input(w::KeySignal::kEnter, 0U, /*focused=*/true)));
    EXPECT_EQ(clicks, 1);

    // No focus -> Enter is silently dropped.
    EXPECT_FALSE(btn.tick(make_key_input(w::KeySignal::kEnter, 0U, /*focused=*/false)));
    EXPECT_EQ(clicks, 1);
}

TEST(UiWidgetsButton, DrawWithoutFontEmitsBackgroundOnly)
{
    w::Button btn { "OK" };
    btn.set_rect(kRect100);
    r::DrawBatcher batcher;
    batcher.begin_frame();
    btn.draw(batcher, /*font=*/nullptr, w::Theme{});
    EXPECT_GT(batcher.vertex_count(), 0U);
    EXPECT_GT(batcher.index_count(), 0U);
}

// =============================================================================
// TextInput
// =============================================================================

TEST(UiWidgetsTextInput, CharacterInsertAdvancesCursor)
{
    std::string last;
    w::TextInput ti { "ab", [&](std::string_view v) { last.assign(v); } };
    ti.set_rect(kRect100);
    ti.set_cursor(2U);
    EXPECT_EQ(ti.text(), "ab");
    EXPECT_EQ(ti.cursor(), 2U);

    EXPECT_TRUE(ti.tick(make_key_input(w::KeySignal::kCharacter, 'c')));
    EXPECT_EQ(ti.text(), "abc");
    EXPECT_EQ(ti.cursor(), 3U);
    EXPECT_EQ(last, "abc");
}

TEST(UiWidgetsTextInput, BackspaceErasesPriorCharacter)
{
    w::TextInput ti { "abc" };
    ti.set_rect(kRect100);
    ti.set_cursor(3U);

    EXPECT_TRUE(ti.tick(make_key_input(w::KeySignal::kBackspace, 0U)));
    EXPECT_EQ(ti.text(), "ab");
    EXPECT_EQ(ti.cursor(), 2U);

    // Backspace at start = no-op.
    ti.set_cursor(0U);
    EXPECT_FALSE(ti.tick(make_key_input(w::KeySignal::kBackspace, 0U)));
    EXPECT_EQ(ti.text(), "ab");
}

TEST(UiWidgetsTextInput, ArrowKeysMoveCursorAndIgnoreOutsideFocus)
{
    w::TextInput ti { "abc" };
    ti.set_rect(kRect100);
    ti.set_cursor(3U);

    EXPECT_FALSE(ti.tick(make_key_input(w::KeySignal::kLeft, 0U)));
    EXPECT_EQ(ti.cursor(), 2U);
    EXPECT_FALSE(ti.tick(make_key_input(w::KeySignal::kHome, 0U)));
    EXPECT_EQ(ti.cursor(), 0U);
    EXPECT_FALSE(ti.tick(make_key_input(w::KeySignal::kEnd, 0U)));
    EXPECT_EQ(ti.cursor(), 3U);

    // Unfocused -- key ignored.
    EXPECT_FALSE(ti.tick(make_key_input(w::KeySignal::kLeft, 0U, /*focused=*/false)));
    EXPECT_EQ(ti.cursor(), 3U);
}

TEST(UiWidgetsTextInput, DeleteRemovesCharacterAtCursor)
{
    w::TextInput ti { "abc" };
    ti.set_rect(kRect100);
    ti.set_cursor(1U);

    EXPECT_TRUE(ti.tick(make_key_input(w::KeySignal::kDelete, 0U)));
    EXPECT_EQ(ti.text(), "ac");
    EXPECT_EQ(ti.cursor(), 1U);

    // Delete at end = no-op.
    ti.set_cursor(ti.text().size());
    EXPECT_FALSE(ti.tick(make_key_input(w::KeySignal::kDelete, 0U)));
    EXPECT_EQ(ti.text(), "ac");
}

// =============================================================================
// Slider
// =============================================================================

TEST(UiWidgetsSlider, ClickInsideSetsValueProportionally)
{
    float last = -1.0F;
    w::Slider s { 0.0F, [&](float v) { last = v; } };
    s.set_rect(kRect100);   // x=10 w=100

    // Click at mouse_x = 60 (= 50% along the track).
    EXPECT_TRUE(s.tick(make_input(60.0F, 25.0F, true, true, false)));
    EXPECT_NEAR(s.value(), 0.5F, 1e-4F);
    EXPECT_NEAR(last,      0.5F, 1e-4F);
}

TEST(UiWidgetsSlider, DragUpdatesValueWhileDown)
{
    int change_count = 0;
    w::Slider s { 0.0F, [&](float /*v*/) { ++change_count; } };
    s.set_rect(kRect100);   // x=10 w=100

    // Press at left edge -> value = 0.
    EXPECT_FALSE(s.tick(make_input(10.0F, 25.0F, true, true, false)));
    EXPECT_NEAR(s.value(), 0.0F, 1e-4F);
    EXPECT_EQ(change_count, 0);  // value did not change from initial 0

    // Drag to right edge -> value = 1.
    EXPECT_TRUE(s.tick(make_input(110.0F, 25.0F, true, false, false)));
    EXPECT_NEAR(s.value(), 1.0F, 1e-4F);
    EXPECT_EQ(change_count, 1);

    // Release -- dragging clears.
    s.tick(make_input(110.0F, 25.0F, false, false, true));
}

TEST(UiWidgetsSlider, ArrowKeysNudgeByStep)
{
    w::Slider s { 0.5F };
    s.set_rect(kRect100);
    s.set_step(0.1F);

    EXPECT_TRUE(s.tick(make_key_input(w::KeySignal::kRight, 0U)));
    EXPECT_NEAR(s.value(), 0.6F, 1e-4F);

    EXPECT_TRUE(s.tick(make_key_input(w::KeySignal::kLeft, 0U)));
    EXPECT_NEAR(s.value(), 0.5F, 1e-4F);
}

// =============================================================================
// Toggle
// =============================================================================

TEST(UiWidgetsToggle, ClickFlipsValue)
{
    int change_count = 0;
    w::Toggle t { false, [&](bool /*v*/) { ++change_count; } };
    t.set_rect(kRect100);
    EXPECT_FALSE(t.value());

    t.tick(make_input(50.0F, 25.0F, true,  true,  false));
    EXPECT_TRUE(t.tick(make_input(50.0F, 25.0F, false, false, true)));
    EXPECT_TRUE(t.value());
    EXPECT_EQ(change_count, 1);

    t.tick(make_input(50.0F, 25.0F, true,  true,  false));
    EXPECT_TRUE(t.tick(make_input(50.0F, 25.0F, false, false, true)));
    EXPECT_FALSE(t.value());
    EXPECT_EQ(change_count, 2);
}

TEST(UiWidgetsToggle, SpaceKeyFlipsWhenFocused)
{
    w::Toggle t { false };
    t.set_rect(kRect100);
    EXPECT_TRUE(t.tick(make_key_input(w::KeySignal::kSpace, 0U, true)));
    EXPECT_TRUE(t.value());
    EXPECT_TRUE(t.tick(make_key_input(w::KeySignal::kEnter, 0U, true)));
    EXPECT_FALSE(t.value());

    // Unfocused -- ignored.
    EXPECT_FALSE(t.tick(make_key_input(w::KeySignal::kSpace, 0U, false)));
    EXPECT_FALSE(t.value());
}

// =============================================================================
// Checkbox
// =============================================================================

TEST(UiWidgetsCheckbox, ClickTogglesValue)
{
    int change_count = 0;
    w::Checkbox c { false, "Enabled", [&](bool /*v*/) { ++change_count; } };
    c.set_rect(kRect100);

    c.tick(make_input(50.0F, 25.0F, true, true, false));
    EXPECT_TRUE(c.tick(make_input(50.0F, 25.0F, false, false, true)));
    EXPECT_TRUE(c.value());
    EXPECT_EQ(change_count, 1);

    // Outside-then-release does NOT flip.
    c.tick(make_input(50.0F, 25.0F, true, true, false));
    EXPECT_FALSE(c.tick(make_input(500.0F, 500.0F, false, false, true)));
    EXPECT_TRUE(c.value());
    EXPECT_EQ(change_count, 1);
}

TEST(UiWidgetsCheckbox, SetValueDoesNotFireCallback)
{
    int change_count = 0;
    w::Checkbox c { false, "Lbl", [&](bool /*v*/) { ++change_count; } };
    c.set_rect(kRect100);
    c.set_value(true);
    EXPECT_TRUE(c.value());
    EXPECT_EQ(change_count, 0);
}

// =============================================================================
// Dropdown
// =============================================================================

TEST(UiWidgetsDropdown, HeaderClickTogglesExpansion)
{
    std::vector<std::string> opts { "A", "B", "C" };
    w::Dropdown d { opts, 0U };
    d.set_rect(kRect100);
    d.set_option_height(20.0F);

    EXPECT_FALSE(d.expanded());

    // Header click expands.
    d.tick(make_input(50.0F, 25.0F, true, true, false));
    d.tick(make_input(50.0F, 25.0F, false, false, true));
    EXPECT_TRUE(d.expanded());

    // Header click collapses (no selection change).
    d.tick(make_input(50.0F, 25.0F, true, true, false));
    d.tick(make_input(50.0F, 25.0F, false, false, true));
    EXPECT_FALSE(d.expanded());
    EXPECT_EQ(d.selected(), 0U);
}

TEST(UiWidgetsDropdown, SelectingOptionCommitsAndCollapses)
{
    std::vector<std::string> opts { "A", "B", "C" };
    std::size_t last = 0U;
    int change_count = 0;
    w::Dropdown d {
        opts, 0U,
        [&](std::size_t v) { last = v; ++change_count; }
    };
    d.set_rect(kRect100);    // header at y=10..40, opt_h=20
    d.set_option_height(20.0F);
    d.set_expanded(true);

    // Move mouse over option B (idx=1): y in [40+20, 40+40) = [60, 80).
    d.tick(make_input(50.0F, 65.0F, false, false, false));
    EXPECT_EQ(d.hovered_option(), 1U);

    // Click option B.
    d.tick(make_input(50.0F, 65.0F, true,  true,  false));
    d.tick(make_input(50.0F, 65.0F, false, false, true));
    EXPECT_EQ(d.selected(), 1U);
    EXPECT_FALSE(d.expanded());
    EXPECT_EQ(last, 1U);
    EXPECT_EQ(change_count, 1);
}

TEST(UiWidgetsDropdown, ArrowKeysChangeSelectionWhenFocused)
{
    std::vector<std::string> opts { "A", "B", "C" };
    w::Dropdown d { opts, 1U };
    d.set_rect(kRect100);

    EXPECT_TRUE(d.tick(make_key_input(w::KeySignal::kDown, 0U, true)));
    EXPECT_EQ(d.selected(), 2U);
    EXPECT_TRUE(d.tick(make_key_input(w::KeySignal::kUp, 0U, true)));
    EXPECT_EQ(d.selected(), 1U);

    // Saturation: can't go below 0 / above last.
    EXPECT_TRUE(d.tick(make_key_input(w::KeySignal::kUp, 0U, true)));
    EXPECT_EQ(d.selected(), 0U);
    EXPECT_FALSE(d.tick(make_key_input(w::KeySignal::kUp, 0U, true)));
    EXPECT_EQ(d.selected(), 0U);
}

// =============================================================================
// Modal
// =============================================================================

TEST(UiWidgetsModal, EscapeWhileFocusedCloses)
{
    int close_count = 0;
    w::Modal m { true, [&] { ++close_count; } };
    m.set_rect(w::Rect { 0.0F, 0.0F, 800.0F, 600.0F });
    m.set_content_rect(w::Rect { 200.0F, 150.0F, 400.0F, 300.0F });

    EXPECT_TRUE(m.tick(make_key_input(w::KeySignal::kEscape, 0U, true)));
    EXPECT_FALSE(m.visible());
    EXPECT_EQ(close_count, 1);

    // Already hidden -> no-op even with Escape.
    EXPECT_FALSE(m.tick(make_key_input(w::KeySignal::kEscape, 0U, true)));
    EXPECT_EQ(close_count, 1);
}

TEST(UiWidgetsModal, OutsideClickClosesInsideClickStaysOpen)
{
    int close_count = 0;
    w::Modal m { true, [&] { ++close_count; } };
    m.set_rect(w::Rect { 0.0F, 0.0F, 800.0F, 600.0F });
    m.set_content_rect(w::Rect { 200.0F, 150.0F, 400.0F, 300.0F });

    // Click INSIDE content -- modal stays open.
    m.tick(make_input(400.0F, 300.0F, true,  true,  false));
    EXPECT_FALSE(m.tick(make_input(400.0F, 300.0F, false, false, true)));
    EXPECT_TRUE(m.visible());
    EXPECT_EQ(close_count, 0);

    // Click on dim layer (outside content but inside rect) -- closes.
    m.tick(make_input(50.0F, 50.0F, true,  true,  false));
    EXPECT_TRUE(m.tick(make_input(50.0F, 50.0F, false, false, true)));
    EXPECT_FALSE(m.visible());
    EXPECT_EQ(close_count, 1);
}

// =============================================================================
// Cross-widget headless draw smoke
// =============================================================================

TEST(UiWidgetsDraw, AllWidgetsEmitWithoutFontWithoutAsserts)
{
    r::DrawBatcher batcher;
    batcher.begin_frame();

    const w::Theme theme {};

    w::Button btn { "Go" };
    btn.set_rect(kRect100);
    btn.draw(batcher, nullptr, theme);

    w::TextInput ti { "hi" };
    ti.set_rect(kRect100);
    ti.draw(batcher, nullptr, theme);

    w::Slider s { 0.4F };
    s.set_rect(kRect100);
    s.draw(batcher, nullptr, theme);

    w::Toggle t { true };
    t.set_rect(kRect100);
    t.draw(batcher, nullptr, theme);

    w::Checkbox c { true, "On" };
    c.set_rect(kRect100);
    c.draw(batcher, nullptr, theme);

    std::vector<std::string> opts { "A", "B" };
    w::Dropdown d { opts, 0U };
    d.set_rect(kRect100);
    d.set_expanded(true);
    d.draw(batcher, nullptr, theme);

    w::Modal m { true };
    m.set_rect(w::Rect { 0.0F, 0.0F, 800.0F, 600.0F });
    m.set_content_rect(w::Rect { 100.0F, 100.0F, 400.0F, 300.0F });
    m.draw(batcher, nullptr, theme);

    EXPECT_GT(batcher.vertex_count(), 0U);
    EXPECT_GT(batcher.index_count(), 0U);
    EXPECT_GT(batcher.command_count(), 0U);
}
