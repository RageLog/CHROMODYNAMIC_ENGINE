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

    // ---- B. Evaluate Per-Entity Modifiers ----
    const float clamped_dt = std::clamp(dt, 0.001F, 0.033F);
    for (std::size_t idx = 0; idx < s.entities.size(); ++idx)
    {
        auto&          ent = s.entities[idx];
        ModifierStack& ms  = w.entity_modifiers[ent.handle.id];

        if (ms.items.empty() && ent.kind != PrimitiveKind::kSponza)
        {
            ms.add_or_replace(std::make_unique<PbrMaterialModifier>(
                ent.tint, ent.metallic, ent.roughness, ent.use_texture));
        }

        if (const auto* pbr = ms.template find<PbrMaterialModifier>(); pbr && pbr->enabled)
        {
            ent.tint        = pbr->base_color;
            ent.metallic    = pbr->metallic;
            ent.roughness   = pbr->roughness;
            ent.use_texture = pbr->use_texture;
            ent.is_pbr      = true;
        }

        if (const auto* efog = ms.template find<FogModifier>(); efog && efog->enabled)
        {
            s.fx.volumetric_fog_on  = efog->volumetric_scattering;
            s.fx.fog_density        = efog->fog_density;
            s.fx.shafts_strength    = efog->light_shafts_strength;
            s.fx.aerial_perspective = efog->aerial_perspective;
        }

        if (auto* cc = ms.template find<CharacterControllerModifier>(); cc && cc->enabled)
        {
            if (auto* lt = s.scene.local(ent.handle))
            {
                float fwd = 0.0F;
                float rgt = 0.0F;
                if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    fwd += 1.0F;
                if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  fwd -= 1.0F;
                if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) rgt += 1.0F;
                if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  rgt -= 1.0F;
                if (std::abs(fwd) > 0.01F || std::abs(rgt) > 0.01F)
                {
                    cd::math::Vec3f cam_fwd = s.cam.target - s.cam.eye;
                    cam_fwd.y = 0.0F;
                    const float fl = cd::math::length(cam_fwd);
                    if (fl > 1e-4F) cam_fwd = cam_fwd * (1.0F / fl);
                    else cam_fwd = { 0.0F, 0.0F, -1.0F };
                    const cd::math::Vec3f cam_rgt { -cam_fwd.z, 0.0F, cam_fwd.x };
                    lt->value.position = lt->value.position +
                        (cam_fwd * fwd + cam_rgt * rgt) * (cc->move_speed * clamped_dt);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
                {
                    if (auto* rb = ms.template find<RigidBodyModifier>())
                        rb->linear_velocity.y = cc->jump_impulse;
                }
            }
        }
    }

    // ---- C. Step 3D RigidBody Physics & Narrowphase Collision Engine ----
    const bool run_physics = s.pie_playing || w.simulate_physics_in_edit || w.step_single_frame;
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
            ModifierStack& ms_a  = w.entity_modifiers[ent_a.handle.id];
            auto*          col_a = ms_a.find_collider();
            if (!col_a) continue;
            auto*          lt_a  = s.scene.local(ent_a.handle);
            if (!lt_a) continue;
            auto*          rb_a  = ms_a.template find<RigidBodyModifier>();

            for (std::size_t j = i + 1; j < n; ++j)
            {
                auto&          ent_b = s.entities[j];
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
    if (clip.w <= 0.05F) return false;
    const float inv_w = 1.0F / clip.w;
    const float ndc_x = clip.x * inv_w;
    const float ndc_y = clip.y * inv_w;
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
        if (const auto* lt = s.scene.local(s.entities[i].handle))
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
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D) && s.selected >= 0)
            duplicate_entity_with_modifiers(s, s.selected, log_push);
    }

    // 1. Draw 3D Collider Wireframes (OBB & Sphere) + Contact Manifolds
    if (w.show_all_collider_wires && !s.pie_playing)
    {
        for (const auto& ent : s.entities)
        {
            const auto* lt  = s.scene.local(ent.handle);
            const auto* col = w.entity_modifiers[ent.handle.id].find_collider();
            if (!lt || !col || !col->show_wireframe) continue;

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

    // 2. Floating Top-Center 3D Viewport Quick-Action HUD Bar (Godot / Unreal style)
    if (!s.pie_playing && sw > 900.0F)
    {
        ImGui::SetNextWindowPos(ImVec2(sw * 0.5F - 14.0F, 26.0F), ImGuiCond_Always, ImVec2(0.5F, 0.0F));
        ImGui::SetNextWindowBgAlpha(0.88F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0F, 4.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 4.0F));
        if (ImGui::Begin("##studio_viewport_hud", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav))
        {
            // Transform Mode
            const bool is_t = (s.gizmo_state.mode == GizmoMode::kTranslate);
            const bool is_r = (s.gizmo_state.mode == GizmoMode::kRotate);
            const bool is_s = (s.gizmo_state.mode == GizmoMode::kScale);
            if (is_t) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22F, 0.52F, 0.90F, 1.0F));
            if (ImGui::SmallButton("Move(1)")) s.gizmo_state.mode = GizmoMode::kTranslate;
            if (is_t) ImGui::PopStyleColor();
            ImGui::SameLine();
            if (is_r) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22F, 0.52F, 0.90F, 1.0F));
            if (ImGui::SmallButton("Rot(2)")) s.gizmo_state.mode = GizmoMode::kRotate;
            if (is_r) ImGui::PopStyleColor();
            ImGui::SameLine();
            if (is_s) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22F, 0.52F, 0.90F, 1.0F));
            if (ImGui::SmallButton("Scale(3)")) s.gizmo_state.mode = GizmoMode::kScale;
            if (is_s) ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &w.grid_snap_enabled);
            ImGui::SameLine();
            if (ImGui::SmallButton("Floor(G)"))
                snap_selected_entity_to_ground(s, log_push);

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            // Physics Transport Controls
            if (w.simulate_physics_in_edit)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.58F, 0.28F, 1.0F));
                if (ImGui::SmallButton("[||] Phys ON"))
                    w.simulate_physics_in_edit = false;
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65F, 0.42F, 0.14F, 1.0F));
                if (ImGui::SmallButton("[>] Phys PAUSED"))
                    w.simulate_physics_in_edit = true;
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Step"))
            {
                w.simulate_physics_in_edit = false;
                w.step_single_frame        = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset(R)"))
                reset_physics_to_home_positions(s, log_push);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75F, 0.32F, 0.16F, 1.0F));
            if (ImGui::SmallButton("Shoot(B)"))
                shoot_physics_ball_from_camera(s, log_push);
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (ImGui::SmallButton("Iso"))
                apply_camera_bookmark(s, { -5.8F, 3.8F, 6.2F }, { 0.0F, 0.8F, 0.0F }, "Iso View", log_push);
            ImGui::SameLine();
            if (ImGui::SmallButton("Top"))
                apply_camera_bookmark(s, { 0.0F, 11.5F, 0.01F }, { 0.0F, 0.0F, 0.0F }, "Top View", log_push);
            ImGui::SameLine();
            if (ImGui::SmallButton("Front"))
                apply_camera_bookmark(s, { 0.0F, 2.2F, 7.8F }, { 0.0F, 1.0F, 0.0F }, "Front View", log_push);
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
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

    // 4. Handle Active Drag-and-Drop of 3D Objects or Modifiers over the 3D Viewport!
    const bool mouse_in_central_viewport = !ImGui::GetIO().WantCaptureMouse ||
        w.is_dragging_spawn || w.is_dragging_modifier;

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
            if (mouse_in_central_viewport && mouse_px.x > 280.0F && mouse_px.x < sw - 300.0F)
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
            if (mouse_px.x > 280.0F && mouse_px.x < sw - 300.0F)
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
}

// =============================================================================
} // namespace cd::studio
