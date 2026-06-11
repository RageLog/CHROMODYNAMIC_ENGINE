// =============================================================================
// CHROMODYNAMIC — cd/editor/AxisGizmo.hpp
// Phase 116 / Wave 284 — translation gizmo state machine.
// Phase 1071 (gizmo v2) — translate/rotate/scale modes, XY/XZ/YZ plane
// pads, and the ray-plane pick helpers (PickRay / intersect_ray_plane /
// axis_drag_plane_normal / pick_ray_from_ndc) shared by every consumer.
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
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <optional>

namespace cd::editor
{

enum class GizmoAxis : std::uint8_t
{
    kNone = 0,
    kX    = 1,
    kY    = 2,
    kZ    = 3,
    // Plane pads (translate mode): drag constrained to a principal plane.
    kXY   = 4,
    kXZ   = 5,
    kYZ   = 6,
};

/// True for the XY/XZ/YZ plane-pad values.
[[nodiscard]] constexpr bool is_plane(GizmoAxis a) noexcept
{
    return a == GizmoAxis::kXY || a == GizmoAxis::kXZ || a == GizmoAxis::kYZ;
}

enum class GizmoMode : std::uint8_t
{
    kTranslate = 0,
    kRotate    = 1,
    kScale     = 2,
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
            // Plane pads pass BOTH in-plane components through.
            case GizmoAxis::kXY: target_.x += delta.x; target_.y += delta.y; break;
            case GizmoAxis::kXZ: target_.x += delta.x; target_.z += delta.z; break;
            case GizmoAxis::kYZ: target_.y += delta.y; target_.z += delta.z; break;
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

    // ---- Mode (v2) -----------------------------------------------------------
    /// Switch translate/rotate/scale. Ignored mid-drag so a hotkey press
    /// cannot corrupt an in-flight drag.
    void set_mode(GizmoMode m) noexcept
    {
        if (!dragging_) mode_ = m;
    }

    [[nodiscard]] GizmoMode mode() const noexcept { return mode_; }

    // ---- Scalar value drags (v2: rotate angle / scale offset) ---------------
    /// Begin a scalar drag around/along `axis` (rotate: radians, scale:
    /// additive factor offset). The caller owns the screen-or-ray metric
    /// that produces the scalar; this class only holds the session state.
    void begin_value_drag(GizmoAxis axis) noexcept
    {
        if (axis == GizmoAxis::kNone) return;
        active_     = axis;
        drag_value_ = 0.0F;
        dragging_   = true;
    }

    /// Overwrite the running total for the active scalar drag.
    void update_value_drag(float total_value) noexcept
    {
        if (dragging_) drag_value_ = total_value;
    }

    /// End the scalar drag; returns the final total (0 when idle).
    float end_value_drag() noexcept
    {
        const float v = drag_value_;
        drag_value_ = 0.0F;
        dragging_   = false;
        active_     = GizmoAxis::kNone;
        return v;
    }

