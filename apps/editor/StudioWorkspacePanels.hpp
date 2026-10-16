// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioWorkspacePanels.hpp
//
// Studio Workbench (2-column Drag & Drop 3D Objects and Modifier Classes),
// Scene Hierarchy Outliner, Node3D Transform & Modifier Stack Inspector,
// Physics Telemetry Console, and Standalone Game Exporter.
// =============================================================================
#pragma once

#include "StudioPhysicsSolver.hpp"

namespace cd::studio
{
// 11. Ergonomic Studio UI Panels (2-Col Drag & Drop Grid, Inspector, Hierarchy)
// =============================================================================
template <typename StateT, typename LogFn>
inline void draw_studio_modifier_stack_ui(
    ModifierStack& stack,
    const char*    stack_owner_label,
    StateT&        /*s*/,
    LogFn&&        log_push)
{
    ImGui::TextColored(ImVec4(0.55F, 0.85F, 1.0F, 1.0F),
                       "Modifier Stack (%s) [%zu active]",
                       stack_owner_label, stack.items.size());

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.24F, 0.34F, 0.90F));
    ImGui::Button("[+] DROP MODIFIER CLASS HERE (or click + Add below)", ImVec2(-1.0F, 26.0F));
    ImGui::PopStyleColor();
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
        {
            const char* cls_id = static_cast<const char*>(payload->Data);
            if (auto mod = instantiate_modifier_by_class(cls_id))
            {
                stack.add_or_replace(std::move(mod));
                studio_world().is_dragging_modifier = false;
                log_push(std::string("[modifier] Instantiated class ") + cls_id + " on " + stack_owner_label);
            }
        }
        ImGui::EndDragDropTarget();
    }

    int remove_idx  = -1;
    int move_up_idx = -1;
    for (std::size_t i = 0; i < stack.items.size(); ++i)
    {
        auto& mod = stack.items[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::Checkbox("##en", &mod->enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("^") && i > 0) move_up_idx = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) remove_idx = static_cast<int>(i);
        ImGui::SameLine();
        const bool open = ImGui::CollapsingHeader(
            (std::string(mod->display_name()) + " (" + std::string(mod->class_name()) + ")").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);

        if (open && mod->enabled)
        {
            ImGui::Indent(10.0F);
            mod->draw_inspector_ui();
            ImGui::Unindent(10.0F);
        }
        ImGui::PopID();
    }

    if (move_up_idx > 0)
    {
        std::swap(stack.items[static_cast<std::size_t>(move_up_idx)],
                  stack.items[static_cast<std::size_t>(move_up_idx - 1)]);
    }
    if (remove_idx >= 0)
    {
        log_push(std::string("[modifier] Removed class ") +
                 std::string(stack.items[static_cast<std::size_t>(remove_idx)]->class_name()));
        stack.items.erase(stack.items.begin() + remove_idx);
    }

    ImGui::Separator();
    if (ImGui::Button("+ Instantiate Modifier Class...", ImVec2(-1.0F, 26.0F)))
    {
        ImGui::OpenPopup("add_modifier_class_popup");
    }
    if (ImGui::BeginPopup("add_modifier_class_popup"))
    {
        ImGui::TextDisabled("Select C++ Modifier Class to Instantiate:");
        ImGui::Separator();
        for (const auto& desc : kModifierCatalog)
        {
            if (ImGui::MenuItem(
                    (std::string(desc.title) + "  (" + desc.class_id + ")").c_str()))
            {
                if (auto mod = instantiate_modifier_by_class(desc.class_id))
                {
                    stack.add_or_replace(std::move(mod));
                    log_push(std::string("[modifier] Instantiated ") + desc.class_id);
                }
            }
        }
        ImGui::EndPopup();
    }
}

