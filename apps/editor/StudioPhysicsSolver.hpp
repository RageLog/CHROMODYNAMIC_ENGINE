// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioPhysicsSolver.hpp
//
// Real-time 4x-substep 3D RigidBody impulse & friction solver, OBB/Sphere 3D
// wireframe renderer, mouse-to-ground raycasting, Floating 3D Viewport HUD,
// Right-Click Viewport Context Menu, and 3D Drag-and-Drop targets.
// =============================================================================
#pragma once

#include "StudioSceneBuilder.hpp"

namespace cd::studio
{
// 9. Tick All Modifiers + Step 3D RigidBody & Narrowphase Collision Solver
// =============================================================================
template <typename StateT, typename LogFn>
inline void tick_studio_modifiers_and_physics(StateT& s, float dt, LogFn&& log_push)
{
    init_clean_studio_workspace(s, log_push);
    auto& w = studio_world();

    // Update EMA-smoothed FPS & frame time ring buffer
    // (Skip cold-start frames 0..3 where one-time GPU image layout transitions occur)
    if (dt > 1e-6F && dt < 0.5F)
    {
        const float inst_fps = 1.0F / dt;
        const float inst_ms  = dt * 1000.0F;
        if (s.frame_idx <= 3u || w.smoothed_frame_ms <= 0.01F)
        {
            w.smoothed_frame_ms = inst_ms;
            w.smoothed_fps      = inst_fps;
        }
        else
        {
            w.smoothed_frame_ms = w.smoothed_frame_ms * 0.85F + inst_ms * 0.15F;
            w.smoothed_fps      = 1000.0F / std::max(0.05F, w.smoothed_frame_ms);
        }
        if (w.smoothed_fps > w.peak_fps && s.frame_idx > 6u) w.peak_fps = w.smoothed_fps;
        w.frame_ms_history[w.frame_ms_cursor % w.frame_ms_history.size()] = inst_ms;
        ++w.frame_ms_cursor;
    }

    // ---- A. Evaluate Environment Modifiers -> Renderer Post-FX State ----
    s.fx.volumetric_fog_on  = false;
    s.fx.fog_density        = 0.0F;
    s.fx.shafts_strength    = 0.0F;
    s.fx.aerial_perspective = 0.0F;
    s.fx.bloom_post         = 0.0F;
    s.fx.chromab_strength   = 0.0F;
    s.fx.vignette_strength  = 0.0F;
    s.fx.film_grain         = 0.0F;
    s.fx.dof_strength       = 0.0F;
    s.fx.ddgi_on            = false;
    s.fx.taa_amount         = 0.0F;
    s.fx.motion_blur        = 0.0F;

    if (const auto* fog = w.environment_modifiers.find<FogModifier>(); fog && fog->enabled)
    {
        s.fx.volumetric_fog_on  = fog->volumetric_scattering;
        s.fx.fog_density        = fog->fog_density;
        s.fx.shafts_strength    = fog->light_shafts_strength;
        s.fx.aerial_perspective = fog->aerial_perspective;
    }
    if (const auto* blm = w.environment_modifiers.find<BloomLensModifier>(); blm && blm->enabled)
    {
        s.fx.bloom_post        = blm->bloom_intensity;
        s.fx.dof_strength      = blm->dof_strength;
        s.fx.chromab_strength  = blm->chromatic_aberration;
        s.fx.vignette_strength = blm->vignette_strength;
        s.fx.film_grain        = blm->film_grain;
    }
    if (const auto* tmp = w.environment_modifiers.find<TonemapCameraModifier>(); tmp && tmp->enabled)
    {
        s.fx.exposure         = tmp->exposure;
        s.fx.saturation_boost = tmp->saturation_boost;
        s.fx.tonemap_op       = tmp->tonemap_op;
        s.fx.taa_amount       = tmp->taa_amount;
        s.fx.motion_blur      = tmp->motion_blur;
    }
    if (const auto* sky = w.environment_modifiers.find<SkyAtmosphereModifier>(); sky && sky->enabled)
    {
        s.fx.clouds_coverage = sky->clouds_coverage;
        s.fx.ao_strength     = sky->ao_strength;
        s.fx.ssr_strength    = sky->ssr_strength;
        s.fx.ddgi_on         = sky->ddgi_gi_enabled;
    }

    // ---- B. Evaluate Per-Entity Modifiers & Godot+ Gameplay Logic ----
    const float raw_clamped_dt = std::clamp(dt, 0.001F, 0.033F);
    const float clamped_dt     = (s.pie_playing && w.autopilot_play_active)
                                     ? std::max(raw_clamped_dt, 1.0F / 50.0F)
                                     : raw_clamped_dt;
    const bool sim_active = (s.pie_playing || w.simulate_physics_in_edit || w.step_single_frame);
    if (!w.rockstar_engine_seeded)
    {
        w.rockstar_engine_seeded = true;
        auto& rpe = w.rockstar_physics_engine;
        (void)rpe.world_2d().create_body(
            cd::physics::core::BodyMotionType::kStatic,
            cd::physics::core::Transform2D { { 0.0F, -1.0F }, 0.0F },
            cd::physics::v2d::BoxShape2D { { 12.0F, 0.5F } });
        (void)rpe.world_2d().create_body(
            cd::physics::core::BodyMotionType::kDynamic,
            cd::physics::core::Transform2D { { 0.0F, 1.5F }, 0.0F },
            cd::physics::v2d::BoxShape2D { { 0.5F, 0.5F } });
        cd::physics::v3d::RigidBody3D rb_floor {};
        rb_floor.motion_type = cd::physics::core::BodyMotionType::kStatic;
        rb_floor.transform.position = { 0.0F, -0.55F, 0.0F };
        rb_floor.shape = cd::physics::v3d::BoxShape3D { { 24.0F, 0.5F, 24.0F } };
        (void)rpe.world_3d().create_body(rb_floor);
        cd::physics::v3d::RigidBody3D rb_crate {};
        rb_crate.motion_type = cd::physics::core::BodyMotionType::kDynamic;
        rb_crate.transform.position = { 0.0F, 1.4F, 0.0F };
        rb_crate.shape = cd::physics::v3d::BoxShape3D { { 0.5F, 0.5F, 0.5F } };
        (void)rpe.world_3d().create_body(rb_crate);
        (void)rpe.create_euphoria_ragdoll({ 0.0F, 1.0F, 0.0F }, 78.0F);
        (void)rpe.create_pacejka_vehicle();
        const auto f_id = rpe.create_fracture_system();
        rpe.get_fracture_system(f_id)->generate_grid_fracture_object(2u, 3u, 2u);
        const auto x_id = rpe.create_xpbd_solver();
        rpe.get_xpbd_solver(x_id)->create_cloth_grid(5u, 5u, 0.12F);
    }
    if (sim_active)
    {
        w.sim_time_accum += clamped_dt;
        w.rockstar_physics_engine.step_all(clamped_dt, 4u, 4u, -0.55F);
    }
    if (w.hud_toast_timer > 0.0F)
    {
        w.hud_toast_timer = std::max(0.0F, w.hud_toast_timer - clamped_dt);
    }
    if (w.autopilot_fire_cooldown > 0.0F)
    {
        w.autopilot_fire_cooldown = std::max(0.0F, w.autopilot_fire_cooldown - clamped_dt);
    }
    bool has_active_dynamic_rb = false;
    bool fire_autopilot_shot   = false;
    int  player_entity_idx     = -1;
    cd::math::Vec3f player_pos { 0.0F, 0.0F, 0.0F };
    w.player_horiz_speed = 0.0F;

    for (std::size_t idx = 0; idx < s.entities.size(); ++idx)
    {
        auto&          ent = s.entities[idx];
        ModifierStack& ms  = w.entity_modifiers[ent.handle.id];

        if (ms.items.empty() && ent.kind != PrimitiveKind::kSponza)
        {
            ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
                ent.tint, ent.metallic, ent.roughness, ent.use_texture,
                ent.multiscatter_comp, ent.iridescence_nm,
                ent.custom_texture_id, ent.custom_texture_path));
        }

        if (const auto* pbr = ms.template find<PbrMaterialModifier>(); pbr && pbr->enabled)
        {
            ent.tint              = pbr->base_color;
            ent.metallic          = pbr->metallic;
            ent.roughness         = pbr->roughness;
            ent.use_texture       = pbr->use_texture;
            ent.custom_texture_id = pbr->use_texture ? std::max(1, pbr->texture_slot) : 0;
            if (!pbr->texture_asset_path.empty())
                ent.custom_texture_path = pbr->texture_asset_path;
            ent.multiscatter_comp = pbr->multiscatter_comp;
            ent.iridescence_nm    = pbr->iridescence_nm;
            ent.is_pbr            = true;
        }

        if (const auto* rb = ms.template find<RigidBodyModifier>();
            rb && rb->enabled && rb->body_type == cd::physics::BodyType::kDynamic && ent.visible && !ent.locked)
        {
            has_active_dynamic_rb = true;
        }

        if (const auto* efog = ms.template find<FogModifier>(); efog && efog->enabled)
        {
            s.fx.volumetric_fog_on  = efog->volumetric_scattering;
            s.fx.fog_density        = efog->fog_density;
            s.fx.shafts_strength    = efog->light_shafts_strength;
            s.fx.aerial_perspective = efog->aerial_perspective;
        }

