// =============================================================================
// CHROMODYNAMIC — cd::input tests
// =============================================================================
#include <cd/input/Axis.hpp>
#include <cd/input/DoubleClick.hpp>
#include <cd/input/GamepadState.hpp>
#include <cd/input/Input.hpp>
#include <cd/input/KeyChord.hpp>
#include <gtest/gtest.h>

namespace
{

TEST(Input, DefaultStateIsEmpty)
{
    cd::input::InputContext ctx;
    EXPECT_FALSE(ctx.state().is_key_down(cd::input::KeyCode::kW));
    EXPECT_FALSE(ctx.state().is_mouse_button_down(cd::input::MouseButton::kLeft));
    EXPECT_FLOAT_EQ(ctx.state().mouse_x(), 0.0F);
    EXPECT_EQ(ctx.queued_event_count(), 0U);
}

TEST(Input, KeyDownEventUpdatesState)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent e {};
    e.kind = cd::input::EventKind::kKeyDown;
    e.key = cd::input::KeyCode::kW;
    ctx.push_event(e);
    EXPECT_TRUE(ctx.state().is_key_down(cd::input::KeyCode::kW));
    EXPECT_EQ(ctx.queued_event_count(), 1U);
}

TEST(Input, KeyUpClearsState)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent d {};
    d.kind = cd::input::EventKind::kKeyDown;
    d.key = cd::input::KeyCode::kSpace;
    ctx.push_event(d);
    cd::input::InputEvent u = d;
    u.kind = cd::input::EventKind::kKeyUp;
    ctx.push_event(u);
    EXPECT_FALSE(ctx.state().is_key_down(cd::input::KeyCode::kSpace));
}

TEST(Input, MouseMoveUpdatesPosition)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent e {};
    e.kind = cd::input::EventKind::kMouseMove;
    e.mouse_x = 100.0F;
    e.mouse_y = 50.0F;
    ctx.push_event(e);
    EXPECT_FLOAT_EQ(ctx.state().mouse_x(), 100.0F);
    EXPECT_FLOAT_EQ(ctx.state().mouse_y(), 50.0F);
}

TEST(Input, WheelAccumulatesAcrossEvents)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent w {};
    w.kind = cd::input::EventKind::kMouseWheel;
    w.wheel = 1.0F;
    ctx.push_event(w);
    ctx.push_event(w);
    EXPECT_FLOAT_EQ(ctx.state().wheel_accumulator(), 2.0F);
    ctx.begin_frame();  // resets wheel for next frame
    EXPECT_FLOAT_EQ(ctx.state().wheel_accumulator(), 0.0F);
}

TEST(Input, DrainEventsClearsQueue)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent e {};
    e.kind = cd::input::EventKind::kKeyDown;
    e.key = cd::input::KeyCode::kA;
    ctx.push_event(e);
    ctx.push_event(e);
    EXPECT_EQ(ctx.queued_event_count(), 2U);
    auto drained = ctx.drain_events();
    EXPECT_EQ(drained.size(), 2U);
    EXPECT_EQ(ctx.queued_event_count(), 0U);
    // Polled state must remain after drain — events and state are separate.
    EXPECT_TRUE(ctx.state().is_key_down(cd::input::KeyCode::kA));
}

TEST(Input, MouseButtonStateTracked)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent d {};
    d.kind = cd::input::EventKind::kMouseButtonDown;
    d.mouse_button = cd::input::MouseButton::kRight;
    ctx.push_event(d);
    EXPECT_TRUE(ctx.state().is_mouse_button_down(cd::input::MouseButton::kRight));
    EXPECT_FALSE(ctx.state().is_mouse_button_down(cd::input::MouseButton::kLeft));
}

// ---------------------------------------------------------------------------
// Phase 22.B — Axis tests (Wave 186)
// ---------------------------------------------------------------------------
TEST(InputAxis, BothPressedIsZero)
{
    cd::input::Axis a;
    a.set_keys(true, true);
    EXPECT_FLOAT_EQ(a.value(), 0.0F);
}

TEST(InputAxis, OnlyPositivePressedIsOne)
{
    cd::input::Axis a;
    a.set_keys(false, true);
    EXPECT_FLOAT_EQ(a.value(), 1.0F);
}

TEST(InputAxis, OnlyNegativePressedIsMinusOne)
{
    cd::input::Axis a;
    a.set_keys(true, false);
    EXPECT_FLOAT_EQ(a.value(), -1.0F);
}

TEST(InputAxis, AnalogOverrideClamps)
{
    cd::input::Axis a;
    a.set_analog(1.5F);
    EXPECT_FLOAT_EQ(a.value(), 1.0F);
    a.set_analog(-2.0F);
    EXPECT_FLOAT_EQ(a.value(), -1.0F);
}

TEST(DoubleClick, FirstClickReturnsOne)
{
    cd::input::DoubleClick dc;
    EXPECT_EQ(dc.click(0.0F), 1u);
}

TEST(DoubleClick, TwoClicksWithinThresholdReturnTwo)
{
    cd::input::DoubleClick dc;
    dc.set_threshold(0.5F);
    EXPECT_EQ(dc.click(0.0F), 1u);
    EXPECT_EQ(dc.click(0.3F), 2u);
}

