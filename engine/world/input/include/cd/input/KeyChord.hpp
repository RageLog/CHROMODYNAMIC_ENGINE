// =============================================================================
// CHROMODYNAMIC — cd/input/KeyChord.hpp
// Phase 44.A / Wave 212 — keyboard chord match (Ctrl+Shift+S style).
//
// A KeyChord pairs a non-modifier "trigger" key with a bitmask of
// modifier keys (Ctrl / Shift / Alt) that must be held when the trigger
// is pressed. The chord registry consumes raw InputEvents and fires
// caller-supplied actions when the chord matches.
//
//   Chord c { KeyCode::kS, Mod::kCtrl | Mod::kShift };
//   if (matches(c, evt, state)) save_as();
//
// L/R variants of each modifier are merged: holding either kLCtrl or
// kRCtrl satisfies `Mod::kCtrl`. The chord requires the trigger event
// to be a kKeyDown (not a release) — matches editor expectations
// (Ctrl+S saves on press, not on release).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/input/Input.hpp>

#include <cstdint>

namespace cd::input
{

enum class Mod : std::uint8_t
{
    kNone  = 0,
    kCtrl  = 1u << 0,
    kShift = 1u << 1,
    kAlt   = 1u << 2,
};

[[nodiscard]] constexpr Mod operator|(Mod a, Mod b) noexcept
{
    return static_cast<Mod>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr Mod operator&(Mod a, Mod b) noexcept
{
    return static_cast<Mod>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

struct KeyChord
{
    KeyCode trigger { KeyCode::kUnknown };
    Mod     mods    { Mod::kNone };
};

[[nodiscard]] inline Mod current_mods(const InputState& s) noexcept
{
    auto m = Mod::kNone;
    if (s.is_key_down(KeyCode::kLCtrl)  || s.is_key_down(KeyCode::kRCtrl))  m = m | Mod::kCtrl;
    if (s.is_key_down(KeyCode::kLShift) || s.is_key_down(KeyCode::kRShift)) m = m | Mod::kShift;
    if (s.is_key_down(KeyCode::kLAlt)   || s.is_key_down(KeyCode::kRAlt))   m = m | Mod::kAlt;
    return m;
}

/// Returns true iff `evt` is a kKeyDown for chord.trigger AND the
/// current modifier mask matches chord.mods exactly (no extra mods
/// allowed — Ctrl+S does NOT fire when the user presses Ctrl+Shift+S).
[[nodiscard]] inline bool matches(const KeyChord& chord,
                                  const InputEvent& evt,
                                  const InputState& state) noexcept
{
    if (evt.kind != EventKind::kKeyDown) return false;
    if (evt.key != chord.trigger) return false;
    return current_mods(state) == chord.mods;
}

}  // namespace cd::input
