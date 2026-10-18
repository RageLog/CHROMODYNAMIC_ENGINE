// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioCollisionMath.hpp
//
// Quaternion <-> Euler degree conversions, grid snapping, oriented bounding box
// (OBB) / Sphere / Capsule narrowphase collision detection, and entity home
// transform snapshot/restore utilities.
// =============================================================================
#pragma once

#include "StudioModifierClasses.hpp"

namespace cd::studio
{
// 5. Euler Degrees <-> Quaternion & Snap Math Utilities
// =============================================================================
[[nodiscard]] inline cd::math::Quatf euler_deg_to_quat(cd::math::Vec3f deg) noexcept
{
    constexpr float kDegToRad = 3.1415926535F / 180.0F;
    const cd::math::Quatf qx = cd::math::Quatf::from_axis_angle({ 1.0F, 0.0F, 0.0F }, deg.x * kDegToRad);
    const cd::math::Quatf qy = cd::math::Quatf::from_axis_angle({ 0.0F, 1.0F, 0.0F }, deg.y * kDegToRad);
    const cd::math::Quatf qz = cd::math::Quatf::from_axis_angle({ 0.0F, 0.0F, 1.0F }, deg.z * kDegToRad);
    return cd::math::normalize(qy * qx * qz);
}

[[nodiscard]] inline cd::math::Vec3f quat_to_euler_deg(const cd::math::Quatf& q_in) noexcept
{
    const cd::math::Quatf q = cd::math::normalize(q_in);
    constexpr float kRadToDeg = 180.0F / 3.1415926535F;

    // Pitch (x-axis rotation)
    const float sinp = 2.0F * (q.w * q.x - q.y * q.z);
    const float pitch = (std::abs(sinp) >= 1.0F)
        ? std::copysign(90.0F, sinp)
        : (std::asin(sinp) * kRadToDeg);

    // Yaw (y-axis rotation)
    const float siny_cosp = 2.0F * (q.w * q.y + q.z * q.x);
    const float cosy_cosp = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
    const float yaw = std::atan2(siny_cosp, cosy_cosp) * kRadToDeg;

    // Roll (z-axis rotation)
    const float sinr_cosp = 2.0F * (q.w * q.z + q.x * q.y);
    const float cosr_cosp = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
    const float roll = std::atan2(sinr_cosp, cosr_cosp) * kRadToDeg;

    return { pitch, yaw, roll };
}

[[nodiscard]] inline cd::math::Vec3f snap_vec3_if_enabled(cd::math::Vec3f v) noexcept
{
    const auto& w = studio_world();
    if (!w.grid_snap_enabled || w.grid_snap_step <= 1e-4F) return v;
    const float st = w.grid_snap_step;
    return {
        std::round(v.x / st) * st,
        std::round(v.y / st) * st,
        std::round(v.z / st) * st,
    };
}

[[nodiscard]] inline cd::math::Vec3f compute_collider_local_half_extents(
    const cd::math::Transformf& tr,
    const IColliderModifier*    col) noexcept
{
    const cd::math::Vec3f sx { std::max(0.05F, std::abs(tr.scale.x)),
                               std::max(0.05F, std::abs(tr.scale.y)),
                               std::max(0.05F, std::abs(tr.scale.z)) };
    cd::math::Vec3f half { 0.5F * sx.x, 0.5F * sx.y, 0.5F * sx.z };
    if (col != nullptr)
    {
        if (const auto* box = dynamic_cast<const BoxColliderModifier*>(col))
        {
            half = { box->half_extents.x * sx.x,
                     box->half_extents.y * sx.y,
                     box->half_extents.z * sx.z };
        }
        else if (const auto* sph = dynamic_cast<const SphereColliderModifier*>(col))
        {
            const float r = sph->radius * std::max({ sx.x, sx.y, sx.z });
            half = { r, r, r };
        }
        else if (const auto* cap = dynamic_cast<const CapsuleColliderModifier*>(col))
        {
            const float r  = cap->radius * std::max(sx.x, sx.z);
            const float hh = (cap->half_height + cap->radius) * sx.y;
            half = { r, hh, r };
        }
    }
    return half;
}

[[nodiscard]] inline cd::physics::Aabb compute_entity_world_aabb(
    const cd::math::Transformf& tr,
    const IColliderModifier*    col) noexcept
{
    const cd::math::Vec3f half   = compute_collider_local_half_extents(tr, col);
    const cd::math::Vec3f offset = (col != nullptr) ? col->center_offset : cd::math::Vec3f { 0.0F, 0.0F, 0.0F };
    if (col != nullptr && (col->shape_kind() == ColliderShapeKind::kSphere ||
                           col->shape_kind() == ColliderShapeKind::kCapsule))
    {
        const cd::math::Vec3f center = tr.position + offset;
        return cd::physics::Aabb {
            .min = { center.x - half.x, center.y - half.y, center.z - half.z },
            .max = { center.x + half.x, center.y + half.y, center.z + half.z },
        };
    }
    const cd::math::Quatf q      = cd::math::normalize(tr.rotation);
    const cd::math::Vec3f center = tr.position + cd::math::rotate(q, offset);

    // Compute rotated world AABB from the 3 local axes so tilted ramps have accurate bounds
    const cd::math::Vec3f ax = cd::math::rotate(q, cd::math::Vec3f { half.x, 0.0F, 0.0F });
    const cd::math::Vec3f ay = cd::math::rotate(q, cd::math::Vec3f { 0.0F, half.y, 0.0F });
    const cd::math::Vec3f az = cd::math::rotate(q, cd::math::Vec3f { 0.0F, 0.0F, half.z });
    const cd::math::Vec3f wh {
        std::abs(ax.x) + std::abs(ay.x) + std::abs(az.x),
        std::abs(ax.y) + std::abs(ay.y) + std::abs(az.y),
        std::abs(ax.z) + std::abs(ay.z) + std::abs(az.z),
    };
    return cd::physics::Aabb {
        .min = { center.x - wh.x, center.y - wh.y, center.z - wh.z },
        .max = { center.x + wh.x, center.y + wh.y, center.z + wh.z },
    };
}

// =============================================================================
// 6. Narrowphase Collision Detection (Supports Oriented Box / Ramp OBB!)
// =============================================================================
[[nodiscard]] inline bool collide_sphere_vs_obb(
    cd::math::Vec3f             sph_center,
    float                       sph_radius,
    const cd::math::Transformf& box_tr,
    const IColliderModifier&    box_col,
    cd::physics::ContactPoint&  out_cp,
    bool                        sphere_is_a) noexcept
{
    const cd::math::Quatf q_box    = cd::math::normalize(box_tr.rotation);
    const cd::math::Quatf q_inv    = cd::math::conjugate(q_box);
    const cd::math::Vec3f box_cent = box_tr.position + cd::math::rotate(q_box, box_col.center_offset);
    const cd::math::Vec3f half     = compute_collider_local_half_extents(box_tr, &box_col);

    // Transform sphere center into the OBB's local space
    const cd::math::Vec3f local_pt = cd::math::rotate(q_inv, sph_center - box_cent);
    const cd::math::Vec3f closest_local {
        std::clamp(local_pt.x, -half.x, half.x),
        std::clamp(local_pt.y, -half.y, half.y),
        std::clamp(local_pt.z, -half.z, half.z),
    };
    const cd::math::Vec3f delta_local = local_pt - closest_local;
    const float dist_sq = delta_local.x * delta_local.x +
                          delta_local.y * delta_local.y +
                          delta_local.z * delta_local.z;
    if (dist_sq > sph_radius * sph_radius) return false;

    const float dist = std::sqrt(dist_sq);
    cd::math::Vec3f local_nrm { 0.0F, 1.0F, 0.0F };
    float pen = 0.0F;
    if (dist > 1e-5F)
    {
        local_nrm = delta_local * (1.0F / dist);
        pen       = sph_radius - dist;
    }
    else
    {
        const float dx = half.x - std::abs(local_pt.x);
        const float dy = half.y - std::abs(local_pt.y);
        const float dz = half.z - std::abs(local_pt.z);
        if (dy <= dx && dy <= dz)
        {
            local_nrm = { 0.0F, (local_pt.y >= 0.0F) ? 1.0F : -1.0F, 0.0F };
            pen       = dy + sph_radius;
        }
        else if (dx <= dz)
        {
            local_nrm = { (local_pt.x >= 0.0F) ? 1.0F : -1.0F, 0.0F, 0.0F };
            pen       = dx + sph_radius;
        }
        else
        {
            local_nrm = { 0.0F, 0.0F, (local_pt.z >= 0.0F) ? 1.0F : -1.0F };
            pen       = dz + sph_radius;
        }
    }

    const cd::math::Vec3f world_nrm     = cd::math::rotate(q_box, local_nrm);
    const cd::math::Vec3f world_closest = box_cent + cd::math::rotate(q_box, closest_local);
    out_cp.normal         = sphere_is_a ? world_nrm : (world_nrm * -1.0F);
    out_cp.penetration    = pen;
    out_cp.world_position = world_closest;
    return out_cp.penetration > 0.0F;
}

[[nodiscard]] inline bool collide_narrowphase(
    const cd::math::Transformf& tr_a, const IColliderModifier& col_a,
    const cd::math::Transformf& tr_b, const IColliderModifier& col_b,
    cd::physics::ContactPoint& out_cp) noexcept
{
    // 1. Sphere vs Sphere exact path
    if (col_a.shape_kind() == ColliderShapeKind::kSphere &&
        col_b.shape_kind() == ColliderShapeKind::kSphere)
    {
        const auto* sa = static_cast<const SphereColliderModifier*>(&col_a);
        const auto* sb = static_cast<const SphereColliderModifier*>(&col_b);
        const float ra = sa->radius * std::max({ std::abs(tr_a.scale.x), std::abs(tr_a.scale.y), std::abs(tr_a.scale.z) });
        const float rb = sb->radius * std::max({ std::abs(tr_b.scale.x), std::abs(tr_b.scale.y), std::abs(tr_b.scale.z) });
        const cd::physics::Sphere sph_a { tr_a.position + col_a.center_offset, ra };
        const cd::physics::Sphere sph_b { tr_b.position + col_b.center_offset, rb };
        if (!cd::physics::intersects(sph_a, sph_b)) return false;

        const cd::math::Vec3f d = sph_a.center - sph_b.center;
        const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const float rsum = ra + rb;
        out_cp.penetration = std::max(0.0F, rsum - dist);
        out_cp.normal = (dist > 1e-5F)
            ? cd::math::Vec3f { d.x / dist, d.y / dist, d.z / dist }
            : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };
        out_cp.world_position = sph_b.center + out_cp.normal * rb;
        return out_cp.penetration > 0.0F;
    }

