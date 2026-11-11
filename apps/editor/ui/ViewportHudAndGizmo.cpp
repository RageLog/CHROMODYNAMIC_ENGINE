// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/ViewportHudAndGizmo.cpp
// =============================================================================
#include "ui/ViewportHudAndGizmo.hpp"

#include "physics/PhysicsPieSolver.hpp"
#include "scene/SceneOperations.hpp"
#include "StudioModifierClasses.hpp"
#include "StudioWorkspacePanels.hpp"
#include "ui/InspectorDock.hpp"
#include "ui/ProjectAndToolsDock.hpp"
#include "ui/ProjectHubModal.hpp"
#include "ui/SceneTreeDock.hpp"
#include "workspace_2d/Workspace2D.hpp"

#include <cd/app_common/CameraMath3D.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/ui/ChromaUi.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

namespace cd::studio::ui
{

void draw_viewport_overlay_strip_and_rose(StudioEngineState& s,
                                          std::uint32_t frame_w,
                                          std::uint32_t frame_h)
{
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();
    const float sw = static_cast<float>(frame_w);
    const float sh = static_cast<float>(frame_h);
    if (sw < 64.0F || sh < 64.0F)
    {
        return;
    }

    // Gizmo 1 / 2 / 3 hotkeys when typing/PIE is inactive
    if (!ImGui::GetIO().WantCaptureKeyboard && !s.pie_playing)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_1, false))
        {
            s.gizmo_state.mode = GizmoMode::kTranslate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_2, false))
        {
            s.gizmo_state.mode = GizmoMode::kRotate;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_3, false))
        {
            s.gizmo_state.mode = GizmoMode::kScale;
        }
    }

    // ---- 1. Top-Left Compact Hammer 5 Floating Viewport Pill Bar ----
    const float vp_left  = sw * cst.dock_left_w_ratio;
    const float vp_right = sw * (1.0F - cst.dock_right_w_ratio);
    const float pill_x   = vp_left + 10.0F;
    const float pill_y   = cst.top_bar_height + 8.0F;
    const float max_pill_w = std::max(260.0F, (vp_right - vp_left) - 96.0F);

    ImGui::SetNextWindowPos(ImVec2(pill_x, pill_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::min(535.0F, max_pill_w), 30.0F), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08F, 0.10F, 0.13F, 0.84F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0F, 4.0F));
    if (ImGui::Begin("##hammer5_vp_overlay", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
    {
        auto mode_pill = [&](const char* lbl, GizmoMode m)
        {
            const bool on = (s.gizmo_state.mode == m);
            if (on)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.52F, 0.88F, 1.0F));
            }
            if (ImGui::SmallButton(lbl))
            {
                s.gizmo_state.mode    = m;
                s.gizmo_state.visible = true;
            }
            if (on)
            {
                ImGui::PopStyleColor();
            }
        };

        mode_pill("Move", GizmoMode::kTranslate);
        ImGui::SameLine(0.0F, 3.0F);
        mode_pill("Rotate", GizmoMode::kRotate);
        ImGui::SameLine(0.0F, 3.0F);
        mode_pill("Scale", GizmoMode::kScale);

        ImGui::SameLine(0.0F, 8.0F);
        constexpr std::array<const char*, 5> kViewLabels {{
            "Lit PBR", "Albedo", "Normals", "Rough/Met", "AO"
        }};
        const int vm_clamped = std::clamp(s.fx.view_mode, 0, 4);
        if (ImGui::SmallButton(kViewLabels[static_cast<std::size_t>(vm_clamped)]))
        {
            s.fx.view_mode = (vm_clamped + 1) % 5;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Cycle Viewport Shading Mode");
        }

        ImGui::SameLine(0.0F, 6.0F);
        if (ImGui::SmallButton("Cam"))
        {
            ImGui::OpenPopup("vp_cam_bookmark_popup");
        }
        if (ImGui::BeginPopup("vp_cam_bookmark_popup"))
        {
            if (ImGui::MenuItem("Perspective Orbit"))
                scene_ops::apply_camera_bookmark(s, { 0.0F, 3.2F, 8.5F }, { 0.0F, 0.8F, 0.0F }, "Perspective");
            if (ImGui::MenuItem("Top (+Y)"))
                scene_ops::apply_camera_bookmark(s, { 0.0F, 16.0F, 0.01F }, { 0.0F, 0.0F, 0.0F }, "Top");
            if (ImGui::MenuItem("Front (+Z)"))
                scene_ops::apply_camera_bookmark(s, { 0.0F, 2.0F, 12.0F }, { 0.0F, 1.2F, 0.0F }, "Front");
            if (ImGui::MenuItem("Right (+X)"))
                scene_ops::apply_camera_bookmark(s, { 12.0F, 2.0F, 0.0F }, { 0.0F, 1.2F, 0.0F }, "Right");
            ImGui::EndPopup();
        }

        auto toggle_pill = [](const char* lbl, bool& flag, const char* tip)
        {
            ImGui::SameLine(0.0F, 4.0F);
            if (flag)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.50F, 0.36F, 0.95F));
            }
            if (ImGui::SmallButton(lbl))
            {
                flag = !flag;
            }
            if (flag)
            {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered() && tip != nullptr)
            {
                ImGui::SetTooltip("%s", tip);
            }
        };

        toggle_pill("Snap", w.grid_snap_enabled, "Grid Snapping");
        toggle_pill("Grid", s.show_editor_floor, "Show Ground Reference Grid");
        toggle_pill("Col",  w.show_all_collider_wires, "Show Collider Wireframes");
        toggle_pill("Phys", w.simulate_physics_in_edit, "Live Physics in Edit Mode");

        ImGui::SameLine(0.0F, 6.0F);
        if (ImGui::SmallButton("Focus"))
        {
            scene_ops::focus_selected_node(s);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // ---- 2. Top-Right 3D X/Y/Z Orientation Rose ----
    auto* bg = ImGui::GetBackgroundDrawList();
    const float rose_cx = vp_right - 36.0F;
    const float rose_cy = cst.top_bar_height + 38.0F;
    const float rose_r  = 22.0F;
    bg->AddCircleFilled(ImVec2(rose_cx, rose_cy), rose_r + 5.0F, IM_COL32(14, 18, 24, 185), 24);
    bg->AddCircle(ImVec2(rose_cx, rose_cy), rose_r + 5.0F, IM_COL32(70, 95, 130, 150), 24, 1.2F);

    const auto basis = cd::app_common::compute_camera_basis(s.cam.eye, s.cam.target);
    auto draw_rose_axis = [&](cd::math::Vec3f world_axis, ImU32 col, const char* lbl)
    {
        const float sx = world_axis.x * basis.right.x + world_axis.y * basis.right.y + world_axis.z * basis.right.z;
        const float sy = -(world_axis.x * basis.up.x + world_axis.y * basis.up.y + world_axis.z * basis.up.z);
        const ImVec2 tip { rose_cx + sx * rose_r, rose_cy + sy * rose_r };
        bg->AddLine(ImVec2(rose_cx, rose_cy), tip, col, 2.2F);
        bg->AddCircleFilled(tip, 4.0F, col, 12);
        bg->AddText(ImVec2(tip.x - 3.5F, tip.y - 6.0F), IM_COL32(255, 255, 255, 235), lbl);
    };
    draw_rose_axis({ 1.0F, 0.0F, 0.0F }, IM_COL32(235, 78, 78, 255),  "X");
    draw_rose_axis({ 0.0F, 1.0F, 0.0F }, IM_COL32(92, 224, 115, 255), "Y");
    draw_rose_axis({ 0.0F, 0.0F, 1.0F }, IM_COL32(88, 158, 250, 255), "Z");

    // ---- 3. Ephemeral Toast Notification (if active) ----
    if (w.hud_toast_timer > 0.0F && !w.hud_toast_message.empty())
    {
        const float cx = 0.5F * (vp_left + vp_right);
        const float by = sh * (1.0F - cst.dock_bottom_h_ratio) - 34.0F;
        bg->AddRectFilled(ImVec2(cx - 170.0F, by), ImVec2(cx + 170.0F, by + 24.0F),
                          IM_COL32(16, 24, 34, 215), 5.0F);
        bg->AddText(ImVec2(cx - 156.0F, by + 5.0F), IM_COL32(145, 230, 255, 255),
                    w.hud_toast_message.c_str());
    }

    // ---- 4. 16:9 Camera PiP Preview (only when Camera selected or piloted) ----
    draw_camera_preview_overlay(
        s, frame_w, frame_h, [&s](std::string msg) { s.push_log(std::move(msg)); });
}

