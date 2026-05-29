// =============================================================================
// HelloAppState.hpp
// -----------------------------------------------------------------------------
// hello_engine-local "thin" application-state aggregate. Bundles the free-look
// camera + picker state into a single struct so the per-frame WASD/look/zoom
// handler and the click-to-pick handler can be extracted out of main() without
// 20 separate reference captures.
//
// Design note: this is NOT a god-object "FrameContext". The aggregate is
// deliberately scoped to camera + input + pick state because those locals
// cluster together (input -> camera mutation -> pick request -> camera-aware
// ray cast). Other locals stay in main() until their own extract phase brings
// in a focused aggregate (e.g. HelloEngineFx already aggregates R-Showcase
// state; HelloAudio aggregates audio chain state; future phases may add
// HelloMaterials, HelloPalette).
//
// Lifted out of main() in Marathon Run 12 phase N13.
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>

namespace cd_sample {

// W6-F speed modifier constants — exposed so the FPS-camera helper can read
// them without duplicating the magic floats. Match the pre-N13 inline values.
inline constexpr float kCamMoveSpeed = 6.0F;    // m/s
inline constexpr float kCamLookSpeed = 0.005F;  // rad/pixel

/// Free-look camera + pick state aggregate.
///
/// Drives the right-mouse FPS look, WASD translation, wheel zoom, manual-mode
/// toggle, and 3D click-to-pick request. The owning main() loop reads
/// `pending_pick` to decide whether to walk the entity list this frame.
struct FreeLookState
{
    // ---- Right-mouse FPS look + WASD translation ----
    bool  right_drag      { false };
    float yaw             { 0.0F };       // around +Y
    float pitch           { -0.15F };     // looking slightly down (matches pre-N13 default)
    float dist            { 8.0F };       // distance from target (used as zoom)
    float last_mouse_x    { 0.0F };
    float last_mouse_y    { 0.0F };
    bool  has_last_mouse  { false };

    // WASD + QE + speed modifiers. Tracked as continuous state (set on
    // key-down, cleared on key-up) so the per-frame update can move smoothly
    // regardless of the input event rate.
    bool key_w     { false };
    bool key_a     { false };
    bool key_s     { false };
    bool key_d     { false };
    bool key_q     { false };
    bool key_e     { false };
    bool key_shift { false };
    bool key_ctrl  { false };

    // Manual mode latches when the user touches WASD or right-mouse drag.
    // Once latched, scene_cam stops overwriting cam.eye / cam.target. Cleared
    // back to false from the palette ("Camera: Toggle Auto-Spin").
    bool manual_mode { false };
};

/// 3D click-to-pick request. Cleared by the picker after walking the entity
/// list; set by the input handler when the user left-clicks in the viewport.
struct PickRequest
{
    bool  pending { false };
    float x       { 0.0F };
    float y       { 0.0F };
};

/// Thin application-state aggregate. Holds the camera + free-look + pick
/// cluster so they can be passed by reference to the input / camera / picker
/// helpers without a 20-parameter call site.
///
/// Other cohesive state clusters (audio, fx, palette, materials) live in
/// their own aggregates (HelloAudio, HelloEngineFx, future HelloPalette /
/// HelloMaterials). HelloAppState is intentionally NOT a kitchen-sink god
/// object — additions should be justified by call-site simplification, not
/// by "the field used to be a main() local".
struct SampleAppState
{
    FreeLookState free_look {};
    PickRequest   pick {};
};

}  // namespace cd_sample

// =============================================================================
// FreeLookState helpers
// =============================================================================
//
// update_free_look_camera: per-frame camera tick that resolves the WASD +
// right-mouse-look + scene-orbit branches into a single call. Lifted out of
// main()'s frame loop in Marathon Run 12 phase A7 (sub-phase of the N13
// SampleAppState rollout). Mirrors the pre-extract behaviour bit-for-bit:
//
//   if (right_drag || any WASD/QE held):
//       on WASD-first frame, snapshot yaw/pitch/dist from current cam basis
//       (prevents the position from snapping when WASD first engages with no
//       prior right-drag warm-up).
//       drive cam.target via WASD + QE (with shift x2.5 / ctrl x0.25 modifiers,
//       both held = 1x for fine alignment),
//       drive cam.eye = cam.target - forward * dist.
//   else if (!manual_mode):
//       scene_cam.update(dt) drives auto-orbit.
//
// Manual mode latches separately (set by the input handler on first WASD or
// right-drag press, cleared by the palette command). We read it here but do
// not mutate it.