    // 2. Sphere vs Oriented Box (OBB) — supports angled Ramps & tilted Walls!
    if ((col_a.shape_kind() == ColliderShapeKind::kSphere && col_b.shape_kind() == ColliderShapeKind::kBox) ||
        (col_a.shape_kind() == ColliderShapeKind::kBox && col_b.shape_kind() == ColliderShapeKind::kSphere))
    {
        const bool a_is_sphere = (col_a.shape_kind() == ColliderShapeKind::kSphere);
        const auto& sph_tr  = a_is_sphere ? tr_a : tr_b;
        const auto& box_tr  = a_is_sphere ? tr_b : tr_a;
        const auto* sph_col = static_cast<const SphereColliderModifier*>(a_is_sphere ? &col_a : &col_b);
        const auto& box_col = a_is_sphere ? col_b : col_a;

        const float r = sph_col->radius * std::max({ std::abs(sph_tr.scale.x), std::abs(sph_tr.scale.y), std::abs(sph_tr.scale.z) });
        return collide_sphere_vs_obb(sph_tr.position + sph_col->center_offset, r,
                                     box_tr, box_col, out_cp, a_is_sphere);
    }

    // 3. If one of the two colliders is a rotated Box (e.g. a tilted Ramp) and the other is an upright dynamic Box/Capsule,
    // test the dynamic Box/Capsule against the tilted OBB ramp so cubes and character capsules climb/slide on ramps!
    const bool rot_a = (col_a.shape_kind() == ColliderShapeKind::kBox) &&
                       ((std::abs(tr_a.rotation.x) + std::abs(tr_a.rotation.z)) > 0.04F);
    const bool rot_b = (col_b.shape_kind() == ColliderShapeKind::kBox) &&
                       ((std::abs(tr_b.rotation.x) + std::abs(tr_b.rotation.z)) > 0.04F);
    if (rot_a != rot_b)
    {
        const bool a_is_ramp = rot_a;
        const auto& dyn_tr   = a_is_ramp ? tr_b : tr_a;
        const auto& dyn_col  = a_is_ramp ? col_b : col_a;
        const auto& ramp_tr  = a_is_ramp ? tr_a : tr_b;
        const auto& ramp_col = a_is_ramp ? col_a : col_b;
        const cd::math::Vec3f dyn_half = compute_collider_local_half_extents(dyn_tr, &dyn_col);
        const float eff_r = std::min({ dyn_half.x, dyn_half.y, dyn_half.z });
        cd::math::Vec3f sph_pt = dyn_tr.position + dyn_col.center_offset;
        if (dyn_col.shape_kind() == ColliderShapeKind::kCapsule && dyn_half.y > eff_r)
        {
            sph_pt.y -= (dyn_half.y - eff_r);
        }
        return collide_sphere_vs_obb(sph_pt, eff_r, ramp_tr, ramp_col, out_cp, !a_is_ramp);
    }

