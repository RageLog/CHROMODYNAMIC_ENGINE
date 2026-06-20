// =============================================================================
// CHROMODYNAMIC — tests/test_input_binding.cpp
// Phase 462 — cd::gameplay::input_binding::ActionMap unit tests.
// =============================================================================
#include <cd/gameplay/input_binding/InputBinding.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{

using cd::gameplay::input_binding::ActionMap;
using cd::gameplay::input_binding::InputBinding;
using cd::gameplay::input_binding::RawInputSnapshot;
using cd::gameplay::input_binding::encode_axis;

// Synthetic scancodes — keep deterministic across tests; real values come from
// cd::input::KeyCode at the call site in production code.
constexpr int kKeyW     = 22;
constexpr int kKeyA     = 1;
constexpr int kKeyJ     = 9;
constexpr int kPadA     = 1;   // gamepad A
constexpr int kPadStart = 6;

// -----------------------------------------------------------------------------
// 1) Bind action to key, simulate key press -> callback fires.
// -----------------------------------------------------------------------------
TEST(ActionMap, BindKeyAndCallbackFiresOnPress)
{
    ActionMap map;
    int fire_count = 0;
    std::string fired_action;

    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));
    map.on_action("jump", [&](const std::string& name) {
        ++fire_count;
        fired_action = name;
    });

    RawInputSnapshot frame0;  // nothing pressed
    map.update(frame0);
    EXPECT_EQ(fire_count, 0);
    EXPECT_FALSE(map.is_action_pressed("jump"));

    RawInputSnapshot frame1;
    frame1.press("keyboard", kKeyJ);
    map.update(frame1);
    EXPECT_EQ(fire_count, 1);
    EXPECT_EQ(fired_action, "jump");
    EXPECT_TRUE(map.is_action_pressed("jump"));
}

// -----------------------------------------------------------------------------
// 2) Unbind stops callback firing.
// -----------------------------------------------------------------------------
TEST(ActionMap, UnbindStopsCallback)
{
    ActionMap map;
    int fire_count = 0;

    InputBinding b{"jump", "keyboard", kKeyJ};
    EXPECT_TRUE(map.bind("jump", b));
    map.on_action("jump", [&](const std::string&) { ++fire_count; });

    // First press fires.
    RawInputSnapshot pressed;
    pressed.press("keyboard", kKeyJ);
    map.update(pressed);
    EXPECT_EQ(fire_count, 1);

    // Release so the next press is again a rising edge after rebind, NOT
    // a held-from-before-unbind state.
    RawInputSnapshot released;
    map.update(released);
    EXPECT_FALSE(map.is_action_pressed("jump"));

    // Unbind and press again — callback must not fire.
    EXPECT_TRUE(map.unbind("jump", b));
    EXPECT_EQ(map.binding_count("jump"), 0U);
    map.update(pressed);
    EXPECT_EQ(fire_count, 1);
    EXPECT_FALSE(map.is_action_pressed("jump"));
}

// -----------------------------------------------------------------------------
// 3) Multiple actions bound to the same key all fire.
// -----------------------------------------------------------------------------
TEST(ActionMap, MultipleActionsOnSameKeyAllFire)
{
    ActionMap map;
    int fire_jump   = 0;
    int fire_attack = 0;

    EXPECT_TRUE(map.bind_button("jump",   "keyboard", kKeyJ));
    EXPECT_TRUE(map.bind_button("attack", "keyboard", kKeyJ));
    map.on_action("jump",   [&](const std::string&) { ++fire_jump; });
    map.on_action("attack", [&](const std::string&) { ++fire_attack; });

    RawInputSnapshot frame;
    frame.press("keyboard", kKeyJ);
    map.update(frame);

    EXPECT_EQ(fire_jump,   1);
    EXPECT_EQ(fire_attack, 1);
    EXPECT_TRUE(map.is_action_pressed("jump"));
    EXPECT_TRUE(map.is_action_pressed("attack"));
}

