// =============================================================================
// CHROMODYNAMIC — cd/editor/PropertyDrawer.hpp
// Phase 107 / Wave 273 — live-drag inspector helper.
//
// Captures the "drag a value live, push a single delta to EditHistory
// on release" pattern that hello_editor's Inspector evolved into.
// Without this, every inspector panel in the engine has to reproduce
// the IsItemActivated / IsItemDeactivatedAfterEdit dance + pre-drag
// snapshot + command construction.
//
// API shape:
//   DragSession<T> session;     // owns the pre-drag snapshot
//   T value = scene.value();
//   if (drag_property(label, value, step, min, max))
//       scene.set_value(value);
//   if (session.on_release(current_value, &delta))
//       history.push(MyCommand{ delta });
//
// This header is ImGui-aware (depends on dear imgui being available
// in the consuming TU) but does NOT include imgui.h itself — callers
// must include it before this header. That way cd::editor doesn't
// drag imgui.h into every translation unit that uses EditHistory.
//
// Usage (matches hello_editor Phase 103 pattern):
//   static cd::editor::DragSessionVec3 pos_session;
//   float xyz[3] { lt.position.x, lt.position.y, lt.position.z };
//   if (ImGui::DragFloat3("##pos", xyz, 0.05F, -10.F, 10.F, "%.3f"))
//       lt.position = { xyz[0], xyz[1], xyz[2] };
//   pos_session.observe(lt.position);
//   cd::math::Vec3f delta {};
//   if (pos_session.consume_release(&delta) && delta != Vec3f{})
//   {
//       lt.position -= delta;  // rewind so the command lands cleanly
//       history.push(make_unique<TranslateCommand>(scene, e, delta));
//   }
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

namespace cd::editor
{

/// Live-drag bookkeeping for a single scalar / vector property.
/// `observe()` should be called every frame the property is rendered
/// (whether the user is dragging or not). It looks at ImGui's last-
/// item state to:
///   1. Snapshot the pre-drag value on first frame of activation.
///   2. Mark a "release pending" flag when the user lets go.
///
/// `consume_release(out_delta)` reads that flag, returns true once,
/// and writes the delta = current - pre_drag.
///
/// Caller is responsible for actually invoking ImGui::DragFloat3 /
/// SliderFloat / whatever widget — this helper sits next to it.
template <class T>
struct DragSession
{
    T    pre_drag {};
    bool armed { false };       ///< pre-drag captured, drag in flight
    bool release_pending { false };  ///< release seen, not yet consumed

    /// Snapshot pre-drag if the LAST ImGui item just became active.
    /// `IsItemActivated_fn` / `IsItemDeactivatedAfterEdit_fn` are
    /// callable pointers (function pointers or lambdas) so this header
    /// doesn't need to #include imgui.h. In practice the caller
    /// passes `&ImGui::IsItemActivated, &ImGui::IsItemDeactivatedAfterEdit`.
    template <class FnActivated, class FnReleased>
    void observe(const T& current, FnActivated is_activated, FnReleased is_released_after_edit)
    {
        if (is_activated())
        {
            pre_drag = current;
            armed = true;
        }
        if (armed && is_released_after_edit())
        {
            release_pending = true;
            armed = false;
            // pre_drag is preserved for the upcoming consume_release()
            // call. (Cleared only when the caller takes the delta.)
        }
    }

    /// If a release is pending, return true and copy delta. Otherwise
    /// return false. Either way clears any pending flag.
    [[nodiscard]] bool consume_release(T* out_delta_or_target = nullptr)
    {
        if (!release_pending) return false;
        if (out_delta_or_target != nullptr) *out_delta_or_target = pre_drag;
        release_pending = false;
        return true;
    }

    /// Cancel an in-flight drag (e.g. selection changed mid-edit).
    void reset() noexcept
    {
        pre_drag = T {};
        armed = false;
        release_pending = false;
    }

    [[nodiscard]] bool is_active() const noexcept { return armed; }
};

using DragSessionVec3  = DragSession<cd::math::Vec3f>;
using DragSessionFloat = DragSession<float>;

/// Convenience helper: compute the delta between two Vec3f and test
/// whether any component changed by more than epsilon. Use to skip
/// no-op history pushes when the user clicked but didn't actually
/// drag.
[[nodiscard]] inline bool drag_delta_significant(const cd::math::Vec3f& a,
                                                 const cd::math::Vec3f& b,
                                                 float epsilon = 1e-6F) noexcept
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    const float adx = (dx < 0.0F) ? -dx : dx;
    const float ady = (dy < 0.0F) ? -dy : dy;
    const float adz = (dz < 0.0F) ? -dz : dz;
    return (adx > epsilon) || (ady > epsilon) || (adz > epsilon);
}

}  // namespace cd::editor