template <typename StateT, typename LogFn>
inline void draw_clean_studio_workspace_panels(
    StateT&                s,
    float                  dt,
    const cd::math::Mat4f& vp,
    std::uint32_t          frame_w,
    std::uint32_t          frame_h,
    LogFn&&                log_push)
{
    auto& w = studio_world();

    // Draw 3D Collider wireframes + floating Viewport HUD toolbar + right-click context menu
    draw_studio_viewport_overlays_and_drop_targets(s, vp, frame_w, frame_h, log_push);

    // =========================================================================
    // PANEL 1 (Left Top): Ergonomic 2-Column Drag & Drop Builder Workbench
    // =========================================================================
    if (ImGui::Begin("Studio Workbench"))
    {
        // Play / Stop + Reset Positions + Shoot Ball row
        if (!s.pie_playing)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.62F, 0.28F, 1.0F));
            if (ImGui::Button("[>] PLAY(F5)", ImVec2(96.0F, 25.0F)))
            {
                s.pie_playing = true;
                log_push("[studio] Entered Play-In-Editor (F5)");
            }
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78F, 0.22F, 0.20F, 1.0F));
            if (ImGui::Button("[#] STOP(F8)", ImVec2(96.0F, 25.0F)))
            {
                s.pie_playing = false;
                reset_physics_to_home_positions(s, log_push);
            }
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Pos(R)", ImVec2(92.0F, 25.0F)))
            reset_physics_to_home_positions(s, log_push);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72F, 0.34F, 0.14F, 1.0F));
        if (ImGui::Button("Shoot(B)", ImVec2(-1.0F, 25.0F)))
            shoot_physics_ball_from_camera(s, log_push);
        ImGui::PopStyleColor();

        ImGui::Checkbox("Live Physics", &w.simulate_physics_in_edit);
        ImGui::SameLine();
        ImGui::Checkbox("Collider Wires", &w.show_all_collider_wires);

        // ---- SECTION A: 1-Click Interactive Scene Templates ----
        ImGui::SeparatorText("Quick Scene Playgrounds (1-Click)");
        if (ImGui::BeginTable("tpl_grid", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.42F, 0.32F, 1.0F));
            if (ImGui::Button("1. Ramp & Tower", ImVec2(-1.0F, 23.0F)))
                load_studio_interactive_template(s, 1, log_push);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.42F, 0.32F, 1.0F));
            if (ImGui::Button("2. Domino Chain", ImVec2(-1.0F, 23.0F)))
                load_studio_interactive_template(s, 2, log_push);
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.42F, 0.32F, 1.0F));
            if (ImGui::Button("3. Bounce & Fog", ImVec2(-1.0F, 23.0F)))
                load_studio_interactive_template(s, 3, log_push);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (ImGui::Button("Clear to Floor", ImVec2(-1.0F, 23.0F)))
            {
                w.initialized = false;
                init_clean_studio_workspace(s, log_push);
            }
            ImGui::EndTable();
        }

        // ---- SECTION B: 2-Column Drag & Drop 3D Objects Grid ----
        ImGui::SeparatorText("1. Drag & Drop 3D Objects -> Viewport");
        struct SpawnCard { const char* id; const char* short_label; const char* tip; };
        constexpr std::array<SpawnCard, 12> kSpawnCards {{
            { "Dynamic_Cube",        "[+] Dynamic Cube",   "Dynamic RigidBody Cube + BoxCollider3D" },
            { "Dynamic_Sphere",      "[+] Bounce Sphere",  "Dynamic Metallic Sphere + SphereCollider3D" },
            { "Static_Ramp",         "[+] Ramp 25 deg",    "Angled 25 deg OBB Ramp (spheres/boxes slide down!)" },
            { "Trampoline_Pad",      "[+] Trampoline",     "High-restitution (0.96) Bouncing Pad" },
            { "Prefab_CrateStack",   "[+] 5x Crate Tower", "Stack of 5 Dynamic Physics Crates" },
            { "Prefab_DominoRow",    "[+] 6x Domino Row",  "Row of 6 Dynamic Dominoes for chain reactions" },
            { "Static_Wall",         "[+] Static Wall",    "Immovable Barrier Wall + BoxCollider3D" },
            { "Cylinder",            "[+] Dyn Cylinder",   "Dynamic Cylinder + CapsuleCollider3D" },
            { "Cone",                "[+] Dynamic Cone",   "Dynamic Cone + BoxCollider3D" },
            { "Torus",               "[+] Dynamic Torus",  "Dynamic Torus + BoxCollider3D" },
            { "Character_CesiumMan", "[+] CesiumMan",      "Skinned Animated Character + Capsule + Controller" },
            { "World_Sponza",        "[+] Sponza Atrium",  "Crytek Sponza Atrium World (glTF)" },
        }};

        if (ImGui::BeginTable("spawn_grid_2col", 2, ImGuiTableFlags_SizingStretchSame))
        {
            for (const auto& card : kSpawnCards)
            {
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17F, 0.28F, 0.40F, 1.0F));
                if (ImGui::Button(card.short_label, ImVec2(-1.0F, 24.0F)))
                {
                    const cd::math::Vec3f pt = camera_spawn_point(s, 3.8F);
                    (void)spawn_entity_with_modifiers(s, card.id, { pt.x, 0.0F, pt.z }, log_push);
                }
                ImGui::PopStyleColor();
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("CD_STUDIO_SPAWN", card.id, std::strlen(card.id) + 1);
                    w.is_dragging_spawn = true;
                    w.drag_spawn_id     = card.id;
                    ImGui::Text("Drop '%s' onto 3D Viewport", card.short_label);
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s\n(Drag onto 3D Viewport or Click to Spawn)", card.tip);
                }
            }
            ImGui::EndTable();
        }

        // ---- SECTION C: 2-Column Drag & Drop Modifier Classes Grid ----
        ImGui::SeparatorText("2. Drag & Drop Modifier Classes");
        if (ImGui::BeginTable("mod_grid_2col", 2, ImGuiTableFlags_SizingStretchSame))
        {
            for (const auto& mdesc : kModifierCatalog)
            {
                ImGui::TableNextColumn();
                // Check if this Modifier class is already active on selected entity or environment
                bool active_here = false;
                if (mdesc.category == ModifierCategory::kEnvironment)
                {
                    for (const auto& m : w.environment_modifiers.items)
                        if (m && m->enabled && m->class_name() == mdesc.class_id) active_here = true;
                }
                else if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                {
                    const auto& ems = w.entity_modifiers[s.entities[static_cast<std::size_t>(s.selected)].handle.id];
                    for (const auto& m : ems.items)
                        if (m && m->enabled && m->class_name() == mdesc.class_id) active_here = true;
                }

                ImGui::PushStyleColor(ImGuiCol_Button,
                    active_here ? ImVec4(0.22F, 0.46F, 0.34F, 1.0F)
                                : ImVec4(0.26F, 0.20F, 0.38F, 1.0F));
                const std::string btn_txt = std::string(active_here ? "[ON] " : "[") +
                    (active_here ? "" : (std::string(mdesc.short_badge) + "] ")) + mdesc.title;
                if (ImGui::Button(btn_txt.c_str(), ImVec2(-1.0F, 23.0F)))
                {
                    if (auto mod = instantiate_modifier_by_class(mdesc.class_id))
                    {
                        if (mod->category() == ModifierCategory::kEnvironment)
                        {
                            w.environment_modifiers.add_or_replace(std::move(mod));
                            log_push(std::string("[modifier] Instantiated ") + mdesc.class_id + " on Environment");
                        }
                        else if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                        {
                            auto& ent = s.entities[static_cast<std::size_t>(s.selected)];
                            w.entity_modifiers[ent.handle.id].add_or_replace(std::move(mod));
                            log_push(std::string("[modifier] Instantiated ") + mdesc.class_id + " on " + ent.name);
                        }
                    }
                }
                ImGui::PopStyleColor();
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("CD_STUDIO_MODIFIER",
                                              mdesc.class_id,
                                              std::strlen(mdesc.class_id) + 1);
                    w.is_dragging_modifier = true;
                    w.drag_modifier_class  = mdesc.class_id;
                    ImGui::Text("Drop class %s onto Entity / Scene", mdesc.class_id);
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s\nC++ Class: cd::studio::%s\n(Drag onto Entity or Click to Attach)",
                                      mdesc.tooltip, mdesc.class_id);
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();

    // =========================================================================
    // PANEL 2 (Left Bottom): Scene Outliner (Filter + Drag & Drop + Context Menu)
    // =========================================================================
    if (ImGui::Begin("Scene"))
    {
        ImGui::SetNextItemWidth(150.0F);
        ImGui::InputTextWithHint("##filter_nodes", "Filter nodes...", w.hierarchy_filter, sizeof(w.hierarchy_filter));
        ImGui::SameLine();
        if (ImGui::SmallButton("Save"))
            (void)save_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
        ImGui::SameLine();
        if (ImGui::SmallButton("Load"))
            (void)load_full_studio_scene(s, "studio_scene.cdscene.json", log_push);

        ImGui::SeparatorText("Node3D Hierarchy (Drag to Parent / Right-Click)");

        ImGui::Selectable("[World Root -- Drop Node Here to Unparent]", false);
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CD_STUDIO_NODE_IDX"))
            {
                const int child_idx = *static_cast<const int*>(p->Data);
                if (child_idx >= 0 && std::cmp_less(child_idx, s.entities.size()))
                {
                    s.scene.detach(s.entities[static_cast<std::size_t>(child_idx)].handle);
                    s.scene.update_transforms();
                    log_push("[hierarchy] Detached '" + s.entities[static_cast<std::size_t>(child_idx)].name + "' to Root");
                }
            }
            if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
            {
                const char* cls_id = static_cast<const char*>(mp->Data);
                if (auto mod = instantiate_modifier_by_class(cls_id))
                {
                    w.environment_modifiers.add_or_replace(std::move(mod));
                    w.is_dragging_modifier = false;
                    log_push(std::string("[modifier] Attached ") + cls_id + " to World Root");
                }
            }
            ImGui::EndDragDropTarget();
        }

        int deferred_dup_idx = -1;
        int deferred_del_idx = -1;
        for (std::size_t i = 0; i < s.entities.size(); ++i)
        {
            auto& ent = s.entities[i];
            if (w.hierarchy_filter[0] != '\0' &&
                ent.name.find(w.hierarchy_filter) == std::string::npos)
            {
                continue;
            }
            ModifierStack& ms = w.entity_modifiers[ent.handle.id];
            const bool has_parent = s.scene.parent_of(ent.handle).is_valid();

            std::string badges;
            for (const auto& m : ms.items)
            {
                if (!m->enabled) continue;
                if (m->class_name() == "RigidBodyModifier")                badges += "[RB]";
                else if (m->class_name() == "BoxColliderModifier")         badges += "[BoxCol]";
                else if (m->class_name() == "SphereColliderModifier")      badges += "[SphCol]";
                else if (m->class_name() == "CapsuleColliderModifier")     badges += "[CapCol]";
                else if (m->class_name() == "FogModifier")                 badges += "[Fog]";
                else if (m->class_name() == "LightEmitterModifier")        badges += "[Light]";
                else if (m->class_name() == "CharacterControllerModifier") badges += "[CharCtl]";
            }

            const std::string row_label =
                (has_parent ? "   |-- " : "") + ent.name + "  " + badges + "##node_" + std::to_string(i);
            const bool is_sel = (s.selected_kind == SelKind::kEntity && std::cmp_equal(s.selected, i));
            if (ImGui::Selectable(row_label.c_str(), is_sel))
            {
                s.selected      = static_cast<int>(i);
                s.selected_kind = SelKind::kEntity;
            }

            // Right-Click Context Menu on any Scene Hierarchy row!
            if (ImGui::BeginPopupContextItem())
            {
                s.selected      = static_cast<int>(i);
                s.selected_kind = SelKind::kEntity;
                ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F), "%s", ent.name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Focus Camera (F)"))
                    focus_selected(s, log_push);
                if (ImGui::MenuItem("Duplicate with Modifiers (Ctrl+D)"))
                    deferred_dup_idx = static_cast<int>(i);
                if (ImGui::MenuItem("Snap to Ground Floor (G)"))
                    snap_selected_entity_to_ground(s, log_push);
                if (has_parent && ImGui::MenuItem("Unparent to Root"))
                {
                    s.scene.detach(ent.handle);
                    s.scene.update_transforms();
                }
                if (ImGui::MenuItem("Delete Entity (Del)"))
                    deferred_del_idx = static_cast<int>(i);
                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropSource())
            {
                const int idx_payload = static_cast<int>(i);
                ImGui::SetDragDropPayload("CD_STUDIO_NODE_IDX", &idx_payload, sizeof(int));
                ImGui::Text("Reparent '%s'", ent.name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* np = ImGui::AcceptDragDropPayload("CD_STUDIO_NODE_IDX"))
                {
                    const int child_idx = *static_cast<const int*>(np->Data);
                    if (child_idx != static_cast<int>(i) &&
                        child_idx >= 0 && std::cmp_less(child_idx, s.entities.size()))
                    {
                        s.scene.attach(s.entities[static_cast<std::size_t>(child_idx)].handle, ent.handle);
                        s.scene.update_transforms();
                        log_push("[hierarchy] Attached '" + s.entities[static_cast<std::size_t>(child_idx)].name +
                                 "' -> '" + ent.name + "'");
                    }
                }
                if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_STUDIO_MODIFIER"))
                {
                    const char* cls_id = static_cast<const char*>(mp->Data);
                    if (auto mod = instantiate_modifier_by_class(cls_id))
                    {
                        ms.add_or_replace(std::move(mod));
                        w.is_dragging_modifier = false;
                        s.selected      = static_cast<int>(i);
                        s.selected_kind = SelKind::kEntity;
                        log_push(std::string("[modifier] Dropped ") + cls_id + " onto '" + ent.name + "'");
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        if (deferred_dup_idx >= 0) duplicate_entity_with_modifiers(s, deferred_dup_idx, log_push);
        if (deferred_del_idx >= 0)
        {
            s.selected      = deferred_del_idx;
            s.selected_kind = SelKind::kEntity;
            delete_selected(s, log_push);
        }

        // Also show Scene Lights in the Hierarchy so clicking a light selects it for the 3D Gizmo!
        ImGui::SeparatorText("Scene Lights (Click to Move with 3D Gizmo)");
        for (std::size_t li = 0; li < s.lights.size(); ++li)
        {
            const bool is_lsel = (s.selected_kind == SelKind::kLight && std::cmp_equal(s.selected, li));
            const std::string l_lbl = "[Light] " + s.lights[li].name + "##scene_l_" + std::to_string(li);
            if (ImGui::Selectable(l_lbl.c_str(), is_lsel))
            {
                s.selected      = static_cast<int>(li);
                s.selected_kind = SelKind::kLight;
            }
        }
    }
    ImGui::End();

    // =========================================================================
    // PANEL 3 (Right): Full Transform (Pos + Euler Deg + Scale) & Modifier Stack
    // =========================================================================
    if (ImGui::Begin("Inspector"))
    {
        if (ImGui::BeginTabBar("StudioInspectorTabs"))
        {
            if (ImGui::BeginTabItem("Entity Inspector"))
            {
                if (s.selected_kind == SelKind::kEntity &&
                    s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                {
                    auto& ent = s.entities[static_cast<std::size_t>(s.selected)];

                    // Quick Action Toolbar at top of Inspector
                    if (ImGui::SmallButton("Focus(F)"))    focus_selected(s, log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Dup(Ctrl+D)")) duplicate_entity_with_modifiers(s, s.selected, log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Floor(G)"))    snap_selected_entity_to_ground(s, log_push);
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.68F, 0.20F, 0.20F, 1.0F));
                    if (ImGui::SmallButton("Delete"))      delete_selected(s, log_push);
                    ImGui::PopStyleColor();

                    // Editable Entity Name
                    char name_buf[128] {};
                    std::snprintf(name_buf, sizeof(name_buf), "%s", ent.name.c_str());
                    ImGui::PushItemWidth(175.0F);
                    if (ImGui::InputText("Name", name_buf, sizeof(name_buf)))
                    {
                        ent.name = name_buf;
                    }

                    if (auto* lt = s.scene.local(ent.handle))
                    {
                        ImGui::SeparatorText("Node3D Transform (Pos / Euler / Scale)");
                        if (ImGui::DragFloat3("Position (m)", &lt->value.position.x, 0.05F, -500.0F, 500.0F, "%.2f"))
                        {
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }

                        // Euler Degrees (Pitch, Yaw, Roll) editor synced with Quaternion
                        if (w.euler_cache_deg.find(ent.handle.id) == w.euler_cache_deg.end())
                        {
                            w.euler_cache_deg[ent.handle.id] = quat_to_euler_deg(lt->value.rotation);
                        }
                        cd::math::Vec3f& euler_deg = w.euler_cache_deg[ent.handle.id];
                        if (ImGui::DragFloat3("Rotation (deg)", &euler_deg.x, 1.0F, -180.0F, 180.0F, "%.1f"))
                        {
                            lt->value.rotation = euler_deg_to_quat(euler_deg);
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }
                        // Quick rotation buttons
                        if (ImGui::SmallButton("0 deg"))
                        {
                            euler_deg = { 0.0F, 0.0F, 0.0F };
                            lt->value.rotation = { 0.0F, 0.0F, 0.0F, 1.0F };
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Ramp -25 deg"))
                        {
                            euler_deg = { 0.0F, 0.0F, -25.0F };
                            lt->value.rotation = euler_deg_to_quat(euler_deg);
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Yaw +45 deg"))
                        {
                            euler_deg.y += 45.0F;
                            lt->value.rotation = euler_deg_to_quat(euler_deg);
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }

                        const cd::math::Vec3f old_scale = lt->value.scale;
                        if (ImGui::DragFloat3("Scale", &lt->value.scale.x, 0.05F, 0.01F, 100.0F, "%.2f"))
                        {
                            if (w.uniform_scale_lock && old_scale.x > 1e-4F)
                            {
                                const float ratio = lt->value.scale.x / old_scale.x;
                                lt->value.scale.y = std::max(0.01F, old_scale.y * ratio);
                                lt->value.scale.z = std::max(0.01F, old_scale.z * ratio);
                            }
                            record_entity_home_transform(s, ent.handle);
                            s.scene.update_transforms();
                        }
                        ImGui::SameLine();
                        ImGui::Checkbox("Uni", &w.uniform_scale_lock);
                    }
                    ImGui::PopItemWidth();

                    ImGui::Separator();
                    draw_studio_modifier_stack_ui(
                        w.entity_modifiers[ent.handle.id],
                        ent.name.c_str(),
                        s, log_push);
                }
                else if (s.selected_kind == SelKind::kLight &&
                         s.selected >= 0 && std::cmp_less(s.selected, s.lights.size()))
                {
                    auto& lr = s.lights[static_cast<std::size_t>(s.selected)];
                    ImGui::TextColored(ImVec4(1.0F, 0.88F, 0.35F, 1.0F), "Selected Light: %s", lr.name.c_str());
                    ImGui::Checkbox("Enabled", &lr.enabled);
                    ImGui::DragFloat3("Position (m)", &lr.light.position.x, 0.1F);
                    ImGui::DragFloat3("Direction", &lr.light.direction.x, 0.02F, -1.0F, 1.0F);
                    if (ImGui::SliderFloat("Kelvin (CCT)", &lr.kelvin, 1800.0F, 15000.0F, "%.0f K"))
                        lr.light.color = cd::light::cct_to_linear_rgb(lr.kelvin);
                    ImGui::ColorEdit3("Color Tint", &lr.light.color.x);
                    ImGui::SliderFloat("Intensity", &lr.light.intensity, 100.0F, 120000.0F);
                    ImGui::SliderFloat("Range (m)", &lr.light.range, 1.0F, 40.0F);
                }
                else
                {
                    ImGui::TextDisabled("Select any Entity or Light, or Right-Click in the 3D Viewport!");
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("World & Fog"))
            {
                ImGui::SliderFloat("World Gravity Y (m/s^2)", &w.world_gravity_y, -25.0F, 5.0F, "%.2f");
                ImGui::Separator();
                draw_studio_modifier_stack_ui(
                    w.environment_modifiers,
                    "World Environment",
                    s, log_push);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Lights & Export"))
            {
                ImGui::SeparatorText("Clustered Scene Lights");
                if (ImGui::Button("+ Add Point Light"))
                {
                    const auto pos = camera_spawn_point(s, 2.5F);
                    s.lights.push_back({ "Point Light (2700K)",
                        cd::light::point(pos, { 1.0F, 0.9F, 0.75F }, 2800.0F, 10.0F), true, 2700.0F });
                    log_push("[studio] added Point Light");
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Add Spot Light"))
                {
                    const auto pos = camera_spawn_point(s, 2.5F);
                    s.lights.push_back({ "Spot Light (3200K)",
                        cd::light::spot(pos, { 0.0F, -1.0F, 0.0F }, { 1.0F, 0.95F, 0.85F }, 5000.0F, 12.0F, 0.30F, 0.50F),
                        true, 3200.0F });
                    log_push("[studio] added Spot Light");
                }
                for (std::size_t li = 0; li < s.lights.size(); ++li)
                {
                    auto& lr = s.lights[li];
                    ImGui::PushID(static_cast<int>(li));
                    ImGui::Checkbox(lr.name.c_str(), &lr.enabled);
                    if (lr.enabled)
                    {
                        ImGui::DragFloat3("Pos", &lr.light.position.x, 0.1F);
                        ImGui::SliderFloat("Intensity", &lr.light.intensity, 100.0F, 120000.0F);
                    }
                    ImGui::PopID();
                }

                ImGui::SeparatorText("Standalone Game Export");
                if (ImGui::Button("[*] EXPORT STANDALONE GAME (F9)", ImVec2(-1.0F, 28.0F)))
                {
                    (void)export_standalone_game_package(s, "build/export/ChromodynamicGame", log_push);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // =========================================================================
    // PANEL 4 (Bottom): Physics & Collision Telemetry + Studio Console
    // =========================================================================
    if (ImGui::Begin("History"))
    {
        std::size_t dyn_rb_count = 0;
        std::size_t col_count    = 0;
        for (const auto& ent : s.entities)
        {
            ModifierStack& ms = w.entity_modifiers[ent.handle.id];
            if (const auto* rb = ms.template find<RigidBodyModifier>(); rb && rb->enabled &&
                rb->body_type == cd::physics::BodyType::kDynamic)
            {
                ++dyn_rb_count;
            }
            if (ms.find_collider() != nullptr) ++col_count;
        }

        ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F),
                           "3D Physics & OBB Collision: %s | Dynamic RBs: %zu | Colliders: %zu | Contacts: %zu | %.1f FPS",
                           (s.pie_playing || w.simulate_physics_in_edit) ? "RUNNING (4x Substep Impulse Solver)" : "PAUSED",
                           dyn_rb_count, col_count, w.live_contacts.size(),
                           static_cast<double>(dt > 1e-5F ? 1.0F / dt : 0.0F));
        ImGui::SameLine();
        if (ImGui::SmallButton("Load Ramp & Tower Demo"))
            load_studio_interactive_template(s, 1, log_push);
        ImGui::SameLine();
        if (ImGui::SmallButton("Load Domino Chain Demo"))
            load_studio_interactive_template(s, 2, log_push);
        ImGui::SameLine();
        if (ImGui::SmallButton("Shoot Ball (B)"))
            shoot_physics_ball_from_camera(s, log_push);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Positions (R)"))
            reset_physics_to_home_positions(s, log_push);

        ImGui::Separator();
        ImGui::BeginChild("studio_log_scroll", ImVec2(0, 0), false);
        for (const auto& line : s.log)
        {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace cd::studio
