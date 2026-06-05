// =============================================================================
// CHROMODYNAMIC — cd/platform/android/AndroidInputBridge.hpp
// Phase 772 — Touch / key event translation: AInputEvent -> cd::ui::input.
//
// Translates raw Android NDK input events (touch, key) into the
// cd::ui::input wire types (MouseEvent, KeyEvent) that the cd::ui widget tree
// consumes.  The bridge is a pure stateless helper — it owns no AInputQueue
// itself; callers pump the queue and call translate_*() per event.
//
// Rationale for a separate bridge header (not folded into AndroidWindow):
//   * Dependency DAG: cd::platform must NOT depend on cd::ui.  This header
//     depends on both <android/input.h> and <cd/ui/input/Input.hpp>; it
//     therefore lives in the platform/android sub-namespace but is gated by
//     __ANDROID__ so it never pollutes non-Android builds.
//   * Testability: the translation tables are exposed as constexpr helpers so
//     a host-side unit test can instantiate them without an Android device.
//
// Integration (from android_main / the game loop):
//
//   AInputEvent* ev = nullptr;
//   while (AInputQueue_getEvent(queue, &ev) >= 0)
//   {
//       if (AInputQueue_preDispatchEvent(queue, ev) != 0) continue;
//       int32_t type = AInputEvent_getType(ev);
//       if (type == AINPUT_EVENT_TYPE_MOTION)
//       {
//           auto me = cd::platform::android::AndroidInputBridge::translate_motion(ev);
//           ui_context.dispatch(me);
//       }
//       else if (type == AINPUT_EVENT_TYPE_KEY)
//       {
//           auto ke = cd::platform::android::AndroidInputBridge::translate_key(ev);
//           ui_context.dispatch(ke);
//       }
//       AInputQueue_finishEvent(queue, ev, 1);
//   }
//
// Touch model:
//   Android MOTION_UP/DOWN/MOVE events from pointer-0 map to MouseEvent with
//   action kRelease / kPress / kMove respectively.  Multi-touch (pointer-1+)
//   is not forwarded to the UI layer in Sprint-1 — the widget tree is
//   designed for single-pointer focus.
//
// Key model:
//   AKEY_EVENT_ACTION_DOWN -> KeyAction::kPress
//   AKEY_EVENT_ACTION_UP   -> KeyAction::kRelease
//   AKEY_EVENT_ACTION_MULTIPLE -> KeyAction::kRepeat
//   Modifier bits (shift/ctrl/alt/meta) map to cd::ui::input::key_mods.
//   The raw AKEYCODE_* value is forwarded as keycode; callers that need
//   semantic mapping can use the provided akeycode_to_platform_keycode()
//   helper which returns the platform-neutral cd::platform::KeyCode value.
// =============================================================================
#pragma once

#if defined(__ANDROID__)

#include <cd/ui/input/Input.hpp>
#include <cd/platform/Window.hpp>  // cd::platform::KeyCode

#include <android/input.h>
#include <cstdint>

namespace cd::platform::android
{

/// Stateless bridge: AInputEvent* -> cd::ui::input wire types.
struct AndroidInputBridge
{
    // ---- Motion (touch) translation -----------------------------------------

    /// Translate a AINPUT_EVENT_TYPE_MOTION event into a cd::ui::input::MouseEvent.
    /// Only pointer-0 is translated (Sprint-1 single-pointer model).
    /// Coordinates are framebuffer pixels (AMotionEvent_getX/Y, pointer index 0).
    [[nodiscard]] static cd::ui::input::MouseEvent translate_motion(const AInputEvent* ev) noexcept
    {
        cd::ui::input::MouseEvent me {};
        me.x      = AMotionEvent_getX(ev, 0);
        me.y      = AMotionEvent_getY(ev, 0);
        me.button = cd::ui::input::MouseButton::kLeft;

        const int32_t action_masked = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
        switch (action_masked)
        {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            me.action = cd::ui::input::MouseAction::kPress;
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            me.action = cd::ui::input::MouseAction::kRelease;
            break;
        case AMOTION_EVENT_ACTION_MOVE:
        default:
            me.action = cd::ui::input::MouseAction::kMove;
            break;
        }
        return me;
    }

