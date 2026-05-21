// =============================================================================
// CHROMODYNAMIC — cd::input tests
// =============================================================================
#include <cd/input/Input.hpp>
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

}  // namespace