#include <cmath>

#include <cd/scene/SceneCameraController.hpp>

namespace cd_sample {

inline void update_free_look_camera(FreeLookState&                    fl,
                                    cd::camera::Camera&               cam,
                                    cd::scene::SceneCameraController& scene_cam,
                                    float                              dt) noexcept
{
    const bool wasd_active = fl.key_w || fl.key_a || fl.key_s || fl.key_d
                          || fl.key_q || fl.key_e;
    if (fl.right_drag || wasd_active)
    {
        // On WASD-first frame, sync yaw/pitch/dist from current cam so the
        // position doesn't snap. State is per-call function-local static to
        // match the pre-extract semantics (single global per program run).
        static bool wasd_was_active_prev = false;
        if (wasd_active && !wasd_was_active_prev && !fl.right_drag)
        {
            const float dxd  = cam.target.x - cam.eye.x;
            const float dyd  = cam.target.y - cam.eye.y;
            const float dzd  = cam.target.z - cam.eye.z;
            const float dist = std::sqrt(dxd * dxd + dyd * dyd + dzd * dzd);
            if (dist > 1e-3F)
            {
                fl.dist  = dist;
                fl.pitch = std::asin(dyd / dist);
                fl.yaw   = std::atan2(dxd, -dzd);
            }
        }
        wasd_was_active_prev = wasd_active;

        // Forward = view direction in world space.
        const float cp = std::cos(fl.pitch);
        const float sp = std::sin(fl.pitch);
        const float cy = std::cos(fl.yaw);
        const float sy = std::sin(fl.yaw);
        const cd::math::Vec3f forward { cp * sy, sp, -cp * cy };
        const cd::math::Vec3f right { cy, 0.0F, sy };

        // WASD moves cam.target; eye trails by fl.dist along -forward.
        // W6-F: hold shift for fast (x2.5), hold ctrl for slow (x0.25);
        // both held cancel to 1x for fine alignment.
        float spd_scale = 1.0F;
        if (fl.key_shift)
            spd_scale *= 2.5F;
        if (fl.key_ctrl)
            spd_scale *= 0.25F;
        const float spd = kCamMoveSpeed * dt * spd_scale;
        if (fl.key_w)
        {
            cam.target.x += forward.x * spd;
            cam.target.y += forward.y * spd;
            cam.target.z += forward.z * spd;
        }
        if (fl.key_s)
        {
            cam.target.x -= forward.x * spd;
            cam.target.y -= forward.y * spd;
            cam.target.z -= forward.z * spd;
        }
        if (fl.key_d)
        {
            cam.target.x += right.x * spd;
            cam.target.z += right.z * spd;
        }
        if (fl.key_a)
        {
            cam.target.x -= right.x * spd;
            cam.target.z -= right.z * spd;
        }
        if (fl.key_e)
        {
            cam.target.y += spd;
        }
        if (fl.key_q)
        {
            cam.target.y -= spd;
        }

        // Eye = target - forward * dist (target stays in view).
        cam.eye.x = cam.target.x - forward.x * fl.dist;
        cam.eye.y = cam.target.y - forward.y * fl.dist;
        cam.eye.z = cam.target.z - forward.z * fl.dist;
    }
    else if (!fl.manual_mode)
    {
        // Only auto-orbit if the user hasn't started manual control.
        // Once manual mode engages, the camera stays exactly where the
        // user left it on right-mouse release / WASD release.
        scene_cam.update(dt);
    }
}

}  // namespace cd_sample