    // ---- Key translation ----------------------------------------------------

    /// Translate a AINPUT_EVENT_TYPE_KEY event into a cd::ui::input::KeyEvent.
    /// keycode carries the raw AKEYCODE_* value; action and modifiers are
    /// mapped to cd::ui::input enumerators.
    [[nodiscard]] static cd::ui::input::KeyEvent translate_key(const AInputEvent* ev) noexcept
    {
        cd::ui::input::KeyEvent ke {};
        ke.keycode = static_cast<std::uint32_t>(AKeyEvent_getKeyCode(ev));

        switch (AKeyEvent_getAction(ev))
        {
        case AKEY_EVENT_ACTION_DOWN:
            ke.action = cd::ui::input::KeyAction::kPress;
            break;
        case AKEY_EVENT_ACTION_UP:
            ke.action = cd::ui::input::KeyAction::kRelease;
            break;
        case AKEY_EVENT_ACTION_MULTIPLE:
        default:
            ke.action = cd::ui::input::KeyAction::kRepeat;
            break;
        }

        const int32_t meta = AKeyEvent_getMetaState(ev);
        std::uint16_t mods = 0;
        if ((meta & AMETA_SHIFT_ON) != 0) mods |= cd::ui::input::key_mods::kShift;
        if ((meta & AMETA_CTRL_ON)  != 0) mods |= cd::ui::input::key_mods::kControl;
        if ((meta & AMETA_ALT_ON)   != 0) mods |= cd::ui::input::key_mods::kAlt;
        if ((meta & AMETA_META_ON)  != 0) mods |= cd::ui::input::key_mods::kSuper;
        ke.modifiers = mods;

        return ke;
    }

    // ---- AKEYCODE -> cd::platform::KeyCode ----------------------------------