    // 4. General AABB vs AABB (Box-Box / Capsule-Box) SAT minimum translation axis
    const cd::physics::Aabb box_a = compute_entity_world_aabb(tr_a, &col_a);
    const cd::physics::Aabb box_b = compute_entity_world_aabb(tr_b, &col_b);
    if (!cd::physics::overlaps(box_a, box_b)) return false;

    const cd::math::Vec3f ca = cd::physics::center(box_a);
    const cd::math::Vec3f cb = cd::physics::center(box_b);
    const cd::math::Vec3f ha = cd::physics::extent(box_a) * 0.5F;
    const cd::math::Vec3f hb = cd::physics::extent(box_b) * 0.5F;

    const float ox = (ha.x + hb.x) - std::abs(ca.x - cb.x);
    const float oy = (ha.y + hb.y) - std::abs(ca.y - cb.y);
    const float oz = (ha.z + hb.z) - std::abs(ca.z - cb.z);
    if (ox <= 0.0F || oy <= 0.0F || oz <= 0.0F) return false;

    if (oy <= ox && oy <= oz)
    {
        out_cp.penetration = oy;
        out_cp.normal      = { 0.0F, (ca.y >= cb.y) ? 1.0F : -1.0F, 0.0F };
    }
    else if (ox <= oz)
    {
        out_cp.penetration = ox;
        out_cp.normal      = { (ca.x >= cb.x) ? 1.0F : -1.0F, 0.0F, 0.0F };
    }
    else
    {
        out_cp.penetration = oz;
        out_cp.normal      = { 0.0F, 0.0F, (ca.z >= cb.z) ? 1.0F : -1.0F };
    }
    out_cp.world_position = {
        0.5F * ( std::max(box_a.min.x, box_b.min.x) + std::min(box_a.max.x, box_b.max.x) ),
        0.5F * ( std::max(box_a.min.y, box_b.min.y) + std::min(box_a.max.y, box_b.max.y) ),
        0.5F * ( std::max(box_a.min.z, box_b.min.z) + std::min(box_a.max.z, box_b.max.z) ),
    };
    return true;
}

