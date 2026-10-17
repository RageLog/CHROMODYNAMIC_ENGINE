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
#include <cd/ui/ChromaUi.hpp>
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

// =============================================================================
// ChromaUI Immediate-Mode Catalog 2.0 + In-Game HUD + Standalone Node/Curve Lab
// =============================================================================

TEST(ChromaUiCatalog2, ThemesCalcTextSizeWidgetsNodeGraphAndHudEmitCleanly)
{
    for (int t = 0; t < 5; ++t)
    {
        ImGui::ApplyTheme(t);
        EXPECT_EQ(cd::ui::chroma::state().theme_index, t);
    }
    ImGui::ApplyTheme(0);

    const ImVec2 sz = ImGui::CalcTextSize("CHROMODYNAMIC STUDIO##hidden_id");
    EXPECT_GT(sz.x, 40.0F);
    EXPECT_GT(sz.y, 8.0F);

    cd::ui::chroma::Context ctx;
    ctx.new_frame();

    ImGui::DockSpace(1U);
    if (ImGui::Begin("Studio Workbench"))
    {
        bool tog = true;
        int  rad = 0;
        float knob_v = 0.5F;
        float curve_pts[8] = { 0.0F, 0.0F, 0.25F, 0.75F, 0.75F, 0.25F, 1.0F, 1.0F };
        float time_s = 1.2F;
        const float kfs[3] = { 0.5F, 1.2F, 2.5F };

        (void)ImGui::ToggleSwitch("Enable Bloom", &tog);
        (void)ImGui::RadioButton("Mode A", &rad, 0);
        (void)ImGui::KnobFloat("Roughness", &knob_v, 0.0F, 1.0F, 20.0F);
        (void)ImGui::CurveEditor("Response", curve_pts, ImVec2(180.0F, 80.0F));
        (void)ImGui::TimelineScrubber("Track", &time_s, 3.0F, kfs, 3);

        if (ImGui::BeginNodeGraph("TestGraph", ImVec2(320.0F, 140.0F)))
        {
            ImGui::BeginNode(1U, "Sampler", ImVec2(10.0F, 10.0F));
            ImGui::NodePin(11U, "Out", true);
            ImGui::EndNode();

            ImGui::BeginNode(2U, "Output", ImVec2(180.0F, 10.0F));
            ImGui::NodePin(21U, "In", false);
            ImGui::EndNode();

            ImGui::NodeLink(11U, 21U);
            ImGui::EndNodeGraph();
        }
    }
    ImGui::End();

    ImGui::PushToast("Build Complete", "All shaders compiled", 0, 2.0F);
    EXPECT_FALSE(cd::ui::chroma::state().toasts.empty());

    const ImVec2 blips[2] = { ImVec2(1.0F, -1.0F), ImVec2(-2.0F, 1.5F) };
    const char* slots[8]  = { "A", "B", "C", "D", "E", "F", "G", "H" };
    const float cds[8]    = { 0.0F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F };
    cd::ui::chroma::hud::DrawHealthShieldBar(ImVec2(20.0F, 500.0F), 240.0F, 85.0F, 100.0F, 50.0F, 100.0F, 0.8F);
    cd::ui::chroma::hud::DrawDynamicCrosshair(ImVec2(400.0F, 300.0F), 10.0F);
    cd::ui::chroma::hud::DrawMinimapRadar(ImVec2(700.0F, 100.0F), 64.0F, 0.5F, blips);
    (void)cd::ui::chroma::hud::DrawInventoryHotbar(ImVec2(400.0F, 580.0F), 0, slots, cds);
    cd::ui::chroma::hud::DrawWorldSpaceEntityTag(ImVec2(350.0F, 220.0F), "Crate_01", 12.4F, 0.9F, true);

    r::DrawBatcher batcher;
    batcher.begin_frame();
    cd::ui::chroma::state().bg_dl.replay_into(batcher);
    cd::ui::chroma::state().win_dl.replay_into(batcher);
    cd::ui::chroma::state().fg_dl.replay_into(batcher);

    EXPECT_GT(batcher.vertex_count(), 100U);
    EXPECT_GT(batcher.index_count(), 150U);
}