// -----------------------------------------------------------------------------
// 4) Axis binding produces a float value.
// -----------------------------------------------------------------------------
TEST(ActionMap, AxisBindingProducesFloat)
{
    ActionMap map;
    constexpr int kAxisLx = 0;
    EXPECT_TRUE(map.bind_axis("move_x", "gamepad_axis", kAxisLx));

    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxisLx, 0.75F);
    map.update(frame);
    EXPECT_FLOAT_EQ(map.axis_value("move_x"), 0.75F);
    EXPECT_TRUE(map.is_action_pressed("move_x"));  // > 0.5 threshold

    // Below threshold -> not pressed but value still surfaced.
    RawInputSnapshot soft;
    soft.set_axis("gamepad_axis", kAxisLx, 0.2F);
    map.update(soft);
    EXPECT_FLOAT_EQ(map.axis_value("move_x"), 0.2F);
    EXPECT_FALSE(map.is_action_pressed("move_x"));

    // Invert flag flips sign.
    ActionMap inverted;
    inverted.bind("move_x_inv",
                  InputBinding{"move_x_inv", "gamepad_axis",
                               encode_axis(kAxisLx, /*invert=*/true)});
    RawInputSnapshot frame2;
    frame2.set_axis("gamepad_axis", kAxisLx, 0.75F);
    inverted.update(frame2);
    EXPECT_FLOAT_EQ(inverted.axis_value("move_x_inv"), -0.75F);
}

// -----------------------------------------------------------------------------
// 5) Action not bound -> queries return safe defaults.
// -----------------------------------------------------------------------------
TEST(ActionMap, UnboundActionReturnsDefaults)
{
    ActionMap map;
    RawInputSnapshot frame;
    frame.press("keyboard", kKeyJ);
    map.update(frame);

    EXPECT_FALSE(map.is_action_pressed("nope"));
    EXPECT_FLOAT_EQ(map.axis_value("nope"), 0.0F);
    EXPECT_EQ(map.binding_count("nope"), 0U);
}

// -----------------------------------------------------------------------------
// 6) Rebind replaces previous bindings; only the new one drives the action.
// -----------------------------------------------------------------------------
TEST(ActionMap, RebindReplacesPreviousBinding)
{
    ActionMap map;
    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));
    EXPECT_EQ(map.binding_count("jump"), 1U);

    // Rebind to W.
    EXPECT_TRUE(map.rebind("jump", InputBinding{"jump", "keyboard", kKeyW}));
    EXPECT_EQ(map.binding_count("jump"), 1U);

    // Old key no longer triggers.
    RawInputSnapshot old_press;
    old_press.press("keyboard", kKeyJ);
    map.update(old_press);
    EXPECT_FALSE(map.is_action_pressed("jump"));

    // New key triggers.
    RawInputSnapshot new_press;
    new_press.press("keyboard", kKeyW);
    map.update(new_press);
    EXPECT_TRUE(map.is_action_pressed("jump"));
}

// -----------------------------------------------------------------------------
// 7) Held button fires callback only once (rising edge semantics), not
//    continuously every frame.
// -----------------------------------------------------------------------------
TEST(ActionMap, CallbackFiresOnceNotContinuously)
{
    ActionMap map;
    int fire_count = 0;

    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));
    map.on_action("jump", [&](const std::string&) { ++fire_count; });

    RawInputSnapshot held;
    held.press("keyboard", kKeyJ);

    // 5 frames held -> exactly 1 callback fire.
    for (int i = 0; i < 5; ++i) map.update(held);
    EXPECT_EQ(fire_count, 1);
    EXPECT_TRUE(map.is_action_pressed("jump"));

    // Release.
    RawInputSnapshot released;
    map.update(released);
    EXPECT_FALSE(map.is_action_pressed("jump"));
    EXPECT_EQ(fire_count, 1);

    // Press again -> rising edge -> +1.
    map.update(held);
    EXPECT_EQ(fire_count, 2);
}

