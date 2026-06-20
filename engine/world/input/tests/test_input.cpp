// =============================================================================
// CHROMODYNAMIC — cd::input tests
// =============================================================================
#include <cd/input/Axis.hpp>
#include <cd/input/Bindings.hpp>
#include <cd/input/Cursor.hpp>
#include <cd/input/DoubleClick.hpp>
#include <cd/input/GamepadState.hpp>
#include <cd/input/Hold.hpp>
#include <cd/input/Input.hpp>
#include <cd/input/KeyChord.hpp>
#include <cd/input/MouseDragState.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>

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

TEST(ActionBindings, BindAndCheckIsDown)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent down {};
    down.kind = cd::input::EventKind::kKeyDown;
    down.key = cd::input::KeyCode::kA;
    ctx.push_event(down);

    cd::input::ActionBindings bindings;
    bindings.bind("MoveLeft", cd::input::KeyCode::kA);
    EXPECT_TRUE(bindings.is_down("MoveLeft", ctx.state()));
    EXPECT_FALSE(bindings.is_down("MoveRight", ctx.state()));
}

TEST(ActionBindings, MultipleKeysForOneAction)
{
    cd::input::InputContext ctx;
    cd::input::ActionBindings bindings;
    bindings.bind("MoveLeft", cd::input::KeyCode::kA);
    bindings.bind("MoveLeft", cd::input::KeyCode::kLeft);

    cd::input::InputEvent left {};
    left.kind = cd::input::EventKind::kKeyDown;
    left.key = cd::input::KeyCode::kLeft;
    ctx.push_event(left);
    EXPECT_TRUE(bindings.is_down("MoveLeft", ctx.state()));
}

TEST(ActionBindings, UnbindRemovesAll)
{
    cd::input::ActionBindings bindings;
    bindings.bind("Jump", cd::input::KeyCode::kSpace);
    bindings.unbind_all("Jump");
    EXPECT_EQ(bindings.action_count(), 0u);
}

TEST(MouseDragState, NotPressedByDefault)
{
    cd::input::MouseDragState s;
    EXPECT_FALSE(s.is_pressed());
    EXPECT_FALSE(s.is_dragging());
}

TEST(MouseDragState, PressMoveBelowThresholdNotDragging)
{
    cd::input::MouseDragState s;
    s.set_threshold(10.0F);
    s.on_press(100.0F, 100.0F);
    s.on_move(102.0F, 101.0F);
    EXPECT_TRUE(s.is_pressed());
    EXPECT_FALSE(s.is_dragging());
}

TEST(MouseDragState, PressMoveAboveThresholdIsDragging)
{
    cd::input::MouseDragState s;
    s.set_threshold(5.0F);
    s.on_press(100.0F, 100.0F);
    s.on_move(110.0F, 110.0F);
    EXPECT_TRUE(s.is_dragging());
    EXPECT_FLOAT_EQ(s.drag_delta_x(), 10.0F);
    EXPECT_FLOAT_EQ(s.drag_delta_y(), 10.0F);
}

TEST(MouseDragState, ReleaseResetsState)
{
    cd::input::MouseDragState s;
    s.on_press(0.0F, 0.0F);
    s.on_move(100.0F, 100.0F);
    s.on_release();
    EXPECT_FALSE(s.is_pressed());
    EXPECT_FALSE(s.is_dragging());
}

TEST(Hold, NotHeldBeforeThreshold)
{
    cd::input::Hold h;
    h.set_threshold(0.5F);
    h.on_press(0.0F);
    EXPECT_FALSE(h.is_held(0.2F));
    EXPECT_TRUE(h.is_pressed());
}

TEST(Hold, HeldAfterThreshold)
{
    cd::input::Hold h;
    h.set_threshold(0.5F);
    h.on_press(0.0F);
    EXPECT_TRUE(h.is_held(0.6F));
    EXPECT_FLOAT_EQ(h.held_duration(0.6F), 0.6F);
}

TEST(Hold, ReleaseClearsHold)
{
    cd::input::Hold h;
    h.set_threshold(0.5F);
    h.on_press(0.0F);
    h.on_release();
    EXPECT_FALSE(h.is_pressed());
    EXPECT_FALSE(h.is_held(10.0F));
    EXPECT_FLOAT_EQ(h.held_duration(10.0F), 0.0F);
}

