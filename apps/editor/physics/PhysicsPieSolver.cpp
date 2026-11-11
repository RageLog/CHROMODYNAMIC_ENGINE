// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/physics/PhysicsPieSolver.cpp
// =============================================================================
#include "physics/PhysicsPieSolver.hpp"

#include "pipeline/AssetIngest.hpp"
#include "scene/SceneOperations.hpp"
#include "StudioPhysicsSolver.hpp"
#include "StudioToolbar.hpp"

#include <cd/ui/ChromaUi.hpp>

#include <algorithm>
#include <utility>

namespace cd::studio::physics_pie
{

namespace
{
auto make_logger(StudioEngineState& s)
{
    return [&s](std::string msg) { s.push_log(std::move(msg)); };
}
}  // namespace

void enter_pie(StudioEngineState& s)
{
    cd::studio::enter_pie_play_mode(s, make_logger(s));
}

void exit_pie(StudioEngineState& s)
{
    cd::studio::exit_pie_play_mode(s, make_logger(s));
}

void reset_physics_positions(StudioEngineState& s)
{
    cd::studio::reset_physics_to_home_positions(s, make_logger(s));
}

void tick_modifiers_and_physics(StudioEngineState& s, float dt)
{
    cd::studio::tick_studio_modifiers_and_physics(s, dt, make_logger(s));
}

void draw_viewport_colliders_and_drop_targets(StudioEngineState& s,
                                              const cd::math::Mat4f& vp,
                                              std::uint32_t width,
                                              std::uint32_t height)
{
    auto& w   = cd::studio::studio_world();
    auto& cst = cd::ui::chroma::state();
    const float sw = static_cast<float>(width);
    const float sh = static_cast<float>(height);
    if (sw < 32.0F || sh < 32.0F)
    {
        return;
    }

    auto* dl = ImGui::GetBackgroundDrawList();

    // 1. Optional Collider Wireframes & Active Contact Normals
    for (std::size_t ei = 0; ei < s.entities.size(); ++ei)
    {
        const auto& ent = s.entities[ei];
        if (!ent.visible)
        {
            continue;
        }
        const bool is_sel = (s.selected_kind == SelKind::kEntity && std::cmp_equal(s.selected, ei));
        if (!w.show_all_collider_wires && !is_sel)
        {
            continue;
        }
        const auto* lt = s.scene.local(ent.handle);
        if (lt == nullptr)
        {
            continue;
        }
        auto& ms = w.entity_modifiers[ent.handle.id];
        if (const auto* trg = ms.find<TriggerVolumeModifier>(); trg != nullptr && trg->enabled)
        {
            cd::studio::draw_wireframe_sphere_3d(
                dl, lt->value.position, trg->trigger_radius, vp, sw, sh,
                IM_COL32(255, 195, 65, 190), 1.4F);
        }
        if (const auto* col = ms.find_collider(); col != nullptr && col->enabled && (w.show_all_collider_wires || is_sel))
        {
            const ImU32 wire_col = is_sel ? IM_COL32(95, 245, 145, 220) : IM_COL32(75, 200, 120, 130);
            const float thick    = is_sel ? 1.8F : 1.1F;
            if (const auto* sph = dynamic_cast<const SphereColliderModifier*>(col))
            {
                cd::studio::draw_wireframe_sphere_3d(
                    dl, lt->value.position + col->center_offset, sph->radius, vp, sw, sh, wire_col, thick);
            }
            else
            {
                cd::studio::draw_wireframe_obb_3d(dl, lt->value, col, vp, sw, sh, wire_col, thick);
            }
        }
    }

    if (w.show_all_collider_wires)
    {
        for (const auto& cp : w.live_contacts)
        {
            cd::studio::draw_3d_line_clipped(
                dl, cp.point, cp.point + cp.normal * 0.65F, vp, sw, sh,
                IM_COL32(255, 85, 85, 235), 2.2F);
        }
    }

    // 2. Central 3D Viewport Drag-and-Drop Landing (Constructs, Modifiers, Meshes, Textures)
    if (s.pie_playing)
    {
        return;
    }

    const ImVec2 mp       = ImGui::GetMousePos();
    const float  vp_left  = sw * cst.dock_left_w_ratio;
    const float  vp_right = sw * (1.0F - cst.dock_right_w_ratio);
    const float  vp_top   = cst.top_bar_height;
    const float  vp_bot   = sh * (1.0F - cst.dock_bottom_h_ratio);
    const bool   in_vp    = (mp.x >= vp_left && mp.x <= vp_right && mp.y >= vp_top && mp.y <= vp_bot);

    if (w.is_dragging_spawn)
    {
        w.drag_preview_pos = cd::studio::unproject_mouse_to_ground(s, mp, sw, sh, 0.5F);
        if (in_vp)
        {
            const cd::physics::Aabb box {
                w.drag_preview_pos - cd::math::Vec3f { 0.5F, 0.5F, 0.5F },
                w.drag_preview_pos + cd::math::Vec3f { 0.5F, 0.5F, 0.5F }
            };
            cd::studio::draw_wireframe_aabb_3d(dl, box, vp, sw, sh, IM_COL32(60, 215, 255, 255), 2.4F);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (in_vp && !w.drag_spawn_id.empty())
            {
                (void)scene_ops::spawn_entity_by_preset(s, w.drag_spawn_id, w.drag_preview_pos);
            }
            w.is_dragging_spawn = false;
            w.drag_spawn_id.clear();
        }
    }

    if (w.is_dragging_modifier)
    {
        w.drag_hover_entity_idx = in_vp ? cd::studio::pick_entity_under_mouse(s, vp, mp, sw, sh) : -1;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (w.drag_hover_entity_idx >= 0 &&
                std::cmp_less(w.drag_hover_entity_idx, s.entities.size()) &&
                !w.drag_modifier_class.empty())
            {
                auto& ent = s.entities[static_cast<std::size_t>(w.drag_hover_entity_idx)];
                if (auto mod = cd::studio::instantiate_modifier_by_class(w.drag_modifier_class))
                {
                    w.entity_modifiers[ent.handle.id].add_or_replace(std::move(mod));
                    s.selected      = w.drag_hover_entity_idx;
                    s.selected_kind = SelKind::kEntity;
                }
            }
            w.is_dragging_modifier  = false;
            w.drag_modifier_class.clear();
            w.drag_hover_entity_idx = -1;
        }
    }