// -----------------------------------------------------------------------------
// 8) clear() wipes everything: no actions remain.
// -----------------------------------------------------------------------------
TEST(ActionMap, ClearedMapHasNoActions)
{
    ActionMap map;
    int fire_count = 0;

    map.bind_button("jump",   "keyboard", kKeyJ);
    map.bind_button("attack", "keyboard", kKeyA);
    map.bind_button("pause",  "gamepad",  kPadStart);
    map.on_action("jump",   [&](const std::string&) { ++fire_count; });
    map.on_action("attack", [&](const std::string&) { ++fire_count; });
    EXPECT_EQ(map.action_count(), 3U);

    map.clear();
    EXPECT_EQ(map.action_count(), 0U);
    EXPECT_EQ(map.binding_count("jump"), 0U);

    // Press the old keys — no callbacks fire, no actions report pressed.
    RawInputSnapshot frame;
    frame.press("keyboard", kKeyJ);
    frame.press("keyboard", kKeyA);
    frame.press("gamepad",  kPadStart);
    map.update(frame);
    EXPECT_EQ(fire_count, 0);
    EXPECT_FALSE(map.is_action_pressed("jump"));
    EXPECT_FALSE(map.is_action_pressed("attack"));
    EXPECT_FALSE(map.is_action_pressed("pause"));
}

// -----------------------------------------------------------------------------
// 9) Bonus — multiple devices on one action (keyboard OR gamepad).
// -----------------------------------------------------------------------------
TEST(ActionMap, MultipleDevicesOnSameAction)
{
    ActionMap map;
    int fire_count = 0;

    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));
    EXPECT_TRUE(map.bind_button("jump", "gamepad",  kPadA));
    EXPECT_EQ(map.binding_count("jump"), 2U);

    map.on_action("jump", [&](const std::string&) { ++fire_count; });

    // Press keyboard only.
    RawInputSnapshot kbd;
    kbd.press("keyboard", kKeyJ);
    map.update(kbd);
    EXPECT_EQ(fire_count, 1);
    EXPECT_TRUE(map.is_action_pressed("jump"));

    // Release, then press gamepad only.
    RawInputSnapshot none;
    map.update(none);
    EXPECT_FALSE(map.is_action_pressed("jump"));

    RawInputSnapshot pad;
    pad.press("gamepad", kPadA);
    map.update(pad);
    EXPECT_EQ(fire_count, 2);
    EXPECT_TRUE(map.is_action_pressed("jump"));
}

// -----------------------------------------------------------------------------
// 10) Bonus — duplicate `bind()` of identical (action, device, code) is rejected.
// -----------------------------------------------------------------------------
TEST(ActionMap, DuplicateBindingRejected)
{
    ActionMap map;
    EXPECT_TRUE (map.bind_button("jump", "keyboard", kKeyJ));
    EXPECT_FALSE(map.bind_button("jump", "keyboard", kKeyJ));
    EXPECT_EQ(map.binding_count("jump"), 1U);
}

// -----------------------------------------------------------------------------
// 11) Bonus — clear_callbacks leaves bindings intact (state can still be polled,
//     no callbacks fire).
// -----------------------------------------------------------------------------
TEST(ActionMap, ClearCallbacksRetainsBindings)
{
    ActionMap map;
    int fire_count = 0;

    map.bind_button("jump", "keyboard", kKeyJ);
    map.on_action("jump", [&](const std::string&) { ++fire_count; });

    EXPECT_EQ(map.clear_callbacks("jump"), 1U);
    EXPECT_EQ(map.binding_count("jump"), 1U);

    RawInputSnapshot frame;
    frame.press("keyboard", kKeyJ);
    map.update(frame);
    EXPECT_EQ(fire_count, 0);
    EXPECT_TRUE(map.is_action_pressed("jump"));  // still polled correctly
}

// =============================================================================
// Phase 1253 additions — depth-100 edge / negative / boundary tests
// =============================================================================