TEST(Widgets, ChromaUiInteractiveSliderInlineEditAndColorPopupOcclusion)
{
    auto& st = cd::ui::chroma::state();
    st = cd::ui::chroma::State {};
    st.io.DisplaySize = ImVec2(1280.0F, 720.0F);
    st.main_viewport.Size = st.io.DisplaySize;
    st.main_viewport.WorkSize = st.io.DisplaySize;

    // 1. SliderFloat inline numeric edit: Ctrl+Click -> type "0.75" -> Enter
    float slider_val = 0.50F;
    st.window_stack.clear();
    ImGui::SetNextWindowPos(ImVec2(100.0F, 100.0F));
    ImGui::SetNextWindowSize(ImVec2(320.0F, 240.0F));
    st.io.MousePos = ImVec2(150.0F, 145.0F);
    st.mouse_clicked[0] = true;
    st.mouse_down[0] = true;
    st.mod_ctrl = true;
    if (ImGui::Begin("InlineEditWin"))
    {
        (void)ImGui::SliderFloat("Roughness", &slider_val, 0.0F, 1.0F);
    }
    ImGui::End();
    EXPECT_NE(st.inline_edit_id, 0U);
    EXPECT_TRUE(st.inline_edit_select_all);

    // Frame 2: type "0.75" and press Enter -> replaces pre-filled "0.500"
    st.window_stack.clear();
    st.mouse_clicked[0] = false;
    st.mouse_down[0] = false;
    st.mod_ctrl = false;
    st.input_chars = "0.75";
    st.keys_pressed[ImGuiKey_Enter] = true;
    ImGui::SetNextWindowPos(ImVec2(100.0F, 100.0F));
    ImGui::SetNextWindowSize(ImVec2(320.0F, 240.0F));
    bool slider_committed = false;
    if (ImGui::Begin("InlineEditWin"))
    {
        slider_committed = ImGui::SliderFloat("Roughness", &slider_val, 0.0F, 1.0F);
    }
    ImGui::End();
    EXPECT_TRUE(slider_committed);
    EXPECT_NEAR(slider_val, 0.75F, 1e-4F);
    EXPECT_EQ(st.inline_edit_id, 0U);

    // 2. ColorEdit3 swatch click -> RGB slider drag (even when mp.y drifts vertically) + background occlusion
    float rgb[3] = { 0.2F, 0.4F, 0.6F };
    st.window_stack.clear();
    st.input_chars.clear();
    st.keys_pressed[ImGuiKey_Enter] = false;
    ImGui::SetNextWindowPos(ImVec2(100.0F, 100.0F));
    ImGui::SetNextWindowSize(ImVec2(360.0F, 300.0F));
    if (ImGui::Begin("ColorWin"))
    {
        (void)ImGui::ColorEdit3("Albedo", rgb);
    }
    ImGui::End();
    const ImVec2 swatch_click_pos { st.last_item_max.x - 60.0F, st.last_item_min.y + 8.0F };

    // Click swatch to open popup
    st.window_stack.clear();
    st.io.MousePos = swatch_click_pos;
    st.mouse_clicked[0] = true;
    st.mouse_down[0] = true;
    ImGui::SetNextWindowPos(ImVec2(100.0F, 100.0F));
    ImGui::SetNextWindowSize(ImVec2(360.0F, 300.0F));
    if (ImGui::Begin("ColorWin"))
    {
        (void)ImGui::ColorEdit3("Albedo", rgb);
    }
    ImGui::End();
    EXPECT_NE(st.open_popup_id, 0U);

    // Next frame: click inside R bar and verify background button at same position is occluded
    const ImVec2 r_bar_pos { st.open_popup_pos.x + 80.0F, st.open_popup_pos.y + 36.0F };
    st.window_stack.clear();
    st.io.MousePos = r_bar_pos;
    st.mouse_clicked[0] = true;
    st.mouse_down[0] = true;
    bool bg_button_clicked = false;
    ImGui::SetNextWindowPos(ImVec2(100.0F, 100.0F));
    ImGui::SetNextWindowSize(ImVec2(360.0F, 300.0F));
    if (ImGui::Begin("ColorWin"))
    {
        (void)ImGui::ColorEdit3("Albedo", rgb);
        ImGui::SetCursorScreenPos(ImVec2(r_bar_pos.x - 10.0F, r_bar_pos.y - 5.0F));
        bg_button_clicked = ImGui::Button("OccludedButton", ImVec2(120.0F, 24.0F));
    }
    ImGui::End();
    EXPECT_FALSE(bg_button_clicked);

    // Drag horizontally while mp.y drifts 40px below the R bar
    st.window_stack.clear();
    st.mouse_clicked[0] = false;
    st.mouse_down[0] = true;
    st.io.MousePos = ImVec2(st.open_popup_pos.x + 170.0F, st.open_popup_pos.y + 85.0F);
    bool color_dragged = false;
    ImGui::SetNextWindowPos(ImVec2(100.0F, 100.0F));
    ImGui::SetNextWindowSize(ImVec2(360.0F, 300.0F));
    if (ImGui::Begin("ColorWin"))
    {
        color_dragged = ImGui::ColorEdit3("Albedo", rgb);
    }
    ImGui::End();
    EXPECT_TRUE(color_dragged);
    EXPECT_GT(rgb[0], 0.80F);
}

