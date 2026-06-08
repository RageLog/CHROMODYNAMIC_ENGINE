// =============================================================================
// CHROMODYNAMIC — cd/input/Input.hpp
// Phase 4 / Sprint S4.6 — input system.
//
// Two complementary surfaces:
//   * `InputState` — polled query API. Game code asks "is W down?" each
//     frame; this is the cheapest and most common pattern.
//   * `InputEvent` queue — discrete events with timestamps (key down,
//     key up, mouse-move delta). Lets gameplay code react to single-frame
//     transitions without sampling-rate ambiguity.
//
// Both surfaces sit behind `InputContext` so platform layers (Win32,
// Wayland, X11) feed events in via `push_event()` and game code reads the
// resulting state. The MVP covers keyboard + mouse; gamepad / touch /
// motion are follow-ups.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <utility>

namespace cd::input
{

/// USB-HID-style key codes. Mirrors the SDL2 / GLFW abstraction so
/// translation layers in platform code stay one-line.
enum class KeyCode : std::uint16_t
{
    kUnknown = 0,
    kA,
    kB,
    kC,
    kD,
    kE,
    kF,
    kG,
    kH,
    kI,
    kJ,
    kK,
    kL,
    kM,
    kN,
    kO,
    kP,
    kQ,
    kR,
    kS,
    kT,
    kU,
    kV,
    kW,
    kX,
    kY,
    kZ,
    // phase993-misc-confusable-suppress: NOLINT on digit keys
    // (k0 vs kO 'oh', k1 vs kI 'eye'). Canonical keyboard convention
    // matches Win32 VK_0, GLFW_KEY_0, SDLK_0; renaming would diverge.
    k0,  // NOLINT(misc-confusable-identifiers)  -- vs kO
    k1,  // NOLINT(misc-confusable-identifiers)  -- vs kI
    k2,
    k3,
    k4,
    k5,
    k6,
    k7,
    k8,
    k9,
    kSpace,
    kEnter,
    kEscape,
    kTab,
    kBackspace,
    kLShift,
    kRShift,
    kLCtrl,
    kRCtrl,
    kLAlt,
    kRAlt,
    kLeft,
    kRight,
    kUp,
    kDown,
    kF1,
    kF2,
    kF3,
    kF4,
    kF5,
    kF6,
    kF7,
    kF8,
    kF9,
    kF10,
    kF11,
    kF12,
    kCount,  ///< Sentinel — keep last.
};

enum class MouseButton : std::uint8_t
{
    kLeft = 0,
    kRight,
    kMiddle,
    kX1,
    kX2,
    kCount,
};

enum class EventKind : std::uint8_t
{
    kKeyDown,
    kKeyUp,
    kMouseMove,
    kMouseButtonDown,
    kMouseButtonUp,
    kMouseWheel,
};

struct InputEvent
{
    EventKind kind { EventKind::kKeyDown };
    /// Key for kKeyDown/kKeyUp, otherwise unused.
    KeyCode key { KeyCode::kUnknown };
    /// Mouse button for kMouseButton*.
    MouseButton mouse_button { MouseButton::kLeft };
    /// Mouse position (absolute, in window-pixel coordinates).
    float mouse_x { 0.0F };
    float mouse_y { 0.0F };
    /// Wheel delta (positive = up / forward).
    float wheel { 0.0F };
};

/// Single-frame polled state.
class InputState
{
public:
    [[nodiscard]] bool is_key_down(KeyCode k) const noexcept
    {
        const auto i = static_cast<std::size_t>(k);
        return i < keys_.size() && keys_[i];
    }

    [[nodiscard]] bool is_mouse_button_down(MouseButton b) const noexcept
    {
        const auto i = static_cast<std::size_t>(b);
        return i < mouse_.size() && mouse_[i];
    }

    [[nodiscard]] float mouse_x() const noexcept
    {
        return mouse_x_;
    }

    [[nodiscard]] float mouse_y() const noexcept
    {
        return mouse_y_;
    }

    [[nodiscard]] float wheel_accumulator() const noexcept
    {
        return wheel_;
    }

    // ---- Mutators (called by InputContext) ----------------------------

    void set_key(KeyCode k, bool down) noexcept
    {
        const auto i = static_cast<std::size_t>(k);
        if (i < keys_.size())
            keys_[i] = down;
    }

    void set_mouse_button(MouseButton b, bool down) noexcept
    {
        const auto i = static_cast<std::size_t>(b);
        if (i < mouse_.size())
            mouse_[i] = down;
    }

    void set_mouse_position(float x, float y) noexcept
    {
        mouse_x_ = x;
        mouse_y_ = y;
    }

    void add_wheel(float delta) noexcept
    {
        wheel_ += delta;
    }

    void clear_wheel() noexcept
    {
        wheel_ = 0.0F;
    }

private:
    std::array<bool, static_cast<std::size_t>(KeyCode::kCount)> keys_ {};
    std::array<bool, static_cast<std::size_t>(MouseButton::kCount)> mouse_ {};
    float mouse_x_ { 0.0F };
    float mouse_y_ { 0.0F };
    float wheel_ { 0.0F };
};

/// Bridges the platform layer (events in) and game code (state +
/// events out). Owns the state and an event queue drained per frame.
class InputContext
{
public:
    InputContext() noexcept = default;

    /// Platform code calls this for every OS event. State is updated in
    /// the same call so polled reads after `push_event` are coherent.
    void push_event(const InputEvent& e);

    /// Game code calls this at the end of each frame to consume events.
    /// Returns the event vector and clears the internal queue.
    [[nodiscard]] std::deque<InputEvent> drain_events() noexcept
    {
        return std::exchange(events_, {});
    }

    /// Snapshot read accessor.
    [[nodiscard]] const InputState& state() const noexcept
    {
        return state_;
    }

    /// Mutable accessor (used by tests; platform code prefers push_event).
    [[nodiscard]] InputState& mutable_state() noexcept
    {
        return state_;
    }

    /// Reset wheel accumulator at the start of each frame.
    void begin_frame() noexcept
    {
        state_.clear_wheel();
    }

    [[nodiscard]] std::size_t queued_event_count() const noexcept
    {
        return events_.size();
    }

private:
    InputState state_ {};
    std::deque<InputEvent> events_ {};
};

}  // namespace cd::input