// -----------------------------------------------------------------------------
// 12) is_action_just_pressed — true ONLY on frame of press, false when held.
// -----------------------------------------------------------------------------
TEST(ActionMap, JustPressedTrueOnlyOnRisingEdge)
{
    // Arrange
    ActionMap map;
    EXPECT_TRUE(map.bind_button("fire", "keyboard", kKeyJ));

    // Act / Assert — before any press
    RawInputSnapshot none;
    map.update(none);
    EXPECT_FALSE(map.is_action_just_pressed("fire"));
    EXPECT_FALSE(map.is_action_pressed("fire"));

    // First frame pressed -> just_pressed true
    RawInputSnapshot held;
    held.press("keyboard", kKeyJ);
    map.update(held);
    EXPECT_TRUE(map.is_action_just_pressed("fire"));
    EXPECT_TRUE(map.is_action_pressed("fire"));

    // Still held -> just_pressed false
    map.update(held);
    EXPECT_FALSE(map.is_action_just_pressed("fire"));
    EXPECT_TRUE(map.is_action_pressed("fire"));

    // Another held frame — still false
    map.update(held);
    EXPECT_FALSE(map.is_action_just_pressed("fire"));
}

// -----------------------------------------------------------------------------
// 13) is_action_just_released — true ONLY on the frame of release.
// -----------------------------------------------------------------------------
TEST(ActionMap, JustReleasedTrueOnlyOnFallingEdge)
{
    // Arrange
    ActionMap map;
    EXPECT_TRUE(map.bind_button("fire", "keyboard", kKeyJ));

    RawInputSnapshot held;
    held.press("keyboard", kKeyJ);
    RawInputSnapshot none;

    // Act — press two frames, then release
    map.update(held);
    map.update(held);
    EXPECT_FALSE(map.is_action_just_released("fire"));

    map.update(none);  // release frame
    EXPECT_TRUE(map.is_action_just_released("fire"));
    EXPECT_FALSE(map.is_action_pressed("fire"));

    // Next frame with no press -> just_released must be false again
    map.update(none);
    EXPECT_FALSE(map.is_action_just_released("fire"));
}

// -----------------------------------------------------------------------------
// 14) just_pressed / just_released on unknown action -> false (safe default).
// -----------------------------------------------------------------------------
TEST(ActionMap, JustPressedReleasedUnknownActionReturnFalse)
{
    ActionMap map;
    RawInputSnapshot frame;
    map.update(frame);
    EXPECT_FALSE(map.is_action_just_pressed("ghost"));
    EXPECT_FALSE(map.is_action_just_released("ghost"));
}

// -----------------------------------------------------------------------------
// 15) Axis dead-zone: value within dead-zone -> axis_value == 0 and not pressed.
// -----------------------------------------------------------------------------
TEST(ActionMap, DeadZoneSuppressesSmallAxisValue)
{
    // Arrange
    ActionMap map;
    constexpr int kAxisLx = 0;
    EXPECT_TRUE(map.bind_axis("move_x", "gamepad_axis", kAxisLx));
    map.set_dead_zone("move_x", 0.15F);

    // Act — supply value inside dead-zone
    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxisLx, 0.10F);
    map.update(frame);

    // Assert
    EXPECT_FLOAT_EQ(map.axis_value("move_x"), 0.0F);
    EXPECT_FALSE(map.is_action_pressed("move_x"));
}

// -----------------------------------------------------------------------------
// 16) Axis dead-zone boundary: value exactly at dead-zone boundary is NOT
//     suppressed (|v| >= dead_zone passes through).
// -----------------------------------------------------------------------------
TEST(ActionMap, DeadZoneBoundaryValuePassesThrough)
{
    ActionMap map;
    constexpr int kAxisLx = 0;
    EXPECT_TRUE(map.bind_axis("move_x", "gamepad_axis", kAxisLx));
    map.set_dead_zone("move_x", 0.15F);

    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxisLx, 0.15F);  // exactly at boundary
    map.update(frame);

    // 0.15 >= dead_zone(0.15) → passes; but < kAxisPressThreshold(0.5) → not pressed
    EXPECT_FLOAT_EQ(map.axis_value("move_x"), 0.15F);
    EXPECT_FALSE(map.is_action_pressed("move_x"));
}