TEST(Widgets, ChromaUiInteractiveCurveNodeChildAndDynamicDocking)
{
    auto& st = cd::ui::chroma::state();
    st = cd::ui::chroma::State {};
    st.io.DisplaySize = ImVec2(1280.0F, 720.0F);
    st.main_viewport.Size = st.io.DisplaySize;
    st.main_viewport.WorkSize = st.io.DisplaySize;

    // 1. CurveEditor control point multi-frame drag
    float curve[8] = { 0.0F, 0.0F, 0.30F, 0.30F, 0.70F, 0.70F, 1.0F, 1.0F };
    ImGui::SetNextWindowPos(ImVec2(50.0F, 80.0F));
    ImGui::SetNextWindowSize(ImVec2(320.0F, 260.0F));
    if (ImGui::Begin("CurveWin"))
    {
        (void)ImGui::CurveEditor("TestCurve", curve, ImVec2(200.0F, 100.0F));
    }
    ImGui::End();
    const ImVec2 c_min = st.last_item_min;
    const ImVec2 c_max = st.last_item_max;
    const ImVec2 pt1_screen {
        c_min.x + 6.0F + curve[2] * ((c_max.x - c_min.x) - 12.0F),
        c_max.y - 6.0F - curve[3] * ((c_max.y - c_min.y) - 12.0F)
    };

    st.window_stack.clear();
    st.io.MousePos = pt1_screen;
    st.mouse_clicked[0] = true;
    st.mouse_down[0] = true;
    ImGui::SetNextWindowPos(ImVec2(50.0F, 80.0F));
    ImGui::SetNextWindowSize(ImVec2(320.0F, 260.0F));
    if (ImGui::Begin("CurveWin"))
    {
        (void)ImGui::CurveEditor("TestCurve", curve, ImVec2(200.0F, 100.0F));
    }
    ImGui::End();

    st.window_stack.clear();
    st.mouse_clicked[0] = false;
    st.mouse_down[0] = true;
    st.io.MousePos = ImVec2(pt1_screen.x + 40.0F, pt1_screen.y - 25.0F);
    bool curve_changed = false;
    ImGui::SetNextWindowPos(ImVec2(50.0F, 80.0F));
    ImGui::SetNextWindowSize(ImVec2(320.0F, 260.0F));
    if (ImGui::Begin("CurveWin"))
    {
        curve_changed = ImGui::CurveEditor("TestCurve", curve, ImVec2(200.0F, 100.0F));
    }
    ImGui::End();
    EXPECT_TRUE(curve_changed);
    EXPECT_GT(curve[2], 0.45F);
    EXPECT_GT(curve[3], 0.45F);

    // 2. BeginNode header drag updates pin screen position on the same frame
    st.window_stack.clear();
    st.active_id = 0U;
    st.mouse_down[0] = false;
    ImGui::SetNextWindowPos(ImVec2(50.0F, 80.0F));
    ImGui::SetNextWindowSize(ImVec2(500.0F, 360.0F));
    if (ImGui::Begin("NodeWin"))
    {
        if (ImGui::BeginNodeGraph("NG", ImVec2(450.0F, 280.0F)))
        {
            ImGui::BeginNode(42U, "ShaderNode", ImVec2(30.0F, 30.0F), ImVec2(160.0F, 80.0F));
            ImGui::NodePin(420U, "Out", true);
            ImGui::EndNode();
            ImGui::EndNodeGraph();
        }
    }
    ImGui::End();
    const ImVec2 initial_pin_pos = st.node_pins[420U].screen_pos;
    const ImVec2 hdr_click { st.node_graph_origin.x + 50.0F, st.node_graph_origin.y + 40.0F };

    // Frame A: click node header
    st.window_stack.clear();
    st.io.MousePos = hdr_click;
    st.mouse_clicked[0] = true;
    st.mouse_down[0] = true;
    st.mouse_delta = ImVec2(0.0F, 0.0F);
    ImGui::SetNextWindowPos(ImVec2(50.0F, 80.0F));
    ImGui::SetNextWindowSize(ImVec2(500.0F, 360.0F));
    if (ImGui::Begin("NodeWin"))
    {
        if (ImGui::BeginNodeGraph("NG", ImVec2(450.0F, 280.0F)))
        {
            ImGui::BeginNode(42U, "ShaderNode", ImVec2(30.0F, 30.0F), ImVec2(160.0F, 80.0F));
            ImGui::NodePin(420U, "Out", true);
            ImGui::EndNode();
            ImGui::EndNodeGraph();
        }
    }
    ImGui::End();
    EXPECT_EQ(st.dragging_node_id, 42U);

    // Frame B: drag node by (+35, +20) -> pin screen_pos updates on the SAME frame!
    st.window_stack.clear();
    st.mouse_clicked[0] = false;
    st.mouse_down[0] = true;
    st.mouse_delta = ImVec2(35.0F, 20.0F);
    ImGui::SetNextWindowPos(ImVec2(50.0F, 80.0F));
    ImGui::SetNextWindowSize(ImVec2(500.0F, 360.0F));
    if (ImGui::Begin("NodeWin"))
    {
        if (ImGui::BeginNodeGraph("NG", ImVec2(450.0F, 280.0F)))
        {
            ImGui::BeginNode(42U, "ShaderNode", ImVec2(30.0F, 30.0F), ImVec2(160.0F, 80.0F));
            ImGui::NodePin(420U, "Out", true);
            ImGui::EndNode();
            ImGui::EndNodeGraph();
        }
    }
    ImGui::End();
    EXPECT_NEAR(st.node_pins[420U].screen_pos.x, initial_pin_pos.x + 35.0F, 1e-3F);
    EXPECT_NEAR(st.node_pins[420U].screen_pos.y, initial_pin_pos.y + 20.0F, 1e-3F);

    // 3. DockBuilder dynamic custom window names + multi-tab slot switching
    ImGui::DockBuilderRemoveNode(100U);
    ImGui::DockBuilderAddNode(100U, ImGuiDockNodeFlags_DockSpace);
    ImGuiID main_node = 100U;
    const ImGuiID left_slot = ImGui::DockBuilderSplitNode(main_node, ImGuiDir_Left, 0.25F, nullptr, &main_node);
    ImGui::DockBuilderDockWindow("CustomTabAlpha", left_slot);
    ImGui::DockBuilderDockWindow("CustomTabBeta", left_slot);
    ImGui::DockBuilderFinish(100U);

    EXPECT_EQ(st.window_dock_slot["CustomTabAlpha"], 1);
    EXPECT_EQ(st.window_dock_slot["CustomTabBeta"], 1);
    EXPECT_EQ(st.active_dock_tab[1], "CustomTabAlpha");

    st.window_stack.clear();
    st.mouse_down[0] = false;
    const bool alpha_vis = ImGui::Begin("CustomTabAlpha");
    ImGui::End();
    const bool beta_vis = ImGui::Begin("CustomTabBeta");
    ImGui::End();
    EXPECT_TRUE(alpha_vis);
    EXPECT_FALSE(beta_vis);

    // Click the second tab header ("CustomTabBeta") in slot 1 (y = 60..86, x ~ 150)
    st.window_stack.clear();
    st.io.MousePos = ImVec2(150.0F, 72.0F);
    st.mouse_clicked[0] = true;
    (void)ImGui::Begin("CustomTabAlpha");
    ImGui::End();
    EXPECT_EQ(st.active_dock_tab[1], "CustomTabBeta");

    // 4. BeginChild / EndChild clamps unbounded wheel scroll
    st.window_stack.clear();
    st.mouse_clicked[0] = false;
    ImGui::SetNextWindowPos(ImVec2(400.0F, 100.0F));
    ImGui::SetNextWindowSize(ImVec2(300.0F, 220.0F));
    st.io.MousePos = ImVec2(450.0F, 160.0F);
    st.io.MouseWheel = -100.0F; // huge downward scroll
    if (ImGui::Begin("ScrollChildWin"))
    {
        if (ImGui::BeginChild("LogChild", ImVec2(260.0F, 80.0F), true))
        {
            for (int i = 0; i < 10; ++i)
            {
                ImGui::Text("Line %d", i);
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
    const ImGuiID child_id = cd::ui::chroma::hash_str("LogChild");
    EXPECT_GT(st.scroll_map[child_id], 0.0F);
    EXPECT_LT(st.scroll_map[child_id], 300.0F);
}