        // 1. CharacterControllerModifier (WASD/Arrows + Space in PIE, or Autonomous E2E Autopilot Driver!)
        if (auto* cc = ms.template find<CharacterControllerModifier>(); cc && cc->enabled && ent.visible && !ent.locked)
        {
            player_entity_idx = static_cast<int>(idx);
            if (auto* lt = s.scene.local(ent.handle))
            {
                cd::math::Vec3f horiz_move_delta { 0.0F, 0.0F, 0.0F };
                bool want_jump = false;

                if (!ImGui::GetIO().WantCaptureKeyboard)
                {
                    float fwd = 0.0F;
                    float rgt = 0.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_UpArrow)    || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_W))) fwd += 1.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_DownArrow)  || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_S))) fwd -= 1.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_RightArrow) || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_D))) rgt += 1.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)  || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_A))) rgt -= 1.0F;
                    if (std::abs(fwd) > 0.01F || std::abs(rgt) > 0.01F)
                    {
                        // Human manual input overrides autopilot
                        w.autopilot_play_active = false;
                        cd::math::Vec3f cam_fwd = s.cam.target - s.cam.eye;
                        cam_fwd.y = 0.0F;
                        const float fl = cd::math::length(cam_fwd);
                        if (fl > 1e-4F) cam_fwd = cam_fwd * (1.0F / fl);
                        else cam_fwd = { 0.0F, 0.0F, -1.0F };
                        const cd::math::Vec3f cam_rgt { -cam_fwd.z, 0.0F, cam_fwd.x };
                        cd::math::Vec3f wish_dir = cam_fwd * fwd + cam_rgt * rgt;
                        const float wl = cd::math::length(wish_dir);
                        if (wl > 1e-4F) wish_dir = wish_dir * (1.0F / wl);
                        horiz_move_delta = wish_dir * (cc->move_speed * clamped_dt);
                    }
                    want_jump = ImGui::IsKeyPressed(ImGuiKey_PageUp, false) ||
                                (s.pie_playing && ImGui::IsKeyPressed(ImGuiKey_Space, false));
                }

                // Autonomous Gameplay Driver (`--autopilot` or `[Auto-Play (P)]` UI toggle)
                if (s.pie_playing && w.autopilot_play_active && !w.level_goal_reached &&
                    cd::math::length(horiz_move_delta) < 1e-5F)
                {
                    // Check if any enemy bot or hazard barrier is active and fire plasma cannonball
                    for (std::size_t ti = 0; ti < s.entities.size(); ++ti)
                    {
                        if (ti == idx || !s.entities[ti].visible) continue;
                        const auto& t_ms = w.entity_modifiers[s.entities[ti].handle.id];
                        const auto* pat  = t_ms.template find<PatrolWaypointModifier>();
                        const auto* trg  = t_ms.template find<TriggerVolumeModifier>();
                        const bool active_threat = (pat && pat->enabled) ||
                            (trg && trg->enabled && trg->action == TriggerActionKind::kHazardDamage);
                        if (active_threat && w.autopilot_fire_cooldown <= 0.0F)
                        {
                            fire_autopilot_shot       = true;
                            w.autopilot_fire_cooldown = 0.42F;
                            break;
                        }
                    }

                    // Select next objective: nearest uncollected CollectibleScore core, then LevelGoalWin beacon
                    int   best_obj_idx  = -1;
                    float best_obj_dist = 1e9F;
                    for (std::size_t ti = 0; ti < s.entities.size(); ++ti)
                    {
                        if (ti == idx || !s.entities[ti].visible) continue;
                        const auto* trg = w.entity_modifiers[s.entities[ti].handle.id].template find<TriggerVolumeModifier>();
                        if (!trg || !trg->enabled || trg->triggered) continue;
                        if (trg->action != TriggerActionKind::kCollectibleScore) continue;
                        if (const auto* t_lt = s.scene.local(s.entities[ti].handle))
                        {
                            const float d = cd::math::length(t_lt->value.position - lt->value.position);
                            if (d < best_obj_dist)
                            {
                                best_obj_dist = d;
                                best_obj_idx  = static_cast<int>(ti);
                            }
                        }
                    }
                    if (best_obj_idx < 0)
                    {
                        for (std::size_t ti = 0; ti < s.entities.size(); ++ti)
                        {
                            if (ti == idx || !s.entities[ti].visible) continue;
                            const auto* trg = w.entity_modifiers[s.entities[ti].handle.id].template find<TriggerVolumeModifier>();
                            if (!trg || !trg->enabled || trg->triggered) continue;
                            if (trg->action == TriggerActionKind::kLevelGoalWin)
                            {
                                best_obj_idx = static_cast<int>(ti);
                                break;
                            }
                        }
                    }

                    if (best_obj_idx >= 0)
                    {
                        if (const auto* obj_lt = s.scene.local(s.entities[static_cast<std::size_t>(best_obj_idx)].handle))
                        {
                            cd::math::Vec3f to_obj = obj_lt->value.position - lt->value.position;
                            const float dy = to_obj.y;
                            to_obj.y = 0.0F;
                            const float hdist = cd::math::length(to_obj);
                            if (hdist > 0.05F)
                            {
                                const cd::math::Vec3f dir = to_obj * (1.0F / hdist);
                                const float step_len = std::min(hdist, cc->move_speed * 1.15F * clamped_dt);
                                horiz_move_delta = dir * step_len;
                            }
                            if (dy > 0.55F && hdist < 2.2F)
                            {
                                want_jump = true;
                            }
                        }
                    }
                }

                if (cd::math::length(horiz_move_delta) > 1e-5F)
                {
                    lt->value.position   = lt->value.position + horiz_move_delta;
                    w.player_horiz_speed = cd::math::length(horiz_move_delta) / std::max(1e-4F, clamped_dt);

                    // Smoothly rotate character to face movement velocity direction
                    const float target_yaw_deg = std::atan2(horiz_move_delta.x, horiz_move_delta.z) * 57.2957795F;
                    float diff = target_yaw_deg - w.player_heading_yaw_deg;
                    while (diff >  180.0F) diff -= 360.0F;
                    while (diff < -180.0F) diff += 360.0F;
                    w.player_heading_yaw_deg += diff * std::clamp(clamped_dt * 14.0F, 0.1F, 1.0F);

                    if (ent.kind == PrimitiveKind::kGltf)
                    {
                        const cd::math::Quatf q_yaw     = euler_deg_to_quat({ 0.0F, w.player_heading_yaw_deg, 0.0F });
                        const cd::math::Quatf q_upright = euler_deg_to_quat({ -90.0F, 0.0F, 0.0F });
                        lt->value.rotation = cd::math::normalize(q_yaw * q_upright);
                    }
                    else
                    {
                        lt->value.rotation = euler_deg_to_quat({ 0.0F, w.player_heading_yaw_deg, 0.0F });
                    }
                    s.scene.update_transforms();
                }

                if (want_jump)
                {
                    if (auto* rb = ms.template find<RigidBodyModifier>())
                    {
                        if (rb->is_grounded || std::abs(rb->linear_velocity.y) < 0.85F)
                        {
                            rb->linear_velocity.y = cc->jump_impulse;
                            rb->is_grounded       = false;
                        }
                    }
                }

                // Respawn player if fallen into abyss (y < -4.0m)
                if (lt->value.position.y < -4.0F)
                {
                    if (auto it = w.home_transforms.find(ent.handle.id); it != w.home_transforms.end())
                        lt->value = it->second;
                    else
                        lt->value.position = { 0.0F, 1.5F, 0.0F };
                    if (auto* rb = ms.template find<RigidBodyModifier>())
                        rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
                    w.player_hp = std::max(15.0F, w.player_hp - 20.0F);
                    w.hud_toast_message = "Fell out of bounds! Respawned at Checkpoint (-20 HP)";
                    w.hud_toast_timer   = 2.5F;
                    s.scene.update_transforms();
                }
                player_pos = lt->value.position;
            }
        }

        // 2. KinematicMoverModifier (Oscillating/Rotating Platform & Rider Carry)
        if (auto* km = ms.template find<KinematicMoverModifier>();
            km && km->enabled && sim_active && ent.visible && !ent.locked &&
            !(s.gizmo.is_dragging() && std::cmp_equal(s.selected, idx)))
        {
            if (auto* lt = s.scene.local(ent.handle))
            {
                if (!w.home_transforms.contains(ent.handle.id))
                    record_entity_home_transform(s, ent.handle);
                const cd::math::Transformf base_tr = w.home_transforms[ent.handle.id];
                const float phase = w.sim_time_accum * km->frequency_hz * 6.2831853F + km->phase_offset;
                const cd::math::Vec3f prev_pos = lt->value.position;
                if (km->amplitude > 1e-4F)
                {
                    lt->value.position = base_tr.position + km->move_axis * (km->amplitude * std::sin(phase));
                }
                km->last_frame_delta = lt->value.position - prev_pos;

                if (std::abs(km->angular_deg_per_sec.x) > 0.01F ||
                    std::abs(km->angular_deg_per_sec.y) > 0.01F ||
                    std::abs(km->angular_deg_per_sec.z) > 0.01F)
                {
                    const cd::math::Vec3f base_eul = w.euler_cache_deg[ent.handle.id];
                    const cd::math::Vec3f cur_eul  = base_eul + km->angular_deg_per_sec * w.sim_time_accum;
                    lt->value.rotation = euler_deg_to_quat(cur_eul);
                }

                // Carry standing riders on top of this kinematic platform
                if (km->carry_riders && cd::math::length(km->last_frame_delta) > 1e-6F)
                {
                    const auto* plat_col = ms.find_collider();
                    const cd::physics::Aabb plat_box = compute_entity_world_aabb(lt->value, plat_col);
                    for (std::size_t ri = 0; ri < s.entities.size(); ++ri)
                    {
                        if (ri == idx) continue;
                        auto& r_ent = s.entities[ri];
                        auto& r_ms  = w.entity_modifiers[r_ent.handle.id];
                        const auto* r_rb = r_ms.template find<RigidBodyModifier>();
                        if (!r_rb || !r_rb->enabled || r_rb->body_type != cd::physics::BodyType::kDynamic)
                            continue;
                        if (auto* r_lt = s.scene.local(r_ent.handle))
                        {
                            const auto* r_col = r_ms.find_collider();
                            const cd::physics::Aabb r_box = compute_entity_world_aabb(r_lt->value, r_col);
                            const bool horiz_overlap =
                                (r_box.max.x > plat_box.min.x && r_box.min.x < plat_box.max.x &&
                                 r_box.max.z > plat_box.min.z && r_box.min.z < plat_box.max.z);
                            const bool standing_on_top =
                                (std::abs(r_box.min.y - plat_box.max.y) < 0.28F);
                            if (horiz_overlap && standing_on_top)
                            {
                                r_lt->value.position = r_lt->value.position + km->last_frame_delta;
                                if (static_cast<int>(ri) == player_entity_idx)
                                    player_pos = r_lt->value.position;
                            }
                        }
                    }
                }
            }
        }
    }

    if (fire_autopilot_shot)
    {
        shoot_physics_ball_from_camera(s, log_push);
        has_active_dynamic_rb = true;
    }

    // Second pass for AI Waypoint Patrol, Projectile Combat & Gameplay Trigger Volumes
    if (sim_active)
    {
        for (std::size_t idx = 0; idx < s.entities.size(); ++idx)
        {
            auto&          ent = s.entities[idx];
            if (!ent.visible || ent.locked) continue;
            ModifierStack& ms  = w.entity_modifiers[ent.handle.id];
            auto*          lt  = s.scene.local(ent.handle);
            if (!lt) continue;

            auto* pat = ms.template find<PatrolWaypointModifier>();
            auto* trg = ms.template find<TriggerVolumeModifier>();

            // Projectile-vs-Enemy & Hazard Combat Check (`CannonBall_*` hits)
            const bool is_combat_target = (std::cmp_not_equal(idx, player_entity_idx)) &&
                                          ((pat && pat->enabled) ||
                                           (trg && trg->enabled && trg->action == TriggerActionKind::kHazardDamage));
            if (is_combat_target)
            {
                const cd::math::Vec3f target_center = lt->value.position + cd::math::Vec3f { 0.0F, 0.55F, 0.0F };
                for (std::size_t bi = 0; bi < s.entities.size(); ++bi)
                {
                    if (bi == idx || !s.entities[bi].visible) continue;
                    if (s.entities[bi].name.find("CannonBall_") != 0) continue;
                    if (auto* b_lt = s.scene.local(s.entities[bi].handle))
                    {
                        if (cd::math::length(b_lt->value.position - target_center) <= 1.25F)
                        {
                            if (pat) pat->enabled = false;
                            if (trg) { trg->enabled = false; trg->triggered = true; }
                            ent.visible            = false;
                            s.entities[bi].visible = false;
                            if (auto* b_rb = w.entity_modifiers[s.entities[bi].handle.id].template find<RigidBodyModifier>())
                                b_rb->enabled = false;
                            const int pts = (pat != nullptr) ? 500 : 250;
                            w.game_score += pts;
                            ++w.defeated_enemies;
                            w.hud_toast_message = "Direct Hit! Neutralized " + ent.name + "! (+" +
                                                  std::to_string(pts) + " Score)";
                            w.hud_toast_timer   = 3.0F;
                            log_push("[combat] Neutralized " + ent.name + " with Plasma CannonBall (+" +
                                     std::to_string(pts) + " Score)");
                            break;
                        }
                    }
                }
                if (!ent.visible) continue;
            }

            // 3. PatrolWaypointModifier (Autonomous AI Patrol & Player Pursuit)
            if (pat && pat->enabled && std::cmp_not_equal(idx, player_entity_idx) &&
                !(s.gizmo.is_dragging() && std::cmp_equal(s.selected, idx)))
            {
                if (!w.home_transforms.contains(ent.handle.id))
                    record_entity_home_transform(s, ent.handle);
                const cd::math::Vec3f home_p = w.home_transforms[ent.handle.id].position;
                bool chased = false;
                if (s.pie_playing && pat->chase_player && player_entity_idx >= 0 && std::cmp_not_equal(idx, player_entity_idx))
                {
                    cd::math::Vec3f to_p = player_pos - lt->value.position;
                    to_p.y = 0.0F;
                    const float dist = cd::math::length(to_p);
                    if (dist > 0.35F && dist <= pat->chase_range)
                    {
                        lt->value.position = lt->value.position + (to_p * (1.0F / dist)) * (pat->move_speed * clamped_dt);
                        chased = true;
                    }
                }
                if (!chased)
                {
                    const int ax = std::clamp(pat->patrol_axis, 0, 2);
                    float* coord = (ax == 0) ? &lt->value.position.x : ((ax == 1) ? &lt->value.position.y : &lt->value.position.z);
                    const float home_c = (ax == 0) ? home_p.x : ((ax == 1) ? home_p.y : home_p.z);
                    *coord += pat->direction_sign * pat->move_speed * clamped_dt;
                    if (*coord > home_c + pat->patrol_span) { *coord = home_c + pat->patrol_span; pat->direction_sign = -1.0F; }
                    if (*coord < home_c - pat->patrol_span) { *coord = home_c - pat->patrol_span; pat->direction_sign =  1.0F; }
                }
            }

            // 4. TriggerVolumeModifier (Collectible, JumpPad, Hazard, Heal, Goal Beacon)
            if (trg && trg->enabled)
            {
                if (trg->cooldown_sec > 0.0F)
                    trg->cooldown_sec = std::max(0.0F, trg->cooldown_sec - clamped_dt);

                // Gently spin collectible cores and goal beacons so they look alive!
                if (trg->action == TriggerActionKind::kCollectibleScore ||
                    trg->action == TriggerActionKind::kLevelGoalWin)
                {
                    const cd::math::Vec3f base_eul = w.euler_cache_deg[ent.handle.id];
                    lt->value.rotation = euler_deg_to_quat({
                        base_eul.x,
                        base_eul.y + w.sim_time_accum * 95.0F,
                        base_eul.z
                    });
                }

                const bool allow_trigger = s.pie_playing || (trg->action == TriggerActionKind::kJumpPadBoost);
                if (allow_trigger && !trg->triggered && trg->cooldown_sec <= 0.0F && player_entity_idx >= 0 &&
                    std::cmp_not_equal(idx, player_entity_idx))
                {
                    auto& p_ent = s.entities[static_cast<std::size_t>(player_entity_idx)];
                    auto* p_lt  = s.scene.local(p_ent.handle);
                    auto* p_rb  = w.entity_modifiers[p_ent.handle.id].template find<RigidBodyModifier>();
                    if (p_lt)
                    {
                        // Measure distance to closest point on player's upright capsule [feet..head]
                        const cd::math::Vec3f p_closest {
                            p_lt->value.position.x,
                            std::clamp(lt->value.position.y, p_lt->value.position.y, p_lt->value.position.y + 1.45F),
                            p_lt->value.position.z
                        };
                        const float dist = cd::math::length(p_closest - lt->value.position);
                        if (dist <= trg->trigger_radius + 0.28F)
                        {
                            switch (trg->action)
                            {
                                case TriggerActionKind::kCollectibleScore:
                                    w.game_score      += static_cast<int>(trg->effect_value);
                                    w.collected_cores += 1;
                                    w.player_energy    = std::min(100.0F, w.player_energy + 15.0F);
                                    w.hud_toast_message = "Collected Energy Core! (+" +
                                        std::to_string(static_cast<int>(trg->effect_value)) + " Score)";
                                    w.hud_toast_timer = 2.8F;
                                    if (trg->one_shot) { trg->triggered = true; ent.visible = false; }
                                    else trg->cooldown_sec = 1.0F;
                                    log_push("[gameplay] Collected " + ent.name + " -> Score=" + std::to_string(w.game_score));
                                    break;
                                case TriggerActionKind::kJumpPadBoost:
                                    if (p_rb)
                                    {
                                        p_rb->linear_velocity.y = std::max(p_rb->linear_velocity.y, trg->effect_value);
                                        p_rb->is_grounded       = false;
                                    }
                                    trg->cooldown_sec   = 0.35F;
                                    w.hud_toast_message = "Jump Pad Boost! (+" +
                                        std::to_string(static_cast<int>(trg->effect_value)) + " m/s Upward)";
                                    w.hud_toast_timer   = 2.0F;
                                    break;
                                case TriggerActionKind::kHazardDamage:
                                    if (w.player_shield > 0.0F)
                                    {
                                        const float absorb = std::min(w.player_shield, trg->effect_value);
                                        w.player_shield -= absorb;
                                        w.player_hp     -= (trg->effect_value - absorb);
                                    }
                                    else
                                    {
                                        w.player_hp -= trg->effect_value;
                                    }
                                    if (p_rb)
                                    {
                                        p_rb->linear_velocity.y = 5.2F;
                                        p_rb->linear_velocity.z += 3.5F;
                                    }
                                    trg->cooldown_sec   = 0.75F;
                                    w.hud_toast_message = "Hazard Hit! (-" +
                                        std::to_string(static_cast<int>(trg->effect_value)) + " Shield/HP)";
                                    w.hud_toast_timer   = 2.5F;
                                    if (w.player_hp <= 0.0F)
                                    {
                                        w.player_hp     = w.player_max_hp;
                                        w.player_shield = 100.0F;
                                        if (auto it = w.home_transforms.find(p_ent.handle.id); it != w.home_transforms.end())
                                            p_lt->value = it->second;
                                        w.hud_toast_message = "Shields Depleted - Respawned at Start Beacon!";
                                    }
                                    break;
                                case TriggerActionKind::kHealStation:
                                    w.player_hp         = std::min(w.player_max_hp, w.player_hp + trg->effect_value);
                                    w.player_shield     = 100.0F;
                                    w.hud_toast_message = "Nano-Repair Station: HP & Shield Restored!";
                                    w.hud_toast_timer   = 2.5F;
                                    if (trg->one_shot) { trg->triggered = true; ent.visible = false; }
                                    else trg->cooldown_sec = 2.0F;
                                    break;
                                case TriggerActionKind::kLevelGoalWin:
                                    w.level_goal_reached    = true;
                                    w.autopilot_play_active = false;
                                    trg->triggered          = true;
                                    w.game_score           += static_cast<int>(trg->effect_value);
                                    w.hud_toast_message     = "VICTORY! Level Goal Reached! Total Score: " +
                                        std::to_string(w.game_score);
                                    w.hud_toast_timer       = 8.0F;
                                    log_push("[gameplay] LEVEL COMPLETE! Final Score: " + std::to_string(w.game_score));
                                    break;
                            }
                        }
                    }
                }
            }
        }
        s.scene.update_transforms();
    }

    // ---- C. Step 3D RigidBody Physics & Narrowphase Collision Engine ----
    const bool run_physics = (s.pie_playing || w.simulate_physics_in_edit || w.step_single_frame) && has_active_dynamic_rb;
    if (!run_physics) return;

    const float phys_dt = w.step_single_frame ? (1.0F / 60.0F) : clamped_dt;
    w.step_single_frame = false;

    w.live_contacts.clear();
    for (auto& ent : s.entities)
    {
        if (auto* col = w.entity_modifiers[ent.handle.id].find_collider())
        {
            col->currently_colliding = false;
            col->active_contacts     = 0;
            col->last_penetration    = 0.0F;
        }
    }

    constexpr int kSubsteps = 4;
    const float sub_dt = phys_dt / static_cast<float>(kSubsteps);

    for (int step = 0; step < kSubsteps; ++step)
    {
        for (std::size_t i = 0; i < s.entities.size(); ++i)
        {
            const auto&    ent = s.entities[i];
            ModifierStack& ms  = w.entity_modifiers[ent.handle.id];
            if (s.gizmo.is_dragging() && std::cmp_equal(s.selected, i))
            {
                if (auto* rb = ms.template find<RigidBodyModifier>())
                    rb->linear_velocity = { 0.0F, 0.0F, 0.0F };
                record_entity_home_transform(s, ent.handle);
                continue;
            }
            if (s.scene.parent_of(ent.handle).is_valid()) continue;

            auto* rb = ms.template find<RigidBodyModifier>();
            if (!rb || !rb->enabled || rb->body_type != cd::physics::BodyType::kDynamic)
                continue;

            auto* lt = s.scene.local(ent.handle);
            if (!lt) continue;

            rb->is_grounded = false;
            if (rb->use_gravity)
            {
                rb->linear_velocity.y += (w.world_gravity_y * rb->gravity_scale) * sub_dt;
            }
            const float damp = std::clamp(1.0F - rb->linear_damping * sub_dt, 0.0F, 1.0F);
            rb->linear_velocity = rb->linear_velocity * damp;
            lt->value.position  = lt->value.position + rb->linear_velocity * sub_dt;
        }

        const std::size_t n = s.entities.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            auto&          ent_a = s.entities[i];
            if (s.scene.parent_of(ent_a.handle).is_valid()) continue;
            ModifierStack& ms_a  = w.entity_modifiers[ent_a.handle.id];
            auto*          col_a = ms_a.find_collider();
            if (!col_a) continue;
            auto*          lt_a  = s.scene.local(ent_a.handle);
            if (!lt_a) continue;
            auto*          rb_a  = ms_a.template find<RigidBodyModifier>();

            for (std::size_t j = i + 1; j < n; ++j)
            {
                auto&          ent_b = s.entities[j];
                if (s.scene.parent_of(ent_b.handle).is_valid()) continue;
                ModifierStack& ms_b  = w.entity_modifiers[ent_b.handle.id];
                auto*          col_b = ms_b.find_collider();
                if (!col_b) continue;
                auto*          lt_b  = s.scene.local(ent_b.handle);
                if (!lt_b) continue;
                auto*          rb_b  = ms_b.template find<RigidBodyModifier>();

                const bool dyn_a = (rb_a && rb_a->enabled && rb_a->body_type == cd::physics::BodyType::kDynamic &&
                                    !(s.gizmo.is_dragging() && std::cmp_equal(s.selected, i)));
                const bool dyn_b = (rb_b && rb_b->enabled && rb_b->body_type == cd::physics::BodyType::kDynamic &&
                                    !(s.gizmo.is_dragging() && std::cmp_equal(s.selected, j)));
                if (!dyn_a && !dyn_b) continue;

                cd::physics::ContactPoint cp {};
                if (!collide_narrowphase(lt_a->value, *col_a, lt_b->value, *col_b, cp))
                    continue;

                col_a->currently_colliding = true;
                col_b->currently_colliding = true;
                ++col_a->active_contacts;
                ++col_b->active_contacts;
                col_a->last_contact_point  = cp.world_position;
                col_b->last_contact_point  = cp.world_position;
                col_a->last_contact_normal = cp.normal;
                col_b->last_contact_normal = cp.normal * -1.0F;
                col_a->last_penetration    = std::max(col_a->last_penetration, cp.penetration);
                col_b->last_penetration    = std::max(col_b->last_penetration, cp.penetration);

                if (step == kSubsteps - 1 && w.live_contacts.size() < 64)
                {
                    w.live_contacts.push_back({ cp.world_position, cp.normal, cp.penetration });
                }

                if (col_a->is_trigger || col_b->is_trigger) continue;

                const float inv_ma = dyn_a ? (1.0F / std::max(0.1F, rb_a->mass)) : 0.0F;
                const float inv_mb = dyn_b ? (1.0F / std::max(0.1F, rb_b->mass)) : 0.0F;
                const float inv_sum = inv_ma + inv_mb;
                if (inv_sum <= 1e-6F) continue;

                constexpr float kSlop    = 0.001F;
                constexpr float kPercent = 0.85F;
                const float corr_mag = std::max(0.0F, cp.penetration - kSlop) / inv_sum * kPercent;
                const cd::math::Vec3f corr = cp.normal * corr_mag;
                if (dyn_a) lt_a->value.position = lt_a->value.position + corr * inv_ma;
                if (dyn_b) lt_b->value.position = lt_b->value.position - corr * inv_mb;

                const cd::math::Vec3f va = dyn_a ? rb_a->linear_velocity : cd::math::Vec3f { 0.0F, 0.0F, 0.0F };
                const cd::math::Vec3f vb = dyn_b ? rb_b->linear_velocity : cd::math::Vec3f { 0.0F, 0.0F, 0.0F };
                const cd::math::Vec3f rv = va - vb;
                const float vn = rv.x * cp.normal.x + rv.y * cp.normal.y + rv.z * cp.normal.z;

                if (cp.normal.y > 0.45F && dyn_a) rb_a->is_grounded = true;
                if (cp.normal.y < -0.45F && dyn_b) rb_b->is_grounded = true;

                if (vn < 0.0F)
                {
                    const float ea = rb_a ? rb_a->restitution : 0.3F;
                    const float eb = rb_b ? rb_b->restitution : 0.3F;
                    const float e = (vn < -0.60F) ? std::max(ea, eb) : 0.0F;
                    const float j_imp = -(1.0F + e) * vn / inv_sum;
                    const cd::math::Vec3f impulse = cp.normal * j_imp;
                    if (dyn_a) rb_a->linear_velocity = rb_a->linear_velocity + impulse * inv_ma;
                    if (dyn_b) rb_b->linear_velocity = rb_b->linear_velocity - impulse * inv_mb;

                    const cd::math::Vec3f tangent_v = rv - cp.normal * vn;
                    const float tv_len = std::sqrt(tangent_v.x * tangent_v.x +
                                                   tangent_v.y * tangent_v.y +
                                                   tangent_v.z * tangent_v.z);
                    if (tv_len > 1e-4F)
                    {
                        const cd::math::Vec3f t_dir = tangent_v * (1.0F / tv_len);
                        const float mu_a = rb_a ? rb_a->friction : 0.5F;
                        const float mu_b = rb_b ? rb_b->friction : 0.5F;
                        const float mu   = std::sqrt(mu_a * mu_b);
                        const float jt   = std::clamp(-tv_len / inv_sum, -j_imp * mu, j_imp * mu);
                        const cd::math::Vec3f f_imp = t_dir * jt;
                        if (dyn_a) rb_a->linear_velocity = rb_a->linear_velocity + f_imp * inv_ma;
                        if (dyn_b) rb_b->linear_velocity = rb_b->linear_velocity - f_imp * inv_mb;
                    }
                }
            }
        }
    }

    // ---- D. 3rd-Person Spring Follow Camera in PIE Mode (Tracks X, Y, Z + Jumps/Platforms) ----
    if (s.pie_playing && player_entity_idx >= 0 && std::cmp_less(player_entity_idx, s.entities.size()))
    {
        if (const auto* p_lt = s.scene.local(s.entities[static_cast<std::size_t>(player_entity_idx)].handle))
        {
            const cd::math::Vec3f cam_rel        = s.cam.eye - s.cam.target;
            const cd::math::Vec3f desired_target = p_lt->value.position + cd::math::Vec3f { 0.0F, 0.95F, 0.0F };
            const float           follow_alpha   = std::clamp(clamped_dt * 10.5F, 0.08F, 1.0F);
            s.cam.target = s.cam.target + (desired_target - s.cam.target) * follow_alpha;
            s.cam.eye    = s.cam.target + cam_rel;
        }
    }

    s.scene.update_transforms();
}