// -----------------------------------------------------------------------------
// 17) Axis dead-zone with value above dead-zone AND above press threshold.
// -----------------------------------------------------------------------------
TEST(ActionMap, DeadZoneAboveThresholdIsPressed)
{
    ActionMap map;
    constexpr int kAxisLx = 0;
    EXPECT_TRUE(map.bind_axis("move_x", "gamepad_axis", kAxisLx));
    map.set_dead_zone("move_x", 0.15F);

    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxisLx, 0.7F);
    map.update(frame);

    EXPECT_FLOAT_EQ(map.axis_value("move_x"), 0.7F);
    EXPECT_TRUE(map.is_action_pressed("move_x"));
}

// -----------------------------------------------------------------------------
// 18) Axis clamp: raw hardware values outside [-1, 1] are clamped.
// -----------------------------------------------------------------------------
TEST(ActionMap, AxisValueClampsToMinusOneToOne)
{
    ActionMap map;
    constexpr int kAxis = 2;
    EXPECT_TRUE(map.bind_axis("throttle", "gamepad_axis", kAxis));

    // Value exceeding +1
    RawInputSnapshot framePos;
    framePos.set_axis("gamepad_axis", kAxis, 1.5F);
    map.update(framePos);
    EXPECT_FLOAT_EQ(map.axis_value("throttle"), 1.0F);

    // Value below -1
    RawInputSnapshot frameNeg;
    frameNeg.set_axis("gamepad_axis", kAxis, -2.0F);
    map.update(frameNeg);
    EXPECT_FLOAT_EQ(map.axis_value("throttle"), -1.0F);
}

// -----------------------------------------------------------------------------
// 19) Axis press threshold boundary: exactly 0.5 is pressed, below is not.
// -----------------------------------------------------------------------------
TEST(ActionMap, AxisPressThresholdBoundary)
{
    ActionMap map;
    constexpr int kAxis = 0;
    EXPECT_TRUE(map.bind_axis("tilt", "gamepad_axis", kAxis));

    // Exactly at threshold -> pressed (>= kAxisPressThreshold).
    RawInputSnapshot atThreshold;
    atThreshold.set_axis("gamepad_axis", kAxis, 0.5F);
    map.update(atThreshold);
    EXPECT_TRUE(map.is_action_pressed("tilt"));
    EXPECT_FLOAT_EQ(map.axis_value("tilt"), 0.5F);

    // Just below threshold -> not pressed.
    RawInputSnapshot belowThreshold;
    belowThreshold.set_axis("gamepad_axis", kAxis, 0.4999F);
    map.update(belowThreshold);
    EXPECT_FALSE(map.is_action_pressed("tilt"));
    EXPECT_FLOAT_EQ(map.axis_value("tilt"), 0.4999F);
}

// -----------------------------------------------------------------------------
// 20) Negative axis value (-0.5) is pressed; (-0.4999) is not.
// -----------------------------------------------------------------------------
TEST(ActionMap, NegativeAxisThresholdBoundary)
{
    ActionMap map;
    constexpr int kAxis = 0;
    EXPECT_TRUE(map.bind_axis("tilt", "gamepad_axis", kAxis));

    RawInputSnapshot atNeg;
    atNeg.set_axis("gamepad_axis", kAxis, -0.5F);
    map.update(atNeg);
    EXPECT_TRUE(map.is_action_pressed("tilt"));
    EXPECT_FLOAT_EQ(map.axis_value("tilt"), -0.5F);

    RawInputSnapshot aboveNeg;
    aboveNeg.set_axis("gamepad_axis", kAxis, -0.4999F);
    map.update(aboveNeg);
    EXPECT_FALSE(map.is_action_pressed("tilt"));
}

