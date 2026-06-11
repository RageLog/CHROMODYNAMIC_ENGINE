// =============================================================================
// HelloPicker.hpp
// -----------------------------------------------------------------------------
// hello_engine-local 3D click-to-pick handler. Lifted out of main() in
// Marathon Run 14 phase N18.
//
// What it does:
//   * Unprojects the click pixel to a world ray using either the FPS-camera
//     basis (when WASD/right-mouse-look is active) or the orbit-camera basis
//     (resting / orbit-only mode).
//   * Sphere-tests every entity (radius scaled by transform.scale + a 1.6x
//     bump for oblong / humanoid meshes).
//   * Also tests punctual / spot / area light positions (directional has no
//     world position).
//   * Returns a PickResult with the chosen { kind, index } + a small log line
//     so the caller can echo to the log panel.
//
// Inputs are bundled in PickInputs so the call site avoids the 12-parameter
// dance the pre-extract code had.
// =============================================================================
#pragma once

#include "HelloAppState.hpp"

#include <cd/camera/Camera.hpp>
#include <cd/light/Light.hpp>
#include <cd/math/Vector.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/scene/Scene.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd_sample {

enum class PickKind : std::uint8_t
{
    kNone   = 0,
    kEntity = 1,
    kLight  = 2
};

struct PickResult
{
    PickKind    kind   { PickKind::kNone };
    int         index  { -1 };
    std::string log    {};
};


struct PickInputs
{
    float          ndc_x {};         // already in [-1, 1]
    float          ndc_y {};         // already in [-1, 1], Y-flipped
    float          aspect {};
    cd::camera::Camera  cam;         // by value -- read-only snapshot
    float          cam_pitch {};
    float          cam_yaw {};
    bool           wasd_active {};
    bool           cam_right_drag {};
    bool           gizmo_hovered {};
};

struct EntityHit
{
    cd::ecs::Entity   handle;
    std::string       name;
};

struct LightHit
{
    cd::math::Vec3f  position;
    cd::light::LightType type;
    std::string      name;
};

[[nodiscard]] inline PickResult
pick_entity_or_light(const PickInputs& in,
                     const std::vector<EntityHit>& entities,
                     const std::vector<LightHit>&  lights,
                     const cd::scene::Scene&       scene)
{
    PickResult out {};

    if (in.gizmo_hovered)
    {
        // Click belongs to the gizmo arrow -- consumer should treat as
        // no-pick.  Caller still flips pending_pick=false beforehand.
        return out;
    }

    const float cp = std::cos(in.cam_pitch);
    const float sp = std::sin(in.cam_pitch);
    const float cy = std::cos(in.cam_yaw);
    const float sy = std::sin(in.cam_yaw);
    cd::math::Vec3f fwd { cp * sy, sp, -cp * cy };
    cd::math::Vec3f rgt { cy, 0.0F, sy };
    cd::math::Vec3f up_v { fwd.y * rgt.z - fwd.z * rgt.y,
                           fwd.z * rgt.x - fwd.x * rgt.z,
                           fwd.x * rgt.y - fwd.y * rgt.x };

    if (!in.cam_right_drag && !in.wasd_active)
    {
        fwd.x = in.cam.target.x - in.cam.eye.x;
        fwd.y = in.cam.target.y - in.cam.eye.y;
        fwd.z = in.cam.target.z - in.cam.eye.z;
        const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
        if (fl > 1e-5F)
        {
            fwd.x /= fl;
            fwd.y /= fl;
            fwd.z /= fl;
        }
        cd::math::Vec3f world_up { 0, 1, 0 };
        rgt.x = fwd.y * world_up.z - fwd.z * world_up.y;
        rgt.y = fwd.z * world_up.x - fwd.x * world_up.z;
        rgt.z = fwd.x * world_up.y - fwd.y * world_up.x;
        const float rl = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
        if (rl > 1e-5F)
        {
            rgt.x /= rl;
            rgt.y /= rl;
            rgt.z /= rl;
        }
        up_v.x = rgt.y * fwd.z - rgt.z * fwd.y;
        up_v.y = rgt.z * fwd.x - rgt.x * fwd.z;
        up_v.z = rgt.x * fwd.y - rgt.y * fwd.x;
    }

    const float tan_half_fov = std::tan(in.cam.fov_y * 0.5F);
    const float scale_x = in.aspect * tan_half_fov;
    const float scale_y = tan_half_fov;
    cd::math::Vec3f ray_dir { fwd.x + rgt.x * in.ndc_x * scale_x + up_v.x * in.ndc_y * scale_y,
                              fwd.y + rgt.y * in.ndc_x * scale_x + up_v.y * in.ndc_y * scale_y,
                              fwd.z + rgt.z * in.ndc_x * scale_x + up_v.z * in.ndc_y * scale_y };
    const float rdl = std::sqrt(ray_dir.x * ray_dir.x + ray_dir.y * ray_dir.y + ray_dir.z * ray_dir.z);
    if (rdl > 1e-5F)
    {
        ray_dir.x /= rdl;
        ray_dir.y /= rdl;
        ray_dir.z /= rdl;
    }

    float best_t = 1e30F;
    int   best_i = -1;
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        auto* lt = scene.local(entities[i].handle);
        if (lt == nullptr)
            continue;
        const cd::math::Vec3f c { lt->value.position.x, lt->value.position.y, lt->value.position.z };
        const float ms = std::max({ lt->value.scale.x, lt->value.scale.y, lt->value.scale.z });
        const float pick_r = 0.55F * std::max(1.0F, ms) * 1.6F;
        const cd::math::Vec3f oc { in.cam.eye.x - c.x, in.cam.eye.y - c.y, in.cam.eye.z - c.z };
        const float b = oc.x * ray_dir.x + oc.y * ray_dir.y + oc.z * ray_dir.z;
        const float cc = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - pick_r * pick_r;
        const float disc = b * b - cc;
        if (disc < 0.0F)
            continue;
        const float t = -b - std::sqrt(disc);
        if (t > 0.0F && t < best_t)
        {
            best_t = t;
            best_i = static_cast<int>(i);
        }
    }

    int   best_light = -1;
    float best_light_t = best_t;
    for (std::size_t i = 0; i < lights.size(); ++i)
    {
        if (lights[i].type == cd::light::LightType::kDirectional)
            continue;
        const auto& Lp = lights[i].position;
        const cd::math::Vec3f oc { in.cam.eye.x - Lp.x, in.cam.eye.y - Lp.y, in.cam.eye.z - Lp.z };
        constexpr float kLightPickR = 0.9F;
        const float b = oc.x * ray_dir.x + oc.y * ray_dir.y + oc.z * ray_dir.z;
        const float cc = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - kLightPickR * kLightPickR;
        const float disc = b * b - cc;
        if (disc < 0.0F)
            continue;
        const float t = -b - std::sqrt(disc);
        if (t > 0.0F && t < best_light_t)
        {
            best_light_t = t;
            best_light = static_cast<int>(i);
        }
    }

    if (best_light >= 0)
    {
        out.kind  = PickKind::kLight;
        out.index = best_light;
        out.log   = std::string("[pick] selected light ") + lights[static_cast<std::size_t>(best_light)].name;
    }
    else if (best_i >= 0)
    {
        out.kind  = PickKind::kEntity;
        out.index = best_i;
        out.log   = std::string("[pick] selected ") + entities[static_cast<std::size_t>(best_i)].name;
    }
    // else kind stays kNone -- caller decides "empty-space click -> unselect".
    return out;
}

} // namespace cd_sample