// =============================================================================
// 10. 3D Viewport Raycasting & Oriented Wireframe Collider / Contact Renderer
// =============================================================================
[[nodiscard]] inline bool project_world_to_screen(
    const cd::math::Vec3f& world_pt,
    const cd::math::Mat4f& vp,
    float                  screen_w,
    float                  screen_h,
    ImVec2&                out_px) noexcept
{
    const cd::math::Vec4f clip = vp * cd::math::Vec4f { world_pt.x, world_pt.y, world_pt.z, 1.0F };
    if (clip.w <= 0.25F) return false;
    const float inv_w = 1.0F / clip.w;
    const float ndc_x = clip.x * inv_w;
    const float ndc_y = clip.y * inv_w;
    if (ndc_x < -4.0F || ndc_x > 4.0F || ndc_y < -4.0F || ndc_y > 4.0F) return false;
    out_px.x = (ndc_x * 0.5F + 0.5F) * screen_w;
    out_px.y = (0.5F - ndc_y * 0.5F) * screen_h;
    return true;
}

inline void draw_3d_line_clipped(
    ImDrawList*            dl,
    const cd::math::Vec3f& a,
    const cd::math::Vec3f& b,
    const cd::math::Mat4f& vp,
    float                  sw,
    float                  sh,
    ImU32                  col,
    float                  thickness = 1.8F) noexcept
{
    ImVec2 pa {}, pb {};
    if (project_world_to_screen(a, vp, sw, sh, pa) &&
        project_world_to_screen(b, vp, sw, sh, pb))
    {
        dl->AddLine(pa, pb, col, thickness);
    }
}