    if (w.is_dragging_asset_mesh || w.is_dragging_asset_texture)
    {
        w.drag_hover_entity_idx = in_vp ? cd::studio::pick_entity_under_mouse(s, vp, mp, sw, sh) : -1;
        const cd::math::Vec3f drop_pos = cd::studio::unproject_mouse_to_ground(s, mp, sw, sh, 0.5F);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (in_vp)
            {
                if (w.is_dragging_asset_mesh && !w.drag_asset_mesh_id.empty())
                {
                    int idx = w.drag_hover_entity_idx;
                    if (idx < 0)
                    {
                        idx = scene_ops::spawn_entity_by_preset(s, "Dynamic_Cube", drop_pos);
                    }
                    if (idx >= 0)
                    {
                        (void)pipeline::assign_mesh_to_entity(s, idx, w.drag_asset_mesh_id);
                        s.selected      = idx;
                        s.selected_kind = SelKind::kEntity;
                    }
                }
                if (w.is_dragging_asset_texture && !w.drag_asset_texture_id.empty())
                {
                    int idx = w.drag_hover_entity_idx;
                    if (idx < 0)
                    {
                        idx = scene_ops::spawn_entity_by_preset(s, "Dynamic_Cube", drop_pos);
                    }
                    if (idx >= 0)
                    {
                        (void)pipeline::assign_texture_to_entity(s, idx, w.drag_asset_texture_id);
                        s.selected      = idx;
                        s.selected_kind = SelKind::kEntity;
                    }
                }
            }
            w.is_dragging_asset_mesh    = false;
            w.drag_asset_mesh_id.clear();
            w.is_dragging_asset_texture = false;
            w.drag_asset_texture_id.clear();
            w.drag_hover_entity_idx     = -1;
        }
    }
}

}  // namespace cd::studio::physics_pie
