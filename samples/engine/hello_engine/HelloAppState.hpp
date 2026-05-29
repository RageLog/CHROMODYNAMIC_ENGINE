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