// -----------------------------------------------------------------------------
// 21) Rebind to the same key (same action, same binding) succeeds after unbind.
// -----------------------------------------------------------------------------
TEST(ActionMap, RebindToSameKeySucceeds)
{
    ActionMap map;
    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));
    EXPECT_EQ(map.binding_count("jump"), 1U);

    // rebind() internally calls unbind_all + bind — same key is fine.
    EXPECT_TRUE(map.rebind("jump", InputBinding{"jump", "keyboard", kKeyJ}));
    EXPECT_EQ(map.binding_count("jump"), 1U);

    RawInputSnapshot frame;
    frame.press("keyboard", kKeyJ);
    map.update(frame);
    EXPECT_TRUE(map.is_action_pressed("jump"));
}

// -----------------------------------------------------------------------------
// 22) Rebind conflicting: key already bound to action B. Both actions fire
//     when the shared key is pressed (library does NOT prevent cross-action
//     key sharing — that is the caller's responsibility).
// -----------------------------------------------------------------------------
TEST(ActionMap, RebindToKeyAlreadyOnDifferentActionBothFire)
{
    ActionMap map;
    int fire_jump   = 0;
    int fire_attack = 0;

    EXPECT_TRUE(map.bind_button("jump",   "keyboard", kKeyW));
    EXPECT_TRUE(map.bind_button("attack", "keyboard", kKeyA));
    map.on_action("jump",   [&](const std::string&) { ++fire_jump; });
    map.on_action("attack", [&](const std::string&) { ++fire_attack; });

    // Rebind "attack" to the same key as "jump".
    EXPECT_TRUE(map.rebind("attack", InputBinding{"attack", "keyboard", kKeyW}));

    RawInputSnapshot frame;
    frame.press("keyboard", kKeyW);
    map.update(frame);

    // Both actions bound to kKeyW must fire.
    EXPECT_EQ(fire_jump,   1);
    EXPECT_EQ(fire_attack, 1);
    EXPECT_TRUE(map.is_action_pressed("jump"));
    EXPECT_TRUE(map.is_action_pressed("attack"));
}

// -----------------------------------------------------------------------------
// 23) Unbound action after unbind_all: binding_count == 0, action still in map.
// -----------------------------------------------------------------------------
TEST(ActionMap, UnbindAllLeavesActionWithZeroBindings)
{
    ActionMap map;
    EXPECT_TRUE(map.bind_button("dash", "keyboard", kKeyW));
    EXPECT_EQ(map.binding_count("dash"), 1U);

    const std::size_t removed = map.unbind_all("dash");
    EXPECT_EQ(removed, 1U);
    EXPECT_EQ(map.binding_count("dash"), 0U);
    // Action entry persists (action_count does not drop to zero; callbacks remain).
    EXPECT_GE(map.action_count(), 1U);

    // Querying pressed after unbind_all returns safe false.
    RawInputSnapshot frame;
    frame.press("keyboard", kKeyW);
    map.update(frame);
    EXPECT_FALSE(map.is_action_pressed("dash"));
    EXPECT_FLOAT_EQ(map.axis_value("dash"), 0.0F);
}

// -----------------------------------------------------------------------------
// 24) Multiple bindings same device different codes -> both contribute to pressed;
//     dominant-magnitude axis value chosen.
// -----------------------------------------------------------------------------
TEST(ActionMap, MultipleBindingsSameDeviceDifferentCodes)
{
    ActionMap map;
    constexpr int kAxisLeft  = 0;
    constexpr int kAxisRight = 1;
    EXPECT_TRUE(map.bind_axis("move_h", "gamepad_axis", kAxisLeft));
    EXPECT_TRUE(map.bind_axis("move_h", "gamepad_axis", encode_axis(kAxisRight, /*invert=*/false)));
    EXPECT_EQ(map.binding_count("move_h"), 2U);

    // Only right axis active at larger magnitude.
    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxisLeft,  0.3F);
    frame.set_axis("gamepad_axis", kAxisRight, 0.8F);
    map.update(frame);

    // Dominant is right (0.8 > 0.3) -> axis_value == 0.8, pressed == true.
    EXPECT_FLOAT_EQ(map.axis_value("move_h"), 0.8F);
    EXPECT_TRUE(map.is_action_pressed("move_h"));
}