// =============================================================================
// 7. Clean Studio Scene Initialization & Physics Snapshot / Reset Helpers
// =============================================================================
template <typename StateT>
inline void record_entity_home_transform(StateT& s, cd::ecs::Entity h) noexcept
{
    if (const auto* lt = s.scene.local(h))
    {
        studio_world().home_transforms[h.id] = lt->value;
        studio_world().euler_cache_deg[h.id] = quat_to_euler_deg(lt->value.rotation);
    }
}

template <typename StateT, typename LogFn>
inline void reset_physics_to_home_positions(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    // 1. Remove runtime-spawned projectiles (CannonBall_*)
    for (std::size_t i = s.entities.size(); i > 0; --i)
    {
        const std::size_t idx = i - 1;
        if (s.entities[idx].name.find("CannonBall_") == 0)
        {
            const auto hid = s.entities[idx].handle;
            w.entity_modifiers.erase(hid.id);
            w.home_transforms.erase(hid.id);
            w.euler_cache_deg.erase(hid.id);
            s.scene.destroy_node(hid);
            s.entities.erase(s.entities.begin() + static_cast<std::ptrdiff_t>(idx));
            if (std::cmp_equal(s.selected, idx))
                s.selected = s.entities.empty() ? -1 : 0;
            else if (std::cmp_greater(s.selected, idx))
                --s.selected;
        }
    }

    // 2. Restore authored home transforms, velocities, and trigger/collectible visibility
    int total_cores = 0;
    for (auto& ent : s.entities)
    {
        if (auto* lt = s.scene.local(ent.handle))
        {
            if (auto it = w.home_transforms.find(ent.handle.id); it != w.home_transforms.end())
            {
                lt->value = it->second;
            }
        }
        ModifierStack& ms = w.entity_modifiers[ent.handle.id];
        if (auto* rb = ms.template find<RigidBodyModifier>())
        {
            rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
            rb->is_grounded     = false;
        }
        if (auto* km = ms.template find<KinematicMoverModifier>())
        {
            km->last_frame_delta = { 0.0F, 0.0F, 0.0F };
        }
        if (auto* pat = ms.template find<PatrolWaypointModifier>())
        {
            pat->enabled        = true;
            pat->direction_sign = 1.0F;
            ent.visible         = true;
        }
        if (auto* trg = ms.template find<TriggerVolumeModifier>())
        {
            trg->enabled      = true;
            trg->triggered    = false;
            trg->cooldown_sec = 0.0F;
            ent.visible       = true;
            if (trg->action == TriggerActionKind::kCollectibleScore)
                ++total_cores;
        }
    }
    w.sim_time_accum          = 0.0F;
    w.player_hp               = w.player_max_hp;
    w.player_shield           = 100.0F;
    w.player_energy           = 100.0F;
    w.game_score              = 0;
    w.collected_cores         = 0;
    w.total_cores_in_level    = total_cores;
    w.defeated_enemies        = 0;
    w.player_horiz_speed      = 0.0F;
    w.autopilot_fire_cooldown = 0.0F;
    w.level_goal_reached      = false;
    w.live_contacts.clear();
    s.scene.update_transforms();
    log_push("[physics] Restored all entities, triggers, and gameplay state to authored snapshot");
}