inline void draw_wireframe_aabb_3d(
    ImDrawList*              dl,
    const cd::physics::Aabb& box,
    const cd::math::Mat4f&   vp,
    float                    sw,
    float                    sh,
    ImU32                    col,
    float                    thickness = 1.8F) noexcept
{
    const std::array<cd::math::Vec3f, 8> c {{
        { box.min.x, box.min.y, box.min.z },
        { box.max.x, box.min.y, box.min.z },
        { box.max.x, box.min.y, box.max.z },
        { box.min.x, box.min.y, box.max.z },
        { box.min.x, box.max.y, box.min.z },
        { box.max.x, box.max.y, box.min.z },
        { box.max.x, box.max.y, box.max.z },
        { box.min.x, box.max.y, box.max.z },
    }};
    constexpr std::size_t edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7},
    };
    for (const auto& e : edges)
    {
        draw_3d_line_clipped(dl, c[e[0]], c[e[1]], vp, sw, sh, col, thickness);
    }
}

/// Draw an Oriented Bounding Box (OBB) wireframe so rotated Ramps & Walls show exact 3D edges!
inline void draw_wireframe_obb_3d(
    ImDrawList*                 dl,
    const cd::math::Transformf& tr,
    const IColliderModifier*    col_mod,
    const cd::math::Mat4f&      vp,
    float                       sw,
    float                       sh,
    ImU32                       col,
    float                       thickness = 1.8F) noexcept
{
    if (col_mod != nullptr && (col_mod->shape_kind() == ColliderShapeKind::kSphere ||
                               col_mod->shape_kind() == ColliderShapeKind::kCapsule))
    {
        draw_wireframe_aabb_3d(dl, compute_entity_world_aabb(tr, col_mod), vp, sw, sh, col, thickness);
        return;
    }
    const cd::math::Quatf q      = cd::math::normalize(tr.rotation);
    const cd::math::Vec3f half   = compute_collider_local_half_extents(tr, col_mod);
    const cd::math::Vec3f offset = (col_mod != nullptr) ? col_mod->center_offset : cd::math::Vec3f { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f center = tr.position + cd::math::rotate(q, offset);

    const std::array<cd::math::Vec3f, 8> lc {{
        { -half.x, -half.y, -half.z },
        {  half.x, -half.y, -half.z },
        {  half.x, -half.y,  half.z },
        { -half.x, -half.y,  half.z },
        { -half.x,  half.y, -half.z },
        {  half.x,  half.y, -half.z },
        {  half.x,  half.y,  half.z },
        { -half.x,  half.y,  half.z },
    }};
    std::array<cd::math::Vec3f, 8> wc {};
    for (std::size_t i = 0; i < 8; ++i)
    {
        wc[i] = center + cd::math::rotate(q, lc[i]);
    }
    constexpr std::size_t edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7},
    };
    for (const auto& e : edges)
    {
        draw_3d_line_clipped(dl, wc[e[0]], wc[e[1]], vp, sw, sh, col, thickness);
    }
}