void draw_pie_gameplay_hud(StudioEngineState& s,
                           float dt,
                           const cd::math::Mat4f& vp,
                           std::uint32_t frame_w,
                           std::uint32_t frame_h)
{
    draw_ingame_hud_workspace(
        s, dt, vp, frame_w, frame_h, [&s](std::string msg) { s.push_log(std::move(msg)); });
}

void draw_shader_graph_workspace(StudioEngineState& s,
                                 std::uint32_t frame_w,
                                 std::uint32_t frame_h)
{
    draw_node_and_app_lab_workspace(
        s, frame_w, frame_h, [&s](std::string msg) { s.push_log(std::move(msg)); });
}

void draw_selection_and_light_overlays(StudioEngineState& s,
                                       const cd::math::Mat4f& vp,
                                       cd::rhi::Extent2D extent)
{
    const auto vw = static_cast<float>(extent.width);
    const auto vh = static_cast<float>(extent.height);

    // ---- 1. Selection Silhouette Ring ----
    if (s.selected_kind == SelKind::kEntity &&
        s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
    {
        s.outline.set(s.entities[static_cast<std::size_t>(s.selected)].handle);
    }
    else
    {
        s.outline.clear();
    }
    s.outline.clamp_params();

    if (s.outline.style != cd::editor::OutlineStyle::kNone && !s.outline.empty())
    {
        auto* dl = ImGui::GetBackgroundDrawList();
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(s.outline.color.x, s.outline.color.y, s.outline.color.z, s.outline.opacity));
        for (const auto& e : s.outline.entities())
        {
            const auto* lt = s.scene.local(e);
            if (lt == nullptr)
            {
                continue;
            }
            const auto proj = cd::app_common::project_world_to_screen_vk(
                vp, lt->value.position, 0.0F, 0.0F, vw, vh);
            if (!proj.in_front || proj.clip_w <= 0.25F)
            {
                continue;
            }
            const float sx = proj.pixel.x;
            const float sy = proj.pixel.y;
            if (sx < -vw || sx > 2.0F * vw || sy < -vh || sy > 2.0F * vh)
            {
                continue;
            }
            const auto& sc = lt->value.scale;
            const float s_max = std::max({ std::abs(sc.x), std::abs(sc.y), std::abs(sc.z), 0.25F });
            const float radius = std::clamp((0.58F * s_max / proj.clip_w) * vh, 14.0F, 180.0F);
            dl->AddCircle(ImVec2(sx, sy), radius, col, 48, s.outline.thickness * 1.5F);
            dl->AddLine(ImVec2(sx - radius - 8.0F, sy), ImVec2(sx - radius - 2.0F, sy), col, s.outline.thickness);
            dl->AddLine(ImVec2(sx + radius + 2.0F, sy), ImVec2(sx + radius + 8.0F, sy), col, s.outline.thickness);
            dl->AddLine(ImVec2(sx, sy - radius - 8.0F), ImVec2(sx, sy - radius - 2.0F), col, s.outline.thickness);
            dl->AddLine(ImVec2(sx, sy + radius + 2.0F), ImVec2(sx, sy + radius + 8.0F), col, s.outline.thickness);
        }
    }

    // ---- 2. Selected Light Marker Wireframe ----
    if (s.selected_kind != SelKind::kLight ||
        s.selected < 0 || !std::cmp_less(s.selected, s.lights.size()))
    {
        return;
    }

    const auto& lrow = s.lights[static_cast<std::size_t>(s.selected)];
    if (!lrow.enabled)
    {
        return;
    }
    const auto& L = lrow.light;
    auto* dl_m = ImGui::GetBackgroundDrawList();
    auto project = [&](const cd::math::Vec3f& p) -> ImVec2
    {
        const auto pr = cd::app_common::project_world_to_screen_vk(vp, p, 0.0F, 0.0F, vw, vh);
        if (!pr.in_front || pr.clip_w <= 0.25F ||
            pr.pixel.x < -vw || pr.pixel.x > 2.0F * vw ||
            pr.pixel.y < -vh || pr.pixel.y > 2.0F * vh)
        {
            return { -1.0F, -1.0F };
        }
        return { pr.pixel.x, pr.pixel.y };
    };

    const ImU32 col     = ImGui::ColorConvertFloat4ToU32(ImVec4(L.color.x, L.color.y, L.color.z, 1.0F));
    const ImU32 col_dim = ImGui::ColorConvertFloat4ToU32(ImVec4(L.color.x * 0.6F, L.color.y * 0.6F, L.color.z * 0.6F, 0.7F));
    const ImU32 col_sel = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0F, 0.85F, 0.0F, 1.0F));

    switch (L.type)
    {
        case cd::light::LightType::kDirectional:
        {
            const cd::math::Vec3f sky_origin { -L.direction.x * 15.0F, -L.direction.y * 15.0F, -L.direction.z * 15.0F };
            const cd::math::Vec3f tip { sky_origin.x + L.direction.x * 8.0F,
                                        sky_origin.y + L.direction.y * 8.0F,
                                        sky_origin.z + L.direction.z * 8.0F };
            const auto p0 = project(sky_origin);
            const auto p1 = project(tip);
            if (p0.x >= 0.0F && p1.x >= 0.0F)
            {
                dl_m->AddLine(p0, p1, col, 3.0F);
                dl_m->AddCircleFilled(p0, 8.0F, col);
                dl_m->AddCircle(p0, 16.0F, col_sel, 16, 3.0F);
                dl_m->AddText(ImVec2(p0.x + 10.0F, p0.y - 8.0F), col, "SUN");
            }
            break;
        }
        case cd::light::LightType::kPoint:
        {
            const auto p = project(L.position);
            if (p.x >= 0.0F)
            {
                dl_m->AddCircleFilled(p, 10.0F, col);
                dl_m->AddCircle(p, 18.0F, col_sel, 16, 3.0F);
                for (int i = 0; i < 24; ++i)
                {
                    const float t0 = static_cast<float>(i) / 24.0F * 2.0F * std::numbers::pi_v<float>;
                    const float t1 = static_cast<float>(i + 1) / 24.0F * 2.0F * std::numbers::pi_v<float>;
                    const auto pa = project({ L.position.x + std::cos(t0) * L.range, L.position.y, L.position.z + std::sin(t0) * L.range });
                    const auto pb = project({ L.position.x + std::cos(t1) * L.range, L.position.y, L.position.z + std::sin(t1) * L.range });
                    if (pa.x >= 0.0F && pb.x >= 0.0F)
                    {
                        dl_m->AddLine(pa, pb, col_dim, 1.5F);
                    }
                }
                dl_m->AddText(ImVec2(p.x + 14.0F, p.y - 8.0F), col, "POINT");
            }
            break;
        }
        case cd::light::LightType::kSpot:
        {
            const auto p_apex = project(L.position);
            const cd::math::Vec3f far_center {
                L.position.x + L.direction.x * L.range,
                L.position.y + L.direction.y * L.range,
                L.position.z + L.direction.z * L.range
            };
            cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
            if (std::abs(L.direction.y) > 0.95F)
            {
                up = { 1.0F, 0.0F, 0.0F };
            }
            cd::math::Vec3f rgt {
                L.direction.y * up.z - L.direction.z * up.y,
                L.direction.z * up.x - L.direction.x * up.z,
                L.direction.x * up.y - L.direction.y * up.x
            };
            const float rl = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
            if (rl > 1e-5F)
            {
                rgt.x /= rl; rgt.y /= rl; rgt.z /= rl;
            }
            const cd::math::Vec3f bt {
                L.direction.y * rgt.z - L.direction.z * rgt.y,
                L.direction.z * rgt.x - L.direction.x * rgt.z,
                L.direction.x * rgt.y - L.direction.y * rgt.x
            };
            const float outer_angle = std::acos(std::clamp(L.cos_outer_cone, -1.0F, 1.0F));
            const float disk_r      = L.range * std::tan(outer_angle);
            constexpr int kEdges    = 4;
            std::array<cd::math::Vec3f, kEdges + 1> rim {};
            for (int i = 0; i <= kEdges; ++i)
            {
                const float t  = static_cast<float>(i) / static_cast<float>(kEdges) * 2.0F * std::numbers::pi_v<float>;
                const float ct = std::cos(t);
                const float st = std::sin(t);
                rim[static_cast<std::size_t>(i)] = {
                    far_center.x + (rgt.x * ct + bt.x * st) * disk_r,
                    far_center.y + (rgt.y * ct + bt.y * st) * disk_r,
                    far_center.z + (rgt.z * ct + bt.z * st) * disk_r
                };
            }
            for (int i = 0; i < kEdges; ++i)
            {
                const auto pe = project(rim[static_cast<std::size_t>(i)]);
                if (p_apex.x >= 0.0F && pe.x >= 0.0F)
                    dl_m->AddLine(p_apex, pe, col_dim, 1.5F);
                const auto pb = project(rim[static_cast<std::size_t>(i) + 1U]);
                if (pe.x >= 0.0F && pb.x >= 0.0F)
                    dl_m->AddLine(pe, pb, col_dim, 1.5F);
            }
            if (p_apex.x >= 0.0F)
            {
                dl_m->AddCircleFilled(p_apex, 8.0F, col);
                dl_m->AddCircle(p_apex, 16.0F, col_sel, 16, 3.0F);
                dl_m->AddText(ImVec2(p_apex.x + 10.0F, p_apex.y - 8.0F), col, "SPOT");
            }
            break;
        }
        case cd::light::LightType::kRectArea:
        case cd::light::LightType::kDiskArea:
        {
            cd::math::Vec3f ln = L.direction;
            const float lnl = std::sqrt(ln.x * ln.x + ln.y * ln.y + ln.z * ln.z);
            if (lnl > 1e-5F) { ln.x /= lnl; ln.y /= lnl; ln.z /= lnl; }
            else { ln = { 0.0F, 0.0F, -1.0F }; }

            cd::math::Vec3f t = L.area_tangent;
            const float pr = t.x * ln.x + t.y * ln.y + t.z * ln.z;
            t.x -= pr * ln.x; t.y -= pr * ln.y; t.z -= pr * ln.z;
            const float tnl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            if (tnl > 1e-5F) { t.x /= tnl; t.y /= tnl; t.z /= tnl; }
            else { t = { 1.0F, 0.0F, 0.0F }; }

            const cd::math::Vec3f b {
                ln.y * t.z - ln.z * t.y,
                ln.z * t.x - ln.x * t.z,
                ln.x * t.y - ln.y * t.x
            };
            const float hw = L.area_width * 0.5F;
            const float hh = L.area_height * 0.5F;
            const auto p0 = project({ L.position.x - t.x * hw - b.x * hh, L.position.y - t.y * hw - b.y * hh, L.position.z - t.z * hw - b.z * hh });
            const auto p1 = project({ L.position.x + t.x * hw - b.x * hh, L.position.y + t.y * hw - b.y * hh, L.position.z + t.z * hw - b.z * hh });
            const auto p2 = project({ L.position.x + t.x * hw + b.x * hh, L.position.y + t.y * hw + b.y * hh, L.position.z + t.z * hw + b.z * hh });
            const auto p3 = project({ L.position.x - t.x * hw + b.x * hh, L.position.y - t.y * hw + b.y * hh, L.position.z - t.z * hw + b.z * hh });
            if (p0.x >= 0.0F && p1.x >= 0.0F && p2.x >= 0.0F && p3.x >= 0.0F)
            {
                dl_m->AddLine(p0, p1, col_sel, 4.0F);
                dl_m->AddLine(p1, p2, col_sel, 4.0F);
                dl_m->AddLine(p2, p3, col_sel, 4.0F);
                dl_m->AddLine(p3, p0, col_sel, 4.0F);
                dl_m->AddText(p0, col, "AREA");
            }
            break;
        }
    }
}