template <typename StateT, typename LogFn>
inline void enter_pie_play_mode(StateT& s, LogFn&& log_push)
{
    auto& w = studio_world();
    for (auto& ent : s.entities)
    {
        if (ent.name.find("CannonBall_") != 0)
        {
            if (!w.simulate_physics_in_edit || !w.home_transforms.contains(ent.handle.id))
            {
                record_entity_home_transform(s, ent.handle);
            }
        }
    }
    reset_physics_to_home_positions(s, log_push);
    w.player_max_hp        = 100.0F;
    w.player_hp            = 100.0F;
    w.player_shield        = 100.0F;
    w.player_energy        = 100.0F;
    w.hud_toast_message    = "PLAY MODE ACTIVE - WASD/Arrows=Move, Space=Jump, B/LMB=Shoot, P=Autopilot!";
    w.hud_toast_timer      = 4.0F;
    s.pie_playing          = true;
    s.gizmo_state.visible  = false;
    log_push("[PIE] Play-In-Editor started (Clean authored snapshot active)");
}

template <typename StateT, typename LogFn>
inline void exit_pie_play_mode(StateT& s, LogFn&& log_push)
{
    auto& w                 = studio_world();
    w.autopilot_play_active = false;
    s.pie_playing           = false;
    s.gizmo_state.visible   = true;
    reset_physics_to_home_positions(s, log_push);
    log_push("[PIE] Play-In-Editor stopped (Pre-PIE scene snapshot restored)");
}

