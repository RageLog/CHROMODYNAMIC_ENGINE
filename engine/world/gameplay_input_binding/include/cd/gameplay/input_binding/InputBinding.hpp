// =============================================================================
// CHROMODYNAMIC — cd/gameplay/input_binding/InputBinding.hpp
// Phase 462 — cd::gameplay::input_binding::ActionMap
//
// Action-mapping (a.k.a. "input remapping" / "input action layer") sits one
// tier above raw `cd::input::InputContext` polling. Gameplay code does NOT
// query "is W pressed?" — it queries "is action `move_forward` pressed?".
// Bindings are registered out-of-band (config file, settings UI, key-rebind
// dialog) so swapping device or rebinding mid-game is a data change rather
// than a code change.
//
// Production engines converge on this shape:
//   * Unreal `UInputComponent::BindAction` / Enhanced Input `IA_*` assets.
//   * Unity `InputActionMap` with composite bindings + control schemes.
//   * Godot `InputMap` with `add_action` / `action_add_event`.
//   * Bevy `bevy_input::Input<KeyCode>` + `leafwing_input_manager` crate.
//
// Three orthogonal kinds cover the practical surface:
//   * kButton  — binary down/up (jump, fire, pause).
//   * kAxis    — single-axis float in [-1, 1] (throttle, look-x).
//   * kVector2 — two-axis float (movement stick, look pad).
//
// Bindings carry a `device` string (e.g. "keyboard", "gamepad", "mouse") and
// an opaque `scancode_or_button` int so the same `ActionMap` can host
// heterogeneous devices simultaneously. The raw input snapshot fed into
// `update()` lives in `RawInputSnapshot` — a deliberately tiny, device-agnostic
// POD so this library does not become a transitive consumer of every backend
// header. Callers translate `cd::input::InputState` / `GamepadState` into
// `RawInputSnapshot` at the call site (one-liner adapter).
//
// Edge-trigger semantics:
//   * `is_action_pressed()` returns the *current* held state — useful for
//     "while-held" actions like sprint.
//   * Registered callbacks fire ONCE per press transition (rising edge), not
//     continuously while held. This matches Unity / Unreal / Godot semantics
//     and is essential for "jump on press" gameplay.
//
// Dependencies (CLAUDE.md §7): cd::core only at the header level — the .cpp
// pulls in <cd/input/Input.hpp> for an adapter helper but the public surface
// stays device-agnostic so this library is reusable in headless tests.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cd::gameplay::input_binding
{

// -----------------------------------------------------------------------------
// ActionKind — discriminator for the value an action produces.
// -----------------------------------------------------------------------------
enum class ActionKind : std::uint8_t
{
    kButton  = 0,  ///< Binary down/up; callbacks fire on rising edge.
    kAxis    = 1,  ///< Single scalar in [-1, 1] (or [0, 1] for trigger-style).
    kVector2 = 2,  ///< Two-axis stick — read via `axis_value` per component.
};

// -----------------------------------------------------------------------------
// InputBinding — one input source bound to an action.
//
// `device` is a free-form short string (e.g. "keyboard", "gamepad",
// "mouse"). `scancode_or_button` is interpreted device-side:
//   * "keyboard"  → `static_cast<int>(cd::input::KeyCode)`
//   * "gamepad"   → `static_cast<int>(cd::input::GamepadButton)` bitmask.
//   * "mouse"     → `static_cast<int>(cd::input::MouseButton)`
//
// For axis bindings, `scancode_or_button` encodes the axis index plus an
// optional inversion bit: see `axis_index()` / `axis_invert()` helpers
// below for the canonical scheme (low byte = axis id, high bit = invert).
// -----------------------------------------------------------------------------
struct InputBinding
{
    std::string action_name;
    std::string device;
    int         scancode_or_button { 0 };

    [[nodiscard]] bool operator==(const InputBinding& other) const noexcept
    {
        return action_name        == other.action_name
            && device             == other.device
            && scancode_or_button == other.scancode_or_button;
    }
};

// -----------------------------------------------------------------------------
// Axis bit-encoding helpers — keep `InputBinding` a POD by stuffing the axis
// id + invert flag into the single `scancode_or_button` int. Axis ids are
// arbitrary small integers chosen by the caller; the library never inspects
// them, it merely looks the id up in `RawInputSnapshot::axes`.
// -----------------------------------------------------------------------------
[[nodiscard]] constexpr int  encode_axis(int axis_id, bool invert = false) noexcept
{
    return (axis_id & 0xFF) | (invert ? 0x100 : 0);
}
[[nodiscard]] constexpr int  axis_index(int encoded) noexcept  { return encoded & 0xFF; }
[[nodiscard]] constexpr bool axis_invert(int encoded) noexcept { return (encoded & 0x100) != 0; }

// -----------------------------------------------------------------------------
// RawInputSnapshot — device-agnostic input frame.
//
// Callers build this from whichever backend they use (cd::input::InputState,
// SDL_GameController, GLFW, a recorded replay tape, ...) and hand it to
// `ActionMap::update()`. Two parallel maps:
//   * `buttons[device]` — set of currently-down `scancode_or_button` ints.
//   * `axes[device]`    — per-device map of axis-id → float in [-1, 1].
//
// Buttons are device-keyed so "key W on keyboard" and "button 0 on gamepad"
// never collide even if they share the same integer code.
// -----------------------------------------------------------------------------
struct RawInputSnapshot
{
    std::unordered_map<std::string, std::unordered_set<int>>           buttons;
    std::unordered_map<std::string, std::unordered_map<int, float>>    axes;

    /// Convenience: mark a button as pressed (idempotent).
    void press(const std::string& device, int code)
    {
        buttons[device].insert(code);
    }

    /// Convenience: set an axis value (replaces previous).
    void set_axis(const std::string& device, int axis_id, float value)
    {
        axes[device][axis_id] = value;
    }
};

// -----------------------------------------------------------------------------
// ActionCallback — fired once per rising edge for button actions.
// -----------------------------------------------------------------------------
using ActionCallback = std::function<void(const std::string& action_name)>;

// -----------------------------------------------------------------------------
// ActionMap — register bindings + callbacks, then call `update()` per frame.
//
// Threading: not thread-safe; drive from the same thread that owns the input
// pump (typically the engine main loop). Snapshot results to immutable POD
// copies if worker threads need read access.
// -----------------------------------------------------------------------------
class ActionMap
{
public:
    ActionMap() = default;

    /// Register a binding under `action_name`. If the same triple
    /// (action, device, code) is bound twice the duplicate is rejected —
    /// returns false. Multiple distinct bindings under the same action
    /// are explicitly supported (keyboard + gamepad map to same action).
    bool bind(const std::string& action_name, const InputBinding& binding);

    /// Convenience overload: kind hint distinguishes button vs axis
    /// bindings. Defaults to `kButton`.
    bool bind_button(const std::string& action_name,
                     const std::string& device,
                     int                code);

    bool bind_axis(const std::string& action_name,
                   const std::string& device,
                   int                axis_id,
                   bool               invert = false);

    /// Remove exactly the matching binding. Returns true if a binding was
    /// removed. Other bindings (different device or code) on the same
    /// action are untouched.
    bool unbind(const std::string& action_name, const InputBinding& binding);

    /// Remove ALL bindings registered under `action_name`. Callbacks are
    /// left intact (rebinding after unbind_all keeps the callback wired).
    /// Returns the number of bindings removed.
    std::size_t unbind_all(const std::string& action_name);

    /// Replace every binding under `action_name` with a single new
    /// binding. Equivalent to `unbind_all` + `bind`. Idempotent for the
    /// "settings UI rebinds a key" path.
    bool rebind(const std::string& action_name, const InputBinding& binding);

    /// Register a callback fired on the rising edge of a button action.
    /// Multiple callbacks may be attached to the same action.
    void on_action(const std::string& action_name, ActionCallback callback);

    /// Drop every callback bound under `action_name`. Used by gameplay
    /// state-machines transitioning out of a mode.
    std::size_t clear_callbacks(const std::string& action_name);

    /// Drive the map from a raw snapshot. Walks every registered action,
    /// resolves its bindings against the snapshot, and:
    ///   * updates `is_action_pressed` / `axis_value` for the next frame.
    ///   * fires every callback registered on actions that transitioned
    ///     from "not-pressed" last frame to "pressed" this frame.
    void update(const RawInputSnapshot& snapshot);

    /// Current held state for a button action (or any action whose
    /// magnitude exceeds the press threshold for axis-style sources).
    [[nodiscard]] bool is_action_pressed(const std::string& action_name) const noexcept;

    /// Raw axis value in [-1, 1] for an axis action. For button actions
    /// returns 1.0F if pressed, 0.0F otherwise. Unknown action -> 0.0F.
    [[nodiscard]] float axis_value(const std::string& action_name) const noexcept;

    /// Number of bindings registered under `action_name` (0 if unknown).
    [[nodiscard]] std::size_t binding_count(const std::string& action_name) const noexcept;

    /// Total number of distinct actions registered (with bindings or
    /// callbacks). Useful for sanity-asserting after `clear()`.
    [[nodiscard]] std::size_t action_count() const noexcept { return actions_.size(); }

    /// Wipe every binding, callback, and cached pressed/axis state.
    void clear() noexcept;

private:
    struct ActionRecord
    {
        std::vector<InputBinding>   bindings;
        std::vector<ActionCallback> callbacks;
        bool   pressed_now  { false };
        bool   pressed_prev { false };
        float  axis         { 0.0F };
    };

    // Returns nullptr if the action has no entry yet. Used by const queries.
    [[nodiscard]] const ActionRecord* find(const std::string& action_name) const noexcept;

    // Ensures an ActionRecord exists for `action_name` and returns a reference.
    [[nodiscard]] ActionRecord& touch(const std::string& action_name);

    // Resolve one binding against the snapshot. Returns the contribution to
    // the action's `pressed` (binary) and `axis` (scalar) values for this
    // frame. The caller aggregates across all bindings on the action.
    static void resolve_binding(const InputBinding&     binding,
                                const RawInputSnapshot& snapshot,
                                bool&                   out_pressed,
                                float&                  out_axis) noexcept;

    std::unordered_map<std::string, ActionRecord> actions_ {};
};

}  // namespace cd::gameplay::input_binding