TEST(Cursor, DefaultNormal)
{
    cd::input::CursorState s;
    EXPECT_EQ(s.requested(), cd::input::CursorMode::kNormal);
    EXPECT_EQ(s.applied(), cd::input::CursorMode::kNormal);
}

TEST(Cursor, RequestSetsRequestedNotApplied)
{
    cd::input::CursorState s;
    s.request(cd::input::CursorMode::kLocked);
    EXPECT_EQ(s.requested(), cd::input::CursorMode::kLocked);
    EXPECT_EQ(s.applied(), cd::input::CursorMode::kNormal);
    s.mark_applied(cd::input::CursorMode::kLocked);
    EXPECT_EQ(s.applied(), cd::input::CursorMode::kLocked);
}

TEST(Cursor, SetAbsoluteComputesDelta)
{
    cd::input::CursorState s;
    s.set_absolute(100.0F, 100.0F);
    s.set_absolute(110.0F, 95.0F);
    EXPECT_FLOAT_EQ(s.delta_x(), 10.0F);
    EXPECT_FLOAT_EQ(s.delta_y(), -5.0F);
}

TEST(Cursor, ClearDeltaZeroes)
{
    cd::input::CursorState s;
    s.set_absolute(0.0F, 0.0F);
    s.set_absolute(5.0F, 5.0F);
    s.clear_delta();
    EXPECT_FLOAT_EQ(s.delta_x(), 0.0F);
    EXPECT_FLOAT_EQ(s.delta_y(), 0.0F);
}

// ===========================================================================
// Phase 1121 — 100% depth sweep (edge + boundary + negative + simultaneous)
// ===========================================================================

// ---------------------------------------------------------------------------
// Hold — exact boundary + helpers
// ---------------------------------------------------------------------------

// Exactly at threshold: (now - press_t) == threshold_ must be held.
TEST(Hold, ExactlyAtThresholdIsHeld)
{
    cd::input::Hold h;
    h.set_threshold(0.5F);
    h.on_press(1.0F);
    EXPECT_TRUE(h.is_held(1.5F));  // 1.5 - 1.0 == 0.5 >= 0.5
}

// One ULP below threshold: must NOT be held.
TEST(Hold, JustBelowThresholdNotHeld)
{
    cd::input::Hold h;
    h.set_threshold(0.5F);
    h.on_press(0.0F);
    // 0.499 < 0.5
    EXPECT_FALSE(h.is_held(0.499F));
}

// Negative threshold clamped to 0: any positive duration counts as held.
TEST(Hold, NegativeThresholdClampedToZero)
{
    cd::input::Hold h;
    h.set_threshold(-1.0F);
    EXPECT_FLOAT_EQ(h.threshold(), 0.0F);
    h.on_press(0.0F);
    // threshold==0 so now==press_t still satisfies >= 0
    EXPECT_TRUE(h.is_held(0.0F));
}

// held_duration returns 0 when not pressed.
TEST(Hold, HeldDurationZeroWhenNotPressed)
{
    cd::input::Hold h;
    h.set_threshold(0.5F);
    // Never pressed → should return 0.
    EXPECT_FLOAT_EQ(h.held_duration(10.0F), 0.0F);
}

// held_duration returns elapsed time correctly while pressed.
TEST(Hold, HeldDurationWhilePressed)
{
    cd::input::Hold h;
    h.on_press(2.0F);
    // 2.7F is not exactly representable; use NEAR with 1e-5 tolerance.
    EXPECT_NEAR(h.held_duration(2.7F), 0.7F, 1e-5F);
}

// Press twice without release: second press resets origin time.
TEST(Hold, SecondPressResetsTimer)
{
    cd::input::Hold h;
    h.set_threshold(0.5F);
    h.on_press(0.0F);
    h.on_press(10.0F);  // re-pressed at t=10
    // duration from new press origin: 10.1 - 10.0 = 0.1 < 0.5
    EXPECT_FALSE(h.is_held(10.1F));
    EXPECT_NEAR(h.held_duration(10.1F), 0.1F, 1e-5F);
}

// ---------------------------------------------------------------------------
// DoubleClick — exact boundary + edge cases
// ---------------------------------------------------------------------------

// Click exactly at threshold distance: still counts (<=).
TEST(DoubleClick, ExactlyAtThresholdStillCounts)
{
    cd::input::DoubleClick dc;
    dc.set_threshold(0.4F);
    dc.click(0.0F);
    // gap == threshold_ exactly → still consecutive (condition: <= threshold_)
    EXPECT_EQ(dc.click(0.4F), 2u);
}