template <typename StateT, typename LogFn>
inline void snap_selected_entity_to_ground(StateT& s, LogFn&& log_push)
{
    if (s.selected < 0 || !std::cmp_less(s.selected, s.entities.size())) return;
    auto& ent = s.entities[static_cast<std::size_t>(s.selected)];
    if (ent.name.find("Ground_Floor") != std::string::npos) return;
    auto* lt = s.scene.local(ent.handle);
    if (!lt) return;

    const auto* col = studio_world().entity_modifiers[ent.handle.id].find_collider();
    const cd::physics::Aabb box = compute_entity_world_aabb(lt->value, col);
    const float bottom_y = box.min.y;
    lt->value.position.y -= bottom_y; // Place bottom flush on y = 0.0m floor
    if (auto* rb = studio_world().entity_modifiers[ent.handle.id].template find<RigidBodyModifier>())
    {
        rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
    }
    record_entity_home_transform(s, ent.handle);
    s.scene.update_transforms();
    log_push("[transform] Snapped '" + ent.name + "' flush to Ground Floor (y = 0)");
}

template <typename StateT, typename LogFn>
inline void duplicate_entity_with_modifiers(StateT& s, int src_idx, LogFn&& log_push)
{
    if (src_idx < 0 || !std::cmp_less(src_idx, s.entities.size())) return;
    auto& w = studio_world();
    const SceneEntity src = s.entities[static_cast<std::size_t>(src_idx)];
    SceneEntity copy      = src;
    copy.handle           = s.scene.create_node();
    copy.name             = src.name + "_Copy";
    cd::world_container::assign_layer(s.ecs_world, copy.handle,
                                      cd::world_container::layer_of(s.ecs_world, src.handle));
    if (const auto* src_lt = s.scene.local(src.handle))
    {
        if (auto* dst_lt = s.scene.local(copy.handle))
        {
            dst_lt->value            = src_lt->value;
            dst_lt->value.position.x += 1.1F;
        }
    }
    // Deep-clone all attached Modifier classes!
    ModifierStack& src_ms = w.entity_modifiers[src.handle.id];
    ModifierStack& dst_ms = w.entity_modifiers[copy.handle.id];
    dst_ms.items.clear();
    for (const auto& m : src_ms.items)
    {
        if (m) dst_ms.items.push_back(m->clone());
    }
    record_entity_home_transform(s, copy.handle);
    s.entities.push_back(std::move(copy));
    s.scene.update_transforms();
    s.selected      = static_cast<int>(s.entities.size()) - 1;
    s.selected_kind = SelKind::kEntity;
    log_push("[edit] Duplicated '" + src.name + "' with all attached Modifier classes");
}

} // namespace cd::studio
