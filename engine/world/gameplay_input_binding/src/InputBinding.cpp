// =============================================================================
// CHROMODYNAMIC — cd/gameplay/input_binding/InputBinding.cpp
// Phase 462 — ActionMap implementation.
// =============================================================================
#include <cd/gameplay/input_binding/InputBinding.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cd::gameplay::input_binding
{

namespace
{

// A button-style binding contributes "pressed" iff its device+code appears in
// the snapshot's buttons set. An axis-style binding contributes "pressed" iff
// |value| crosses this threshold — keeps "axis -> action" bridges from
// firing on every stick jitter. 0.5 matches Unity / Unreal defaults.
constexpr float kAxisPressThreshold = 0.5F;

[[nodiscard]] bool is_axis_device(const std::string& device) noexcept
{
    // Heuristic that mirrors `RawInputSnapshot::set_axis` callers: any device
    // whose name suggests analog input. Callers can opt-in explicitly by
    // using `bind_axis()` which always treats the code as an encoded axis id.
    // Note: we still resolve against `axes[device]` for unknown devices too,
    // because the lookup is harmless when the device map is empty.
    return device == "axis"
        || device == "gamepad_axis"
        || device == "mouse_axis";
}

}  // namespace

// -----------------------------------------------------------------------------
// resolve_binding — see header for contract.
// NOTE: dead-zone and clamp are applied by update() after aggregation so that
// multi-binding dominant selection sees raw magnitudes, then post-processes.
// resolve_binding itself only applies invert + threshold on the raw value.
// -----------------------------------------------------------------------------
void ActionMap::resolve_binding(const InputBinding&     binding,
                                const RawInputSnapshot& snapshot,
                                bool&                   out_pressed,
                                float&                  out_axis) noexcept
{
    out_pressed = false;
    out_axis    = 0.0F;

    // 1) Axis-style device first — these encode (axis_id | invert_bit).
    if (is_axis_device(binding.device))
    {
        const auto dev_it = snapshot.axes.find(binding.device);
        if (dev_it == snapshot.axes.end())
        {
            return;
        }
        const int  id      = axis_index(binding.scancode_or_button);
        const bool invert  = axis_invert(binding.scancode_or_button);
        const auto axis_it = dev_it->second.find(id);
        if (axis_it == dev_it->second.end())
        {
            return;
        }
        float v = axis_it->second;
        // Clamp raw input to [-1, 1] — hardware can occasionally exceed range.
        v = std::clamp(v, -1.0F, 1.0F);
        if (invert) { v = -v; }
        out_axis    = v;
        // Threshold test deferred — update() applies dead-zone first.
        out_pressed = std::fabs(v) >= kAxisPressThreshold;
        return;
    }

    // 2) Button-style — look up the (device, code) pair in the buttons set.
    const auto dev_it = snapshot.buttons.find(binding.device);
    if (dev_it == snapshot.buttons.end())
    {
        return;
    }
    if (dev_it->second.contains(binding.scancode_or_button))
    {
        out_pressed = true;
        out_axis    = 1.0F;
    }
}

// -----------------------------------------------------------------------------
// Internal record helpers
// -----------------------------------------------------------------------------
const ActionMap::ActionRecord* ActionMap::find(const std::string& action_name) const noexcept
{
    const auto it = actions_.find(action_name);
    return (it == actions_.end()) ? nullptr : &it->second;
}

ActionMap::ActionRecord& ActionMap::touch(const std::string& action_name)
{
    return actions_[action_name];
}

// -----------------------------------------------------------------------------
// Binding management
// -----------------------------------------------------------------------------
bool ActionMap::bind(const std::string& action_name, const InputBinding& binding)
{
    ActionRecord& rec = touch(action_name);

    // Reject exact duplicates so `bind()` is idempotent at the (action,
    // device, code) granularity. Two distinct devices on the same action
    // are still allowed (keyboard + gamepad -> same `jump`).
    const auto same = [&](const InputBinding& b) {
        return b.device == binding.device
            && b.scancode_or_button == binding.scancode_or_button;
    };
    if (std::ranges::any_of(rec.bindings, same))
    {
        return false;
    }

    InputBinding stored = binding;
    stored.action_name  = action_name;  // canonicalize regardless of caller.
    rec.bindings.push_back(std::move(stored));
    return true;
}

bool ActionMap::bind_button(const std::string& action_name,
                            const std::string& device,
                            int                code)
{
    return bind(action_name, InputBinding{ action_name, device, code });
}

bool ActionMap::bind_axis(const std::string& action_name,
                          const std::string& device,
                          int                axis_id,
                          bool               invert)
{
    return bind(action_name,
                InputBinding{ action_name, device, encode_axis(axis_id, invert) });
}

bool ActionMap::unbind(const std::string& action_name, const InputBinding& binding)
{
    auto it = actions_.find(action_name);
    if (it == actions_.end())
    {
        return false;
    }
    auto& bindings = it->second.bindings;
    const auto same = [&](const InputBinding& b) {
        return b.device == binding.device
            && b.scancode_or_button == binding.scancode_or_button;
    };
    const auto erased = std::erase_if(bindings, same);
    return erased > 0;
}

std::size_t ActionMap::unbind_all(const std::string& action_name)
{
    auto it = actions_.find(action_name);
    if (it == actions_.end())
    {
        return 0;
    }
    const std::size_t n = it->second.bindings.size();
    it->second.bindings.clear();
    // Also reset cached pressed/axis state so a stale press doesn't leak
    // across a rebind.
    it->second.pressed_now  = false;
    it->second.pressed_prev = false;
    it->second.axis         = 0.0F;
    return n;
}

bool ActionMap::rebind(const std::string& action_name, const InputBinding& binding)
{
    unbind_all(action_name);
    return bind(action_name, binding);
}

// -----------------------------------------------------------------------------
// Callback management
// -----------------------------------------------------------------------------
void ActionMap::on_action(const std::string& action_name, ActionCallback callback)
{
    if (!callback) return;
    ActionRecord& rec = touch(action_name);
    rec.callbacks.push_back(std::move(callback));
}

std::size_t ActionMap::clear_callbacks(const std::string& action_name)
{
    auto it = actions_.find(action_name);
    if (it == actions_.end())
    {
        return 0;
    }
    const std::size_t n = it->second.callbacks.size();
    it->second.callbacks.clear();
    return n;
}

// -----------------------------------------------------------------------------
// Frame update
// -----------------------------------------------------------------------------
void ActionMap::update(const RawInputSnapshot& snapshot)
{
    for (auto& [name, rec] : actions_)
    {
        rec.pressed_prev = rec.pressed_now;

        bool  any_pressed   = false;
        float axis_max_abs  = 0.0F;  // magnitude — used to pick dominant binding
        float axis_signed   = 0.0F;  // signed value of dominant binding

        for (const auto& b : rec.bindings)
        {
            bool  p = false;
            float v = 0.0F;
            resolve_binding(b, snapshot, p, v);
            any_pressed = any_pressed || p;
            if (std::fabs(v) > axis_max_abs)
            {
                axis_max_abs = std::fabs(v);
                axis_signed  = v;
            }
        }

        // Apply per-action dead-zone: if the dominant axis magnitude is below
        // the dead-zone the value collapses to 0.  Re-derive `any_pressed` so
        // that an axis stuck in the dead-zone does not register as a press.
        // Button bindings are not affected by the dead-zone (they are binary).
        if (rec.dead_zone > 0.0F && std::fabs(axis_signed) < rec.dead_zone)
        {
            axis_signed = 0.0F;
            // Recompute pressed from button bindings only — their contribution
            // stands regardless; axis bindings that fell into dead-zone lose
            // their press vote.
            bool button_pressed = false;
            bool axis_pressed   = false;
            for (const auto& b : rec.bindings)
            {
                bool  p = false;
                float v = 0.0F;
                resolve_binding(b, snapshot, p, v);
                if (is_axis_device(b.device))
                {
                    // Count this axis binding's press only if its absolute
                    // value clears the dead-zone.
                    if (std::fabs(v) >= rec.dead_zone) { axis_pressed = axis_pressed || p; }
                }
                else
                {
                    button_pressed = button_pressed || p;
                }
            }
            any_pressed = button_pressed || axis_pressed;
        }
        // Final clamp — ensures axis_value() always stays in [-1, 1].
        axis_signed = std::clamp(axis_signed, -1.0F, 1.0F);

        rec.pressed_now = any_pressed;
        rec.axis        = axis_signed;

        // Rising edge — fire callbacks exactly once per press transition.
        if (rec.pressed_now && !rec.pressed_prev)
        {
            // Copy the callback list before iterating: a callback might
            // re-enter the map (e.g. clear_callbacks on self) and we don't
            // want to invalidate `rec.callbacks` mid-walk.
            const auto callbacks = rec.callbacks;
            for (const auto& cb : callbacks)
            {
                if (cb) cb(name);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------
bool ActionMap::is_action_pressed(const std::string& action_name) const noexcept
{
    const auto* rec = find(action_name);
    return rec != nullptr && rec->pressed_now;
}

bool ActionMap::is_action_just_pressed(const std::string& action_name) const noexcept
{
    const auto* rec = find(action_name);
    return rec != nullptr && rec->pressed_now && !rec->pressed_prev;
}

bool ActionMap::is_action_just_released(const std::string& action_name) const noexcept
{
    const auto* rec = find(action_name);
    return rec != nullptr && !rec->pressed_now && rec->pressed_prev;
}

float ActionMap::axis_value(const std::string& action_name) const noexcept
{
    const auto* rec = find(action_name);
    return rec != nullptr ? rec->axis : 0.0F;
}

void ActionMap::set_dead_zone(const std::string& action_name, float dead_zone) noexcept
{
    touch(action_name).dead_zone = std::clamp(dead_zone, 0.0F, 1.0F);
}

std::size_t ActionMap::binding_count(const std::string& action_name) const noexcept
{
    const auto* rec = find(action_name);
    return rec != nullptr ? rec->bindings.size() : 0;
}

void ActionMap::clear() noexcept
{
    actions_.clear();
}

}  // namespace cd::gameplay::input_binding