// Click one epsilon past threshold: resets to 1.
TEST(DoubleClick, JustPastThresholdResetsToOne)
{
    cd::input::DoubleClick dc;
    dc.set_threshold(0.4F);
    dc.click(0.0F);
    // 0.401 > 0.4 → reset
    EXPECT_EQ(dc.click(0.401F), 1u);
}

// After reset(), a click at non-zero time starts fresh (count_==0 path).
TEST(DoubleClick, AfterResetFirstClickAtNonZeroTime)
{
    cd::input::DoubleClick dc;
    dc.click(0.0F);
    dc.click(0.1F);
    dc.reset();
    // count_==0 so the (count_ > 0 && ...) branch is false → reset to 1
    EXPECT_EQ(dc.click(5.0F), 1u);
    EXPECT_EQ(dc.count(), 1u);
}

// Negative threshold clamped to 0: every click within 0s window resets.
TEST(DoubleClick, NegativeThresholdClampedToZero)
{
    cd::input::DoubleClick dc;
    dc.set_threshold(-99.0F);
    EXPECT_FLOAT_EQ(dc.threshold(), 0.0F);
    dc.click(0.0F);
    // gap 0.1 > 0 → resets to 1
    EXPECT_EQ(dc.click(0.1F), 1u);
}

// ---------------------------------------------------------------------------
// KeyChord — L/R modifier variants + no-mod chord + wrong trigger
// ---------------------------------------------------------------------------

// RCtrl satisfies Mod::kCtrl.
TEST(KeyChord, RCtrlSatisfiesCtrlMod)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent d {};
    d.kind = cd::input::EventKind::kKeyDown;
    d.key  = cd::input::KeyCode::kRCtrl;
    ctx.push_event(d);

    cd::input::InputEvent s_down {};
    s_down.kind = cd::input::EventKind::kKeyDown;
    s_down.key  = cd::input::KeyCode::kS;
    cd::input::KeyChord c { cd::input::KeyCode::kS, cd::input::Mod::kCtrl };
    EXPECT_TRUE(cd::input::matches(c, s_down, ctx.state()));
}

// RAlt satisfies Mod::kAlt.
TEST(KeyChord, RAltSatisfiesAltMod)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent d {};
    d.kind = cd::input::EventKind::kKeyDown;
    d.key  = cd::input::KeyCode::kRAlt;
    ctx.push_event(d);

    cd::input::InputEvent f_down {};
    f_down.kind = cd::input::EventKind::kKeyDown;
    f_down.key  = cd::input::KeyCode::kF;
    cd::input::KeyChord c { cd::input::KeyCode::kF, cd::input::Mod::kAlt };
    EXPECT_TRUE(cd::input::matches(c, f_down, ctx.state()));
}

// RShift satisfies Mod::kShift.
TEST(KeyChord, RShiftSatisfiesShiftMod)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent d {};
    d.kind = cd::input::EventKind::kKeyDown;
    d.key  = cd::input::KeyCode::kRShift;
    ctx.push_event(d);

    cd::input::InputEvent z_down {};
    z_down.kind = cd::input::EventKind::kKeyDown;
    z_down.key  = cd::input::KeyCode::kZ;
    cd::input::KeyChord c { cd::input::KeyCode::kZ, cd::input::Mod::kShift };
    EXPECT_TRUE(cd::input::matches(c, z_down, ctx.state()));
}

// No-modifier chord matches only when no mod is held.
TEST(KeyChord, NoModifierChordMatchesWithNoModsHeld)
{
    cd::input::InputContext ctx;  // empty — no modifiers
    cd::input::InputEvent f5_down {};
    f5_down.kind = cd::input::EventKind::kKeyDown;
    f5_down.key  = cd::input::KeyCode::kF5;
    cd::input::KeyChord c { cd::input::KeyCode::kF5, cd::input::Mod::kNone };
    EXPECT_TRUE(cd::input::matches(c, f5_down, ctx.state()));
}