TEST(DoubleClick, GapBeyondThresholdResetsToOne)
{
    cd::input::DoubleClick dc;
    dc.set_threshold(0.4F);
    EXPECT_EQ(dc.click(0.0F), 1u);
    EXPECT_EQ(dc.click(0.3F), 2u);
    EXPECT_EQ(dc.click(1.0F), 1u);   // gap = 0.7s > 0.4s
}

TEST(DoubleClick, TripleClickReachesThree)
{
    cd::input::DoubleClick dc;
    dc.set_threshold(0.5F);
    dc.click(0.0F);
    dc.click(0.2F);
    EXPECT_EQ(dc.click(0.4F), 3u);
}

TEST(DoubleClick, ResetZeroesCounter)
{
    cd::input::DoubleClick dc;
    dc.click(0.0F);
    dc.click(0.1F);
    dc.reset();
    EXPECT_EQ(dc.count(), 0u);
    EXPECT_EQ(dc.click(10.0F), 1u);   // fresh start
}

TEST(KeyChord, MatchesCtrlSOnKeyDown)
{
    cd::input::InputContext ctx;
    // press Ctrl
    cd::input::InputEvent ctrl_down {};
    ctrl_down.kind = cd::input::EventKind::kKeyDown;
    ctrl_down.key = cd::input::KeyCode::kLCtrl;
    ctx.push_event(ctrl_down);
    // press S
    cd::input::InputEvent s_down {};
    s_down.kind = cd::input::EventKind::kKeyDown;
    s_down.key = cd::input::KeyCode::kS;
    cd::input::KeyChord c { cd::input::KeyCode::kS, cd::input::Mod::kCtrl };
    EXPECT_TRUE(cd::input::matches(c, s_down, ctx.state()));
}

TEST(KeyChord, RequiresTriggerKeyDownNotUp)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent ctrl_down {};
    ctrl_down.kind = cd::input::EventKind::kKeyDown;
    ctrl_down.key = cd::input::KeyCode::kLCtrl;
    ctx.push_event(ctrl_down);
    cd::input::InputEvent s_up {};
    s_up.kind = cd::input::EventKind::kKeyUp;
    s_up.key = cd::input::KeyCode::kS;
    cd::input::KeyChord c { cd::input::KeyCode::kS, cd::input::Mod::kCtrl };
    EXPECT_FALSE(cd::input::matches(c, s_up, ctx.state()));
}

TEST(KeyChord, MissingModRejected)
{
    cd::input::InputContext ctx;  // no Ctrl pressed
    cd::input::InputEvent s_down {};
    s_down.kind = cd::input::EventKind::kKeyDown;
    s_down.key = cd::input::KeyCode::kS;
    cd::input::KeyChord c { cd::input::KeyCode::kS, cd::input::Mod::kCtrl };
    EXPECT_FALSE(cd::input::matches(c, s_down, ctx.state()));
}

TEST(KeyChord, ExtraModRejected)
{
    cd::input::InputContext ctx;
    for (auto k : { cd::input::KeyCode::kLCtrl, cd::input::KeyCode::kLShift })
    {
        cd::input::InputEvent d {};
        d.kind = cd::input::EventKind::kKeyDown;
        d.key = k;
        ctx.push_event(d);
    }
    cd::input::InputEvent s_down {};
    s_down.kind = cd::input::EventKind::kKeyDown;
    s_down.key = cd::input::KeyCode::kS;
    // chord expects ONLY Ctrl, but Shift is also held → no match
    cd::input::KeyChord c { cd::input::KeyCode::kS, cd::input::Mod::kCtrl };
    EXPECT_FALSE(cd::input::matches(c, s_down, ctx.state()));
}

TEST(GamepadState, DefaultIsZeroAndDisconnected)
{
    cd::input::GamepadState g;
    EXPECT_FALSE(g.connected);
    EXPECT_FLOAT_EQ(g.left_stick_x, 0.0F);
    EXPECT_EQ(g.buttons, 0u);
}

TEST(GamepadState, ButtonBitmaskCheck)
{
    cd::input::GamepadState g;
    g.buttons = static_cast<std::uint32_t>(cd::input::GamepadButton::kA)
              | static_cast<std::uint32_t>(cd::input::GamepadButton::kDpadLeft);
    EXPECT_TRUE(cd::input::is_button_down(g, cd::input::GamepadButton::kA));
    EXPECT_TRUE(cd::input::is_button_down(g, cd::input::GamepadButton::kDpadLeft));
    EXPECT_FALSE(cd::input::is_button_down(g, cd::input::GamepadButton::kB));
}

TEST(GamepadState, DeadzoneZerosSmallInput)
{
    float x = 0.05F;
    float y = 0.05F;
    cd::input::apply_deadzone(x, y, 0.15F);
    EXPECT_FLOAT_EQ(x, 0.0F);
    EXPECT_FLOAT_EQ(y, 0.0F);
}

TEST(GamepadState, DeadzoneScalesOutsideRange)
{
    float x = 1.0F;
    float y = 0.0F;
    cd::input::apply_deadzone(x, y, 0.2F);
    EXPECT_NEAR(x, 1.0F, 1e-4F);
    EXPECT_FLOAT_EQ(y, 0.0F);
}

}  // namespace