    /// Map a raw AKEYCODE_* value to the platform-neutral cd::platform::KeyCode.
    /// Returns KeyCode::kUnknown for unmapped codes.
    [[nodiscard]] static cd::platform::KeyCode akeycode_to_platform_keycode(int32_t akeycode) noexcept
    {
        switch (akeycode)
        {
        case AKEYCODE_ESCAPE:      return cd::platform::KeyCode::kEscape;
        case AKEYCODE_ENTER:       return cd::platform::KeyCode::kEnter;
        case AKEYCODE_SPACE:       return cd::platform::KeyCode::kSpace;
        case AKEYCODE_TAB:         return cd::platform::KeyCode::kTab;
        case AKEYCODE_DEL:         return cd::platform::KeyCode::kBackspace;
        case AKEYCODE_FORWARD_DEL: return cd::platform::KeyCode::kDelete;
        case AKEYCODE_SHIFT_LEFT:  return cd::platform::KeyCode::kLShift;
        case AKEYCODE_SHIFT_RIGHT: return cd::platform::KeyCode::kRShift;
        case AKEYCODE_CTRL_LEFT:   return cd::platform::KeyCode::kLCtrl;
        case AKEYCODE_CTRL_RIGHT:  return cd::platform::KeyCode::kRCtrl;
        case AKEYCODE_ALT_LEFT:    return cd::platform::KeyCode::kLAlt;
        case AKEYCODE_ALT_RIGHT:   return cd::platform::KeyCode::kRAlt;
        case AKEYCODE_DPAD_LEFT:   return cd::platform::KeyCode::kLeft;
        case AKEYCODE_DPAD_RIGHT:  return cd::platform::KeyCode::kRight;
        case AKEYCODE_DPAD_UP:     return cd::platform::KeyCode::kUp;
        case AKEYCODE_DPAD_DOWN:   return cd::platform::KeyCode::kDown;
        case AKEYCODE_A:           return cd::platform::KeyCode::kA;
        case AKEYCODE_B:           return cd::platform::KeyCode::kB;
        case AKEYCODE_C:           return cd::platform::KeyCode::kC;
        case AKEYCODE_D:           return cd::platform::KeyCode::kD;
        case AKEYCODE_E:           return cd::platform::KeyCode::kE;
        case AKEYCODE_F:           return cd::platform::KeyCode::kF;
        case AKEYCODE_G:           return cd::platform::KeyCode::kG;
        case AKEYCODE_H:           return cd::platform::KeyCode::kH;
        case AKEYCODE_I:           return cd::platform::KeyCode::kI;
        case AKEYCODE_J:           return cd::platform::KeyCode::kJ;
        case AKEYCODE_K:           return cd::platform::KeyCode::kK;
        case AKEYCODE_L:           return cd::platform::KeyCode::kL;
        case AKEYCODE_M:           return cd::platform::KeyCode::kM;
        case AKEYCODE_N:           return cd::platform::KeyCode::kN;
        case AKEYCODE_O:           return cd::platform::KeyCode::kO;
        case AKEYCODE_P:           return cd::platform::KeyCode::kP;
        case AKEYCODE_Q:           return cd::platform::KeyCode::kQ;
        case AKEYCODE_R:           return cd::platform::KeyCode::kR;
        case AKEYCODE_S:           return cd::platform::KeyCode::kS;
        case AKEYCODE_T:           return cd::platform::KeyCode::kT;
        case AKEYCODE_U:           return cd::platform::KeyCode::kU;
        case AKEYCODE_V:           return cd::platform::KeyCode::kV;
        case AKEYCODE_W:           return cd::platform::KeyCode::kW;
        case AKEYCODE_X:           return cd::platform::KeyCode::kX;
        case AKEYCODE_Y:           return cd::platform::KeyCode::kY;
        case AKEYCODE_Z:           return cd::platform::KeyCode::kZ;
        case AKEYCODE_0:           return cd::platform::KeyCode::k0;
        case AKEYCODE_1:           return cd::platform::KeyCode::k1;
        case AKEYCODE_2:           return cd::platform::KeyCode::k2;
        case AKEYCODE_3:           return cd::platform::KeyCode::k3;
        case AKEYCODE_4:           return cd::platform::KeyCode::k4;
        case AKEYCODE_5:           return cd::platform::KeyCode::k5;
        case AKEYCODE_6:           return cd::platform::KeyCode::k6;
        case AKEYCODE_7:           return cd::platform::KeyCode::k7;
        case AKEYCODE_8:           return cd::platform::KeyCode::k8;
        case AKEYCODE_9:           return cd::platform::KeyCode::k9;
        case AKEYCODE_F1:          return cd::platform::KeyCode::kF1;
        case AKEYCODE_F2:          return cd::platform::KeyCode::kF2;
        case AKEYCODE_F3:          return cd::platform::KeyCode::kF3;
        case AKEYCODE_F4:          return cd::platform::KeyCode::kF4;
        case AKEYCODE_F5:          return cd::platform::KeyCode::kF5;
        case AKEYCODE_F6:          return cd::platform::KeyCode::kF6;
        case AKEYCODE_F7:          return cd::platform::KeyCode::kF7;
        case AKEYCODE_F8:          return cd::platform::KeyCode::kF8;
        case AKEYCODE_F9:          return cd::platform::KeyCode::kF9;
        case AKEYCODE_F10:         return cd::platform::KeyCode::kF10;
        case AKEYCODE_F11:         return cd::platform::KeyCode::kF11;
        case AKEYCODE_F12:         return cd::platform::KeyCode::kF12;
        default:                   return cd::platform::KeyCode::kUnknown;
        }
    }
};

}  // namespace cd::platform::android

#endif  // __ANDROID__