    [[nodiscard]] float drag_value() const noexcept { return drag_value_; }

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
    GizmoMode       mode_                { GizmoMode::kTranslate };
    float           drag_value_          { 0.0F };
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

/// Normal of a plane-pad axis. Returns the zero vector for non-planes.
[[nodiscard]] inline cd::math::Vec3f plane_normal(GizmoAxis a) noexcept
{
    switch (a)
    {
        case GizmoAxis::kXY: return { 0.0F, 0.0F, 1.0F };
        case GizmoAxis::kXZ: return { 0.0F, 1.0F, 0.0F };
        case GizmoAxis::kYZ: return { 1.0F, 0.0F, 0.0F };
        default:             return { 0.0F, 0.0F, 0.0F };
    }
}

// ---------------------------------------------------------------------------
// Ray-plane pick kit (v2). World-space drag metrics beat the v1
// screen-delta metric because precision no longer collapses when the
// drag axis runs near-parallel to the view direction.
// ---------------------------------------------------------------------------

struct PickRay
{
    cd::math::Vec3f origin {};
    cd::math::Vec3f dir    {};  ///< unit length
};

/// Ray vs infinite plane. Returns the hit point, or nullopt when the ray
/// is parallel to the plane (within epsilon) or the hit is behind the
/// ray origin.
[[nodiscard]] inline std::optional<cd::math::Vec3f>
intersect_ray_plane(const PickRay&         ray,
                    const cd::math::Vec3f& plane_point,
                    const cd::math::Vec3f& plane_normal) noexcept
{
    const float denom = cd::math::dot(ray.dir, plane_normal);
    if (std::fabs(denom) < 1e-6F) return std::nullopt;
    const cd::math::Vec3f to_plane {
        plane_point.x - ray.origin.x,
        plane_point.y - ray.origin.y,
        plane_point.z - ray.origin.z };
    const float t = cd::math::dot(to_plane, plane_normal) / denom;
    if (t < 0.0F) return std::nullopt;
    return cd::math::Vec3f { ray.origin.x + ray.dir.x * t,
                             ray.origin.y + ray.dir.y * t,
                             ray.origin.z + ray.dir.z * t };
}

/// Best drag plane for an AXIS drag: the plane that CONTAINS the axis
/// and faces the camera as much as possible — n = a × (view × a), i.e.
/// the view direction with its along-axis component removed. Falls back
/// to any perpendicular when the axis is parallel to the view.
[[nodiscard]] inline cd::math::Vec3f
axis_drag_plane_normal(GizmoAxis axis, const cd::math::Vec3f& view_dir) noexcept
{
    const cd::math::Vec3f a = axis_dir(axis);
    const float along = cd::math::dot(view_dir, a);
    cd::math::Vec3f n { view_dir.x - a.x * along,
                        view_dir.y - a.y * along,
                        view_dir.z - a.z * along };
    const float len2 = cd::math::dot(n, n);
    if (len2 < 1e-10F)
    {
        // Axis is parallel to the view: any plane containing the axis
        // works equally badly/well; pick a fixed perpendicular.
        return (axis == GizmoAxis::kX) ? cd::math::Vec3f { 0.0F, 1.0F, 0.0F }
                                       : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
    }
    const float inv = 1.0F / std::sqrt(len2);
    return { n.x * inv, n.y * inv, n.z * inv };
}

/// Unproject an NDC point through inverse(view_proj) into a world-space
/// pick ray (Vulkan depth convention: near plane at NDC z = 0, far at 1).
/// Returns nullopt on degenerate w (broken matrix).
[[nodiscard]] inline std::optional<PickRay>
pick_ray_from_ndc(const cd::math::Mat4f& inv_view_proj,
                  float ndc_x, float ndc_y) noexcept
{
    const auto unproject =
        [&](float z) -> std::optional<cd::math::Vec3f>
    {
        const cd::math::Vec4f h =
            inv_view_proj * cd::math::Vec4f { ndc_x, ndc_y, z, 1.0F };
        if (std::fabs(h.w) < 1e-7F) return std::nullopt;
        const float inv_w = 1.0F / h.w;
        return cd::math::Vec3f { h.x * inv_w, h.y * inv_w, h.z * inv_w };
    };
    const auto near_p = unproject(0.0F);
    const auto far_p  = unproject(1.0F);
    if (!near_p.has_value() || !far_p.has_value()) return std::nullopt;
    cd::math::Vec3f d { far_p->x - near_p->x,
                        far_p->y - near_p->y,
                        far_p->z - near_p->z };
    const float len2 = cd::math::dot(d, d);
    if (len2 < 1e-12F) return std::nullopt;
    const float inv = 1.0F / std::sqrt(len2);
    return PickRay { *near_p, { d.x * inv, d.y * inv, d.z * inv } };
}

}  // namespace cd::editor