// Wrong trigger key: Ctrl+A chord does NOT match Ctrl+S event.
TEST(KeyChord, WrongTriggerKeyRejected)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent d {};
    d.kind = cd::input::EventKind::kKeyDown;
    d.key  = cd::input::KeyCode::kLCtrl;
    ctx.push_event(d);

    cd::input::InputEvent a_down {};
    a_down.kind = cd::input::EventKind::kKeyDown;
    a_down.key  = cd::input::KeyCode::kA;
    // chord expects kS, but event is kA
    cd::input::KeyChord c { cd::input::KeyCode::kS, cd::input::Mod::kCtrl };
    EXPECT_FALSE(cd::input::matches(c, a_down, ctx.state()));
}

// Ctrl+Shift+Z compound chord fires exactly when both mods held.
TEST(KeyChord, CompoundCtrlShiftZMatches)
{
    cd::input::InputContext ctx;
    for (auto k : { cd::input::KeyCode::kLCtrl, cd::input::KeyCode::kLShift })
    {
        cd::input::InputEvent d {};
        d.kind = cd::input::EventKind::kKeyDown;
        d.key  = k;
        ctx.push_event(d);
    }
    cd::input::InputEvent z_down {};
    z_down.kind = cd::input::EventKind::kKeyDown;
    z_down.key  = cd::input::KeyCode::kZ;
    cd::input::KeyChord c { cd::input::KeyCode::kZ,
                            cd::input::Mod::kCtrl | cd::input::Mod::kShift };
    EXPECT_TRUE(cd::input::matches(c, z_down, ctx.state()));
}

// ---------------------------------------------------------------------------
// Gamepad — dead-zone exact boundary + zero dead-zone + triggers + buttons
// ---------------------------------------------------------------------------

// Exactly at deadzone edge: magnitude == deadzone → zeroed.
TEST(GamepadState, DeadzoneExactBoundaryZeroed)
{
    // Build a vector with magnitude exactly == deadzone.
    constexpr float dz = 0.15F;
    // (dz, 0) has magnitude dz exactly.
    float x = dz;
    float y = 0.0F;
    cd::input::apply_deadzone(x, y, dz);
    EXPECT_FLOAT_EQ(x, 0.0F);
    EXPECT_FLOAT_EQ(y, 0.0F);
}

// Just outside deadzone: output is non-zero.
TEST(GamepadState, DeadzoneJustOutsideIsNonZero)
{
    constexpr float dz = 0.15F;
    float x = dz + 0.01F;
    float y = 0.0F;
    cd::input::apply_deadzone(x, y, dz);
    EXPECT_GT(x, 0.0F);
}

// Zero deadzone: apply_deadzone returns immediately, values unchanged.
TEST(GamepadState, ZeroDeadzoneNoOp)
{
    float x = 0.05F;
    float y = 0.05F;
    cd::input::apply_deadzone(x, y, 0.0F);
    EXPECT_FLOAT_EQ(x, 0.05F);
    EXPECT_FLOAT_EQ(y, 0.05F);
}

// Full-magnitude (1, 0) survives deadzone rescale to ~1.
TEST(GamepadState, UnitMagnitudeSurvivesDeadzone)
{
    float x = 1.0F;
    float y = 0.0F;
    cd::input::apply_deadzone(x, y, 0.15F);
    const float mag = std::sqrt(x * x + y * y);
    EXPECT_NEAR(mag, 1.0F, 1e-5F);
}

// Trigger values (scalars, not sticks): stored and readable.
TEST(GamepadState, TriggerValuesStored)
{
    cd::input::GamepadState g;
    g.left_trigger  = 0.75F;
    g.right_trigger = 0.25F;
    EXPECT_FLOAT_EQ(g.left_trigger,  0.75F);
    EXPECT_FLOAT_EQ(g.right_trigger, 0.25F);
}

// Connected flag round-trip.
TEST(GamepadState, ConnectedFlag)
{
    cd::input::GamepadState g;
    g.connected = true;
    EXPECT_TRUE(g.connected);
}

// All 14 buttons individually readable via is_button_down.
TEST(GamepadState, AllButtonsIndividuallyReadable)
{
    using GB = cd::input::GamepadButton;
    const std::array<GB, 14> all_buttons = {
        GB::kA, GB::kB, GB::kX, GB::kY,
        GB::kLB, GB::kRB, GB::kStart, GB::kBack,
        GB::kLStickDown, GB::kRStickDown,
        GB::kDpadUp, GB::kDpadDown, GB::kDpadLeft, GB::kDpadRight,
    };
    for (const auto btn : all_buttons)
    {
        cd::input::GamepadState g;
        g.buttons = static_cast<std::uint32_t>(btn);
        EXPECT_TRUE(cd::input::is_button_down(g, btn))
            << "Button mask " << static_cast<std::uint32_t>(btn) << " not readable";
        // No other button should be set.
        for (const auto other : all_buttons)
        {
            if (other == btn) continue;
            EXPECT_FALSE(cd::input::is_button_down(g, other))
                << "Button " << static_cast<std::uint32_t>(other)
                << " spuriously set by mask "
                << static_cast<std::uint32_t>(btn);
        }
    }
}

