// =============================================================================
// CHROMODYNAMIC — tests/test_input_binding.cpp
// Phase 462 — cd::gameplay::input_binding::ActionMap unit tests.
// =============================================================================
#include <cd/gameplay/input_binding/InputBinding.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{

using cd::gameplay::input_binding::ActionKind;
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

}  // namespace
