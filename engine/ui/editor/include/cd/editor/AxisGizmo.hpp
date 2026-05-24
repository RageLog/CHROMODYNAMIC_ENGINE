// =============================================================================
// CHROMODYNAMIC — cd/editor/AxisGizmo.hpp
// Phase 116 / Wave 284 — translation gizmo state machine.
//
// Tracks the "is the mouse over the +X / +Y / +Z arrow, am I currently
// dragging one of them, what was the target's transform when the drag
// started" state for an editor-style axis-translation gizmo.
//
// Rendering geometry and ray-from-mouse projection are the caller's
// job — this header owns ONLY the state machine. Typical wiring:
//
//   gizmo.set_target(scene.world_translation_of(selected));
//   const auto axis = pick_axis_via_ray(mouse_ray, gizmo);
//   gizmo.set_hover(axis);
//   if (mouse_down && axis != kNone)  gizmo.begin_drag(axis, mouse_world_pos);
//   if (dragging)                     gizmo.update_drag(mouse_world_pos);
//   if (mouse_up)                     gizmo.end_drag();
//
// Drag math: the delta along the active axis is projected onto the
// axis unit vector and added to `target`. The caller writes the
// resulting position back to the scene + pushes a TranslateCommand to
// EditHistory on `end_drag()`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>

namespace cd::editor
{

enum class GizmoAxis : std::uint8_t
{
    kNone = 0,
    kX    = 1,
    kY    = 2,
    kZ    = 3,
};

class AxisGizmo
{
public:
    AxisGizmo() noexcept = default;

    // ---- Target ------------------------------------------------------------
    void set_target(const cd::math::Vec3f& world_pos) noexcept
    {
        if (!dragging_) target_ = world_pos;
    }

    [[nodiscard]] const cd::math::Vec3f& target() const noexcept { return target_; }

    // ---- Hover -------------------------------------------------------------
    void set_hover(GizmoAxis axis) noexcept
    {
        if (!dragging_) hover_ = axis;
    }

    [[nodiscard]] GizmoAxis hover() const noexcept { return hover_; }

    // ---- Drag --------------------------------------------------------------
    /// Begin a drag along `axis`. `world_pick_pos` is the world-space point
    /// where the mouse ray first hit the axis line — used as the drag
    /// origin so delta math is robust against the user clicking
    /// anywhere along the arrow.
    void begin_drag(GizmoAxis axis, const cd::math::Vec3f& world_pick_pos) noexcept
    {
        if (axis == GizmoAxis::kNone) return;
        active_      = axis;
        drag_origin_ = world_pick_pos;
        drag_target_origin_ = target_;
        dragging_    = true;
    }

    /// Update the target with a fresh world-space mouse hit on the
    /// active axis line. Only the active-axis component of the delta
    /// is applied; the other two components are ignored.
    void update_drag(const cd::math::Vec3f& world_current_pos) noexcept
    {
        if (!dragging_) return;
        const cd::math::Vec3f delta {
            world_current_pos.x - drag_origin_.x,
            world_current_pos.y - drag_origin_.y,
            world_current_pos.z - drag_origin_.z };
        target_ = drag_target_origin_;
        switch (active_)
        {
            case GizmoAxis::kX: target_.x += delta.x; break;
            case GizmoAxis::kY: target_.y += delta.y; break;
            case GizmoAxis::kZ: target_.z += delta.z; break;
            default: break;
        }
    }

    /// End the drag. Returns the total delta the caller should push as a
    /// TranslateCommand (zero-vector if no drag was active).
    cd::math::Vec3f end_drag() noexcept
    {
        cd::math::Vec3f total {
            target_.x - drag_target_origin_.x,
            target_.y - drag_target_origin_.y,
            target_.z - drag_target_origin_.z };
        dragging_ = false;
        active_   = GizmoAxis::kNone;
        return total;
    }

    [[nodiscard]] bool      is_dragging() const noexcept { return dragging_; }
    [[nodiscard]] GizmoAxis active_axis() const noexcept { return active_; }

    /// Tunable: how far the user has to mouse off-axis (perpendicular
    /// distance to the arrow line, in pixels) before the hover clears.
    /// Caller's ray-vs-cylinder test should consult this.
    float hover_tolerance_pixels { 8.0F };

private:
    cd::math::Vec3f target_              {};                 ///< current gizmo position
    cd::math::Vec3f drag_origin_         {};                 ///< world-space pick point
    cd::math::Vec3f drag_target_origin_  {};                 ///< target snapshot at drag start
    GizmoAxis       hover_               { GizmoAxis::kNone };
    GizmoAxis       active_              { GizmoAxis::kNone };
    bool            dragging_            { false };
};

/// Unit-vector for a gizmo axis. Returns the zero vector for `kNone`.
[[nodiscard]] inline cd::math::Vec3f axis_dir(GizmoAxis a) noexcept
{
    switch (a)
    {
        case GizmoAxis::kX: return { 1.0F, 0.0F, 0.0F };
        case GizmoAxis::kY: return { 0.0F, 1.0F, 0.0F };
        case GizmoAxis::kZ: return { 0.0F, 0.0F, 1.0F };
        default:            return { 0.0F, 0.0F, 0.0F };
    }
}

/// Conventional axis colour (used by both the gizmo geometry shader
/// and any inspector-side hover indicator).
[[nodiscard]] inline cd::math::Vec3f axis_color(GizmoAxis a) noexcept
{
    switch (a)
    {
        case GizmoAxis::kX: return { 0.95F, 0.25F, 0.20F };  // warm red
        case GizmoAxis::kY: return { 0.30F, 0.85F, 0.30F };  // green
        case GizmoAxis::kZ: return { 0.25F, 0.40F, 0.95F };  // cool blue
        default:            return { 0.5F,  0.5F,  0.5F  };
    }
}

}  // namespace cd::editor