inline void draw_wireframe_sphere_3d(
    ImDrawList*            dl,
    cd::math::Vec3f        center,
    float                  radius,
    const cd::math::Mat4f& vp,
    float                  sw,
    float                  sh,
    ImU32                  col,
    float                  thickness = 1.8F) noexcept
{
    constexpr int kSegs = 20;
    for (int ring = 0; ring < 3; ++ring)
    {
        for (int i = 0; i < kSegs; ++i)
        {
            const float a0 = (static_cast<float>(i) / kSegs) * 6.2831853F;
            const float a1 = (static_cast<float>(i + 1) / kSegs) * 6.2831853F;
            cd::math::Vec3f p0 = center;
            cd::math::Vec3f p1 = center;
            if (ring == 0)
            {
                p0.x += std::cos(a0) * radius; p0.z += std::sin(a0) * radius;
                p1.x += std::cos(a1) * radius; p1.z += std::sin(a1) * radius;
            }
            else if (ring == 1)
            {
                p0.x += std::cos(a0) * radius; p0.y += std::sin(a0) * radius;
                p1.x += std::cos(a1) * radius; p1.y += std::sin(a1) * radius;
            }
            else
            {
                p0.y += std::cos(a0) * radius; p0.z += std::sin(a0) * radius;
                p1.y += std::cos(a1) * radius; p1.z += std::sin(a1) * radius;
            }
            draw_3d_line_clipped(dl, p0, p1, vp, sw, sh, col, thickness);
        }
    }
}

