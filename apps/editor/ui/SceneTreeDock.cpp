// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/ui/SceneTreeDock.cpp
// =============================================================================
#include "ui/SceneTreeDock.hpp"

#include "pipeline/AssetIngest.hpp"
#include "scene/SceneOperations.hpp"
#include "StudioModifierClasses.hpp"

#include <cd/ui/ChromaUi.hpp>
#include <cd/world_container/LayerMember.hpp>
#include <cd/world_container/World.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <utility>

namespace cd::studio::ui
{

void draw_scene_tree_dock(StudioEngineState& s)
{
    auto& w = studio_world();

    if (!ImGui::Begin("Scene Outliner", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::End();
        return;
    }

    // ---- 1. Clean Top Action Row: [+ Add Node] + Search Filter ----
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.48F, 0.78F, 1.0F));
    if (ImGui::Button("+ Add", ImVec2(64.0F, 24.0F)))
    {
        ImGui::OpenPopup("outliner_add_node_popup");
    }
    ImGui::PopStyleColor();

    if (ImGui::BeginPopup("outliner_add_node_popup"))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.88F, 1.0F, 1.0F), "Add Node to Scene");
        ImGui::Separator();
        if (ImGui::MenuItem("Cube (Static PBR)"))
            (void)scene_ops::spawn_primitive_node(s, "Cube", PrimitiveKind::kCube, { 0.85F, 0.87F, 0.90F }, true, 0.05F, 0.38F);
        if (ImGui::MenuItem("Sphere (PBR)"))
            (void)scene_ops::spawn_primitive_node(s, "Sphere", PrimitiveKind::kSphere, { 0.85F, 0.87F, 0.90F }, true, 0.15F, 0.25F);
        if (ImGui::MenuItem("Cylinder"))
            (void)scene_ops::spawn_primitive_node(s, "Cylinder", PrimitiveKind::kCylinder, { 0.82F, 0.85F, 0.88F }, true, 0.10F, 0.35F);
        if (ImGui::MenuItem("Cone"))
            (void)scene_ops::spawn_primitive_node(s, "Cone", PrimitiveKind::kCone, { 0.90F, 0.78F, 0.55F }, true, 0.05F, 0.42F);
        if (ImGui::MenuItem("Torus"))
            (void)scene_ops::spawn_primitive_node(s, "Torus", PrimitiveKind::kTorus, { 0.75F, 0.82F, 0.95F }, true, 0.35F, 0.22F);
        if (ImGui::MenuItem("Ground Floor"))
            (void)scene_ops::spawn_ground_floor(s);
        ImGui::Separator();
        if (ImGui::MenuItem("Dynamic RigidBody Cube"))
            (void)scene_ops::spawn_entity_by_preset(s, "Dynamic_Cube", { 0.0F, 1.8F, 0.0F });
        if (ImGui::MenuItem("Bouncing Sphere"))
            (void)scene_ops::spawn_entity_by_preset(s, "Dynamic_Sphere", { 0.0F, 2.2F, 0.0F });
        if (ImGui::MenuItem("Character Vessel 3D (WASD)"))
            (void)scene_ops::spawn_character_vessel(s, false);
        if (ImGui::MenuItem("Skinned Avatar (CesiumMan)"))
            (void)scene_ops::spawn_entity_by_preset(s, "Character_CesiumMan", { 0.0F, 0.0F, 0.0F });
        ImGui::Separator();
        if (ImGui::MenuItem("Directional Sun Light"))
            (void)scene_ops::spawn_light_preset(s, 0);
        if (ImGui::MenuItem("Point Omni Light"))
            (void)scene_ops::spawn_light_preset(s, 1);
        if (ImGui::MenuItem("Spot Cone Light"))
            (void)scene_ops::spawn_light_preset(s, 2);
        if (ImGui::MenuItem("Rect Area Softbox"))
            (void)scene_ops::spawn_light_preset(s, 3);
        ImGui::Separator();
        if (ImGui::MenuItem("Scene Camera 3D"))
            (void)scene_ops::spawn_scene_camera(s, false);
        if (ImGui::MenuItem("Volumetric Clouds"))
            (void)scene_ops::spawn_volumetric_clouds(s);
        if (ImGui::MenuItem("Volumetric Fog"))
            (void)scene_ops::spawn_volumetric_fog(s);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0F, 6.0F);
    static char s_outliner_search[96] {};
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##outliner_search", "Filter scene nodes...", s_outliner_search, sizeof(s_outliner_search));

    // ---- 2. Active Stage Summary & Layer Filter ----
    static int s_layer_filter_idx = 0;
    constexpr std::array<const char*, 8> kLayerNames {{
        "All Layers", "Default", "Geometry", "Physics", "Characters", "Gameplay", "Lights", "UI"
    }};
    ImGui::TextDisabled("%s (%zu nodes, %zu lights)",
                        w.active_level_name.empty() ? "Stage" : w.active_level_name.c_str(),
                        s.entities.size(),
                        s.lights.size());
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##outliner_layer_filter", kLayerNames[static_cast<std::size_t>(s_layer_filter_idx)]))
    {
        for (std::size_t li = 0; li < kLayerNames.size(); ++li)
        {
            const bool is_sel = (std::cmp_equal(s_layer_filter_idx, li));
            if (ImGui::Selectable(kLayerNames[li], is_sel))
            {
                s_layer_filter_idx = static_cast<int>(li);
                if (li > 0U)
                {
                    w.active_layer_name = kLayerNames[li];
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    // ---- 3. Unified Scrollable Hierarchy Tree ----
    ImGui::BeginChild("outliner_tree_scroll", ImVec2(0.0F, 0.0F), false);

    auto matches_search = [&](const std::string& nm) -> bool
    {
        if (s_outliner_search[0] == '\0')
        {
            return true;
        }
        return nm.find(s_outliner_search) != std::string::npos;
    };

    // A. Lights Section
    if ((s_layer_filter_idx == 0 || s_layer_filter_idx == 6) && !s.lights.empty())
    {
        int pending_delete_light = -1;
        for (std::size_t li = 0; li < s.lights.size(); ++li)
        {
            auto& lr = s.lights[li];
            if (!matches_search(lr.name))
            {
                continue;
            }
            ImGui::PushID(static_cast<int>(10000 + li));
            const bool is_sel = (s.selected_kind == SelKind::kLight && std::cmp_equal(s.selected, li));

            // Inline rename mode for Light
            if (std::cmp_equal(w.inline_rename_light_idx, li))
            {
                ImGui::SetNextItemWidth(-1.0F);
                (void)ImGui::InputText("##ren_lgt", w.inline_rename_buf, sizeof(w.inline_rename_buf), 0);
                if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
                {
                    if (w.inline_rename_buf[0] != '\0')
                    {
                        lr.name = w.inline_rename_buf;
                    }
                    w.inline_rename_light_idx = -1;
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered())
                {
                    w.inline_rename_light_idx = -1;
                }
                ImGui::PopID();
                continue;
            }

            const char* badge = "PT";
            if (lr.light.type == cd::light::LightType::kDirectional) badge = "SUN";
            else if (lr.light.type == cd::light::LightType::kSpot)   badge = "SPOT";
            else if (lr.light.type == cd::light::LightType::kRectArea ||
                     lr.light.type == cd::light::LightType::kDiskArea) badge = "AREA";

            if (ImGui::IconButton("##lgt_vis",
                                  lr.enabled ? cd::ui::chroma::IconKind::kEyeOpen : cd::ui::chroma::IconKind::kEyeClosed,
                                  lr.enabled ? IM_COL32(185, 205, 225, 220) : IM_COL32(110, 115, 125, 160),
                                  false,
                                  ImVec2(20.0F, 20.0F)))
            {
                lr.enabled             = !lr.enabled;
                s.last_tlas_scene_hash = 0;
            }
            ImGui::SameLine(0.0F, 4.0F);

            const std::string row_lbl = lr.name + "##lgt_row_" + std::to_string(li);
            if (ImGui::IconSelectable(row_lbl.c_str(),
                                      cd::ui::chroma::IconKind::kLight,
                                      lr.enabled ? IM_COL32(255, 212, 92, 255) : IM_COL32(120, 125, 135, 180),
                                      is_sel,
                                      badge))
            {
                s.selected      = static_cast<int>(li);
                s.selected_kind = SelKind::kLight;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                w.inline_rename_light_idx  = static_cast<int>(li);
                w.inline_rename_entity_idx = -1;
                std::snprintf(w.inline_rename_buf, sizeof(w.inline_rename_buf), "%s", lr.name.c_str());
            }
            if (ImGui::BeginPopupContextItem())
            {
                s.selected      = static_cast<int>(li);
                s.selected_kind = SelKind::kLight;
                if (ImGui::MenuItem("Focus Camera (F)"))
                    scene_ops::focus_selected_node(s);
                if (ImGui::MenuItem("Rename"))
                {
                    w.inline_rename_light_idx  = static_cast<int>(li);
                    w.inline_rename_entity_idx = -1;
                    std::snprintf(w.inline_rename_buf, sizeof(w.inline_rename_buf), "%s", lr.name.c_str());
                }
                if (ImGui::MenuItem("Duplicate (Ctrl+D)"))
                    scene_ops::duplicate_selected_node(s);
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Light (Del)"))
                    pending_delete_light = static_cast<int>(li);
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        if (pending_delete_light >= 0)
        {
            scene_ops::delete_light_at(s, static_cast<std::size_t>(pending_delete_light));
        }
    }

    // B. Scene Entities Section
    int pending_delete_entity = -1;
    int pending_dup_entity    = -1;
    for (std::size_t ei = 0; ei < s.entities.size(); ++ei)
    {
        auto& ent = s.entities[ei];
        if (!matches_search(ent.name))
        {
            continue;
        }
        if (s_layer_filter_idx > 0)
        {
            const std::string_view req_layer = kLayerNames[static_cast<std::size_t>(s_layer_filter_idx)];
            const std::string_view ent_layer = cd::world_container::layer_of(s.ecs_world, ent.handle);
            if (!ent_layer.empty() && ent_layer != req_layer)
            {
                continue;
            }
        }

        ImGui::PushID(static_cast<int>(ei));
        const bool is_sel = (s.selected_kind == SelKind::kEntity && std::cmp_equal(s.selected, ei));

        if (std::cmp_equal(w.inline_rename_entity_idx, ei))
        {
            ImGui::SetNextItemWidth(-1.0F);
            (void)ImGui::InputText("##ren_ent", w.inline_rename_buf, sizeof(w.inline_rename_buf), 0);
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                if (w.inline_rename_buf[0] != '\0')
                {
                    ent.name = w.inline_rename_buf;
                }
                w.inline_rename_entity_idx = -1;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered())
            {
                w.inline_rename_entity_idx = -1;
            }
            ImGui::PopID();
            continue;
        }

        auto& ms = w.entity_modifiers[ent.handle.id];
        cd::ui::chroma::IconKind icon = cd::ui::chroma::IconKind::kCube;
        ImU32 icon_col = IM_COL32(130, 198, 255, 255);
        std::string_view badge {};

        if (ent.name.find("Camera") != std::string::npos || ms.find<CameraLensModifier>() != nullptr)
        {
            icon     = cd::ui::chroma::IconKind::kCamera;
            icon_col = IM_COL32(105, 225, 255, 255);
            badge    = "CAM";
        }
        else if (ms.find<CharacterControllerModifier>() != nullptr || ent.kind == PrimitiveKind::kGltf)
        {
            icon     = cd::ui::chroma::IconKind::kAvatar;
            icon_col = IM_COL32(95, 240, 170, 255);
            badge    = "CHAR";
        }
        else if (ent.name.find("Cloud") != std::string::npos || ent.name.find("Fog") != std::string::npos ||
                 ms.find<CloudLayerModifier>() != nullptr || ms.find<FogModifier>() != nullptr)
        {
            icon     = cd::ui::chroma::IconKind::kSparkles;
            icon_col = IM_COL32(185, 165, 255, 255);
            badge    = "ENV";
        }
        else if (const auto* rb = ms.find<RigidBodyModifier>();
                 rb != nullptr && rb->enabled && rb->body_type == cd::physics::BodyType::kDynamic)
        {
            icon     = cd::ui::chroma::IconKind::kPhysics;
            icon_col = IM_COL32(255, 185, 95, 255);
            badge    = "RB";
        }
        else if (ent.kind == PrimitiveKind::kSphere)
        {
            icon = cd::ui::chroma::IconKind::kSphere;
        }
        else if (ent.kind == PrimitiveKind::kSponza)
        {
            icon     = cd::ui::chroma::IconKind::kWorld;
            icon_col = IM_COL32(255, 210, 115, 255);
            badge    = "GLTF";
        }

        if (!ent.visible)
        {
            icon_col = IM_COL32(115, 120, 130, 160);
        }

        if (ImGui::IconButton("##ent_vis",
                              ent.visible ? cd::ui::chroma::IconKind::kEyeOpen : cd::ui::chroma::IconKind::kEyeClosed,
                              ent.visible ? IM_COL32(185, 205, 225, 220) : IM_COL32(110, 115, 125, 150),
                              false,
                              ImVec2(20.0F, 20.0F)))
        {
            ent.visible            = !ent.visible;
            s.last_tlas_scene_hash = 0;
        }
        ImGui::SameLine(0.0F, 2.0F);
        if (ImGui::IconButton("##ent_lck",
                              ent.locked ? cd::ui::chroma::IconKind::kLockClosed : cd::ui::chroma::IconKind::kLockOpen,
                              ent.locked ? IM_COL32(245, 185, 85, 240) : IM_COL32(120, 130, 145, 145),
                              false,
                              ImVec2(20.0F, 20.0F)))
        {
            ent.locked = !ent.locked;
        }
        ImGui::SameLine(0.0F, 4.0F);

        const std::string row_lbl = ent.name + "##ent_row_" + std::to_string(ei);
        if (ImGui::IconSelectable(row_lbl.c_str(),
                                  icon,
                                  icon_col,
                                  is_sel,
                                  badge))
        {
            s.selected      = static_cast<int>(ei);
            s.selected_kind = SelKind::kEntity;
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            scene_ops::focus_selected_node(s);
        }

        // Accept mesh/texture/modifier drops directly onto Outliner row
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p_mesh = ImGui::AcceptDragDropPayload("CD_ASSET_MESH"))
            {
                const char* path = static_cast<const char*>(p_mesh->Data);
                (void)pipeline::assign_mesh_to_entity(s, static_cast<int>(ei), path);
                w.is_dragging_asset_mesh = false;
            }
            if (const ImGuiPayload* p_tex = ImGui::AcceptDragDropPayload("CD_ASSET_TEXTURE"))
            {
                const char* path = static_cast<const char*>(p_tex->Data);
                (void)pipeline::assign_texture_to_entity(s, static_cast<int>(ei), path);
                w.is_dragging_asset_texture = false;
            }
            if (const ImGuiPayload* p_mod = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
            {
                const char* cls_id = static_cast<const char*>(p_mod->Data);
                if (auto mod = instantiate_modifier_by_class(cls_id))
                {
                    ms.add_or_replace(std::move(mod));
                }
                w.is_dragging_modifier = false;
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem())
        {
            s.selected      = static_cast<int>(ei);
            s.selected_kind = SelKind::kEntity;
            if (ImGui::MenuItem("Focus Camera (F)"))
                scene_ops::focus_selected_node(s);
            if (ImGui::MenuItem("Rename"))
            {
                w.inline_rename_entity_idx = static_cast<int>(ei);
                w.inline_rename_light_idx  = -1;
                std::snprintf(w.inline_rename_buf, sizeof(w.inline_rename_buf), "%s", ent.name.c_str());
            }
            if (ImGui::MenuItem("Duplicate (Ctrl+D)"))
                pending_dup_entity = static_cast<int>(ei);
            if (ImGui::MenuItem("Snap to Ground (G)"))
                scene_ops::snap_selected_to_ground(s);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Node (Del)"))
                pending_delete_entity = static_cast<int>(ei);
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    if (pending_dup_entity >= 0)
    {
        s.selected      = pending_dup_entity;
        s.selected_kind = SelKind::kEntity;
        scene_ops::duplicate_selected_node(s);
    }
    if (pending_delete_entity >= 0)
    {
        s.selected      = pending_delete_entity;
        s.selected_kind = SelKind::kEntity;
        scene_ops::delete_selected_node(s);
    }

    ImGui::EndChild();
    ImGui::End();
}

}  // namespace cd::studio::ui
