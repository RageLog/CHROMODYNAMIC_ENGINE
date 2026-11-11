// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/InspectorDock.cpp
// =============================================================================
#include "ui/InspectorDock.hpp"

#include "physics/PhysicsPieSolver.hpp"
#include "pipeline/AssetIngest.hpp"
#include "scene/SceneOperations.hpp"
#include "StudioModifierClasses.hpp"
#include "StudioWorkspacePanels.hpp"

#include <cd/app_common/CameraMath3D.hpp>
#include <cd/ui/ChromaUi.hpp>
#include <cd/world_container/World.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace cd::studio::ui
{

namespace
{

[[nodiscard]] std::string active_mesh_display_name(const SceneEntity& ent)
{
    if (!ent.custom_mesh_path.empty())
    {
        return std::filesystem::path(ent.custom_mesh_path).filename().string();
    }
    switch (ent.kind)
    {
        case PrimitiveKind::kSphere:     return "Sphere.mesh";
        case PrimitiveKind::kCone:       return "Cone.mesh";
        case PrimitiveKind::kCylinder:   return "Cylinder.mesh";
        case PrimitiveKind::kTorus:      return "Torus.mesh";
        case PrimitiveKind::kTorusKnot:  return "TorusKnot.mesh";
        case PrimitiveKind::kGltf:       return "CesiumMan.glb";
        case PrimitiveKind::kSponza:     return "Sponza.gltf";
        case PrimitiveKind::kCustomGltf: return "CustomMesh.obj";
        case PrimitiveKind::kCube:
        default:                         return "Cube.mesh";
    }
}

[[nodiscard]] std::string active_texture_display_name(const SceneEntity& ent)
{
    if (!ent.use_texture)
    {
        return "None (Solid Albedo)";
    }
    if (!ent.custom_texture_path.empty())
    {
        return std::filesystem::path(ent.custom_texture_path).filename().string();
    }
    switch (ent.custom_texture_id)
    {
        case 1:  return "Earth_Albedo.png";
        case 2:  return "UV_Checkerboard_2K.tex";
        case 3:  return "Gold_Leaf_24K.tex";
        case 4:  return "Carbon_Fiber_Twill.tex";
        case 5:  return "Carrara_Marble.tex";
        case 6:  return "Oak_Wood_Planks.tex";
        default: return "Procedural_Texture.tex";
    }
}

void draw_component_stack_cards(StudioEngineState& s,
                                ModifierStack& stack,
                                bool is_environment_stack)
{
    auto& w = studio_world();
    int remove_idx = -1;
    int move_up_idx = -1;

    for (std::size_t mi = 0; mi < stack.items.size(); ++mi)
    {
        auto& mod = stack.items[mi];
        if (!mod)
        {
            continue;
        }
        ImGui::PushID(static_cast<int>(20000 + mi));

        ImGui::Checkbox("##mod_en", &mod->enabled);
        ImGui::SameLine(0.0F, 6.0F);

        const std::string hdr_label = std::string(mod->display_name()) + "##mod_hdr";
        const bool open = ImGui::TreeNodeEx(
            hdr_label.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);

        if (ImGui::BeginPopupContextItem("mod_card_ctx"))
        {
            if (mi > 0U && ImGui::MenuItem("Move Up"))
            {
                move_up_idx = static_cast<int>(mi);
            }
            if (ImGui::MenuItem("Remove Component"))
            {
                remove_idx = static_cast<int>(mi);
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            if (mod->enabled)
            {
                mod->draw_inspector_ui();
            }
            else
            {
                ImGui::TextDisabled("Component disabled");
            }
            if (ImGui::SmallButton("Remove##rm_btn"))
            {
                remove_idx = static_cast<int>(mi);
            }
            if (mi > 0U)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Move Up##up_btn"))
                {
                    move_up_idx = static_cast<int>(mi);
                }
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    if (move_up_idx > 0 && std::cmp_less(move_up_idx, stack.items.size()))
    {
        const auto idx = static_cast<std::size_t>(move_up_idx);
        std::swap(stack.items[idx - 1U], stack.items[idx]);
    }
    if (remove_idx >= 0 && std::cmp_less(remove_idx, stack.items.size()))
    {
        const std::string removed_name { stack.items[static_cast<std::size_t>(remove_idx)]->display_name() };
        stack.items.erase(stack.items.begin() + remove_idx);
        s.push_log("[inspector] Removed component: " + removed_name);
    }

    const char* btn_lbl = is_environment_stack
        ? "+ Add Environment Effect...##add_env_comp"
        : "+ Add Component...##add_ent_comp";
    const char* pop_id = is_environment_stack
        ? "insp_add_env_comp_popup"
        : "insp_add_ent_comp_popup";

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.46F, 0.76F, 1.0F));
    if (ImGui::Button(btn_lbl, ImVec2(-1.0F, 26.0F)))
    {
        ImGui::OpenPopup(pop_id);
    }
    ImGui::PopStyleColor();

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
        {
            const char* cls_id = static_cast<const char*>(mp->Data);
            if (auto inst = instantiate_modifier_by_class(cls_id))
            {
                stack.add_or_replace(std::move(inst));
                w.is_dragging_modifier = false;
                s.push_log(std::string("[inspector] Attached component: ") + cls_id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopup(pop_id))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F),
                           is_environment_stack ? "Environment & Post-FX" : "Add Entity Component");
        ImGui::Separator();
        for (const auto& entry : kModifierCatalog)
        {
            const bool is_env_item = (entry.category == ModifierCategory::kEnvironment);
            if (is_environment_stack != is_env_item)
            {
                continue;
            }
            if (ImGui::MenuItem(entry.title))
            {
                if (auto inst = instantiate_modifier_by_class(entry.class_id))
                {
                    stack.add_or_replace(std::move(inst));
                    s.push_log(std::string("[inspector] Added component: ") + entry.title);
                }
            }
            if (ImGui::IsItemHovered() && entry.tooltip != nullptr)
            {
                ImGui::SetTooltip("%s", entry.tooltip);
            }
        }
        ImGui::EndPopup();
    }
}

void draw_selected_entity_inspector(StudioEngineState& s)
{
    auto& w = studio_world();
    const std::size_t sel_idx = static_cast<std::size_t>(s.selected);
    auto& ent = s.entities[sel_idx];

    // ---- 1. Compact Header: Eye + Lock + Editable Name ----
    if (ImGui::IconButton("##insp_vis",
                          ent.visible ? cd::ui::chroma::IconKind::kEyeOpen : cd::ui::chroma::IconKind::kEyeClosed,
                          ent.visible ? IM_COL32(115, 215, 255, 255) : IM_COL32(145, 110, 110, 190),
                          false, ImVec2(24.0F, 24.0F)))
    {
        ent.visible = !ent.visible;
        s.last_tlas_scene_hash = 0;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Toggle Visibility");
    }

    ImGui::SameLine(0.0F, 4.0F);
    if (ImGui::IconButton("##insp_lck",
                          ent.locked ? cd::ui::chroma::IconKind::kLockClosed : cd::ui::chroma::IconKind::kLockOpen,
                          ent.locked ? IM_COL32(255, 185, 85, 255) : IM_COL32(135, 150, 175, 190),
                          ent.locked, ImVec2(24.0F, 24.0F)))
    {
        ent.locked = !ent.locked;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Lock Selection / Gizmo");
    }

    ImGui::SameLine(0.0F, 6.0F);
    char name_buf[128] {};
    std::snprintf(name_buf, sizeof(name_buf), "%s", ent.name.c_str());
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputText("##insp_ent_name", name_buf, sizeof(name_buf)))
    {
        if (name_buf[0] != '\0')
        {
            ent.name = name_buf;
        }
    }

    // Layer assignment + Clean 4-button action bar
    constexpr std::array<const char*, 7> kStdLayers {{
        "Default", "Geometry", "Physics", "Characters", "Gameplay", "Lights", "UI"
    }};
    const std::string_view cur_lyr_sv = cd::world_container::layer_of(s.ecs_world, ent.handle);
    const std::string cur_lyr = cur_lyr_sv.empty() ? "Default" : std::string(cur_lyr_sv);

    ImGui::TextDisabled("Layer");
    ImGui::SameLine(52.0F);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##insp_ent_layer", cur_lyr.c_str()))
    {
        for (const char* lyr : kStdLayers)
        {
            const bool is_on = (cur_lyr == lyr);
            if (ImGui::Selectable(lyr, is_on))
            {
                cd::world_container::assign_layer(s.ecs_world, ent.handle, lyr);
                s.push_log("[layer] Assigned '" + ent.name + "' -> " + lyr);
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginTable("insp_ent_actions", 4, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        if (ImGui::Button("Focus", ImVec2(-1.0F, 22.0F)))
        {
            scene_ops::focus_selected_node(s);
        }
        ImGui::TableNextColumn();
        if (ImGui::Button("Duplicate", ImVec2(-1.0F, 22.0F)))
        {
            scene_ops::duplicate_selected_node(s);
            ImGui::EndTable();
            return;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button("Ground", ImVec2(-1.0F, 22.0F)))
        {
            scene_ops::snap_selected_to_ground(s);
        }
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.42F, 0.42F, 1.0F));
        const bool do_delete = ImGui::Button("Delete", ImVec2(-1.0F, 22.0F));
        ImGui::PopStyleColor();
        ImGui::EndTable();
        if (do_delete)
        {
            ent.locked = false;
            scene_ops::delete_selected_node(s);
            return;
        }
    }

    ImGui::Separator();

    // ---- 2. Collapsible Transform Section ----
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (auto* lt = s.scene.local(ent.handle))
        {
            bool tr_changed = false;
            if (ImGui::DragFloat3("Position##tr_pos", &lt->value.position.x, 0.05F, -5000.0F, 5000.0F, "%.2f"))
            {
                tr_changed = true;
            }

            auto euler_it = w.euler_cache_deg.find(ent.handle.id);
            if (euler_it == w.euler_cache_deg.end())
            {
                euler_it = w.euler_cache_deg.emplace(
                    ent.handle.id,
                    cd::app_common::quat_to_euler_deg(lt->value.rotation)).first;
            }
            if (ImGui::DragFloat3("Rotation##tr_rot", &euler_it->second.x, 0.5F, -360.0F, 360.0F, "%.1f deg"))
            {
                lt->value.rotation = cd::app_common::euler_deg_to_quat(euler_it->second);
                tr_changed = true;
            }

            cd::math::Vec3f prev_scale = lt->value.scale;
            if (ImGui::DragFloat3("Scale##tr_scl", &lt->value.scale.x, 0.02F, 0.02F, 250.0F, "%.2f"))
            {
                lt->value.scale.x = std::max(0.02F, lt->value.scale.x);
                lt->value.scale.y = std::max(0.02F, lt->value.scale.y);
                lt->value.scale.z = std::max(0.02F, lt->value.scale.z);
                if (w.uniform_scale_lock)
                {
                    float ratio = 1.0F;
                    if (std::fabs(lt->value.scale.x - prev_scale.x) > 1e-4F && prev_scale.x > 1e-4F)
                        ratio = lt->value.scale.x / prev_scale.x;
                    else if (std::fabs(lt->value.scale.y - prev_scale.y) > 1e-4F && prev_scale.y > 1e-4F)
                        ratio = lt->value.scale.y / prev_scale.y;
                    else if (std::fabs(lt->value.scale.z - prev_scale.z) > 1e-4F && prev_scale.z > 1e-4F)
                        ratio = lt->value.scale.z / prev_scale.z;
                    lt->value.scale = {
                        std::max(0.02F, prev_scale.x * ratio),
                        std::max(0.02F, prev_scale.y * ratio),
                        std::max(0.02F, prev_scale.z * ratio)
                    };
                }
                tr_changed = true;
            }

            ImGui::Checkbox("Uniform Scale", &w.uniform_scale_lock);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset Transform"))
            {
                lt->value.position = { 0.0F, 0.0F, 0.0F };
                lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                lt->value.scale    = { 1.0F, 1.0F, 1.0F };
                w.euler_cache_deg[ent.handle.id] = { 0.0F, 0.0F, 0.0F };
                tr_changed = true;
            }

            if (tr_changed)
            {
                w.home_transforms[ent.handle.id] = lt->value;
                s.scene.update_transforms();
                s.last_tlas_scene_hash = 0;
            }
        }
    }

    // ---- 3. Collapsible Mesh & Surface Material Section ----
    if (ImGui::CollapsingHeader("Mesh & Surface Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // A. Mesh Slot
        const std::string mesh_btn = "Mesh: " + active_mesh_display_name(ent) + "##insp_mesh_slot";
        if (ImGui::IconButton(mesh_btn.c_str(), cd::ui::chroma::IconKind::kMesh3D,
                              IM_COL32(95, 195, 255, 255), false, ImVec2(-1.0F, 24.0F)))
        {
            ImGui::OpenPopup("insp_mesh_slot_popup");
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_ASSET_MESH"))
            {
                const char* mesh_path = static_cast<const char*>(mp->Data);
                (void)pipeline::assign_mesh_to_entity(s, s.selected, mesh_path);
                w.is_dragging_asset_mesh = false;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopup("insp_mesh_slot_popup"))
        {
            ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F), "Assign 3D Mesh");
            ImGui::Separator();
            constexpr std::array<std::pair<const char*, const char*>, 8> kBuiltInMeshes {{
                { "Cube",             "assets/meshes/Cube.mesh" },
                { "Sphere",           "assets/meshes/Sphere.mesh" },
                { "Cylinder",         "assets/meshes/Cylinder.mesh" },
                { "Cone",             "assets/meshes/Cone.mesh" },
                { "Torus",            "assets/meshes/Torus.mesh" },
                { "TorusKnot",        "assets/meshes/TorusKnot.mesh" },
                { "CesiumMan (glTF)", "CesiumMan.glb" },
                { "Sponza Atrium",    "Sponza.gltf" },
            }};
            for (const auto& [label, path] : kBuiltInMeshes)
            {
                if (ImGui::MenuItem(label))
                {
                    (void)pipeline::assign_mesh_to_entity(s, s.selected, path);
                }
            }
            for (const auto& ca : w.loaded_context_assets)
            {
                if (ca.kind != 0U)
                {
                    continue;
                }
                if (ImGui::MenuItem((ca.name + "##ctx_m").c_str()))
                {
                    (void)pipeline::assign_mesh_to_entity(s, s.selected, ca.path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import Mesh from Disk (.obj / .gltf / .glb)..."))
            {
                std::string picked;
                if (scene_ops::open_os_file_dialog(
                        "3D Mesh Files (*.obj;*.gltf;*.glb;*.mesh)\0*.obj;*.gltf;*.glb;*.mesh\0All Files (*.*)\0*.*\0",
                        picked,
                        "Select 3D Mesh"))
                {
                    (void)pipeline::import_or_assign_file(s, picked, true);
                }
            }
            ImGui::EndPopup();
        }

        // B. Albedo Texture Slot
        const std::string tex_btn = "Albedo Map: " + active_texture_display_name(ent) + "##insp_tex_slot";
        if (ImGui::IconButton(tex_btn.c_str(), cd::ui::chroma::IconKind::kTexture2D,
                              IM_COL32(85, 235, 165, 255), false, ImVec2(-1.0F, 24.0F)))
        {
            ImGui::OpenPopup("insp_tex_slot_popup");
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* tp = ImGui::AcceptDragDropPayload("CD_ASSET_TEXTURE"))
            {
                const char* tex_path = static_cast<const char*>(tp->Data);
                (void)pipeline::assign_texture_to_entity(s, s.selected, tex_path);
                w.is_dragging_asset_texture = false;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopup("insp_tex_slot_popup"))
        {
            ImGui::TextColored(ImVec4(0.45F, 0.94F, 0.68F, 1.0F), "Assign Albedo Texture");
            ImGui::Separator();
            if (ImGui::MenuItem("None (Solid Albedo Tint)"))
            {
                (void)pipeline::assign_texture_to_entity(s, s.selected, "None");
            }
            ImGui::Separator();
            constexpr std::array<std::pair<const char*, const char*>, 6> kBuiltInTextures {{
                { "UV Checkerboard 2K", "assets/textures/UV_Checkerboard_2K.tex" },
                { "Gold Leaf 24K",      "assets/textures/Gold_Leaf_24K.tex" },
                { "Carbon Fiber Twill", "assets/textures/Carbon_Fiber_Twill.tex" },
                { "Carrara Marble",     "assets/textures/Carrara_Marble.tex" },
                { "Oak Wood Planks",    "assets/textures/Oak_Wood_Planks.tex" },
                { "Earth Albedo",       "assets/textures/Earth_Albedo.png" },
            }};
            for (const auto& [label, path] : kBuiltInTextures)
            {
                if (ImGui::MenuItem(label))
                {
                    (void)pipeline::assign_texture_to_entity(s, s.selected, path);
                }
            }
            for (const auto& ca : w.loaded_context_assets)
            {
                if (ca.kind != 1U && ca.kind != 2U)
                {
                    continue;
                }
                if (ImGui::MenuItem((ca.name + "##ctx_t").c_str()))
                {
                    (void)pipeline::assign_texture_to_entity(s, s.selected, ca.path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import Texture from Disk (.png / .jpg / .tga)..."))
            {
                std::string picked;
                if (scene_ops::open_os_file_dialog(
                        "Texture Files (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tex)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tex\0All Files (*.*)\0*.*\0",
                        picked,
                        "Select Albedo Texture"))
                {
                    (void)pipeline::import_or_assign_file(s, picked, true);
                }
            }
            ImGui::EndPopup();
        }

        // C. Direct Inline PBR Surface Sliders
        bool surf_changed = false;
        if (ImGui::ColorEdit3("Albedo##mat_alb", &ent.tint.x))
        {
            surf_changed = true;
        }
        if (ImGui::SliderFloat("Metallic##mat_met", &ent.metallic, 0.0F, 1.0F, "%.2f"))
        {
            ent.is_pbr   = true;
            surf_changed = true;
        }
        if (ImGui::SliderFloat("Roughness##mat_rgh", &ent.roughness, 0.04F, 1.0F, "%.2f"))
        {
            ent.is_pbr   = true;
            surf_changed = true;
        }
        if (ImGui::TreeNode("Advanced Optics"))
        {
            if (ImGui::SliderFloat("Multi-Scatter##mat_ms", &ent.multiscatter_comp, 0.0F, 1.0F, "%.2f"))
            {
                ent.is_pbr   = true;
                surf_changed = true;
            }
            if (ImGui::SliderFloat("Iridescence (nm)##mat_ir", &ent.iridescence_nm, 0.0F, 800.0F, "%.0f nm"))
            {
                ent.is_pbr   = true;
                surf_changed = true;
            }
            ImGui::TreePop();
        }

        if (surf_changed)
        {
            s.last_tlas_scene_hash = 0;
            if (auto* pbr_mod = w.entity_modifiers[ent.handle.id].find<PbrMaterialModifier>())
            {
                pbr_mod->base_color        = ent.tint;
                pbr_mod->metallic          = ent.metallic;
                pbr_mod->roughness         = ent.roughness;
                pbr_mod->multiscatter_comp = ent.multiscatter_comp;
                pbr_mod->iridescence_nm    = ent.iridescence_nm;
            }
        }
    }

    // ---- 4. Collapsible Skeletal Animation Section (when applicable) ----
    auto* anim_mod = w.entity_modifiers[ent.handle.id].find<AnimatorStateMachineModifier>();
    if (ent.kind == PrimitiveKind::kGltf || anim_mod != nullptr)
    {
        if (ImGui::CollapsingHeader("Skeletal Animation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Pause Playback", &s.anim_paused);
            ImGui::SliderFloat("Playback Speed", &s.anim_speed, 0.1F, 3.0F, "%.2fx");
            if (anim_mod != nullptr)
            {
                ImGui::Checkbox("Auto Velocity Transitions", &anim_mod->auto_stateful_transitions);
                for (std::size_t si = 0; si < 6U; ++si)
                {
                    if (si > 0U && si != 3U)
                    {
                        ImGui::SameLine();
                    }
                    const bool active_st = (std::cmp_equal(anim_mod->current_state_idx, si));
                    if (active_st)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.56F, 0.36F, 1.0F));
                    }
                    if (ImGui::SmallButton(AnimatorStateMachineModifier::kStateLabels[si]))
                    {
                        anim_mod->set_state(static_cast<int>(si));
                    }
                    if (active_st)
                    {
                        ImGui::PopStyleColor();
                    }
                }
            }
        }
    }

    // ---- 5. Collapsible Components & Modifiers Section ----
    if (ImGui::CollapsingHeader("Components & Modifiers", ImGuiTreeNodeFlags_DefaultOpen))
    {
        draw_component_stack_cards(s, w.entity_modifiers[ent.handle.id], false);
    }
}

void draw_selected_light_inspector(StudioEngineState& s)
{
    const std::size_t sel_li = static_cast<std::size_t>(s.selected);
    auto& lr = s.lights[sel_li];

    ImGui::Checkbox("##lgt_en", &lr.enabled);
    ImGui::SameLine(0.0F, 6.0F);
    char lgt_name_buf[128] {};
    std::snprintf(lgt_name_buf, sizeof(lgt_name_buf), "%s", lr.name.c_str());
    ImGui::SetNextItemWidth(-64.0F);
    if (ImGui::InputText("##lgt_name", lgt_name_buf, sizeof(lgt_name_buf)))
    {
        if (lgt_name_buf[0] != '\0')
        {
            lr.name = lgt_name_buf;
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.42F, 0.42F, 1.0F));
    const bool do_delete = ImGui::Button("Delete", ImVec2(-1.0F, 0.0F));
    ImGui::PopStyleColor();
    if (do_delete)
    {
        scene_ops::delete_light_at(s, sel_li);
        return;
    }

    ImGui::Separator();
    draw_studio_light_controls(
        lr,
        [&s](std::string msg) { s.push_log(std::move(msg)); },
        "_insp");
}

}  // namespace

void draw_inspector_dock(StudioEngineState& s)
{
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();

    if (!ImGui::Begin("Inspector", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("StudioInspectorTabs"))
    {
        // ---- Tab 0: Selected Node / Light Inspector ----
        if (ImGui::BeginTabItem("Inspector"))
        {
            ImGui::BeginChild("inspector_entity_scroll", ImVec2(0.0F, 0.0F), false);
            if (s.selected_kind == SelKind::kEntity &&
                s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
            {
                draw_selected_entity_inspector(s);
            }
            else if (s.selected_kind == SelKind::kLight &&
                     s.selected >= 0 && std::cmp_less(s.selected, s.lights.size()))
            {
                draw_selected_light_inspector(s);
            }
            else
            {
                ImGui::Separator();
                ImGui::TextDisabled("Nothing selected.");
                ImGui::TextDisabled("Click any node in Scene Outliner or the 3D Viewport.");
                ImGui::Separator();
                if (ImGui::Button("+ Add PBR Cube", ImVec2(-1.0F, 24.0F)))
                {
                    (void)scene_ops::spawn_primitive_node(
                        s, "Cube", PrimitiveKind::kCube, { 0.85F, 0.87F, 0.90F }, true, 0.05F, 0.38F);
                }
                if (ImGui::Button("+ Add Point Light", ImVec2(-1.0F, 24.0F)))
                {
                    (void)scene_ops::spawn_light_preset(s, 1);
                }
                if (ImGui::Button("Switch to 2D Engine Studio", ImVec2(-1.0F, 24.0F)))
                {
                    cst.workspace_mode = 3;
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ---- Tab 1: World Physics, GI & Post-FX ----
        if (ImGui::BeginTabItem("World & Post-FX"))
        {
            ImGui::BeginChild("inspector_world_scroll", ImVec2(0.0F, 0.0F), false);

            if (ImGui::CollapsingHeader("World Physics", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Simulate Physics in Edit Mode", &w.simulate_physics_in_edit);
                ImGui::SliderFloat("Gravity Y (m/s^2)", &w.world_gravity_y, -25.0F, 5.0F, "%.2f");
                if (ImGui::Button("Reset Physics Positions", ImVec2(-1.0F, 24.0F)))
                {
                    physics_pie::reset_physics_positions(s);
                }
            }

            if (ImGui::CollapsingHeader("Lighting, GI & Tonemapping", ImGuiTreeNodeFlags_DefaultOpen))
            {
                constexpr std::array<const char*, 5> kTonemappers {{
                    "ACES Narkowicz", "ACES Hill", "Hable Uncharted", "AgX Filmic", "HDR10 PQ"
                }};
                int tm = std::clamp(s.fx.tonemap_op, 0, 4);
                if (ImGui::Combo("Tonemapper", &tm, kTonemappers.data(), static_cast<int>(kTonemappers.size())))
                {
                    s.fx.tonemap_op = tm;
                }
                ImGui::SliderFloat("Exposure", &s.fx.exposure, 0.1F, 4.0F, "%.2f");
                ImGui::SliderFloat("Bloom", &s.fx.bloom_post, 0.0F, 0.25F, "%.3f");
                ImGui::SliderFloat("Ambient Occlusion", &s.fx.ao_strength, 0.0F, 1.5F, "%.2f");
                ImGui::SliderFloat("SSR Reflections", &s.fx.ssr_strength, 0.0F, 1.0F, "%.2f");
                ImGui::SliderFloat("TAA Blend", &s.fx.taa_amount, 0.0F, 0.95F, "%.2f");
                ImGui::Checkbox("Ray-Traced DDGI Indirect Bounce", &s.fx.ddgi_on);
                ImGui::Checkbox("Parallel HDR Command Lanes (3x)", &s.fx.hdr_parallel_lanes);
            }

            if (ImGui::CollapsingHeader("Environment Effects", ImGuiTreeNodeFlags_DefaultOpen))
            {
                draw_component_stack_cards(s, w.environment_modifiers, true);
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

}  // namespace cd::studio::ui