// -----------------------------------------------------------------------------
// 25) Simultaneous press of two bindings on same action: callback fires once.
// -----------------------------------------------------------------------------
TEST(ActionMap, SimultaneousPressOfTwoBindingsFiresCallbackOnce)
{
    ActionMap map;
    int fire_count = 0;

    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));
    EXPECT_TRUE(map.bind_button("jump", "gamepad",  kPadA));
    map.on_action("jump", [&](const std::string&) { ++fire_count; });

    // Both devices pressed simultaneously.
    RawInputSnapshot both;
    both.press("keyboard", kKeyJ);
    both.press("gamepad",  kPadA);
    map.update(both);

    // Callback fires exactly once — one rising edge per action.
    EXPECT_EQ(fire_count, 1);
    EXPECT_TRUE(map.is_action_pressed("jump"));
}

// -----------------------------------------------------------------------------
// 26) Axis value zero when no snapshot is provided yet (initial state).
// -----------------------------------------------------------------------------
TEST(ActionMap, AxisValueDefaultsToZeroBeforeFirstUpdate)
{
    ActionMap map;
    constexpr int kAxis = 0;
    EXPECT_TRUE(map.bind_axis("look_x", "gamepad_axis", kAxis));
    // No update() called yet — axis should be 0.
    EXPECT_FLOAT_EQ(map.axis_value("look_x"), 0.0F);
    EXPECT_FALSE(map.is_action_pressed("look_x"));
}

// -----------------------------------------------------------------------------
// 27) set_dead_zone on an action with zero dead_zone (default) leaves values
//     unchanged — including a value that is already 0.
// -----------------------------------------------------------------------------
TEST(ActionMap, ZeroDeadZoneIsNoop)
{
    ActionMap map;
    constexpr int kAxis = 0;
    EXPECT_TRUE(map.bind_axis("move_y", "gamepad_axis", kAxis));
    map.set_dead_zone("move_y", 0.0F);  // explicit zero — same as default

    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxis, 0.05F);
    map.update(frame);

    // No dead-zone filtering — 0.05 passes through.
    EXPECT_FLOAT_EQ(map.axis_value("move_y"), 0.05F);
    EXPECT_FALSE(map.is_action_pressed("move_y"));
}

// -----------------------------------------------------------------------------
// 28) Dead-zone does not suppress a button binding on the same action.
// -----------------------------------------------------------------------------
TEST(ActionMap, DeadZoneDoesNotSuppressButtonBindingOnSameAction)
{
    ActionMap map;
    constexpr int kAxis = 0;
    EXPECT_TRUE(map.bind_axis("dash",   "gamepad_axis", kAxis));
    EXPECT_TRUE(map.bind_button("dash", "keyboard",     kKeyW));
    map.set_dead_zone("dash", 0.5F);

    // Axis in dead-zone but button pressed.
    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxis, 0.1F);  // inside dead-zone
    frame.press("keyboard", kKeyW);
    map.update(frame);

    // Button press must win despite axis being in dead-zone.
    EXPECT_TRUE(map.is_action_pressed("dash"));
    // The button binding contributes axis magnitude 1.0 (dominant over the
    // dead-zone-zeroed 0.1 from the axis binding). axis_value == 1.0.
    EXPECT_FLOAT_EQ(map.axis_value("dash"), 1.0F);
}

// -----------------------------------------------------------------------------
// 29) Inverted axis binding returns negative value; threshold test uses |value|.
// -----------------------------------------------------------------------------
TEST(ActionMap, InvertedAxisPressedWhenAbsoluteValueExceedsThreshold)
{
    ActionMap map;
    constexpr int kAxis = 0;
    // Invert flag set.
    EXPECT_TRUE(map.bind_axis("look_inv", "gamepad_axis", kAxis, /*invert=*/true));

    RawInputSnapshot frame;
    frame.set_axis("gamepad_axis", kAxis, 0.8F);
    map.update(frame);

    // Inverted: stored value is -0.8; |−0.8| >= 0.5 → pressed.
    EXPECT_FLOAT_EQ(map.axis_value("look_inv"), -0.8F);
    EXPECT_TRUE(map.is_action_pressed("look_inv"));
}

