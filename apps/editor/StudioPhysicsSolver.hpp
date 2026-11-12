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
    bool has_any_enabled_light = false;
    for (const auto& lr : s.lights)
    {
        if (lr.enabled && lr.light.intensity > 0.001F)
        {
            has_any_enabled_light = true;
            break;
        }
    }
    if (const auto* sky = w.environment_modifiers.find<SkyAtmosphereModifier>(); sky && sky->enabled)
    {
        s.fx.clouds_coverage = has_any_enabled_light ? sky->clouds_coverage : 0.0F;
        s.fx.ao_strength     = sky->ao_strength;
        s.fx.ssr_strength    = sky->ssr_strength;
        s.fx.ddgi_on         = sky->ddgi_gi_enabled;
    }
    if (!has_any_enabled_light)
    {
        s.fx.clouds_coverage   = 0.0F;
        s.fx.volumetric_fog_on = false;
    }

    // ---- B. Evaluate Per-Entity Modifiers & Godot+ Gameplay Logic ----
    const float raw_clamped_dt = std::clamp(dt, 1.0e-5F, 0.033F);
    const float clamped_dt     = (s.pie_playing && w.autopilot_play_active && s.config.autopilot_e2e)
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
    bool has_active_dynamic_rb    = false;
    bool fire_autopilot_shot      = false;
    bool has_fog_entity           = false;
    bool any_visible_fog_entity   = false;
    bool has_cloud_entity         = false;
    bool any_visible_cloud_entity = false;
    int  player_entity_idx        = -1;
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

        if (const auto* efog = ms.template find<FogModifier>())
        {
            has_fog_entity = true;
            if (efog->enabled && ent.visible && has_any_enabled_light)
            {
                any_visible_fog_entity  = true;
                s.fx.volumetric_fog_on  = efog->volumetric_scattering;
                s.fx.fog_density        = efog->fog_density;
                s.fx.shafts_strength    = efog->light_shafts_strength;
                s.fx.aerial_perspective = efog->aerial_perspective;
            }
        }

        if (const auto* cld = ms.template find<CloudLayerModifier>())
        {
            has_cloud_entity = true;
            if (cld->enabled && ent.visible && has_any_enabled_light)
            {
                any_visible_cloud_entity = true;
                s.fx.clouds_coverage     = cld->clouds_coverage;
            }
        }

        // 1. CharacterControllerModifier (WASD/Arrows + Space in PIE, or Autonomous E2E Autopilot Driver!)
        if (auto* cc = ms.template find<CharacterControllerModifier>(); cc && cc->enabled && ent.visible && !ent.locked)
        {
            player_entity_idx = static_cast<int>(idx);
            auto* anim = ms.template find<AnimatorStateMachineModifier>();
            const bool is_2d_char = (anim != nullptr && anim->is_2d_character);

            if (auto* lt = s.scene.local(ent.handle))
            {
                cd::math::Vec3f horiz_move_delta { 0.0F, 0.0F, 0.0F };
                bool want_jump = false;
                bool is_sprinting = false;

                if (!ImGui::GetIO().WantCaptureKeyboard)
                {
                    float fwd = 0.0F;
                    float rgt = 0.0F;
                    if (!is_2d_char)
                    {
                        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)   || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_W))) fwd += 1.0F;
                        if (ImGui::IsKeyDown(ImGuiKey_DownArrow) || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_S))) fwd -= 1.0F;
                    }
                    if (ImGui::IsKeyDown(ImGuiKey_RightArrow) || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_D))) rgt += 1.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)  || (s.pie_playing && ImGui::IsKeyDown(ImGuiKey_A))) rgt -= 1.0F;
                    is_sprinting = s.mod_shift;
                    const float speed_mult = is_sprinting ? 1.55F : 1.0F;

                    if (std::abs(fwd) > 0.01F || std::abs(rgt) > 0.01F)
                    {
                        // Human manual input overrides autopilot
                        w.autopilot_play_active = false;
                        if (is_2d_char)
                        {
                            horiz_move_delta = { rgt * cc->move_speed * speed_mult * clamped_dt, 0.0F, 0.0F };
                        }
                        else
                        {
                            cd::math::Vec3f cam_fwd = s.cam.target - s.cam.eye;
                            cam_fwd.y = 0.0F;
                            const float fl = cd::math::length(cam_fwd);
                            if (fl > 1e-4F) cam_fwd = cam_fwd * (1.0F / fl);
                            else cam_fwd = { 0.0F, 0.0F, -1.0F };
                            const cd::math::Vec3f cam_rgt { -cam_fwd.z, 0.0F, cam_fwd.x };
                            cd::math::Vec3f wish_dir = cam_fwd * fwd + cam_rgt * rgt;
                            const float wl = cd::math::length(wish_dir);
                            if (wl > 1e-4F) wish_dir = wish_dir * (1.0F / wl);
                            horiz_move_delta = wish_dir * (cc->move_speed * speed_mult * clamped_dt);
                        }
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
                    if (is_2d_char) lt->value.position.z = 0.0F;
                    w.player_horiz_speed = cd::math::length(horiz_move_delta) / std::max(1e-4F, clamped_dt);

                    // Smoothly rotate character to face movement velocity direction
                    const float target_yaw_deg = is_2d_char
                        ? (horiz_move_delta.x >= 0.0F ? 0.0F : 180.0F)
                        : (std::atan2(horiz_move_delta.x, horiz_move_delta.z) * 57.2957795F);
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

                // Evaluate 6-State AnimatorStateMachineModifier (Idle/Walk/Run/Jump/Fall/Attack)
                if (anim && anim->enabled)
                {
                    if (anim->attack_timer > 0.0F)
                    {
                        anim->attack_timer = std::max(0.0F, anim->attack_timer - clamped_dt);
                    }
                    if (anim->auto_stateful_transitions && sim_active)
                    {
                        const auto* rb = ms.template find<RigidBodyModifier>();
                        const bool  grounded = (rb == nullptr) ? true : rb->is_grounded;
                        const float vy       = (rb == nullptr) ? 0.0F : rb->linear_velocity.y;
                        if (anim->attack_timer > 0.0F)
                            anim->set_state(5); // Attack
                        else if (!grounded && vy > 0.35F)
                            anim->set_state(3); // Jump
                        else if (!grounded && vy < -0.55F)
                            anim->set_state(4); // Fall
                        else if (w.player_horiz_speed > cc->move_speed * 1.15F)
                            anim->set_state(2); // Run
                        else if (w.player_horiz_speed > 0.12F)
                            anim->set_state(1); // Walk
                        else
                            anim->set_state(0); // Idle
                    }
                    const int st_idx = std::clamp(anim->current_state_idx, 0, 5);
                    s.anim_speed     = anim->state_speeds[static_cast<std::size_t>(st_idx)];

                    // Sync any attached child model entities' AnimatorStateMachineModifier & procedural bob/swing
                    for (std::size_t ci = 0; ci < s.entities.size(); ++ci)
                    {
                        if (ci == idx) continue;
                        const auto& c_ent = s.entities[ci];
                        if (s.scene.parent_of(c_ent.handle) == ent.handle)
                        {
                            if (auto* c_anim = w.entity_modifiers[c_ent.handle.id].template find<AnimatorStateMachineModifier>())
                            {
                                c_anim->set_state(st_idx);
                                c_anim->attack_timer = anim->attack_timer;
                            }
                            if (sim_active)
                            {
                                if (auto* c_lt = s.scene.local(c_ent.handle))
                                {
                                    const float bob = (st_idx == 1 || st_idx == 2)
                                        ? std::abs(std::sin(w.sim_time_accum * (st_idx == 2 ? 14.0F : 9.0F))) * 0.08F
                                        : 0.0F;
                                    c_lt->value.position.y = 0.05F + bob;
                                }
                            }
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

    if (has_fog_entity && !any_visible_fog_entity)
    {
        s.fx.volumetric_fog_on = false;
        s.fx.fog_density       = 0.0F;
    }
    if (has_cloud_entity && !any_visible_cloud_entity)
    {
        s.fx.clouds_coverage = 0.0F;
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
        if (ent.name.find("Ground_Floor") != std::string::npos ||
            ent.name.find("Volumetric_Clouds") != std::string::npos ||
            ent.name.find("Volumetric_Fog") != std::string::npos)
            continue;
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


} // namespace cd::studio