template <typename StateT>
[[nodiscard]] inline cd::math::Vec3f unproject_mouse_to_ground(
    const StateT& s,
    ImVec2        mouse_px,
    float         screen_w,
    float         screen_h,
    float         ground_y = 0.0F) noexcept
{
    const float nx = (mouse_px.x / std::max(1.0F, screen_w)) * 2.0F - 1.0F;
    const float ny = 1.0F - (mouse_px.y / std::max(1.0F, screen_h)) * 2.0F;

    cd::math::Vec3f fwd = s.cam.target - s.cam.eye;
    const float flen = cd::math::length(fwd);
    if (flen > 1e-5F) fwd = fwd * (1.0F / flen);
    else fwd = { 0.0F, 0.0F, -1.0F };

    cd::math::Vec3f right = cd::math::cross(fwd, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
    const float rlen = cd::math::length(right);
    if (rlen > 1e-5F) right = right * (1.0F / rlen);
    else right = { 1.0F, 0.0F, 0.0F };

    const cd::math::Vec3f up = cd::math::cross(right, fwd);
    const float aspect = screen_w / std::max(1.0F, screen_h);
    const float tan_half_fov = std::tan(s.cam.fov_y * 0.5F);

    cd::math::Vec3f ray_dir =
        fwd + right * (nx * aspect * tan_half_fov) + up * (ny * tan_half_fov);
    const float dlen = cd::math::length(ray_dir);
    if (dlen > 1e-5F) ray_dir = ray_dir * (1.0F / dlen);

    if (std::abs(ray_dir.y) > 1e-4F)
    {
        const float t = (ground_y - s.cam.eye.y) / ray_dir.y;
        if (t > 0.2F && t < 45.0F)
        {
            const cd::math::Vec3f hit = s.cam.eye + ray_dir * t;
            return snap_vec3_if_enabled({
                std::clamp(hit.x, -8.5F, 8.5F),
                ground_y,
                std::clamp(hit.z, -8.5F, 8.5F),
            });
        }
    }
    const cd::math::Vec3f fallback = s.cam.eye + ray_dir * 5.0F;
    return snap_vec3_if_enabled({
        std::clamp(fallback.x, -8.5F, 8.5F),
        std::max(ground_y, fallback.y),
        std::clamp(fallback.z, -8.5F, 8.5F),
    });
}

template <typename StateT>
[[nodiscard]] inline int pick_entity_under_mouse(
    const StateT&          s,
    const cd::math::Mat4f& vp,
    ImVec2                 mouse_px,
    float                  screen_w,
    float                  screen_h) noexcept
{
    int   best_idx  = -1;
    float best_dist = 85.0F;
    for (std::size_t i = 0; i < s.entities.size(); ++i)
    {
        const auto& ent = s.entities[i];
        if (!ent.visible || ent.locked) continue;
        if (ent.name.find("Ground_Floor") != std::string::npos) continue;
        if (const auto* lt = s.scene.local(ent.handle))
        {
            ImVec2 sp {};
            if (project_world_to_screen(lt->value.position, vp, screen_w, screen_h, sp))
            {
                const float dx = sp.x - mouse_px.x;
                const float dy = sp.y - mouse_px.y;
                const float d  = std::sqrt(dx * dx + dy * dy);
                if (d < best_dist)
                {
                    best_dist = d;
                    best_idx  = static_cast<int>(i);
                }
            }
        }
    }
    return best_idx;
}

/// Draw 3D Collider Wireframes, Active Contacts, Drag-and-Drop Previews, Floating Viewport HUD Toolbar, and Right-Click Context Menu!
template <typename StateT, typename LogFn>
inline void draw_studio_viewport_overlays_and_drop_targets(
    StateT&                s,
    const cd::math::Mat4f& vp,
    std::uint32_t          width,
    std::uint32_t          height,
    LogFn&&                log_push)
{
    auto& w  = studio_world();
    auto* dl = ImGui::GetBackgroundDrawList();
    const float sw = static_cast<float>(width);
    const float sh = static_cast<float>(height);

    // Keyboard shortcuts when typing is not active
    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) s.gizmo_state.mode = GizmoMode::kTranslate;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) s.gizmo_state.mode = GizmoMode::kRotate;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) s.gizmo_state.mode = GizmoMode::kScale;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) snap_selected_entity_to_ground(s, log_push);
        if (ImGui::IsKeyPressed(ImGuiKey_B, false)) shoot_physics_ball_from_camera(s, log_push);
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) reset_physics_to_home_positions(s, log_push);
        if (!s.mod_ctrl && !s.mod_shift && ImGui::IsKeyPressed(ImGuiKey_P, false))
        {
            if (!s.pie_playing) enter_pie_play_mode(s, log_push);
            w.autopilot_play_active = !w.autopilot_play_active;
            w.hud_toast_message = w.autopilot_play_active
                ? "Autopilot AI Driver ENGAGED - Playing Level Automatically!"
                : "Autopilot AI Driver OFF - Manual WASD/Space Control";
            w.hud_toast_timer = 3.0F;
        }
    }
    if (s.pie_playing && !ImGui::GetIO().WantCaptureMouse &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetMousePos().y > 64.0F)
    {
        shoot_physics_ball_from_camera(s, log_push);
    }

    // 1. Draw 3D Collider Wireframes (OBB & Sphere) + Trigger Volume Sensor Radii + Contact Manifolds
    if (w.show_all_collider_wires && !s.pie_playing)
    {
        for (std::size_t ei = 0; ei < s.entities.size(); ++ei)
        {
            const auto& ent = s.entities[ei];
            if (!ent.visible) continue;
            if (ent.name.find("Ground_Floor") != std::string::npos && !std::cmp_equal(s.selected, ei))
                continue;
            const auto* lt  = s.scene.local(ent.handle);
            if (!lt) continue;
            auto& ms = w.entity_modifiers[ent.handle.id];
            if (const auto* trg = ms.template find<TriggerVolumeModifier>(); trg && trg->enabled)
            {
                draw_wireframe_sphere_3d(dl, lt->value.position, trg->trigger_radius,
                                         vp, sw, sh, IM_COL32(255, 200, 45, 200), 1.6F);
            }
            const auto* col = ms.find_collider();
            if (!col || !col->show_wireframe) continue;

            const ImU32 wire_color = col->currently_colliding
                ? IM_COL32(255, 72, 42, 240)
                : IM_COL32(45, 235, 95, 195);
            const float thick = col->currently_colliding ? 2.6F : 1.6F;

            if (col->shape_kind() == ColliderShapeKind::kSphere)
            {
                const auto* sc = static_cast<const SphereColliderModifier*>(col);
                const float r  = sc->radius * std::max({ std::abs(lt->value.scale.x),
                                                         std::abs(lt->value.scale.y),
                                                         std::abs(lt->value.scale.z) });
                draw_wireframe_sphere_3d(dl, lt->value.position + col->center_offset,
                                         r, vp, sw, sh, wire_color, thick);
            }
            else
            {
                draw_wireframe_obb_3d(dl, lt->value, col, vp, sw, sh, wire_color, thick);
            }
        }

        for (const auto& cp : w.live_contacts)
        {
            ImVec2 pt_px {};
            if (project_world_to_screen(cp.point, vp, sw, sh, pt_px))
            {
                dl->AddCircleFilled(pt_px, 5.5F, IM_COL32(255, 230, 40, 255));
                draw_3d_line_clipped(dl, cp.point, cp.point + cp.normal * 0.65F,
                                     vp, sw, sh, IM_COL32(255, 230, 40, 255), 2.5F);
            }
        }
    }

    // 2. Full-Width Frameless Studio Ribbon Toolbar (y = 25..54, bridging MainMenuBar and DockSpace)
    if (sw > 400.0F)
    {
        auto& cst = cd::ui::chroma::state();
        ImGui::SetNextWindowPos(ImVec2(0.0F, 25.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sw, 29.0F), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0F, 4.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 3.0F));
        if (ImGui::Begin("##studio_viewport_hud", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
        {
            // Group 1 (Left): [+ Create] + [New Empty] + Stage / Project Selector
            if (ImGui::IconButton("Create##rib_create", cd::ui::chroma::IconKind::kPlus,
                                  IM_COL32(95, 245, 175, 255), false, ImVec2(64.0F, 20.0F)))
            {
                cst.workspace_mode = 0;
                w.focus_bottom_node_catalog_tab = true;
                ImGui::OpenPopup("studio_viewport_context_menu");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spawn 3D Object / Open 250-Construct Catalog");

            ImGui::SameLine();
            if (ImGui::IconButton("New Empty##rib_empty", cd::ui::chroma::IconKind::kFileScene,
                                  IM_COL32(130, 210, 255, 255),
                                  w.active_project_name == "Empty_Project", ImVec2(82.0F, 20.0F)))
            {
                reset_to_clean_empty_project(s, log_push);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reset to Clean Empty Project (Ground + Studio Lighting, 0 Demo Objects)");

            ImGui::SameLine();
            std::string short_lvl = w.active_level_name;
            if (const auto paren = short_lvl.find(" ("); paren != std::string::npos)
                short_lvl = short_lvl.substr(0, paren);
            if (short_lvl.size() > 22U)
                short_lvl = short_lvl.substr(0, 20U) + "..";
            const std::string lvl_btn = short_lvl + "##ribbon_lvl";
            if (ImGui::IconButton(lvl_btn.c_str(), cd::ui::chroma::IconKind::kWorld,
                                  IM_COL32(105, 205, 255, 255), false, ImVec2(148.0F, 20.0F)))
            {
                ImGui::OpenPopup("ribbon_world_level_popup");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Active Project: %s | Stage: %s\n(Click to open Sample Projects from Disk or switch Stage)",
                                  w.active_project_name.c_str(), w.active_level_name.c_str());
            if (ImGui::BeginPopup("ribbon_world_level_popup"))
            {
                ImGui::TextColored(ImVec4(0.45F, 0.86F, 1.0F, 1.0F),
                                   "Active Project: %s (%s)",
                                   w.active_project_name.c_str(), w.active_level_name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("New Empty Project (Clean Stage)"))
                {
                    reset_to_clean_empty_project(s, log_push);
                }
                if (ImGui::BeginMenu("Open Sample Project from Disk (showcases/*.cdproj)"))
                {
                    for (std::size_t si = 0; si < kShowcaseCatalog.size(); ++si)
                    {
                        if (ImGui::MenuItem(kShowcaseCatalog[si].title))
                            load_showcase_project_by_index(s, si, log_push);
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                const auto* proj = s.cd_world.project();
                std::string deferred_popup_level {};
                if (proj)
                {
                    for (const auto& wname : proj->world_names())
                    {
                        if (ImGui::BeginMenu(wname.c_str()))
                        {
                            for (std::size_t li : proj->levels_in_world(wname))
                            {
                                const auto* lvl = proj->level(li);
                                if (!lvl) continue;
                                const std::string lvl_nm { lvl->name() };
                                const bool is_cur = (w.active_level_name == lvl_nm);
                                if (ImGui::MenuItem(lvl_nm.c_str(), nullptr, is_cur))
                                {
                                    deferred_popup_level = lvl_nm;
                                }
                            }
                            ImGui::EndMenu();
                        }
                    }
                }
                if (!deferred_popup_level.empty())
                {
                    (void)switch_to_project_level(s, deferred_popup_level, log_push);
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Group 2 (Transform Gizmo & Snap): Icon-driven Move / Rot / Scale | Snap / Floor
            const bool is_t = (s.gizmo_state.mode == GizmoMode::kTranslate);
            const bool is_r = (s.gizmo_state.mode == GizmoMode::kRotate);
            const bool is_s = (s.gizmo_state.mode == GizmoMode::kScale);
            if (ImGui::IconButton("Move##rib_t", cd::ui::chroma::IconKind::kTranslate,
                                  IM_COL32(255, 115, 115, 255), is_t, ImVec2(56.0F, 20.0F)))
            {
                s.gizmo_state.mode = GizmoMode::kTranslate;
            }
            ImGui::SameLine();
            if (ImGui::IconButton("Rot##rib_r", cd::ui::chroma::IconKind::kRotate,
                                  IM_COL32(115, 235, 145, 255), is_r, ImVec2(50.0F, 20.0F)))
            {
                s.gizmo_state.mode = GizmoMode::kRotate;
            }
            ImGui::SameLine();
            if (ImGui::IconButton("Scale##rib_s", cd::ui::chroma::IconKind::kScale,
                                  IM_COL32(115, 185, 255, 255), is_s, ImVec2(58.0F, 20.0F)))
            {
                s.gizmo_state.mode = GizmoMode::kScale;
            }

            ImGui::SameLine();
            ImGui::Checkbox("Snap", &w.grid_snap_enabled);
            ImGui::SameLine();
            if (ImGui::SmallButton("Floor(G)"))
                snap_selected_entity_to_ground(s, log_push);

            // Group 3 (Center Transport Deck — centered over the 3D Viewport)
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x < sw * 0.44F)
            {
                ImGui::SetCursorScreenPos(ImVec2(sw * 0.44F, ImGui::GetCursorScreenPos().y));
            }
            if (!s.pie_playing)
            {
                if (ImGui::IconButton("Play (F5)##rib_play", cd::ui::chroma::IconKind::kPlay,
                                      IM_COL32(85, 245, 155, 255), false, ImVec2(78.0F, 20.0F)))
                {
                    enter_pie_play_mode(s, log_push);
                }
            }
            else
            {
                if (ImGui::IconButton("Stop (Esc)##rib_stop", cd::ui::chroma::IconKind::kStop,
                                      IM_COL32(255, 105, 105, 255), true, ImVec2(82.0F, 20.0F)))
                {
                    exit_pie_play_mode(s, log_push);
                }
            }
            ImGui::SameLine();
            if (ImGui::IconButton(w.simulate_physics_in_edit ? "Sim ON##rib_sim" : "Sim OFF##rib_sim",
                                  cd::ui::chroma::IconKind::kPhysics,
                                  w.simulate_physics_in_edit ? IM_COL32(95, 235, 215, 255) : IM_COL32(245, 185, 95, 255),
                                  w.simulate_physics_in_edit, ImVec2(72.0F, 20.0F)))
            {
                w.simulate_physics_in_edit = !w.simulate_physics_in_edit;
            }
            ImGui::SameLine();
            if (ImGui::IconButton("Step##rib_step", cd::ui::chroma::IconKind::kStep,
                                  IM_COL32(180, 210, 250, 255), false, ImVec2(52.0F, 20.0F)))
            {
                w.simulate_physics_in_edit = false;
                w.step_single_frame        = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset(R)"))
                reset_physics_to_home_positions(s, log_push);
            ImGui::SameLine();
            if (ImGui::IconButton("Shoot(B)##rib_shoot", cd::ui::chroma::IconKind::kSphere,
                                  IM_COL32(255, 165, 95, 255), false, ImVec2(72.0F, 20.0F)))
            {
                shoot_physics_ball_from_camera(s, log_push);
            }
            ImGui::SameLine();
            if (ImGui::IconButton(w.autopilot_play_active ? "Bot ON##rib_bot" : "Bot(P)##rib_bot",
                                  cd::ui::chroma::IconKind::kAvatar,
                                  IM_COL32(115, 240, 175, 255), w.autopilot_play_active, ImVec2(66.0F, 20.0F)))
            {
                if (!s.pie_playing) enter_pie_play_mode(s, log_push);
                w.autopilot_play_active = !w.autopilot_play_active;
            }

            // Group 4 (Right): Workspace Mode Switcher + Theme Cycle
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x < sw - 348.0F)
            {
                ImGui::SetCursorScreenPos(ImVec2(sw - 348.0F, ImGui::GetCursorScreenPos().y));
            }
            const int cur_mode = cst.workspace_mode;
            ImGui::PushStyleColor(ImGuiCol_Button, (cur_mode == 0) ? ImVec4(0.15F, 0.52F, 0.91F, 1.0F) : ImVec4(0.13F, 0.16F, 0.22F, 1.0F));
            if (ImGui::SmallButton("3D Studio"))
            {
                cst.workspace_mode = 0;
                if (s.pie_playing) exit_pie_play_mode(s, log_push);
                ImGui::PushToast("Workspace Mode", "Switched to 3D Studio Workspace", 0, 2.0F);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, (cur_mode == 1) ? ImVec4(0.15F, 0.52F, 0.91F, 1.0F) : ImVec4(0.13F, 0.16F, 0.22F, 1.0F));
            if (ImGui::SmallButton("In-Game HUD"))
            {
                cst.workspace_mode = 1;
                ImGui::PushToast("Workspace Mode", "Switched to In-Game HUD & 3D World UI Mode", 0, 2.0F);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, (cur_mode == 2) ? ImVec4(0.15F, 0.52F, 0.91F, 1.0F) : ImVec4(0.13F, 0.16F, 0.22F, 1.0F));
            if (ImGui::SmallButton("Node & App Lab"))
            {
                cst.workspace_mode = 2;
                ImGui::PushToast("Workspace Mode", "Switched to Standalone Node & Curve Lab", 0, 2.0F);
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.20F, 0.27F, 1.0F));
            if (ImGui::SmallButton("Theme"))
            {
                ImGui::ApplyTheme((cst.theme_index + 1) % 5);
                ImGui::PushToast("Chroma Theme", cd::ui::chroma::theme_preset_name(cst.theme_index), 0, 1.8F);
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    // 2B. In-Viewport HUD Overlays (Camera & Shading Pill Bar, 3D Orientation Rose, Telemetry, Shortcut Hint Strip)
    if (sw > 600.0F && cd::ui::chroma::state().workspace_mode == 0 && !s.pie_playing)
    {
        auto& cst = cd::ui::chroma::state();
        const float vp_x0 = std::round(sw * cst.dock_left_w_ratio);
        const float vp_x1 = std::round(sw * (1.0F - cst.dock_right_w_ratio));
        const float vp_y0 = 54.0F;
        const float vp_y1 = std::round(54.0F + std::max(100.0F, sh - 54.0F) * (1.0F - cst.dock_bottom_h_ratio));
        const float vp_w  = vp_x1 - vp_x0;
        const float vp_h  = vp_y1 - vp_y0;

        if (vp_w > 340.0F && vp_h > 180.0F)
        {
            // Top-Left Viewport Camera & Display Pill Bar
            ImGui::SetNextWindowPos(ImVec2(vp_x0 + 10.0F, vp_y0 + 8.0F), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(412.0F, 26.0F), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.82F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0F, 3.0F));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 2.0F));
            if (ImGui::Begin("##vp_camera_pill_bar", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove))
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15F, 0.22F, 0.32F, 1.0F));
                if (ImGui::SmallButton("Persp"))
                    apply_camera_bookmark(s, { -7.5F, 2.2F, 1.2F }, { 0.0F, 1.5F, -0.2F }, "Perspective Overview", log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Iso"))
                    apply_camera_bookmark(s, { -5.8F, 3.8F, 6.2F }, { 0.0F, 0.8F, 0.0F }, "Iso View", log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Top"))
                    apply_camera_bookmark(s, { 0.0F, 11.5F, 0.01F }, { 0.0F, 0.0F, 0.0F }, "Top View", log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Front"))
                    apply_camera_bookmark(s, { 0.0F, 2.2F, 7.8F }, { 0.0F, 1.0F, 0.0F }, "Front View", log_push);
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, s.show_editor_floor
                    ? ImVec4(0.14F, 0.44F, 0.72F, 1.0F)
                    : ImVec4(0.14F, 0.17F, 0.23F, 1.0F));
                if (ImGui::SmallButton(s.show_editor_floor ? "Grid: ON" : "Grid: OFF"))
                    s.show_editor_floor = !s.show_editor_floor;
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, w.show_all_collider_wires
                    ? ImVec4(0.14F, 0.52F, 0.36F, 1.0F)
                    : ImVec4(0.14F, 0.17F, 0.23F, 1.0F));
                if (ImGui::SmallButton("Wire Colliders"))
                    w.show_all_collider_wires = !w.show_all_collider_wires;
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.26F, 0.38F, 1.0F));
                if (ImGui::SmallButton("Focus (F)"))
                    focus_selected(s, log_push);
                ImGui::PopStyleColor();
            }
            ImGui::End();
            ImGui::PopStyleVar(3);

            // Top-Right Telemetry Pill + Blender 4 / Source 2 3D Orientation Gizmo Rose
            const float fps = (w.smoothed_fps > 1.0F) ? w.smoothed_fps : 60.0F;
            const float ms  = (w.smoothed_frame_ms > 0.01F) ? w.smoothed_frame_ms : 16.6F;
            char tele_buf[96] {};
            std::snprintf(tele_buf, sizeof(tele_buf), "%.0f FPS (%.1f ms)  |  %zu Entities  |  RT+PBR",
                          static_cast<double>(fps), static_cast<double>(ms),
                          s.entities.size());
            const ImVec2 tele_sz = ImGui::CalcTextSize(tele_buf);
            const ImVec2 tp0 { vp_x1 - tele_sz.x - 24.0F, vp_y0 + 8.0F };
            const ImVec2 tp1 { vp_x1 - 8.0F, vp_y0 + 30.0F };
            dl->AddRectFilled(tp0, tp1, IM_COL32(14, 17, 24, 210), 5.0F);
            dl->AddRect(tp0, tp1, IM_COL32(46, 58, 78, 190), 5.0F, 0, 1.0F);
            dl->AddText(ImVec2(tp0.x + 8.0F, tp0.y + 4.0F), IM_COL32(190, 212, 238, 255), tele_buf);

            // 3D Orientation Rose projected by camera eye -> target basis
            const ImVec2 rose_c { vp_x1 - 42.0F, vp_y0 + 68.0F };
            const float  rose_r = 24.0F;
            dl->AddCircleFilled(rose_c, rose_r + 8.0F, IM_COL32(14, 17, 24, 185), 24);
            dl->AddCircle(rose_c, rose_r + 8.0F, IM_COL32(46, 58, 78, 180), 24, 1.0F);

            cd::math::Vec3f cam_fwd = s.cam.target - s.cam.eye;
            const float flen = cd::math::length(cam_fwd);
            if (flen > 1e-5F) cam_fwd = cam_fwd * (1.0F / flen);
            else cam_fwd = { 0.0F, 0.0F, -1.0F };

            cd::math::Vec3f cam_right = cd::math::cross(cam_fwd, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
            const float rlen = cd::math::length(cam_right);
            if (rlen > 1e-5F) cam_right = cam_right * (1.0F / rlen);
            else cam_right = { 1.0F, 0.0F, 0.0F };

            const cd::math::Vec3f cam_up = cd::math::cross(cam_right, cam_fwd);

            struct AxisDef
            {
                cd::math::Vec3f dir;
                const char*     label;
                ImU32           col;
                ImU32           neg_col;
            };
            const AxisDef axes[3] = {
                { { 1.0F, 0.0F, 0.0F }, "X", IM_COL32(235, 68, 68, 255),  IM_COL32(125, 42, 42, 165) },
                { { 0.0F, 1.0F, 0.0F }, "Y", IM_COL32(72, 208, 96, 255),  IM_COL32(42, 115, 56, 165) },
                { { 0.0F, 0.0F, 1.0F }, "Z", IM_COL32(62, 146, 250, 255), IM_COL32(38, 82, 140, 165) },
            };
            for (const auto& ax : axes)
            {
                const float px = cd::math::dot(ax.dir, cam_right) * rose_r;
                const float py = -cd::math::dot(ax.dir, cam_up) * rose_r;
                const ImVec2 pos_tip { rose_c.x + px, rose_c.y + py };
                const ImVec2 neg_tip { rose_c.x - px * 0.72F, rose_c.y - py * 0.72F };
                dl->AddLine(rose_c, neg_tip, ax.neg_col, 1.4F);
                dl->AddCircleFilled(neg_tip, 3.2F, ax.neg_col, 12);
                dl->AddLine(rose_c, pos_tip, ax.col, 2.2F);
                dl->AddCircleFilled(pos_tip, 6.8F, ax.col, 16);
                dl->AddText(ImVec2(pos_tip.x - 3.5F, pos_tip.y - 5.5F), IM_COL32(255, 255, 255, 255), ax.label, nullptr, 1.15F);
            }

            // Bottom-Left Viewport Shortcut Hint Strip
            const char* hint_str = "LMB: Select / Gizmo   |   RMB+WASD: Fly Cam   |   F: Focus   |   G: Snap Floor   |   Ctrl+D: Duplicate   |   F5: Play";
            const ImVec2 hsz = ImGui::CalcTextSize(hint_str);
            const ImVec2 hp0 { vp_x0 + 10.0F, vp_y1 - 26.0F };
            const ImVec2 hp1 { std::min(vp_x1 - 10.0F, hp0.x + hsz.x + 18.0F), vp_y1 - 6.0F };
            dl->AddRectFilled(hp0, hp1, IM_COL32(14, 17, 24, 195), 4.0F);
            dl->AddRect(hp0, hp1, IM_COL32(44, 54, 72, 165), 4.0F, 0, 1.0F);
            dl->AddText(ImVec2(hp0.x + 8.0F, hp0.y + 3.0F), IM_COL32(168, 184, 208, 245), hint_str);
        }
    }


    // 3. Right-Click 3D Viewport Context Menu (Quick Right-Click without camera drag)
    const ImVec2 mouse_px = ImGui::GetMousePos();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::GetIO().WantCaptureMouse)
    {
        w.rmb_down_pos = mouse_px;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::GetIO().WantCaptureMouse)
    {
        const float dx = mouse_px.x - w.rmb_down_pos.x;
        const float dy = mouse_px.y - w.rmb_down_pos.y;
        if (dx * dx + dy * dy < 36.0F)
        {
            w.context_menu_world_pos = unproject_mouse_to_ground(s, mouse_px, sw, sh, 0.0F);
            ImGui::OpenPopup("studio_viewport_context_menu");
        }
    }
    if (ImGui::BeginPopup("studio_viewport_context_menu"))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F),
                           "3D Viewport Action @ (%.1f, %.1f, %.1f)",
                           static_cast<double>(w.context_menu_world_pos.x),
                           static_cast<double>(w.context_menu_world_pos.y),
                           static_cast<double>(w.context_menu_world_pos.z));
        ImGui::Separator();
        if (ImGui::BeginMenu("+ Spawn 3D Object Here"))
        {
            if (ImGui::MenuItem("Dynamic Cube (RB + BoxCollider)"))
                (void)spawn_entity_with_modifiers(s, "Dynamic_Cube", w.context_menu_world_pos, log_push);
            if (ImGui::MenuItem("Bouncing Sphere (RB + SphereCollider)"))
                (void)spawn_entity_with_modifiers(s, "Dynamic_Sphere", w.context_menu_world_pos, log_push);
            if (ImGui::MenuItem("Inclined Physics Ramp (25 deg OBB)"))
                (void)spawn_entity_with_modifiers(s, "Static_Ramp", w.context_menu_world_pos, log_push);
            if (ImGui::MenuItem("Bouncing Trampoline Pad"))
                (void)spawn_entity_with_modifiers(s, "Trampoline_Pad", w.context_menu_world_pos, log_push);
            if (ImGui::MenuItem("5x Physics Crate Tower"))
                (void)spawn_entity_with_modifiers(s, "Prefab_CrateStack", w.context_menu_world_pos, log_push);
            if (ImGui::MenuItem("6x Dynamic Domino Row"))
                (void)spawn_entity_with_modifiers(s, "Prefab_DominoRow", w.context_menu_world_pos, log_push);
            if (ImGui::MenuItem("Static Wall Barrier"))
                (void)spawn_entity_with_modifiers(s, "Static_Wall", w.context_menu_world_pos, log_push);
            if (ImGui::MenuItem("Skinned Character (CesiumMan)"))
                (void)spawn_entity_with_modifiers(s, "Character_CesiumMan", w.context_menu_world_pos, log_push);
            ImGui::EndMenu();
        }
        if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
        {
            const auto& sel_ent = s.entities[static_cast<std::size_t>(s.selected)];
            ImGui::Separator();
            ImGui::TextDisabled("Selected: %s", sel_ent.name.c_str());
            if (ImGui::MenuItem("Focus Camera on Selected (F)"))
                focus_selected(s, log_push);
            if (ImGui::MenuItem("Duplicate with Modifiers (Ctrl+D)"))
                duplicate_entity_with_modifiers(s, s.selected, log_push);
            if (ImGui::MenuItem("Snap Bottom to Ground Floor (G)"))
                snap_selected_entity_to_ground(s, log_push);
            if (ImGui::BeginMenu("Attach Modifier Class to Selected"))
            {
                for (const auto& mdesc : kModifierCatalog)
                {
                    if (ImGui::MenuItem(mdesc.title))
                    {
                        if (auto mod = instantiate_modifier_by_class(mdesc.class_id))
                        {
                            if (mod->category() == ModifierCategory::kEnvironment)
                                w.environment_modifiers.add_or_replace(std::move(mod));
                            else
                                w.entity_modifiers[sel_ent.handle.id].add_or_replace(std::move(mod));
                            log_push(std::string("[modifier] Attached ") + mdesc.class_id);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Assign Mesh Asset to Selected"))
            {
                const char* meshes[] = {
                    "assets/meshes/Cube.mesh",
                    "assets/meshes/Sphere.mesh",
                    "assets/meshes/Cylinder.mesh",
                    "assets/meshes/Cone.mesh",
                    "assets/meshes/Torus.mesh",
                    "assets/meshes/Ramp_Wedge.mesh",
                    "assets/samples/Generic/CesiumMan.glb",
                    "assets/samples/Sponza/Sponza.gltf"
                };
                for (const char* mpath : meshes)
                {
                    if (ImGui::MenuItem(mpath))
                        (void)assign_mesh_asset_to_entity(s, s.selected, mpath, log_push);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Apply Surface Texture to Selected"))
            {
                const char* texs[] = {
                    "assets/textures/UV_Checkerboard_2K.tex",
                    "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg",
                    "assets/textures/Carbon_Fiber_Twill.tex",
                    "assets/textures/Carrara_Marble.tex",
                    "assets/textures/Oak_Wood_Planks.tex",
                    "assets/textures/Earth_Albedo.png",
                    "Clear Texture (Solid Albedo)"
                };
                for (const char* tpath : texs)
                {
                    if (ImGui::MenuItem(tpath))
                        (void)assign_texture_asset_to_entity(s, s.selected, tpath, log_push);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete Selected (Del)"))
                delete_selected(s, log_push);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Shoot Physics CannonBall (B)"))
            shoot_physics_ball_from_camera(s, log_push);
        if (ImGui::MenuItem("Reset All Physics Positions (R)"))
            reset_physics_to_home_positions(s, log_push);
        ImGui::EndPopup();
    }

    // 4. Handle Active Drag-and-Drop of 3D Objects, Modifiers, Meshes & Textures over the 3D Viewport!
    const auto& ui_st = cd::ui::chroma::state();
    const float vp_min_x = sw * ui_st.dock_left_w_ratio;
    const float vp_max_x = sw * (1.0F - ui_st.dock_right_w_ratio);
    const float vp_min_y = 54.0F;
    const float vp_max_y = 54.0F + std::max(100.0F, sh - 54.0F) * (1.0F - ui_st.dock_bottom_h_ratio);
    const bool mouse_in_central_viewport =
        (mouse_px.x > vp_min_x && mouse_px.x < vp_max_x &&
         mouse_px.y > vp_min_y && mouse_px.y < vp_max_y);

    if (w.is_dragging_spawn)
    {
        w.drag_preview_pos = unproject_mouse_to_ground(s, mouse_px, sw, sh, 0.0F);
        const cd::physics::Aabb preview_box {
            .min = { w.drag_preview_pos.x - 0.5F, w.drag_preview_pos.y,        w.drag_preview_pos.z - 0.5F },
            .max = { w.drag_preview_pos.x + 0.5F, w.drag_preview_pos.y + 1.0F, w.drag_preview_pos.z + 0.5F },
        };
        draw_wireframe_aabb_3d(dl, preview_box, vp, sw, sh, IM_COL32(60, 215, 255, 255), 2.8F);
        ImVec2 lbl_px {};
        if (project_world_to_screen(w.drag_preview_pos, vp, sw, sh, lbl_px))
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[DROP 3D] %s @ (%.1f, %.1f, %.1f)",
                          w.drag_spawn_id.c_str(),
                          static_cast<double>(w.drag_preview_pos.x),
                          static_cast<double>(w.drag_preview_pos.y),
                          static_cast<double>(w.drag_preview_pos.z));
            dl->AddRectFilled(ImVec2(lbl_px.x - 4.0F, lbl_px.y - 24.0F),
                              ImVec2(lbl_px.x + 240.0F, lbl_px.y - 2.0F),
                              IM_COL32(15, 35, 55, 230), 4.0F);
            dl->AddText(ImVec2(lbl_px.x + 2.0F, lbl_px.y - 20.0F),
                        IM_COL32(90, 235, 255, 255), buf);
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (mouse_in_central_viewport)
            {
                (void)spawn_entity_with_modifiers(s, w.drag_spawn_id, w.drag_preview_pos, log_push);
            }
            w.is_dragging_spawn = false;
            w.drag_spawn_id.clear();
        }
    }

    if (w.is_dragging_modifier)
    {
        w.drag_hover_entity_idx = pick_entity_under_mouse(s, vp, mouse_px, sw, sh);
        if (w.drag_hover_entity_idx >= 0 && std::cmp_less(w.drag_hover_entity_idx, s.entities.size()))
        {
            const auto& target_ent = s.entities[static_cast<std::size_t>(w.drag_hover_entity_idx)];
            if (const auto* lt = s.scene.local(target_ent.handle))
            {
                const cd::physics::Aabb hl = compute_entity_world_aabb(lt->value, nullptr);
                draw_wireframe_aabb_3d(dl, hl, vp, sw, sh, IM_COL32(255, 215, 0, 255), 3.0F);
                ImVec2 sp {};
                if (project_world_to_screen(lt->value.position, vp, sw, sh, sp))
                {
                    const std::string txt = "Drop Modifier [" + w.drag_modifier_class + "] -> " + target_ent.name;
                    dl->AddRectFilled(ImVec2(sp.x - 4.0F, sp.y - 26.0F),
                                      ImVec2(sp.x + 290.0F, sp.y - 2.0F),
                                      IM_COL32(45, 35, 10, 235), 4.0F);
                    dl->AddText(ImVec2(sp.x + 2.0F, sp.y - 22.0F),
                                IM_COL32(255, 225, 65, 255), txt.c_str());
                }
            }
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (mouse_in_central_viewport)
            {
                auto mod = instantiate_modifier_by_class(w.drag_modifier_class);
                if (mod)
                {
                    if (mod->category() == ModifierCategory::kEnvironment)
                    {
                        w.environment_modifiers.add_or_replace(std::move(mod));
                        log_push("[modifier] Dropped Environment class '" + w.drag_modifier_class + "' onto Scene");
                    }
                    else if (w.drag_hover_entity_idx >= 0 &&
                             std::cmp_less(w.drag_hover_entity_idx, s.entities.size()))
                    {
                        auto& ent = s.entities[static_cast<std::size_t>(w.drag_hover_entity_idx)];
                        w.entity_modifiers[ent.handle.id].add_or_replace(std::move(mod));
                        s.selected      = w.drag_hover_entity_idx;
                        s.selected_kind = SelKind::kEntity;
                        log_push("[modifier] Dropped class '" + w.drag_modifier_class + "' onto '" + ent.name + "'");
                    }
                }
            }
            w.is_dragging_modifier  = false;
            w.drag_modifier_class.clear();
            w.drag_hover_entity_idx = -1;
        }
    }

    if (w.is_dragging_asset_mesh || w.is_dragging_asset_texture)
    {
        w.drag_hover_entity_idx = pick_entity_under_mouse(s, vp, mouse_px, sw, sh);
        const int target_idx = (w.drag_hover_entity_idx >= 0) ? w.drag_hover_entity_idx : s.selected;
        if (target_idx >= 0 && std::cmp_less(target_idx, s.entities.size()))
        {
            const auto& target_ent = s.entities[static_cast<std::size_t>(target_idx)];
            if (const auto* lt = s.scene.local(target_ent.handle))
            {
                const cd::physics::Aabb hl = compute_entity_world_aabb(lt->value, nullptr);
                const ImU32 col = w.is_dragging_asset_mesh
                    ? IM_COL32(80, 220, 255, 255)
                    : IM_COL32(120, 255, 140, 255);
                draw_wireframe_aabb_3d(dl, hl, vp, sw, sh, col, 3.0F);
                ImVec2 sp {};
                if (project_world_to_screen(lt->value.position, vp, sw, sh, sp))
                {
                    const std::string txt = w.is_dragging_asset_mesh
                        ? ("Assign Mesh [" + w.drag_asset_mesh_id + "] -> " + target_ent.name)
                        : ("Apply Texture [" + w.drag_asset_texture_id + "] -> " + target_ent.name);
                    dl->AddRectFilled(ImVec2(sp.x - 4.0F, sp.y - 26.0F),
                                      ImVec2(sp.x + 320.0F, sp.y - 2.0F),
                                      IM_COL32(15, 42, 28, 235), 4.0F);
                    dl->AddText(ImVec2(sp.x + 2.0F, sp.y - 22.0F),
                                IM_COL32(140, 255, 175, 255), txt.c_str());
                }
            }
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (mouse_in_central_viewport && target_idx >= 0)
            {
                if (w.is_dragging_asset_mesh)
                    (void)assign_mesh_asset_to_entity(s, target_idx, w.drag_asset_mesh_id, log_push);
                if (w.is_dragging_asset_texture)
                    (void)assign_texture_asset_to_entity(s, target_idx, w.drag_asset_texture_id, log_push);
                s.selected      = target_idx;
                s.selected_kind = SelKind::kEntity;
            }
            w.is_dragging_asset_mesh    = false;
            w.drag_asset_mesh_id.clear();
            w.is_dragging_asset_texture = false;
            w.drag_asset_texture_id.clear();
            w.drag_hover_entity_idx     = -1;
        }
    }
}

// =============================================================================
} // namespace cd::studio
