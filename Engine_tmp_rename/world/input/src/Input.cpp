// =============================================================================
// CHROMODYNAMIC — cd/input/Input.cpp
// =============================================================================
#include <cd/input/Input.hpp>

namespace cd::input
{

void InputContext::push_event(const InputEvent& e)
{
    // Update polled state synchronously so a `state().is_key_down()` call
    // immediately after `push_event` reflects the new value. Then queue the
    // event for game-side draining.
    switch (e.kind)
    {
        case EventKind::kKeyDown:
            state_.set_key(e.key, true);
            break;
        case EventKind::kKeyUp:
            state_.set_key(e.key, false);
            break;
        case EventKind::kMouseButtonDown:
            state_.set_mouse_button(e.mouse_button, true);
            break;
        case EventKind::kMouseButtonUp:
            state_.set_mouse_button(e.mouse_button, false);
            break;
        case EventKind::kMouseMove:
            state_.set_mouse_position(e.mouse_x, e.mouse_y);
            break;
        case EventKind::kMouseWheel:
            state_.add_wheel(e.wheel);
            break;
    }
    events_.push_back(e);
}

}  // namespace cd::input
