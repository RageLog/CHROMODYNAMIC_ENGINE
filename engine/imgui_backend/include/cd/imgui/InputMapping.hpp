// =============================================================================
// CHROMODYNAMIC — cd/imgui/InputMapping.hpp
//
// Pure, header-only translation functions between the engine's platform
// event types (cd::platform::KeyCode, cd::platform::MouseButton) and the
// corresponding Dear ImGui constants (ImGuiKey, ImGuiMouseButton).
//
// These functions carry no state and depend only on <cd/platform/Window.hpp>
// and <imgui.h>. They are exposed as public API so that:
//   (a) tests can verify every branch without a live Vulkan device, and
//   (b) future backends (D3D12, Metal, SDL) can share the same mapping.
//
// Context.cpp delegates to these functions; application code may call them
// directly when building a custom event-routing layer.
// =============================================================================
#pragma once

#include <cd/platform/Window.hpp>
#include <imgui.h>

namespace cd::imgui
{

/// Translate a cd::platform::KeyCode to the corresponding ImGuiKey constant.
/// Returns ImGuiKey_None for keys that have no ImGui equivalent (e.g. the
/// kCount sentinel or any future key added to cd::platform before this table
/// is extended).
[[nodiscard]] inline ImGuiKey to_imgui_key(cd::platform::KeyCode k) noexcept
{
    using K = cd::platform::KeyCode;
    switch (k)
    {
        // Letters
        case K::kA: return ImGuiKey_A;
        case K::kB: return ImGuiKey_B;
        case K::kC: return ImGuiKey_C;
        case K::kD: return ImGuiKey_D;
        case K::kE: return ImGuiKey_E;
        case K::kF: return ImGuiKey_F;
        case K::kG: return ImGuiKey_G;
        case K::kH: return ImGuiKey_H;
        case K::kI: return ImGuiKey_I;
        case K::kJ: return ImGuiKey_J;
        case K::kK: return ImGuiKey_K;
        case K::kL: return ImGuiKey_L;
        case K::kM: return ImGuiKey_M;
        case K::kN: return ImGuiKey_N;
        case K::kO: return ImGuiKey_O;
        case K::kP: return ImGuiKey_P;
        case K::kQ: return ImGuiKey_Q;
        case K::kR: return ImGuiKey_R;
        case K::kS: return ImGuiKey_S;
        case K::kT: return ImGuiKey_T;
        case K::kU: return ImGuiKey_U;
        case K::kV: return ImGuiKey_V;
        case K::kW: return ImGuiKey_W;
        case K::kX: return ImGuiKey_X;
        case K::kY: return ImGuiKey_Y;
        case K::kZ: return ImGuiKey_Z;
        // Digits
        case K::k0: return ImGuiKey_0;
        case K::k1: return ImGuiKey_1;
        case K::k2: return ImGuiKey_2;
        case K::k3: return ImGuiKey_3;
        case K::k4: return ImGuiKey_4;
        case K::k5: return ImGuiKey_5;
        case K::k6: return ImGuiKey_6;
        case K::k7: return ImGuiKey_7;
        case K::k8: return ImGuiKey_8;
        case K::k9: return ImGuiKey_9;
        // Editing
        case K::kSpace:     return ImGuiKey_Space;
        case K::kEnter:     return ImGuiKey_Enter;
        case K::kEscape:    return ImGuiKey_Escape;
        case K::kTab:       return ImGuiKey_Tab;
        case K::kBackspace: return ImGuiKey_Backspace;
        case K::kDelete:    return ImGuiKey_Delete;
        // Modifiers
        case K::kLShift: return ImGuiKey_LeftShift;
        case K::kRShift: return ImGuiKey_RightShift;
        case K::kLCtrl:  return ImGuiKey_LeftCtrl;
        case K::kRCtrl:  return ImGuiKey_RightCtrl;
        case K::kLAlt:   return ImGuiKey_LeftAlt;
        case K::kRAlt:   return ImGuiKey_RightAlt;
        // Arrow keys
        case K::kLeft:  return ImGuiKey_LeftArrow;
        case K::kRight: return ImGuiKey_RightArrow;
        case K::kUp:    return ImGuiKey_UpArrow;
        case K::kDown:  return ImGuiKey_DownArrow;
        // Function keys
        case K::kF1:  return ImGuiKey_F1;
        case K::kF2:  return ImGuiKey_F2;
        case K::kF3:  return ImGuiKey_F3;
        case K::kF4:  return ImGuiKey_F4;
        case K::kF5:  return ImGuiKey_F5;
        case K::kF6:  return ImGuiKey_F6;
        case K::kF7:  return ImGuiKey_F7;
        case K::kF8:  return ImGuiKey_F8;
        case K::kF9:  return ImGuiKey_F9;
        case K::kF10: return ImGuiKey_F10;
        case K::kF11: return ImGuiKey_F11;
        case K::kF12: return ImGuiKey_F12;
        // Unmapped: kUnknown, kCount
        default:
            return ImGuiKey_None;
    }
}

/// Translate a cd::platform::MouseButton to the corresponding ImGuiMouseButton
/// index used by ImGuiIO::AddMouseButtonEvent.
///
/// ImGui defines 5 button slots (ImGuiMouseButton_COUNT = 5):
///   0 = Left, 1 = Right, 2 = Middle, 3 = X1, 4 = X2.
/// kX1/kX2 are the thumb / side buttons common on gaming mice. Returning 0
/// (Left) for them would misroute their events onto the left-button state —
/// this function maps them to their correct slots instead.
/// kCount is a sentinel; it returns 0 as a safe no-op (the platform layer
/// must never emit kCount as an actual button press).
[[nodiscard]] inline int to_imgui_mouse_button(cd::platform::MouseButton b) noexcept
{
    using M = cd::platform::MouseButton;
    switch (b)
    {
        case M::kLeft:   return 0;
        case M::kRight:  return 1;
        case M::kMiddle: return 2;
        case M::kX1:     return 3;
        case M::kX2:     return 4;
        default:         return 0;
    }
}

}  // namespace cd::imgui