// ---------------------------------------------------------------------------
// MouseDragState — threshold exact boundary + guards + re-press + diagonal
// ---------------------------------------------------------------------------

// Exactly at threshold distance: dragging must become true.
TEST(MouseDragState, ExactlyAtThresholdIsDragging)
{
    cd::input::MouseDragState s;
    s.set_threshold(5.0F);
    s.on_press(0.0F, 0.0F);
    // Move exactly (5, 0) → magnitude == 5.0 >= 5.0
    s.on_move(5.0F, 0.0F);
    EXPECT_TRUE(s.is_dragging());
}

// One unit below threshold: not dragging.
TEST(MouseDragState, JustBelowThresholdNotDragging)
{
    cd::input::MouseDragState s;
    s.set_threshold(5.0F);
    s.on_press(0.0F, 0.0F);
    // magnitude = 4.9 < 5.0
    s.on_move(4.9F, 0.0F);
    EXPECT_FALSE(s.is_dragging());
}

// on_move while not pressed is a no-op (guard path).
TEST(MouseDragState, OnMoveIgnoredWhenNotPressed)
{
    cd::input::MouseDragState s;
    s.on_move(999.0F, 999.0F);  // not pressed — should be a no-op
    EXPECT_FALSE(s.is_pressed());
    EXPECT_FALSE(s.is_dragging());
}

// Re-press after release resets drag state and origin.
TEST(MouseDragState, RePressAfterReleaseResetsState)
{
    cd::input::MouseDragState s;
    s.set_threshold(4.0F);
    s.on_press(0.0F, 0.0F);
    s.on_move(100.0F, 100.0F);
    EXPECT_TRUE(s.is_dragging());
    s.on_release();
    // New press at a different origin.
    s.on_press(50.0F, 50.0F);
    EXPECT_TRUE(s.is_pressed());
    EXPECT_FALSE(s.is_dragging());  // drag flag cleared on new press
    EXPECT_FLOAT_EQ(s.press_origin_x(), 50.0F);
    EXPECT_FLOAT_EQ(s.press_origin_y(), 50.0F);
    EXPECT_FLOAT_EQ(s.drag_delta_x(), 0.0F);
    EXPECT_FLOAT_EQ(s.drag_delta_y(), 0.0F);
}

// press_origin accessors return correct values.
TEST(MouseDragState, PressOriginAccessors)
{
    cd::input::MouseDragState s;
    s.on_press(123.0F, 456.0F);
    EXPECT_FLOAT_EQ(s.press_origin_x(), 123.0F);
    EXPECT_FLOAT_EQ(s.press_origin_y(), 456.0F);
}

// Diagonal movement: sqrt(dx^2 + dy^2) must exceed threshold.
TEST(MouseDragState, DiagonalThresholdCheck)
{
    cd::input::MouseDragState s;
    s.set_threshold(10.0F);
    s.on_press(0.0F, 0.0F);
    // Move (6, 6): magnitude = sqrt(72) ≈ 8.485 < 10 → not dragging.
    s.on_move(6.0F, 6.0F);
    EXPECT_FALSE(s.is_dragging());
    // Move (8, 8): magnitude = sqrt(128) ≈ 11.31 >= 10 → dragging.
    s.on_move(8.0F, 8.0F);
    EXPECT_TRUE(s.is_dragging());
}

// Negative threshold clamped to 0: any movement triggers drag.
TEST(MouseDragState, NegativeThresholdClampedToZero)
{
    cd::input::MouseDragState s;
    s.set_threshold(-5.0F);
    EXPECT_FLOAT_EQ(s.threshold(), 0.0F);
    s.on_press(0.0F, 0.0F);
    s.on_move(0.001F, 0.0F);  // any non-zero movement >= 0
    EXPECT_TRUE(s.is_dragging());
}

// ---------------------------------------------------------------------------
// Axis — return value of set_keys + default value
// ---------------------------------------------------------------------------