void update_and_draw_transform_gizmo(StudioEngineState& s,
                                     const cd::math::Mat4f& vp,
                                     cd::rhi::Extent2D extent)
{
    auto& gizmo        = s.gizmo;
    auto& gizmo_state  = s.gizmo_state;
    bool& pending_pick = s.app_state.pick.pending;
    auto& window       = *s.window;
    const auto& cam    = s.cam;

    auto gizmo_target_pos = [&]() -> cd::math::Vec3f*
    {
        if (s.selected < 0)
        {
            return nullptr;
        }
        if (s.selected_kind == SelKind::kEntity)
        {
            if (std::cmp_greater_equal(s.selected, s.entities.size()))
            {
                return nullptr;
            }
            const auto& sel_ent = s.entities[static_cast<std::size_t>(s.selected)];
            if (!sel_ent.visible || sel_ent.locked)
            {
                return nullptr;
            }
            if (auto* lt = s.scene.local(sel_ent.handle))
            {
                return &lt->value.position;
            }
            return nullptr;
        }
        if (s.selected_kind == SelKind::kLight)
        {
            if (std::cmp_greater_equal(s.selected, s.lights.size()))
            {
                return nullptr;
            }
            auto& L = s.lights[static_cast<std::size_t>(s.selected)].light;
            if (L.type == cd::light::LightType::kDirectional)
            {
                return nullptr;
            }
            return &L.position;
        }
        return nullptr;
    };

    cd::math::Vec3f* target_pos = gizmo_target_pos();
    if (!gizmo_state.visible || target_pos == nullptr)
    {
        gizmo_state.was_hovered = false;
        return;
    }

    const bool target_is_entity = (s.selected_kind == SelKind::kEntity);
    const cd::ecs::Entity sel_ent =
        target_is_entity ? s.entities[static_cast<std::size_t>(s.selected)].handle : cd::ecs::Entity {};
    cd::scene::LocalTransform* lt = target_is_entity ? s.scene.local(sel_ent) : nullptr;

    gizmo.set_target(*target_pos);
    const auto vw = static_cast<float>(extent.width);
    const auto vh = static_cast<float>(extent.height);
    const auto& tgt = gizmo.target();

    const cd::math::Vec4f wp_org { tgt.x, tgt.y, tgt.z, 1.0F };
    cd::math::Vec4f clip_org {};
    for (std::size_t r = 0; r < 4; ++r)
    {
        clip_org[r] = vp[0][r] * wp_org[0] + vp[1][r] * wp_org[1] + vp[2][r] * wp_org[2] + vp[3][r] * wp_org[3];
    }
    const float w_org = clip_org[3];

    auto project = [&](const cd::math::Vec3f& p) -> ImVec2
    {
        if (w_org <= 0.20F)
        {
            return { -1.0F, -1.0F };
        }
        const cd::math::Vec4f wp { p.x, p.y, p.z, 1.0F };
        cd::math::Vec4f c {};
        for (std::size_t r = 0; r < 4; ++r)
        {
            c[r] = vp[0][r] * wp[0] + vp[1][r] * wp[1] + vp[2][r] * wp[2] + vp[3][r] * wp_org[3];
        }
        if (c[3] <= 0.20F)
        {
            return { -1.0F, -1.0F };
        }
        const float sx = (c[0] / w_org * 0.5F + 0.5F) * vw;
        const float sy = (1.0F - (c[1] / w_org * 0.5F + 0.5F)) * vh;
        if (sx < -vw || sx > 2.0F * vw || sy < -vh || sy > 2.0F * vh)
        {
            return { -1.0F, -1.0F };
        }
        return { sx, sy };
    };

    const float dx_cam   = tgt.x - cam.eye.x;
    const float dy_cam   = tgt.y - cam.eye.y;
    const float dz_cam   = tgt.z - cam.eye.z;
    const float cam_dist = std::sqrt(dx_cam * dx_cam + dy_cam * dy_cam + dz_cam * dz_cam);
    const float inv_dist = (cam_dist > 1e-4F) ? (1.0F / cam_dist) : 0.0F;
    const float vx_dir   = dx_cam * inv_dist;
    const float vy_dir   = dy_cam * inv_dist;
    const float vz_dir   = dz_cam * inv_dist;
    const float kAxisLen = std::clamp(cam_dist * 0.18F, 0.42F, 1.25F);

    const ImVec2 p_org = project(tgt);
    if (p_org.x < 0.0F)
    {
        return;
    }

    const ImVec2 p_x = project({ tgt.x + kAxisLen, tgt.y, tgt.z });
    const ImVec2 p_y = project({ tgt.x, tgt.y + kAxisLen, tgt.z });
    const ImVec2 p_z = project({ tgt.x, tgt.y, tgt.z + kAxisLen });

    auto axis_screen_len = [&](ImVec2 p) -> float
    {
        if (p.x < 0.0F)
        {
            return 0.0F;
        }
        return std::hypot(p.x - p_org.x, p.y - p_org.y);
    };

    const bool show_x = (p_x.x >= 0.0F) &&
        ((std::abs(vx_dir) < 0.92F && axis_screen_len(p_x) >= 14.0F) || gizmo.active_axis() == cd::editor::GizmoAxis::kX);
    const bool show_y = (p_y.x >= 0.0F) &&
        ((std::abs(vy_dir) < 0.92F && axis_screen_len(p_y) >= 14.0F) || gizmo.active_axis() == cd::editor::GizmoAxis::kY);
    const bool show_z = (p_z.x >= 0.0F) &&
        ((std::abs(vz_dir) < 0.92F && axis_screen_len(p_z) >= 14.0F) || gizmo.active_axis() == cd::editor::GizmoAxis::kZ);

    auto* dl = ImGui::GetBackgroundDrawList();
    auto axis_color_imgui = [](cd::editor::GizmoAxis a) -> ImU32
    {
        const auto c = cd::editor::axis_color(a);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, 1.0F));
    };
    const ImU32 cx = axis_color_imgui(cd::editor::GizmoAxis::kX);
    const ImU32 cy = axis_color_imgui(cd::editor::GizmoAxis::kY);
    const ImU32 cz = axis_color_imgui(cd::editor::GizmoAxis::kZ);

    auto thick = [&](cd::editor::GizmoAxis a) -> float
    {
        const bool all_active = (gizmo.hover() == cd::editor::GizmoAxis::kXYZ ||
                                 gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ);
        return (all_active || gizmo.hover() == a || gizmo.active_axis() == a) ? 5.0F : 3.0F;
    };

    if (gizmo_state.mode != GizmoMode::kRotate)
    {
        if (show_x) dl->AddLine(p_org, p_x, cx, thick(cd::editor::GizmoAxis::kX));
        if (show_y) dl->AddLine(p_org, p_y, cy, thick(cd::editor::GizmoAxis::kY));
        if (show_z) dl->AddLine(p_org, p_z, cz, thick(cd::editor::GizmoAxis::kZ));
    }

    const bool center_active = (gizmo.hover() == cd::editor::GizmoAxis::kXYZ ||
                                gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ);
    const ImU32 center_ring_col = ImGui::ColorConvertFloat4ToU32(
        center_active ? ImVec4(1.0F, 0.88F, 0.25F, 1.0F) : ImVec4(0.35F, 0.92F, 0.98F, 0.90F));
    const ImU32 center_fill_col = ImGui::ColorConvertFloat4ToU32(
        center_active ? ImVec4(1.0F, 0.88F, 0.25F, 1.0F) : ImVec4(0.95F, 0.96F, 0.98F, 0.95F));
    dl->AddCircle(p_org, 9.5F, center_ring_col, 24, center_active ? 2.5F : 1.8F);
    dl->AddCircleFilled(p_org, 5.0F, center_fill_col);

    if (gizmo_state.mode == GizmoMode::kTranslate)
    {
        auto arrowhead_and_label = [&](ImVec2 from, ImVec2 to, ImU32 col, const char* lbl)
        {
            const float dx  = to.x - from.x;
            const float dy  = to.y - from.y;
            const float len = std::hypot(dx, dy);
            if (len < 6.0F)
            {
                return;
            }
            const float nx    = dx / len;
            const float ny    = dy / len;
            const float sx    = -ny;
            const float sy    = nx;
            const float kHead = std::clamp(len * 0.28F, 5.5F, 11.0F);
            const float kWing = kHead * 0.50F;
            dl->AddTriangleFilled(
                to,
                ImVec2(to.x - nx * kHead + sx * kWing, to.y - ny * kHead + sy * kWing),
                ImVec2(to.x - nx * kHead - sx * kWing, to.y - ny * kHead - sy * kWing),
                col);
            dl->AddText(ImVec2(to.x + nx * 8.0F - 4.0F, to.y + ny * 8.0F - 6.0F), col, lbl);
        };
        if (show_x) arrowhead_and_label(p_org, p_x, cx, "X");
        if (show_y) arrowhead_and_label(p_org, p_y, cy, "Y");
        if (show_z) arrowhead_and_label(p_org, p_z, cz, "Z");
    }
    else if (gizmo_state.mode == GizmoMode::kRotate)
    {
        constexpr int kRingSeg = 48;
        auto draw_ring = [&](cd::math::Vec3f u, cd::math::Vec3f v, ImU32 c, float t)
        {
            for (int i = 0; i < kRingSeg; ++i)
            {
                const float a = static_cast<float>(i) / kRingSeg * 2.0F * std::numbers::pi_v<float>;
                const float b = static_cast<float>(i + 1) / kRingSeg * 2.0F * std::numbers::pi_v<float>;
                const auto pa = project({ tgt.x + (u.x * std::cos(a) + v.x * std::sin(a)) * kAxisLen,
                                          tgt.y + (u.y * std::cos(a) + v.y * std::sin(a)) * kAxisLen,
                                          tgt.z + (u.z * std::cos(a) + v.z * std::sin(a)) * kAxisLen });
                const auto pb = project({ tgt.x + (u.x * std::cos(b) + v.x * std::sin(b)) * kAxisLen,
                                          tgt.y + (u.y * std::cos(b) + v.y * std::sin(b)) * kAxisLen,
                                          tgt.z + (u.z * std::cos(b) + v.z * std::sin(b)) * kAxisLen });
                if (pa.x >= 0.0F && pb.x >= 0.0F)
                {
                    dl->AddLine(pa, pb, c, t);
                }
            }
        };
        draw_ring({ 0, 1, 0 }, { 0, 0, 1 }, cx, (gizmo.hover() == cd::editor::GizmoAxis::kX) ? 4.0F : 2.0F);
        draw_ring({ 1, 0, 0 }, { 0, 0, 1 }, cy, (gizmo.hover() == cd::editor::GizmoAxis::kY) ? 4.0F : 2.0F);
        draw_ring({ 1, 0, 0 }, { 0, 1, 0 }, cz, (gizmo.hover() == cd::editor::GizmoAxis::kZ) ? 4.0F : 2.0F);
    }
    else
    {
        auto cube_at = [&](ImVec2 c, ImU32 col, const char* lbl)
        {
            dl->AddRectFilled(ImVec2(c.x - 5.0F, c.y - 5.0F), ImVec2(c.x + 5.0F, c.y + 5.0F), col);
            dl->AddText(ImVec2(c.x + 7.0F, c.y - 6.0F), col, lbl);
        };
        if (show_x) cube_at(p_x, cx, "X");
        if (show_y) cube_at(p_y, cy, "Y");
        if (show_z) cube_at(p_z, cz, "Z");
    }

    // Hover test
    const ImVec2 mp = ImGui::GetIO().MousePos;
    auto dist_to_seg = [](ImVec2 a, ImVec2 b, ImVec2 p) -> float
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float l2 = dx * dx + dy * dy;
        if (l2 < 1e-4F)
        {
            return std::hypot(p.x - a.x, p.y - a.y);
        }
        const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0F, 1.0F);
        return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
    };

    cd::editor::GizmoAxis best = cd::editor::GizmoAxis::kNone;
    float best_d = gizmo.hover_tolerance_pixels;
    const float center_dist_px = std::hypot(mp.x - p_org.x, mp.y - p_org.y);
    if (center_dist_px <= 13.5F)
    {
        best   = cd::editor::GizmoAxis::kXYZ;
        best_d = center_dist_px;
    }
    else if (gizmo_state.mode == GizmoMode::kRotate)
    {
        constexpr int kHoverSeg = 48;
        auto ring_dist = [&](cd::math::Vec3f u, cd::math::Vec3f v) -> float
        {
            float min_d = std::numeric_limits<float>::infinity();
            ImVec2 prev {};
            bool prev_ok = false;
            for (int i = 0; i <= kHoverSeg; ++i)
            {
                const float a = static_cast<float>(i) / kHoverSeg * 2.0F * std::numbers::pi_v<float>;
                const auto pw = project({ tgt.x + (u.x * std::cos(a) + v.x * std::sin(a)) * kAxisLen,
                                          tgt.y + (u.y * std::cos(a) + v.y * std::sin(a)) * kAxisLen,
                                          tgt.z + (u.z * std::cos(a) + v.z * std::sin(a)) * kAxisLen });
                if (pw.x >= 0.0F)
                {
                    if (prev_ok)
                    {
                        min_d = std::min(dist_to_seg(prev, pw, mp), min_d);
                    }
                    prev    = pw;
                    prev_ok = true;
                }
                else
                {
                    prev_ok = false;
                }
            }
            return min_d;
        };
        if (const float dx = ring_dist({ 0, 1, 0 }, { 0, 0, 1 }); dx < best_d) { best_d = dx; best = cd::editor::GizmoAxis::kX; }
        if (const float dy = ring_dist({ 1, 0, 0 }, { 0, 0, 1 }); dy < best_d) { best_d = dy; best = cd::editor::GizmoAxis::kY; }
        if (const float dz = ring_dist({ 1, 0, 0 }, { 0, 1, 0 }); dz < best_d) { best_d = dz; best = cd::editor::GizmoAxis::kZ; }
    }
    else
    {
        if (show_x) { if (const float d = dist_to_seg(p_org, p_x, mp); d < best_d) { best_d = d; best = cd::editor::GizmoAxis::kX; } }
        if (show_y) { if (const float d = dist_to_seg(p_org, p_y, mp); d < best_d) { best_d = d; best = cd::editor::GizmoAxis::kY; } }
        if (show_z) { if (const float d = dist_to_seg(p_org, p_z, mp); d < best_d) { best_d = d; best = cd::editor::GizmoAxis::kZ; } }
    }

    const bool over_imgui_ui =
        cd::ui::chroma::state().mouse_in_any_window ||
        !cd::ui::chroma::state().active_menu_name.empty() ||
        cd::ui::chroma::state().open_popup_id != 0U ||
        (ImGui::GetIO().WantCaptureMouse && ImGui::IsAnyItemHovered());
    if (over_imgui_ui && !gizmo.is_dragging())
    {
        best = cd::editor::GizmoAxis::kNone;
    }
    gizmo.set_hover(best);
    gizmo_state.was_hovered = (best != cd::editor::GizmoAxis::kNone);
    if (pending_pick && best != cd::editor::GizmoAxis::kNone)
    {
        pending_pick = false;
    }

    const auto cam_basis = cd::app_common::compute_camera_basis(cam.eye, cam.target);

    auto ray_view_plane_hit = [&](ImVec2 mouse_pixel, cd::math::Vec3f plane_org) -> std::optional<cd::math::Vec3f>
    {
        const auto vw_w = static_cast<float>(window.width());
        const auto vh_w = static_cast<float>(window.height());
        if (vw_w < 1.0F || vh_w < 1.0F)
        {
            return std::nullopt;
        }
        const auto rdir = cd::app_common::screen_pixel_to_world_ray(
            cam_basis, cam.fov_y, 0.0F, 0.0F, vw_w, vh_w, mouse_pixel.x, mouse_pixel.y);
        return cd::app_common::ray_plane_hit_point(cam.eye, rdir, plane_org, cam_basis.forward);
    };

    auto ray_axis_offset = [&](cd::editor::GizmoAxis axis,
                               ImVec2 mouse_pixel,
                               cd::math::Vec3f world_start) -> std::optional<float>
    {
        const auto vw_w = static_cast<float>(window.width());
        const auto vh_w = static_cast<float>(window.height());
        if (vw_w < 1.0F || vh_w < 1.0F)
        {
            return std::nullopt;
        }
        const auto rdir = cd::app_common::screen_pixel_to_world_ray(
            cam_basis, cam.fov_y, 0.0F, 0.0F, vw_w, vh_w, mouse_pixel.x, mouse_pixel.y);
        cd::math::Vec3f a { 0.0F, 0.0F, 0.0F };
        if (axis == cd::editor::GizmoAxis::kX)      a = { 1.0F, 0.0F, 0.0F };
        else if (axis == cd::editor::GizmoAxis::kY) a = { 0.0F, 1.0F, 0.0F };
        else if (axis == cd::editor::GizmoAxis::kZ) a = { 0.0F, 0.0F, 1.0F };

        const cd::math::Vec3f c1 {
            cam_basis.forward.y * a.z - cam_basis.forward.z * a.y,
            cam_basis.forward.z * a.x - cam_basis.forward.x * a.z,
            cam_basis.forward.x * a.y - cam_basis.forward.y * a.x
        };
        cd::math::Vec3f n {
            a.y * c1.z - a.z * c1.y,
            a.z * c1.x - a.x * c1.z,
            a.x * c1.y - a.y * c1.x
        };
        const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (nl < 1e-5F)
        {
            return std::nullopt;
        }
        n.x /= nl; n.y /= nl; n.z /= nl;
        const auto hit = cd::app_common::ray_plane_hit_point(cam.eye, rdir, world_start, n);
        if (!hit.has_value())
        {
            return std::nullopt;
        }
        return (hit->x - world_start.x) * a.x +
               (hit->y - world_start.y) * a.y +
               (hit->z - world_start.z) * a.z;
    };

    if (!gizmo.is_dragging() && best != cd::editor::GizmoAxis::kNone &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !over_imgui_ui)
    {
        gizmo.begin_drag(best, *target_pos);
        gizmo_state.drag_anchor      = mp;
        gizmo_state.drag_world_start = *target_pos;
        if (lt != nullptr)
        {
            gizmo_state.drag_scale_start = lt->value.scale;
            gizmo_state.drag_rot_start   = lt->value.rotation;
        }
        if (s.selected_kind == SelKind::kLight && s.selected >= 0 &&
            std::cmp_less(s.selected, s.lights.size()))
        {
            const auto& L = s.lights[static_cast<std::size_t>(s.selected)].light;
            gizmo_state.light_drag_dir_start     = L.direction;
            gizmo_state.light_drag_tangent_start = L.area_tangent;
            gizmo_state.light_drag_range_start   = L.range;
            gizmo_state.light_drag_area_w_start  = L.area_width;
            gizmo_state.light_drag_area_h_start  = L.area_height;
        }
        if (best == cd::editor::GizmoAxis::kXYZ)
        {
            if (auto hit = ray_view_plane_hit(mp, *target_pos); hit.has_value())
            {
                gizmo_state.drag_plane_hit_start = *hit;
                gizmo_state.drag_use_ray_plane   = true;
            }
            else
            {
                gizmo_state.drag_plane_hit_start = *target_pos;
                gizmo_state.drag_use_ray_plane   = false;
            }
        }
        else if (auto off = ray_axis_offset(best, mp, *target_pos); off.has_value())
        {
            gizmo_state.drag_initial_offset = *off;
            gizmo_state.drag_use_ray_plane  = true;
        }
        else
        {
            gizmo_state.drag_use_ray_plane = false;
        }
    }

    if (!gizmo.is_dragging())
    {
        return;
    }

    const bool is_xyz = (gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ);
    ImVec2 axis_screen_end = p_x;
    if (gizmo.active_axis() == cd::editor::GizmoAxis::kY)      axis_screen_end = p_y;
    else if (gizmo.active_axis() == cd::editor::GizmoAxis::kZ) axis_screen_end = p_z;
    const float ax_dx     = axis_screen_end.x - p_org.x;
    const float ax_dy     = axis_screen_end.y - p_org.y;
    const float ax_len_px = std::hypot(ax_dx, ax_dy);

    float delta_world = 0.0F;
    if (!is_xyz && gizmo_state.drag_use_ray_plane && gizmo_state.mode == GizmoMode::kTranslate)
    {
        if (auto off = ray_axis_offset(gizmo.active_axis(), mp, gizmo_state.drag_world_start); off.has_value())
        {
            delta_world = *off - gizmo_state.drag_initial_offset;
        }
    }
    if (is_xyz)
    {
        delta_world = ((mp.x - gizmo_state.drag_anchor.x) - (mp.y - gizmo_state.drag_anchor.y)) * 0.015F;
    }
    else if (ax_len_px > 1.0F && (!gizmo_state.drag_use_ray_plane || gizmo_state.mode != GizmoMode::kTranslate))
    {
        const float dot_px = (mp.x - gizmo_state.drag_anchor.x) * (ax_dx / ax_len_px) +
                             (mp.y - gizmo_state.drag_anchor.y) * (ax_dy / ax_len_px);
        delta_world = dot_px * (kAxisLen / ax_len_px);
    }

    if (is_xyz || ax_len_px > 1.0F || gizmo_state.drag_use_ray_plane)
    {
        auto& w = studio_world();
        switch (gizmo_state.mode)
        {
            case GizmoMode::kTranslate:
            {
                cd::math::Vec3f cur = gizmo_state.drag_world_start;
                switch (gizmo.active_axis())
                {
                    case cd::editor::GizmoAxis::kX: cur.x += delta_world; break;
                    case cd::editor::GizmoAxis::kY: cur.y += delta_world; break;
                    case cd::editor::GizmoAxis::kZ: cur.z += delta_world; break;
                    case cd::editor::GizmoAxis::kXYZ:
                    {
                        bool applied = false;
                        if (gizmo_state.drag_use_ray_plane)
                        {
                            if (auto hit = ray_view_plane_hit(mp, gizmo_state.drag_world_start); hit.has_value())
                            {
                                cur.x += hit->x - gizmo_state.drag_plane_hit_start.x;
                                cur.y += hit->y - gizmo_state.drag_plane_hit_start.y;
                                cur.z += hit->z - gizmo_state.drag_plane_hit_start.z;
                                applied = true;
                            }
                        }
                        if (!applied)
                        {
                            const float mouse_dx = mp.x - gizmo_state.drag_anchor.x;
                            const float mouse_dy = mp.y - gizmo_state.drag_anchor.y;
                            const float world_per_px = std::max(0.5F, cam_dist) * 0.0022F;
                            cur.x += (cam_basis.right.x * mouse_dx - cam_basis.up.x * mouse_dy) * world_per_px;
                            cur.y += (cam_basis.right.y * mouse_dx - cam_basis.up.y * mouse_dy) * world_per_px;
                            cur.z += (cam_basis.right.z * mouse_dx - cam_basis.up.z * mouse_dy) * world_per_px;
                        }
                        break;
                    }
                    default: break;
                }
                if (w.grid_snap_enabled)
                {
                    cur = cd::app_common::snap_vec3_to_grid(cur, w.grid_snap_step);
                }
                *target_pos = cur;
                gizmo.update_drag(cur);
                break;
            }
            case GizmoMode::kScale:
            {
                const float factor = std::exp(delta_world * 0.5F);
                if (lt != nullptr)
                {
                    cd::math::Vec3f cur = gizmo_state.drag_scale_start;
                    switch (gizmo.active_axis())
                    {
                        case cd::editor::GizmoAxis::kX:   cur.x *= factor; break;
                        case cd::editor::GizmoAxis::kY:   cur.y *= factor; break;
                        case cd::editor::GizmoAxis::kZ:   cur.z *= factor; break;
                        case cd::editor::GizmoAxis::kXYZ: cur.x *= factor; cur.y *= factor; cur.z *= factor; break;
                        default: break;
                    }
                    lt->value.scale = { std::max(cur.x, 0.05F), std::max(cur.y, 0.05F), std::max(cur.z, 0.05F) };
                }
                else if (s.selected_kind == SelKind::kLight && s.selected >= 0 &&
                         std::cmp_less(s.selected, s.lights.size()))
                {
                    auto& L = s.lights[static_cast<std::size_t>(s.selected)].light;
                    if (L.type == cd::light::LightType::kPoint || L.type == cd::light::LightType::kSpot)
                    {
                        L.range = std::clamp(gizmo_state.light_drag_range_start * factor, 0.1F, 200.0F);
                    }
                    else if (L.type == cd::light::LightType::kRectArea || L.type == cd::light::LightType::kDiskArea)
                    {
                        L.area_width  = std::clamp(gizmo_state.light_drag_area_w_start * factor, 0.05F, 50.0F);
                        L.area_height = std::clamp(gizmo_state.light_drag_area_h_start * factor, 0.05F, 50.0F);
                    }
                }
                break;
            }
            case GizmoMode::kRotate:
            {
                cd::math::Quatf q { 0.0F, 0.0F, 0.0F, 1.0F };
                if (gizmo.active_axis() == cd::editor::GizmoAxis::kXYZ)
                {
                    const float yaw_ang   = (mp.x - gizmo_state.drag_anchor.x) * 0.012F;
                    const float pitch_ang = (mp.y - gizmo_state.drag_anchor.y) * 0.012F;
                    const cd::math::Quatf q_yaw {
                        cam_basis.up.x * std::sin(yaw_ang * 0.5F),
                        cam_basis.up.y * std::sin(yaw_ang * 0.5F),
                        cam_basis.up.z * std::sin(yaw_ang * 0.5F),
                        std::cos(yaw_ang * 0.5F)
                    };
                    const cd::math::Quatf q_pitch {
                        cam_basis.right.x * std::sin(pitch_ang * 0.5F),
                        cam_basis.right.y * std::sin(pitch_ang * 0.5F),
                        cam_basis.right.z * std::sin(pitch_ang * 0.5F),
                        std::cos(pitch_ang * 0.5F)
                    };
                    q = {
                        q_yaw.w * q_pitch.x + q_yaw.x * q_pitch.w + q_yaw.y * q_pitch.z - q_yaw.z * q_pitch.y,
                        q_yaw.w * q_pitch.y - q_yaw.x * q_pitch.z + q_yaw.y * q_pitch.w + q_yaw.z * q_pitch.x,
                        q_yaw.w * q_pitch.z + q_yaw.x * q_pitch.y - q_yaw.y * q_pitch.x + q_yaw.z * q_pitch.w,
                        q_yaw.w * q_pitch.w - q_yaw.x * q_pitch.x - q_yaw.y * q_pitch.y - q_yaw.z * q_pitch.z
                    };
                }
                else
                {
                    const float anchor_dx = gizmo_state.drag_anchor.x - p_org.x;
                    const float anchor_dy = gizmo_state.drag_anchor.y - p_org.y;
                    if (std::hypot(anchor_dx, anchor_dy) < 5.0F)
                    {
                        break;
                    }
                    float ang = std::atan2(anchor_dy, anchor_dx) -
                                std::atan2(mp.y - p_org.y, mp.x - p_org.x);
                    while (ang > std::numbers::pi_v<float>)  ang -= 2.0F * std::numbers::pi_v<float>;
                    while (ang < -std::numbers::pi_v<float>) ang += 2.0F * std::numbers::pi_v<float>;
                    const float ca = std::cos(ang * 0.5F);
                    const float sa = std::sin(ang * 0.5F);
                    if (gizmo.active_axis() == cd::editor::GizmoAxis::kX)      q = { sa, 0.0F, 0.0F, ca };
                    else if (gizmo.active_axis() == cd::editor::GizmoAxis::kY) q = { 0.0F, sa, 0.0F, ca };
                    else if (gizmo.active_axis() == cd::editor::GizmoAxis::kZ) q = { 0.0F, 0.0F, sa, ca };
                }
                if (lt != nullptr)
                {
                    const auto& b = gizmo_state.drag_rot_start;
                    lt->value.rotation = {
                        q.w * b.x + q.x * b.w + q.y * b.z - q.z * b.y,
                        q.w * b.y - q.x * b.z + q.y * b.w + q.z * b.x,
                        q.w * b.z + q.x * b.y - q.y * b.x + q.z * b.w,
                        q.w * b.w - q.x * b.x - q.y * b.y - q.z * b.z
                    };
                }
                else if (s.selected_kind == SelKind::kLight && s.selected >= 0 &&
                         std::cmp_less(s.selected, s.lights.size()))
                {
                    auto rotate_v = [&](cd::math::Vec3f v) -> cd::math::Vec3f
                    {
                        const cd::math::Vec3f t { q.w * v.x + q.y * v.z - q.z * v.y,
                                                  q.w * v.y + q.z * v.x - q.x * v.z,
                                                  q.w * v.z + q.x * v.y - q.y * v.x };
                        const float tw = -(q.x * v.x + q.y * v.y + q.z * v.z);
                        return { tw * -q.x + t.x * q.w + t.y * -q.z - t.z * -q.y,
                                 tw * -q.y - t.x * -q.z + t.y * q.w + t.z * -q.x,
                                 tw * -q.z + t.x * -q.y - t.y * -q.x + t.z * q.w };
                    };
                    auto norm_v = [](cd::math::Vec3f v) -> cd::math::Vec3f
                    {
                        const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                        return (l > 1e-5F) ? cd::math::Vec3f { v.x / l, v.y / l, v.z / l } : v;
                    };
                    auto& Lr   = s.lights[static_cast<std::size_t>(s.selected)].light;
                    Lr.direction = norm_v(rotate_v(gizmo_state.light_drag_dir_start));
                    auto rt    = rotate_v(gizmo_state.light_drag_tangent_start);
                    const float pr = rt.x * Lr.direction.x + rt.y * Lr.direction.y + rt.z * Lr.direction.z;
                    rt.x -= pr * Lr.direction.x;
                    rt.y -= pr * Lr.direction.y;
                    rt.z -= pr * Lr.direction.z;
                    Lr.area_tangent = norm_v(rt);
                }
                break;
            }
        }
        if (target_is_entity && lt != nullptr)
        {
            w.home_transforms[sel_ent.id] = lt->value;
            w.euler_cache_deg[sel_ent.id] = cd::app_common::quat_to_euler_deg(lt->value.rotation);
            s.scene.update_transforms();
            s.last_tlas_scene_hash = 0;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        (void)gizmo.end_drag();
        switch (gizmo_state.mode)
        {
            case GizmoMode::kTranslate:
            {
                const float dx = target_pos->x - gizmo_state.drag_world_start.x;
                const float dy = target_pos->y - gizmo_state.drag_world_start.y;
                const float dz = target_pos->z - gizmo_state.drag_world_start.z;
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 1e-4F && target_is_entity && lt != nullptr)
                {
                    *target_pos = gizmo_state.drag_world_start;
                    s.history.push(std::make_unique<cd::editor::TranslateCommand>(
                        s.scene, sel_ent, cd::math::Vec3f { dx, dy, dz }));
                    s.push_log("[gizmo] entity translate (undoable)");
                }
                break;
            }
            case GizmoMode::kScale:
                if (target_is_entity && lt != nullptr)
                {
                    const auto end_sc = lt->value.scale;
                    const auto st_sc  = gizmo_state.drag_scale_start;
                    if (std::abs(end_sc.x - st_sc.x) + std::abs(end_sc.y - st_sc.y) + std::abs(end_sc.z - st_sc.z) > 1e-4F)
                    {
                        const cd::math::Vec3f factor {
                            (std::abs(st_sc.x) > 1e-5F) ? (end_sc.x / st_sc.x) : 1.0F,
                            (std::abs(st_sc.y) > 1e-5F) ? (end_sc.y / st_sc.y) : 1.0F,
                            (std::abs(st_sc.z) > 1e-5F) ? (end_sc.z / st_sc.z) : 1.0F
                        };
                        lt->value.scale = st_sc;
                        s.history.push(std::make_unique<cd::editor::ScaleCommand>(s.scene, sel_ent, factor));
                        s.push_log("[gizmo] entity scale (undoable)");
                    }
                }
                break;
            case GizmoMode::kRotate:
                if (target_is_entity && lt != nullptr)
                {
                    const auto end_rot = lt->value.rotation;
                    const auto st_rot  = gizmo_state.drag_rot_start;
                    if (std::abs(end_rot.x - st_rot.x) + std::abs(end_rot.y - st_rot.y) +
                        std::abs(end_rot.z - st_rot.z) + std::abs(end_rot.w - st_rot.w) > 1e-4F)
                    {
                        lt->value.rotation = st_rot;
                        s.history.push(std::make_unique<cd::editor::RotateCommand>(s.scene, sel_ent, end_rot));
                        s.push_log("[gizmo] entity rotate (undoable)");
                    }
                }
                break;
        }
        if (target_is_entity && lt != nullptr)
        {
            auto& w = studio_world();
            w.home_transforms[sel_ent.id] = lt->value;
            w.euler_cache_deg[sel_ent.id] = cd::app_common::quat_to_euler_deg(lt->value.rotation);
            s.scene.update_transforms();
            s.last_tlas_scene_hash = 0;
        }
    }
}

