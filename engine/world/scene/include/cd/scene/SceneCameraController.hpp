// =============================================================================
// CHROMODYNAMIC — cd/scene/SceneCameraController.hpp
// Phase 110 / Wave 274 — scene-aware orbit camera facade.
//
// Wraps cd::camera::OrbitController with a "follow this entity" hook
// so editor / gameplay code can do:
//
//     scene_cam.attach_to(scene, hero_entity);
//     scene_cam.update(dt);
//     renderer.set_camera(scene_cam.camera());
//
// When attached, every `update()` reads the target entity's world
// transform from the scene graph and points the underlying
// OrbitController at the target entity's world-space position. Drag
// input still rotates the camera around that target; zoom changes
// the orbit radius; the user can detach by passing an invalid entity
// or calling `detach()`, after which `target()` is editable directly.
//
// Header-only; depends only on already-public scene / camera headers.
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/camera/OrbitController.hpp>
#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/scene/Scene.hpp>

namespace cd::scene
{

class SceneCameraController
{
public:
    SceneCameraController() noexcept = default;

    /// Bind the controller to a camera + (optionally) an entity to
    /// follow. `target_entity` may be a default-constructed Entity
    /// (no follow); in that case `target()` is left at whatever the
    /// caller set on the camera.
    void attach(cd::camera::Camera&     cam,
                Scene&                  scene,
                cd::ecs::Entity         target_entity = {}) noexcept
    {
        camera_ = &cam;
        scene_  = &scene;
        target_ = target_entity;
        // Sync orbit state from current eye → target so the user's
        // pre-existing camera position survives binding.
        orbit_.sync_from_camera(cam);
    }

    /// Detach the follow link. `update()` after this still rewrites
    /// the camera via the orbit controller, but using the camera's
    /// own `target` field (which the caller can mutate freely).
    void detach_target() noexcept { target_ = {}; }

    /// Per-frame tick — pulls target world position (if any) into the
    /// camera, then `orbit.apply` rewrites the eye.
    void update(float dt = 0.0F) noexcept
    {
        if (camera_ == nullptr) return;
        if (scene_ != nullptr && target_.is_valid())
        {
            // World transform's translation = follow target.
            if (const auto* w = scene_->world_transform(target_); w != nullptr)
            {
                camera_->target = { w->matrix[3][0], w->matrix[3][1], w->matrix[3][2] };
            }
        }
        if (dt > 0.0F && orbit_auto_spin_)
            orbit_.update_auto(*camera_, dt);
        else
            orbit_.apply(*camera_);
    }

    /// Forward to the underlying orbit controller — mouse drag pixels.
    void drag(float dx_px, float dy_px) noexcept
    {
        orbit_.drag(dx_px, dy_px);
    }

    /// Forward scroll-wheel / pinch zoom delta.
    void zoom(float delta) noexcept
    {
        orbit_.zoom(delta);
    }

    /// Enable continuous yaw rotation (Phase 14.E auto-spin pattern).
    void set_auto_spin(bool on) noexcept { orbit_auto_spin_ = on; }

    [[nodiscard]] bool auto_spin() const noexcept { return orbit_auto_spin_; }
    [[nodiscard]] cd::ecs::Entity target_entity() const noexcept { return target_; }
    [[nodiscard]] cd::camera::Camera* camera() noexcept { return camera_; }
    [[nodiscard]] cd::camera::OrbitController& orbit() noexcept { return orbit_; }

private:
    cd::camera::Camera*          camera_ { nullptr };
    Scene*                       scene_  { nullptr };
    cd::ecs::Entity              target_ {};
    cd::camera::OrbitController  orbit_ {};
    bool                         orbit_auto_spin_ { false };
};

}  // namespace cd::scene