// Default value is 0 before any input.
TEST(InputAxis, DefaultValueIsZero)
{
    const cd::input::Axis a;
    EXPECT_FLOAT_EQ(a.value(), 0.0F);
}

// set_keys returns the new value.
TEST(InputAxis, SetKeysReturnsValue)
{
    cd::input::Axis a;
    EXPECT_FLOAT_EQ(a.set_keys(false, true),  1.0F);
    EXPECT_FLOAT_EQ(a.set_keys(true,  false), -1.0F);
    EXPECT_FLOAT_EQ(a.set_keys(false, false),  0.0F);
    EXPECT_FLOAT_EQ(a.set_keys(true,  true),   0.0F);
}

// Analog in-range value is stored verbatim.
TEST(InputAxis, AnalogInRangeStoredVerbatim)
{
    cd::input::Axis a;
    a.set_analog(0.75F);
    EXPECT_FLOAT_EQ(a.value(), 0.75F);
    a.set_analog(-0.5F);
    EXPECT_FLOAT_EQ(a.value(), -0.5F);
}

// Analog exact boundary: ±1.0 must survive (no over-clamp).
TEST(InputAxis, AnalogExactBoundaryPreserved)
{
    cd::input::Axis a;
    a.set_analog(1.0F);
    EXPECT_FLOAT_EQ(a.value(), 1.0F);
    a.set_analog(-1.0F);
    EXPECT_FLOAT_EQ(a.value(), -1.0F);
}

// Transitioning from analog override to digital clears to ±1 / 0.
TEST(InputAxis, TransitionFromAnalogToDigital)
{
    cd::input::Axis a;
    a.set_analog(0.42F);
    a.set_keys(true, false);  // now digital
    EXPECT_FLOAT_EQ(a.value(), -1.0F);
}

// ---------------------------------------------------------------------------
// ActionBindings — clear, action_count, unbound returns false
// ---------------------------------------------------------------------------

// clear() wipes all bindings.
TEST(ActionBindings, ClearEmptiesAllBindings)
{
    cd::input::ActionBindings b;
    b.bind("Jump",  cd::input::KeyCode::kSpace);
    b.bind("Run",   cd::input::KeyCode::kLShift);
    b.clear();
    EXPECT_EQ(b.action_count(), 0u);
}

// Querying an action that was never bound returns false (not a crash).
TEST(ActionBindings, UnboundActionReturnsFalse)
{
    cd::input::InputContext ctx;
    const cd::input::ActionBindings b;  // empty
    EXPECT_FALSE(b.is_down("Ghost", ctx.state()));
}

// action_hash is consistent (same name → same hash).
TEST(ActionBindings, ActionHashConsistency)
{
    constexpr std::uint32_t h1 = cd::input::action_hash("Fire");
    constexpr std::uint32_t h2 = cd::input::action_hash("Fire");
    EXPECT_EQ(h1, h2);
    // Different names must produce different hashes (FNV-1a; not a collision test,
    // just ensure "Jump" ≠ "Fire").
    constexpr std::uint32_t h3 = cd::input::action_hash("Jump");
    EXPECT_NE(h1, h3);
}

// ---------------------------------------------------------------------------
// InputContext — OOB safety + simultaneous keys + begin_frame isolation
// ---------------------------------------------------------------------------

// Pushing kUnknown (index 0) must not crash or corrupt state.
TEST(Input, PushUnknownKeyIsNoCrash)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent e {};
    e.kind = cd::input::EventKind::kKeyDown;
    e.key  = cd::input::KeyCode::kUnknown;
    ctx.push_event(e);  // must not crash
    // kUnknown maps to index 0; is_key_down(kUnknown) may be true — that's fine.
    EXPECT_EQ(ctx.queued_event_count(), 1U);
}

// Two simultaneous keys held: both readable.
TEST(Input, TwoSimultaneousKeysHeld)
{
    cd::input::InputContext ctx;
    for (auto k : { cd::input::KeyCode::kA, cd::input::KeyCode::kD })
    {
        cd::input::InputEvent d {};
        d.kind = cd::input::EventKind::kKeyDown;
        d.key  = k;
        ctx.push_event(d);
    }
    EXPECT_TRUE(ctx.state().is_key_down(cd::input::KeyCode::kA));
    EXPECT_TRUE(ctx.state().is_key_down(cd::input::KeyCode::kD));
}