void draw_command_palette(StudioEngineState& s, cd::rhi::Extent2D extent)
{
    if (!s.palette_visible)
    {
        return;
    }
    const auto vw_p = static_cast<float>(extent.width);
    constexpr float kW = 520.0F;
    constexpr float kH = 360.0F;
    ImGui::SetNextWindowPos(ImVec2((vw_p - kW) * 0.5F, 64.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kW, kH), ImGuiCond_Always);
    if (ImGui::Begin("Command Palette", &s.palette_visible,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
    {
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        char buf[128] {};
        std::snprintf(buf, sizeof(buf), "%s", s.palette_query.c_str());
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::InputTextWithHint("##palette_q", "Type command...", buf, sizeof(buf)))
        {
            s.palette_query = buf;
        }
        ImGui::Separator();
        const auto hits = s.palette.filter(s.palette_query);
        if (hits.empty())
        {
            ImGui::TextDisabled("No matching command (%zu registered)", s.palette.size());
        }
        else
        {
            for (std::size_t i = 0; i < hits.size() && i < 24U; ++i)
            {
                const auto& e = s.palette.at(hits[i]);
                if (ImGui::Selectable(("  " + e.label).c_str()))
                {
                    (void)s.palette.invoke(hits[i]);
                    s.palette_visible = false;
                    s.palette_query.clear();
                    break;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                (void)s.palette.invoke(hits.front());
                s.palette_visible = false;
                s.palette_query.clear();
            }
        }
    }
    ImGui::End();
}

void draw_workspace_shell(StudioEngineState& s,
                          float dt,
                          const cd::math::Mat4f& vp,
                          std::uint32_t frame_w,
                          std::uint32_t frame_h)
{
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();

    if (w.show_project_manager)
    {
        draw_project_hub_modal(s, frame_w, frame_h);
        return;
    }

    if (cst.workspace_mode == 3)
    {
        workspace_2d::draw_workspace_2d(s, dt, frame_w, frame_h);
        return;
    }

    physics_pie::draw_viewport_colliders_and_drop_targets(s, vp, frame_w, frame_h);

    if (s.pie_playing || cst.workspace_mode == 1)
    {
        draw_pie_gameplay_hud(s, dt, vp, frame_w, frame_h);
        return;
    }

    if (cst.workspace_mode == 2)
    {
        draw_shader_graph_workspace(s, frame_w, frame_h);
        return;
    }

    // Default Clean 3D Studio Workspace (Source 2 Hammer 5 + Godot 4 layout)
    draw_viewport_overlay_strip_and_rose(s, frame_w, frame_h);
    draw_scene_tree_dock(s);
    draw_inspector_dock(s);
    draw_project_and_tools_dock(s);
}

}  // namespace cd::studio::ui