// -----------------------------------------------------------------------------
// 30) unbind() on an action that was never registered returns false gracefully.
// -----------------------------------------------------------------------------
TEST(ActionMap, UnbindNeverRegisteredActionReturnsFalse)
{
    ActionMap map;
    InputBinding ghost{"ghost", "keyboard", kKeyW};
    EXPECT_FALSE(map.unbind("ghost", ghost));
    EXPECT_EQ(map.binding_count("ghost"), 0U);
}

// -----------------------------------------------------------------------------
// 31) on_action with null callback is silently dropped (no crash on update).
// -----------------------------------------------------------------------------
TEST(ActionMap, NullCallbackDroppedSilently)
{
    ActionMap map;
    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));
    map.on_action("jump", nullptr);  // must not register a null slot

    RawInputSnapshot frame;
    frame.press("keyboard", kKeyJ);
    // update() must not crash when traversing null callback.
    EXPECT_NO_THROW(map.update(frame));
    EXPECT_TRUE(map.is_action_pressed("jump"));
}

// -----------------------------------------------------------------------------
// 32) Callback re-entry safety: callback calls clear_callbacks on self; the
//     remaining callbacks in the same frame must still complete without crash.
// -----------------------------------------------------------------------------
TEST(ActionMap, CallbackReentryViaClearCallbacksSafe)
{
    ActionMap map;
    int fire_a = 0;
    int fire_b = 0;

    EXPECT_TRUE(map.bind_button("jump", "keyboard", kKeyJ));

    // First callback clears all callbacks on "jump" mid-fire.
    map.on_action("jump", [&](const std::string& name) {
        ++fire_a;
        map.clear_callbacks(name);
    });
    map.on_action("jump", [&](const std::string&) { ++fire_b; });

    RawInputSnapshot frame;
    frame.press("keyboard", kKeyJ);
    // Must not crash — update() copies the callback list before iterating.
    EXPECT_NO_THROW(map.update(frame));
    EXPECT_EQ(fire_a, 1);
    EXPECT_EQ(fire_b, 1);  // second cb ran from the copied snapshot

    // Release and re-press — now no callbacks remain.
    RawInputSnapshot none;
    map.update(none);
    map.update(frame);
    EXPECT_EQ(fire_a, 1);  // no more fires after clear
    EXPECT_EQ(fire_b, 1);
}

// -----------------------------------------------------------------------------
// 33) encode_axis / axis_index / axis_invert round-trip helpers.
// -----------------------------------------------------------------------------
TEST(ActionMap, AxisEncodingRoundTrip)
{
    constexpr int kId = 5;
    const int enc_normal  = cd::gameplay::input_binding::encode_axis(kId, false);
    const int enc_inverted = cd::gameplay::input_binding::encode_axis(kId, true);

    EXPECT_EQ(cd::gameplay::input_binding::axis_index(enc_normal),  kId);
    EXPECT_FALSE(cd::gameplay::input_binding::axis_invert(enc_normal));

    EXPECT_EQ(cd::gameplay::input_binding::axis_index(enc_inverted), kId);
    EXPECT_TRUE(cd::gameplay::input_binding::axis_invert(enc_inverted));

    // Max axis id in 8-bit range.
    constexpr int kMaxId = 0xFF;
    const int enc_max = cd::gameplay::input_binding::encode_axis(kMaxId, true);
    EXPECT_EQ(cd::gameplay::input_binding::axis_index(enc_max), kMaxId);
    EXPECT_TRUE(cd::gameplay::input_binding::axis_invert(enc_max));
}

}  // namespace