// begin_frame does NOT clear key state — only the wheel.
TEST(Input, BeginFrameClearsOnlyWheel)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent kd {};
    kd.kind = cd::input::EventKind::kKeyDown;
    kd.key  = cd::input::KeyCode::kW;
    ctx.push_event(kd);

    cd::input::InputEvent wd {};
    wd.kind  = cd::input::EventKind::kMouseWheel;
    wd.wheel = 3.0F;
    ctx.push_event(wd);

    ctx.begin_frame();
    EXPECT_TRUE(ctx.state().is_key_down(cd::input::KeyCode::kW));   // key survives
    EXPECT_FLOAT_EQ(ctx.state().wheel_accumulator(), 0.0F);          // wheel reset
}

// mutable_state() exposes the same underlying state.
TEST(Input, MutableStateAccessorCoherent)
{
    cd::input::InputContext ctx;
    ctx.mutable_state().set_key(cd::input::KeyCode::kF1, true);
    EXPECT_TRUE(ctx.state().is_key_down(cd::input::KeyCode::kF1));
}

// Two simultaneous mouse buttons held.
TEST(Input, TwoSimultaneousMouseButtonsHeld)
{
    cd::input::InputContext ctx;
    for (auto b : { cd::input::MouseButton::kLeft, cd::input::MouseButton::kRight })
    {
        cd::input::InputEvent d {};
        d.kind = cd::input::EventKind::kMouseButtonDown;
        d.mouse_button = b;
        ctx.push_event(d);
    }
    EXPECT_TRUE(ctx.state().is_mouse_button_down(cd::input::MouseButton::kLeft));
    EXPECT_TRUE(ctx.state().is_mouse_button_down(cd::input::MouseButton::kRight));
}

// Event order preserved in drain: FIFO.
TEST(Input, DrainPreservesFifoOrder)
{
    cd::input::InputContext ctx;
    cd::input::InputEvent a_down {};
    a_down.kind = cd::input::EventKind::kKeyDown;
    a_down.key  = cd::input::KeyCode::kA;
    cd::input::InputEvent b_down {};
    b_down.kind = cd::input::EventKind::kKeyDown;
    b_down.key  = cd::input::KeyCode::kB;
    ctx.push_event(a_down);
    ctx.push_event(b_down);
    const auto events = ctx.drain_events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].key, cd::input::KeyCode::kA);
    EXPECT_EQ(events[1].key, cd::input::KeyCode::kB);
}

// MouseButton::kCount OOB: is_mouse_button_down must return false safely.
TEST(Input, MouseButtonCountOOBReturnsFalse)
{
    const cd::input::InputState s;
    EXPECT_FALSE(s.is_mouse_button_down(cd::input::MouseButton::kCount));
}

// ---------------------------------------------------------------------------
// Cursor — kHidden mode + first delta from zero + sequential moves
// ---------------------------------------------------------------------------

// kHidden mode round-trip.
TEST(Cursor, HiddenModeRoundTrip)
{
    cd::input::CursorState s;
    s.request(cd::input::CursorMode::kHidden);
    EXPECT_EQ(s.requested(), cd::input::CursorMode::kHidden);
    s.mark_applied(cd::input::CursorMode::kHidden);
    EXPECT_EQ(s.applied(), cd::input::CursorMode::kHidden);
}

// First set_absolute from zero computes correct delta relative to default (0, 0).
TEST(Cursor, FirstAbsoluteComputesDeltaFromZero)
{
    cd::input::CursorState s;
    s.set_absolute(10.0F, -5.0F);
    // default last_x_/last_y_ == 0 → delta = (10, -5)
    EXPECT_FLOAT_EQ(s.delta_x(), 10.0F);
    EXPECT_FLOAT_EQ(s.delta_y(), -5.0F);
}

// Three sequential moves accumulate the LAST delta.
TEST(Cursor, SequentialMovesTrackLastDelta)
{
    cd::input::CursorState s;
    s.set_absolute(0.0F, 0.0F);
    s.set_absolute(5.0F, 5.0F);
    s.set_absolute(3.0F, 8.0F);
    // Last move: (3-5, 8-5) = (-2, 3)
    EXPECT_FLOAT_EQ(s.delta_x(), -2.0F);
    EXPECT_FLOAT_EQ(s.delta_y(),  3.0F);
    EXPECT_FLOAT_EQ(s.x(), 3.0F);
    EXPECT_FLOAT_EQ(s.y(), 8.0F);
}

}  // namespace
