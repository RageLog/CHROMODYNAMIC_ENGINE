// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioWorkspacePanels.hpp
//
// Studio Workbench (2-column Drag & Drop 3D Objects and Modifier Classes),
// Scene Hierarchy Outliner, Node3D Transform & Modifier Stack Inspector,
// Physics Telemetry Console, and Standalone Game Exporter.
// =============================================================================
#pragma once

#include "StudioPhysicsSolver.hpp"
#include <filesystem>

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
    ImGui::Button("[+] DROP MODIFIER CLASS HERE (or click + Add below)", ImVec2(-1.0F, 24.0F));
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
            std::string(mod->display_name()).c_str(),
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

template <typename StateT>
inline void draw_ingame_hud_workspace(
    StateT&                s,
    float                  dt,
    const cd::math::Mat4f& vp,
    std::uint32_t          frame_w,
    std::uint32_t          frame_h)
{
    const float sw = static_cast<float>(frame_w);
    const float sh = static_cast<float>(frame_h);

    static float s_health     = 86.0F;
    static float s_shield     = 64.0F;
    static float s_spread     = 0.22F;
    static int   s_active_slot = 0;
    static bool  s_show_tags  = true;

    // 1. Project 3D entities to screen and draw World-Space Entity Health/Status Tags
    if (s_show_tags)
    {
        for (std::size_t i = 0; i < s.entities.size(); ++i)
        {
            const auto& ent = s.entities[i];
            if (ent.kind == PrimitiveKind::kSponza || ent.name.find("Floor") != std::string::npos)
                continue;
            const auto* lt = s.scene.local(ent.handle);
            if (!lt) continue;

            const cd::math::Vec3f tag_world = {
                lt->value.position.x,
                lt->value.position.y + lt->value.scale.y * 0.85F + 0.45F,
                lt->value.position.z
            };
            ImVec2 tag_px{};
            if (project_world_to_screen(tag_world, vp, sw, sh, tag_px))
            {
                const float dx = lt->value.position.x - s.cam.eye.x;
                const float dy = lt->value.position.y - s.cam.eye.y;
                const float dz = lt->value.position.z - s.cam.eye.z;
                const float dist_m = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float hp_frac = 0.35F + 0.65F * std::abs(std::sin(static_cast<float>(i) * 1.3F + 0.5F));
                cd::ui::chroma::hud::DrawWorldSpaceEntityTag(
                    tag_px, ent.name.c_str(), dist_m, hp_frac, static_cast<int>(i) == s.selected);
            }
        }
    }

    // 2. Top-Right Radar Minimap with live entity blips
    ImVec2 blip_coords[16]{};
    std::size_t blip_count = 0;
    for (std::size_t i = 0; i < s.entities.size() && blip_count < 16U; ++i)
    {
        if (const auto* lt = s.scene.local(s.entities[i].handle))
        {
            blip_coords[blip_count] = ImVec2((lt->value.position.x - s.cam.eye.x) * 0.08F,
                                             (lt->value.position.z - s.cam.eye.z) * 0.08F);
            ++blip_count;
        }
    }
    const float cam_yaw = std::atan2(s.cam.target.z - s.cam.eye.z, s.cam.target.x - s.cam.eye.x);
    cd::ui::chroma::hud::DrawMinimapRadar(
        ImVec2(sw - 110.0F, 155.0F), 82.0F, cam_yaw,
        std::span<const ImVec2>(blip_coords, blip_count));

    // 3. Center Dynamic Crosshair
    cd::ui::chroma::hud::DrawDynamicCrosshair(
        ImVec2(sw * 0.5F, sh * 0.5F), 6.0F + s_spread * 16.0F, IM_COL32(90, 245, 165, 230));

    // 4. Bottom-Left Player Health & Shield Bars
    cd::ui::chroma::hud::DrawHealthShieldBar(
        ImVec2(28.0F, sh - 92.0F), 280.0F, s_health, 100.0F, s_shield, 100.0F, 0.78F);

    // 5. Bottom-Center Inventory Hotbar (8 slots)
    const char* slot_names[8] = {
        "Pulse Rifle", "Gravity Beam", "Omni Wrench", "Nano Medkit",
        "EMP Grenade", "Scanner", "Jet Thruster", "Chrono Core"
    };
    const float slot_cooldowns[8] = { 0.0F, 0.25F, 0.0F, 0.0F, 0.60F, 0.0F, 0.15F, 0.0F };
    s_active_slot = cd::ui::chroma::hud::DrawInventoryHotbar(
        ImVec2(sw * 0.5F, sh - 10.0F), s_active_slot, slot_names, slot_cooldowns);

    // 6. Floating Tactical HUD Controller Window (Top-Left)
    ImGui::SetNextWindowPos(ImVec2(24.0F, 74.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320.0F, 240.0F), ImGuiCond_Always);
    if (ImGui::Begin("In-Game HUD Telemetry & Controls"))
    {
        ImGui::TextColored(ImVec4(0.35F, 0.95F, 0.55F, 1.0F), "ChromaUI In-Game HUD Overlay (%.1f FPS)",
                           static_cast<double>(dt > 1e-5F ? 1.0F / dt : 60.0F));
        ImGui::SliderFloat("Armor Integrity", &s_health, 0.0F, 100.0F, "%.0f HP");
        ImGui::SliderFloat("Kinetic Shield", &s_shield, 0.0F, 100.0F, "%.0f SP");
        ImGui::SliderFloat("Reticle Bloom", &s_spread, 0.0F, 1.0F, "%.2f");
        ImGui::SliderInt("Equipped Slot", &s_active_slot, 0, 7);
        ImGui::ToggleSwitch("3D World-Space Entity Tags", &s_show_tags);
        if (ImGui::Button("Trigger Mission Objective Toast", ImVec2(-1.0F, 24.0F)))
        {
            ImGui::PushToast("Mission Objective", "Secure Sponza Courtyard", 0, 3.0F);
        }
    }
    ImGui::End();
}

inline void draw_node_and_app_lab_workspace(std::uint32_t frame_w, std::uint32_t frame_h)
{
    const float sw = static_cast<float>(frame_w);
    const float sh = static_cast<float>(frame_h);

    static float s_curve_pts[8] = {
        0.0F, 0.0F,
        0.28F, 0.72F,
        0.68F, 0.25F,
        1.0F, 1.0F
    };
    static float s_timeline_t  = 2.45F;
    static float s_knob_rough  = 0.32F;
    static float s_knob_metal  = 0.85F;
    static float s_knob_ior    = 1.50F;
    static float s_tint_col[3] = { 0.24F, 0.68F, 0.96F };
    static bool  s_hdr_bloom   = true;
    static bool  s_ray_shadows = true;
    static int   s_blend_mode  = 0;

    // Left Window: Node Graph & Shader Pipeline Editor
    ImGui::SetNextWindowPos(ImVec2(14.0F, 68.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw * 0.62F - 20.0F, sh - 82.0F), ImGuiCond_Always);
    if (ImGui::Begin("ChromaUI Node Graph & Timeline Studio"))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.85F, 1.0F, 1.0F),
                           "PBR Material & Post-FX Shader Graph (Drag Nodes, Cubic Bezier Wires)");
        ImGui::BeginNodeGraph("StudioShaderGraph", ImVec2(0.0F, sh - 265.0F));

        ImGui::BeginNode(1U, "Texture2D Sampler (Albedo)", ImVec2(32.0F, 42.0F),
                         ImVec2(205.0F, 86.0F), IM_COL32(45, 115, 185, 255));
        ImGui::NodePin(101U, "UV Coord", false, IM_COL32(120, 200, 255, 255));
        ImGui::NodePin(102U, "RGBA Out", true, IM_COL32(255, 180, 80, 255));
        ImGui::EndNode();

        ImGui::BeginNode(2U, "Fresnel & Roughness", ImVec2(32.0F, 175.0F),
                         ImVec2(205.0F, 86.0F), IM_COL32(145, 65, 175, 255));
        ImGui::NodePin(201U, "Roughness", true, IM_COL32(100, 235, 160, 255));
        ImGui::NodePin(202U, "Metallic", true, IM_COL32(100, 235, 160, 255));
        ImGui::EndNode();

        ImGui::BeginNode(3U, "PBR GGX Surface BRDF", ImVec2(315.0F, 82.0F),
                         ImVec2(215.0F, 118.0F), IM_COL32(35, 145, 95, 255));
        ImGui::NodePin(301U, "BaseColor", false, IM_COL32(255, 180, 80, 255));
        ImGui::NodePin(302U, "Roughness", false, IM_COL32(100, 235, 160, 255));
        ImGui::NodePin(303U, "Metallic", false, IM_COL32(100, 235, 160, 255));
        ImGui::NodePin(304U, "Radiance", true, IM_COL32(95, 195, 255, 255));
        ImGui::EndNode();

        ImGui::BeginNode(4U, "ACES Filmic Output", ImVec2(610.0F, 98.0F),
                         ImVec2(195.0F, 76.0F), IM_COL32(185, 85, 45, 255));
        ImGui::NodePin(401U, "Linear HDR", false, IM_COL32(95, 195, 255, 255));
        ImGui::EndNode();

        ImGui::NodeLink(102U, 301U, IM_COL32(255, 180, 80, 235), 2.6F);
        ImGui::NodeLink(201U, 302U, IM_COL32(100, 235, 160, 235), 2.4F);
        ImGui::NodeLink(202U, 303U, IM_COL32(100, 235, 160, 235), 2.4F);
        ImGui::NodeLink(304U, 401U, IM_COL32(95, 195, 255, 245), 2.8F);

        ImGui::EndNodeGraph();

        ImGui::Separator();
        const float kf_times[5] = { 0.35F, 1.20F, 2.45F, 3.80F, 4.65F };
        ImGui::TimelineScrubber("Keyframe Track", &s_timeline_t, 5.0F, kf_times, 5);
    }
    ImGui::End();

    // Right Window: Standalone App Widget Gallery (Knobs, Curve Editor, Color Picker, Tree, Toggles)
    ImGui::SetNextWindowPos(ImVec2(sw * 0.62F + 6.0F, 68.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw * 0.38F - 20.0F, sh - 82.0F), ImGuiCond_Always);
    if (ImGui::Begin("ChromaUI Widget Catalog 2.0 Showcase"))
    {
        ImGui::TextColored(ImVec4(0.45F, 0.90F, 1.0F, 1.0F), "Rotary Knobs & Toggle Switches");
        ImGui::KnobFloat("Roughness", &s_knob_rough, 0.0F, 1.0F, 22.0F);
        ImGui::SameLine();
        ImGui::KnobFloat("Metallic", &s_knob_metal, 0.0F, 1.0F, 22.0F);
        ImGui::SameLine();
        ImGui::KnobFloat("IndexRefr", &s_knob_ior, 1.0F, 2.5F, 22.0F);

        ImGui::Separator();
        ImGui::ToggleSwitch("HDR Bloom Convolution Pass", &s_hdr_bloom);
        ImGui::ToggleSwitch("Ray-Traced Contact Shadows", &s_ray_shadows);

        ImGui::RadioButton("Opaque", &s_blend_mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("AlphaBlend", &s_blend_mode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Additive", &s_blend_mode, 2);

        ImGui::Separator();
        ImGui::ColorEdit3("Surface Albedo (Click Swatch)", s_tint_col);

        ImGui::Separator();
        ImGui::CurveEditor("Easing Response Curve", s_curve_pts, ImVec2(0.0F, 125.0F));

        ImGui::SeparatorText("Telemetry, Search & Hierarchy");
        static char s_node_filter[64] = "GGX Surface BRDF";
        ImGui::InputTextWithHint("Filter", "Search shader nodes...", s_node_filter, sizeof(s_node_filter));
        ImGui::ProgressBar(0.84F, ImVec2(-1.0F, 16.0F), "Shader Permutation Cache 84%");

        static constexpr float kFrameTimes[16] = {
            4.2F, 4.4F, 4.1F, 4.8F, 5.1F, 4.5F, 4.3F, 4.0F,
            4.2F, 4.6F, 4.3F, 4.1F, 3.9F, 4.2F, 4.4F, 4.1F
        };
        ImGui::PlotLines("##gpu_ft", kFrameTimes, 16, 0, "GPU Pass Frame Time (4.1 ms)", 3.0F, 6.5F, ImVec2(0.0F, 48.0F));

        if (ImGui::TreeNodeEx("RenderGraph Pass Pipeline", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TreeNodeEx("1. GBuffer Velocity + Normal (0.82 ms)", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreeNodeEx("2. Direct Lighting + Ray Shadows (1.45 ms)", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreeNodeEx("3. ACES Filmic + ChromaUI Pass (0.34 ms)", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreePop();
        }
    }
    ImGui::End();
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
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();

    // Draw 3D Collider wireframes + full-width Ribbon Toolbar + right-click context menu
    draw_studio_viewport_overlays_and_drop_targets(s, vp, frame_w, frame_h, log_push);

    if (s.pie_playing || cst.workspace_mode == 1)
    {
        draw_ingame_hud_workspace(s, dt, vp, frame_w, frame_h);
        return;
    }
    if (cst.workspace_mode == 2)
    {
        draw_node_and_app_lab_workspace(frame_w, frame_h);
        return;
    }

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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14F, 0.45F, 0.36F, 1.0F));
            if (ImGui::Button("0. Clean Ultra-FPS", ImVec2(-1.0F, 19.5F)))
            {
                load_studio_interactive_template(s, 0, log_push);
            }
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.42F, 0.32F, 1.0F));
            if (ImGui::Button("1. Ramp & Tower", ImVec2(-1.0F, 19.5F)))
                load_studio_interactive_template(s, 1, log_push);
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.42F, 0.32F, 1.0F));
            if (ImGui::Button("2. Domino Chain", ImVec2(-1.0F, 19.5F)))
                load_studio_interactive_template(s, 2, log_push);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24F, 0.34F, 0.50F, 1.0F));
            if (ImGui::Button("4. PBR 5x5 Grid", ImVec2(-1.0F, 19.5F)))
                load_studio_interactive_template(s, 4, log_push);
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.32F, 0.26F, 0.48F, 1.0F));
            if (ImGui::Button("5. Sponza Atrium", ImVec2(-1.0F, 19.5F)))
                load_studio_interactive_template(s, 5, log_push);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42F, 0.30F, 0.18F, 1.0F));
            if (ImGui::Button("6. Asset & Tex Lab", ImVec2(-1.0F, 19.5F)))
                load_studio_interactive_template(s, 12, log_push);
            ImGui::PopStyleColor();
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
                if (ImGui::Button(card.short_label, ImVec2(-1.0F, 19.5F)))
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
                if (ImGui::Button(btn_txt.c_str(), ImVec2(-1.0F, 19.5F)))
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
    // PANEL 2 (Left Bottom): Multi-World, Multi-Level, Layer & Scene Outliner
    // =========================================================================
    if (ImGui::Begin("Scene"))
    {
        if (ImGui::BeginTabBar("StudioSceneOutlinerTabs"))
        {
            if (ImGui::BeginTabItem("Hierarchy & Entities"))
            {
                ImGui::SetNextItemWidth(135.0F);
                ImGui::InputTextWithHint("##filter_nodes", "Filter nodes...", w.hierarchy_filter, sizeof(w.hierarchy_filter));
                ImGui::SameLine();
                if (ImGui::SmallButton("Save"))
                    (void)save_full_studio_scene(s, "studio_scene.cdscene.json", log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Load"))
                    (void)load_full_studio_scene(s, "studio_scene.cdscene.json", log_push);

                ImGui::TextColored(ImVec4(0.55F, 0.88F, 1.0F, 1.0F),
                                   "World: %s", w.active_world_name.c_str());
                ImGui::TextColored(ImVec4(0.45F, 0.96F, 0.68F, 1.0F),
                                   "Level: %s", w.active_level_name.c_str());

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
                    if (!ent.visible) badges += "[HIDDEN]";
                    if (ent.locked)   badges += "[LOCKED]";
                    if (ent.use_texture && ent.custom_texture_id > 0)
                        badges += "[TEX:" + std::to_string(ent.custom_texture_id) + "]";
                    const std::string_view ent_lyr = cd::world_container::layer_of(s.ecs_world, ent.handle);
                    if (!ent_lyr.empty() && ent_lyr != "Default")
                    {
                        badges += "[" + std::string(ent_lyr) + "]";
                    }
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
                        if (auto* active_lvl = s.cd_world.active_level_ptr())
                        {
                            if (ImGui::BeginMenu("Assign to Layer"))
                            {
                                for (std::size_t li = 0; li < active_lvl->layer_count(); ++li)
                                {
                                    const auto* lyr = active_lvl->layer(li);
                                    if (!lyr) continue;
                                    const bool on_lyr = (ent_lyr == lyr->name());
                                    const std::string lyr_nm { lyr->name() };
                                    if (ImGui::MenuItem(lyr_nm.c_str(), nullptr, on_lyr))
                                    {
                                        cd::world_container::assign_layer(s.ecs_world, ent.handle, lyr_nm);
                                        log_push("[layer] Assigned '" + ent.name + "' -> Layer '" + lyr_nm + "'");
                                    }
                                }
                                ImGui::EndMenu();
                            }
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
                        if (const ImGuiPayload* mesh_p = ImGui::AcceptDragDropPayload("CD_ASSET_MESH"))
                        {
                            const char* mesh_id = static_cast<const char*>(mesh_p->Data);
                            (void)assign_mesh_asset_to_entity(s, static_cast<int>(i), mesh_id, log_push);
                            w.is_dragging_asset_mesh = false;
                            s.selected      = static_cast<int>(i);
                            s.selected_kind = SelKind::kEntity;
                        }
                        if (const ImGuiPayload* tex_p = ImGui::AcceptDragDropPayload("CD_ASSET_TEXTURE"))
                        {
                            const char* tex_id = static_cast<const char*>(tex_p->Data);
                            (void)assign_texture_asset_to_entity(s, static_cast<int>(i), tex_id, log_push);
                            w.is_dragging_asset_texture = false;
                            s.selected      = static_cast<int>(i);
                            s.selected_kind = SelKind::kEntity;
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
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Worlds & Levels"))
            {
                if (auto* proj = s.cd_world.project())
                {
                    const std::string proj_nm { proj->name() };
                    ImGui::TextColored(ImVec4(0.45F, 0.95F, 0.65F, 1.0F),
                                       "Project: %s (%zu Levels)",
                                       proj_nm.c_str(), proj->level_count());
                    if (ImGui::SmallButton("Restore Master 5-World Project"))
                    {
                        s.cd_world.set_project(nullptr);
                        populate_master_showcase_project(s.cd_world);
                        if (const auto* np = s.cd_world.project())
                        {
                            w.active_project_name = std::string(np->name());
                            if (const auto* lvl0 = np->level(0))
                                (void)switch_to_project_level(s, lvl0->name(), log_push);
                        }
                        log_push("[project] Restored ChromodynamicMasterShowcase (5 Worlds, 12 Levels)");
                    }
                    ImGui::Separator();

                    if (const auto* cur_proj = s.cd_world.project())
                    {
                        const auto worlds = cur_proj->world_names();
                        for (const auto& wname : worlds)
                        {
                            ImGui::SeparatorText(wname.c_str());
                            const auto lvl_indices = cur_proj->levels_in_world(wname);
                            for (std::size_t li : lvl_indices)
                            {
                                const auto* lvl = cur_proj->level(li);
                                if (!lvl) continue;
                                const std::string lvl_nm { lvl->name() };
                                const bool is_active = (cur_proj->active_level() == li);
                                const std::string btn_lbl = (is_active ? "[ACTIVE] " : "   ") + lvl_nm + "##wl_" + lvl_nm;
                                if (ImGui::Selectable(btn_lbl.c_str(), is_active))
                                {
                                    (void)switch_to_project_level(s, lvl->name(), log_push);
                                }
                                if (!lvl->description().empty() && ImGui::IsItemHovered())
                                {
                                    const std::string wn_str { lvl->world_name() };
                                    const std::string desc_str { lvl->description() };
                                    ImGui::SetTooltip("World: %s\nLevel: %s\n%s",
                                                      wn_str.c_str(), lvl_nm.c_str(), desc_str.c_str());
                                }
                            }
                        }
                    }

                    ImGui::Separator();
                    ImGui::SetNextItemWidth(145.0F);
                    ImGui::InputTextWithHint("##new_lvl_nm", "New level name...", w.new_level_name_buf, sizeof(w.new_level_name_buf));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+ Add Level") && w.new_level_name_buf[0] != '\0')
                    {
                        if (auto* mut_proj = s.cd_world.project())
                        {
                            auto* nl = mut_proj->add_level(w.new_level_name_buf);
                            nl->set_world_name(w.active_world_name.empty() ? "World 1: Ultra-FPS & Physics" : w.active_world_name);
                            nl->set_description("User-created custom level");
                            nl->set_template_id(0);
                            populate_standard_level_layers(*nl);
                            (void)switch_to_project_level(s, w.new_level_name_buf, log_push);
                        }
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Layers"))
            {
                if (auto* active_lvl = s.cd_world.active_level_ptr())
                {
                    const std::string active_lvl_nm { active_lvl->name() };
                    ImGui::TextColored(ImVec4(0.55F, 0.88F, 1.0F, 1.0F),
                                       "Level: %s (%zu Layers)",
                                       active_lvl_nm.c_str(), active_lvl->layer_count());
                    ImGui::TextDisabled("Toggle Vis (culls rendering) or Lock (blocks selection/gizmo)");
                    ImGui::Separator();

                    for (std::size_t li = 0; li < active_lvl->layer_count(); ++li)
                    {
                        auto* lyr = active_lvl->layer(li);
                        if (!lyr) continue;
                        ImGui::PushID(static_cast<int>(li));
                        const std::string lyr_nm { lyr->name() };
                        bool vis = lyr->visible();
                        if (ImGui::Checkbox("Vis", &vis))
                        {
                            lyr->set_visible(vis);
                            for (auto& e : s.entities)
                            {
                                if (cd::world_container::layer_of(s.ecs_world, e.handle) == lyr_nm)
                                {
                                    e.visible = vis;
                                }
                            }
                        }
                        ImGui::SameLine();
                        bool lck = lyr->locked();
                        if (ImGui::Checkbox("Lock", &lck))
                        {
                            lyr->set_locked(lck);
                            for (auto& e : s.entities)
                            {
                                if (cd::world_container::layer_of(s.ecs_world, e.handle) == lyr_nm)
                                {
                                    e.locked = lck;
                                }
                            }
                        }
                        ImGui::SameLine();
                        bool pst = lyr->persistent();
                        if (ImGui::Checkbox("Persist", &pst)) lyr->set_persistent(pst);
                        ImGui::SameLine();
                        const bool is_spawn_lyr = (w.active_layer_name == lyr_nm);
                        const std::string lyr_lbl = (is_spawn_lyr ? "[Spawn] " : "") + lyr_nm;
                        if (ImGui::Selectable(lyr_lbl.c_str(), is_spawn_lyr))
                        {
                            w.active_layer_name = lyr_nm;
                            log_push("[layer] Active spawn layer set to '" + lyr_nm + "'");
                        }
                        ImGui::PopID();
                    }

                    ImGui::Separator();
                    ImGui::SetNextItemWidth(145.0F);
                    ImGui::InputTextWithHint("##new_lyr_nm", "New layer...", w.new_layer_name_buf, sizeof(w.new_layer_name_buf));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+ Add Layer") && w.new_layer_name_buf[0] != '\0')
                    {
                        if (active_lvl->find_layer(w.new_layer_name_buf) == nullptr)
                        {
                            (void)active_lvl->add_layer(w.new_layer_name_buf);
                        }
                        w.active_layer_name = w.new_layer_name_buf;
                        log_push(std::string("[layer] Added layer '") + w.new_layer_name_buf + "'");
                    }
                }
                else
                {
                    ImGui::TextDisabled("No active level selected.");
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
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

                    // Layer assignment & visibility/lock state
                    if (auto* active_lvl = s.cd_world.active_level_ptr())
                    {
                        const std::string cur_lyr_str { cd::world_container::layer_of(s.ecs_world, ent.handle) };
                        if (ImGui::BeginCombo("Layer", cur_lyr_str.c_str()))
                        {
                            for (std::size_t li = 0; li < active_lvl->layer_count(); ++li)
                            {
                                const auto* lyr = active_lvl->layer(li);
                                if (!lyr) continue;
                                const std::string lyr_nm { lyr->name() };
                                const bool sel = (cur_lyr_str == lyr_nm);
                                if (ImGui::Selectable(lyr_nm.c_str(), sel))
                                {
                                    cd::world_container::assign_layer(s.ecs_world, ent.handle, lyr_nm);
                                    ent.visible = lyr->visible();
                                    ent.locked  = lyr->locked();
                                    log_push("[layer] Assigned '" + ent.name + "' -> Layer '" + lyr_nm + "'");
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                    ImGui::Checkbox("Visible", &ent.visible);
                    ImGui::SameLine();
                    ImGui::Checkbox("Locked", &ent.locked);

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

                    // ---- Dedicated Mesh & Surface Texture Asset Slots (Drop Targets + Quick Assign) ----
                    ImGui::SeparatorText("Mesh & Surface Texture Assets (Drop Target)");
                    auto mesh_kind_name = [](PrimitiveKind k) noexcept -> const char*
                    {
                        switch (k)
                        {
                            case PrimitiveKind::kCube:     return "Cube.mesh";
                            case PrimitiveKind::kSphere:   return "Sphere.mesh";
                            case PrimitiveKind::kCylinder: return "Cylinder.mesh";
                            case PrimitiveKind::kCone:     return "Cone.mesh";
                            case PrimitiveKind::kTorus:    return "Torus.mesh";
                            case PrimitiveKind::kGltf:     return "CesiumMan.glb";
                            case PrimitiveKind::kSponza:   return "Sponza.gltf";
                        }
                        return "Cube.mesh";
                    };
                    const std::string cur_mesh_lbl = std::string("[MESH] ") +
                        (!ent.custom_mesh_path.empty() ? ent.custom_mesh_path : std::string(mesh_kind_name(ent.kind))) +
                        "  (Drop Mesh Here)";
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15F, 0.30F, 0.44F, 0.95F));
                    ImGui::Button(cur_mesh_lbl.c_str(), ImVec2(-1.0F, 22.0F));
                    ImGui::PopStyleColor();
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* mp = ImGui::AcceptDragDropPayload("CD_ASSET_MESH"))
                        {
                            const char* mesh_id = static_cast<const char*>(mp->Data);
                            (void)assign_mesh_asset_to_entity(s, s.selected, mesh_id, log_push);
                            w.is_dragging_asset_mesh = false;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::SmallButton("Cube"))      (void)assign_mesh_asset_to_entity(s, s.selected, "assets/meshes/Cube.mesh", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Sphere"))    (void)assign_mesh_asset_to_entity(s, s.selected, "assets/meshes/Sphere.mesh", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Cyl"))       (void)assign_mesh_asset_to_entity(s, s.selected, "assets/meshes/Cylinder.mesh", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Torus"))     (void)assign_mesh_asset_to_entity(s, s.selected, "assets/meshes/Torus.mesh", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("CesiumMan")) (void)assign_mesh_asset_to_entity(s, s.selected, "assets/samples/Generic/CesiumMan.glb", log_push);

                    auto tex_slot_name = [](int slot, const std::string& path) -> std::string
                    {
                        if (!path.empty()) return path;
                        switch (slot)
                        {
                            case 1: return "Earth_Albedo.png (Slot 1)";
                            case 2: return "UV_Checkerboard_2K.tex (Slot 2)";
                            case 3: return "Brick_Wall_PBR.tex (Slot 3)";
                            case 4: return "Carbon_Fiber_Twill.tex (Slot 4)";
                            case 5: return "Carrara_Marble.tex (Slot 5)";
                            case 6: return "Oak_Wood_Planks.tex (Slot 6)";
                            default: return "None (Solid Albedo Tint)";
                        }
                    };
                    const std::string cur_tex_lbl = std::string("[TEX] ") +
                        tex_slot_name(ent.use_texture ? ent.custom_texture_id : 0, ent.custom_texture_path) +
                        "  (Drop Texture Here)";
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16F, 0.36F, 0.26F, 0.95F));
                    ImGui::Button(cur_tex_lbl.c_str(), ImVec2(-1.0F, 22.0F));
                    ImGui::PopStyleColor();
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* tp = ImGui::AcceptDragDropPayload("CD_ASSET_TEXTURE"))
                        {
                            const char* tex_id = static_cast<const char*>(tp->Data);
                            (void)assign_texture_asset_to_entity(s, s.selected, tex_id, log_push);
                            w.is_dragging_asset_texture = false;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::SmallButton("Checker")) (void)assign_texture_asset_to_entity(s, s.selected, "assets/textures/UV_Checkerboard_2K.tex", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Brick"))   (void)assign_texture_asset_to_entity(s, s.selected, "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Carbon"))  (void)assign_texture_asset_to_entity(s, s.selected, "assets/textures/Carbon_Fiber_Twill.tex", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Marble"))  (void)assign_texture_asset_to_entity(s, s.selected, "assets/textures/Carrara_Marble.tex", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Wood"))    (void)assign_texture_asset_to_entity(s, s.selected, "assets/textures/Oak_Wood_Planks.tex", log_push);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))   (void)assign_texture_asset_to_entity(s, s.selected, "None", log_push);

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
    // PANEL 4 (Bottom): Content Browser & Folder Tree, Console, Showcase Hub, Profiler
    // =========================================================================
    if (ImGui::Begin("History"))
    {
        if (ImGui::BeginTabBar("BottomDockTabBar"))
        {
            if (ImGui::BeginTabItem("Content Browser & Folder Tree"))
            {
                // Top Breadcrumb & Filter Bar
                if (ImGui::SmallButton("[Root: assets]"))
                    w.current_content_folder = "assets";
                ImGui::SameLine();
                if (ImGui::SmallButton("[Up ..]"))
                {
                    const auto pos = w.current_content_folder.find_last_of("/\\");
                    if (pos != std::string::npos && pos > 0)
                        w.current_content_folder = w.current_content_folder.substr(0, pos);
                    else
                        w.current_content_folder = "assets";
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.50F, 0.90F, 1.0F, 1.0F),
                                   "Folder: %s", w.current_content_folder.c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150.0F);
                ImGui::InputTextWithHint("##asset_search", "Filter files/assets...",
                                         w.content_search_filter, sizeof(w.content_search_filter));
                ImGui::SameLine();
                const char* filter_names[] = { "All", "Meshes", "Textures", "Materials" };
                for (int fi = 0; fi < 4; ++fi)
                {
                    if (fi > 0) ImGui::SameLine();
                    const bool active_f = (w.content_type_filter == fi);
                    if (active_f) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.50F, 0.82F, 1.0F));
                    if (ImGui::SmallButton(filter_names[fi])) w.content_type_filter = fi;
                    if (active_f) ImGui::PopStyleColor();
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.45F, 0.96F, 0.65F, 1.0F),
                                   "| %s", w.last_asset_status_msg.c_str());

                ImGui::Separator();

                // ---- LEFT PANE: Interactive Folder Tree ----
                ImGui::BeginChild("folder_tree_scroll", ImVec2(265.0F, 0.0F), true);
                ImGui::TextColored(ImVec4(0.95F, 0.82F, 0.38F, 1.0F), "Workspace Folder Tree");
                struct FolderNode
                {
                    const char* label;
                    const char* path;
                };
                constexpr std::array<FolderNode, 9> kFolders {{
                    { "[+] assets/ (All Workspace Assets)",          "assets" },
                    { " |-- [Meshes] Primitives & glTF",             "assets/meshes" },
                    { " |-- [Textures] PBR Surface Maps",            "assets/textures" },
                    { " |-- samples/Sponza/ (Atrium glTF)",          "assets/samples/Sponza" },
                    { " |    |-- Sponza/textures/ (69 Disk JPG/PNG)","assets/samples/Sponza/textures" },
                    { " |-- samples/Generic/ (CesiumMan.glb)",       "assets/samples/Generic" },
                    { "[+] showcases/ (12 .cdproj Worlds)",          "showcases" },
                    { "[+] apps/editor/shaders/ (GLSL)",             "apps/editor/shaders" },
                    { "[+] docs/ADR/ (Academic SOTA Papers)",        "docs/ADR" },
                }};
                for (const auto& fn : kFolders)
                {
                    const bool is_cur_dir = (w.current_content_folder == fn.path);
                    if (ImGui::Selectable(fn.label, is_cur_dir))
                    {
                        w.current_content_folder = fn.path;
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                // ---- RIGHT PANE: Context Asset Cards Grid (Drag & Drop + 1-Click Assign) ----
                ImGui::BeginChild("asset_cards_scroll", ImVec2(0.0F, 0.0F), true);

                struct AssetEntry
                {
                    std::string name;
                    std::string path;
                    int         kind; // 1 = Mesh, 2 = Texture, 3 = Material, 4 = Scene/Shader
                    std::string badge;
                };
                std::vector<AssetEntry> entries {};
                entries.reserve(32);

                // 1. Always provide core ready-to-assign Meshes & Textures when in "assets", "assets/meshes", or "assets/textures"
                if (w.current_content_folder == "assets")
                {
                    // Row 1: Core 3D Meshes & glTFs
                    entries.push_back({ "Sphere.mesh",           "assets/meshes/Sphere.mesh",                             1, "[MESH]" });
                    entries.push_back({ "Cube.mesh",             "assets/meshes/Cube.mesh",                               1, "[MESH]" });
                    entries.push_back({ "Cylinder.mesh",         "assets/meshes/Cylinder.mesh",                           1, "[MESH]" });
                    entries.push_back({ "CesiumMan.glb",         "assets/samples/Generic/CesiumMan.glb",                  1, "[GLTF]" });
                    // Row 2: Core Surface Textures & PBR Maps (immediately visible!)
                    entries.push_back({ "UV_Checkerboard_2K.tex","assets/textures/UV_Checkerboard_2K.tex",                2, "[TEX]" });
                    entries.push_back({ "Brick_Wall_PBR.tex",    "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg",2, "[TEX]" });
                    entries.push_back({ "Carbon_Fiber_Twill.tex","assets/textures/Carbon_Fiber_Twill.tex",                2, "[TEX]" });
                    entries.push_back({ "Carrara_Marble.tex",    "assets/textures/Carrara_Marble.tex",                    2, "[TEX]" });
                    // Row 3+: Additional Meshes, Wood Texture & PBR Materials
                    entries.push_back({ "Oak_Wood_Planks.tex",   "assets/textures/Oak_Wood_Planks.tex",                   2, "[TEX]" });
                    entries.push_back({ "Torus.mesh",            "assets/meshes/Torus.mesh",                              1, "[MESH]" });
                    entries.push_back({ "Cone.mesh",             "assets/meshes/Cone.mesh",                               1, "[MESH]" });
                    entries.push_back({ "Sponza.gltf",           "assets/samples/Sponza/Sponza.gltf",                     1, "[GLTF]" });
                    entries.push_back({ "Mat_Gold_24K.pbr",      "assets/materials/Mat_Gold_24K.pbr",                     3, "[MAT]" });
                    entries.push_back({ "Mat_Iridescent_440nm.pbr","assets/materials/Mat_Iridescent_440nm.pbr",           3, "[MAT]" });
                }
                else
                {
                    if (w.current_content_folder == "assets/meshes" ||
                        w.current_content_folder == "assets/samples/Generic" || w.current_content_folder == "assets/samples/Sponza")
                    {
                        entries.push_back({ "Sphere.mesh",    "assets/meshes/Sphere.mesh",    1, "[MESH]" });
                        entries.push_back({ "Cube.mesh",      "assets/meshes/Cube.mesh",      1, "[MESH]" });
                        entries.push_back({ "Cylinder.mesh",  "assets/meshes/Cylinder.mesh",  1, "[MESH]" });
                        entries.push_back({ "Torus.mesh",     "assets/meshes/Torus.mesh",     1, "[MESH]" });
                        entries.push_back({ "Cone.mesh",      "assets/meshes/Cone.mesh",      1, "[MESH]" });
                        entries.push_back({ "Ramp_Wedge.mesh","assets/meshes/Ramp_Wedge.mesh",1, "[MESH]" });
                        entries.push_back({ "CesiumMan.glb",  "assets/samples/Generic/CesiumMan.glb", 1, "[GLTF]" });
                        entries.push_back({ "Sponza.gltf",    "assets/samples/Sponza/Sponza.gltf",    1, "[GLTF]" });
                    }
                    if (w.current_content_folder == "assets/textures" ||
                        w.current_content_folder == "assets/samples/Sponza/textures")
                    {
                        entries.push_back({ "UV_Checkerboard_2K.tex",  "assets/textures/UV_Checkerboard_2K.tex",  2, "[TEX]" });
                        entries.push_back({ "Brick_Wall_PBR.tex",      "assets/samples/Sponza/textures/sponza_brick_a_diff.jpg", 2, "[TEX]" });
                        entries.push_back({ "Carbon_Fiber_Twill.tex",  "assets/textures/Carbon_Fiber_Twill.tex",  2, "[TEX]" });
                        entries.push_back({ "Carrara_Marble.tex",      "assets/textures/Carrara_Marble.tex",      2, "[TEX]" });
                        entries.push_back({ "Oak_Wood_Planks.tex",     "assets/textures/Oak_Wood_Planks.tex",     2, "[TEX]" });
                        entries.push_back({ "Earth_Albedo.png",        "assets/textures/Earth_Albedo.png",        2, "[TEX]" });
                        entries.push_back({ "Mat_Gold_24K.pbr",        "assets/materials/Mat_Gold_24K.pbr",       3, "[MAT]" });
                        entries.push_back({ "Mat_Iridescent_440nm.pbr","assets/materials/Mat_Iridescent_440nm.pbr",3,"[MAT]" });
                    }
                }

                // 2. Scan real disk directory via std::filesystem::directory_iterator if it exists on disk!
                std::error_code ec {};
                if (std::filesystem::exists(w.current_content_folder, ec) &&
                    std::filesystem::is_directory(w.current_content_folder, ec))
                {
                    std::size_t disk_count = 0;
                    for (const auto& dir_ent : std::filesystem::directory_iterator(w.current_content_folder, ec))
                    {
                        if (disk_count >= 24U) break;
                        const auto p = dir_ent.path();
                        const std::string fname = p.filename().string();
                        const std::string ext   = p.extension().string();
                        const std::string full  = p.generic_string();
                        if (dir_ent.is_directory(ec))
                        {
                            entries.push_back({ "[DIR] " + fname, full, 0, "[DIR]" });
                            ++disk_count;
                        }
                        else if (ext == ".gltf" || ext == ".glb" || ext == ".obj")
                        {
                            entries.push_back({ fname, full, 1, "[GLTF]" });
                            ++disk_count;
                        }
                        else if (ext == ".jpg" || ext == ".png" || ext == ".ktx2" || ext == ".hdr")
                        {
                            entries.push_back({ fname, full, 2, "[DISK-TEX]" });
                            ++disk_count;
                        }
                        else if (ext == ".glsl" || ext == ".cdproj" || ext == ".md" || ext == ".json")
                        {
                            entries.push_back({ fname, full, 4, "[FILE]" });
                            ++disk_count;
                        }
                    }
                }

                if (ImGui::BeginTable("asset_cards_grid_4col", 4, ImGuiTableFlags_SizingStretchSame))
                {
                    int card_id = 0;
                    for (const auto& item : entries)
                    {
                        if (w.content_type_filter != 0 && item.kind != 0 && item.kind != w.content_type_filter)
                            continue;
                        if (w.content_search_filter[0] != '\0' &&
                            item.name.find(w.content_search_filter) == std::string::npos)
                            continue;

                        ImGui::TableNextColumn();
                        ImGui::PushID(card_id++);
                        ImVec4 btn_col = ImVec4(0.18F, 0.24F, 0.34F, 1.0F);
                        if (item.kind == 0)      btn_col = ImVec4(0.32F, 0.28F, 0.16F, 1.0F); // Directory
                        else if (item.kind == 1) btn_col = ImVec4(0.16F, 0.34F, 0.50F, 1.0F); // Mesh
                        else if (item.kind == 2) btn_col = ImVec4(0.16F, 0.40F, 0.28F, 1.0F); // Texture
                        else if (item.kind == 3) btn_col = ImVec4(0.38F, 0.24F, 0.46F, 1.0F); // Material

                        ImGui::PushStyleColor(ImGuiCol_Button, btn_col);
                        const std::string card_lbl = item.badge + " " + item.name;
                        if (ImGui::Button(card_lbl.c_str(), ImVec2(-1.0F, 20.5F)))
                        {
                            w.selected_content_asset = item.path;
                            if (item.kind == 0)
                            {
                                w.current_content_folder = item.path;
                            }
                            else if (item.kind == 1)
                            {
                                if (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                    (void)assign_mesh_asset_to_entity(s, s.selected, item.path, log_push);
                                else
                                {
                                    const cd::math::Vec3f pt = camera_spawn_point(s, 3.5F);
                                    const int nidx = spawn_entity_with_modifiers(s, "Dynamic_Cube", { pt.x, 0.0F, pt.z }, log_push);
                                    (void)assign_mesh_asset_to_entity(s, nidx, item.path, log_push);
                                }
                            }
                            else if (item.kind == 2 || item.kind == 3)
                            {
                                const int tidx = (s.selected >= 0 && std::cmp_less(s.selected, s.entities.size()))
                                    ? s.selected
                                    : (!s.entities.empty() ? static_cast<int>(s.entities.size() - 1U) : -1);
                                if (tidx >= 0)
                                    (void)assign_texture_asset_to_entity(s, tidx, item.path, log_push);
                            }
                        }
                        ImGui::PopStyleColor();

                        // Drag-and-Drop Source for Mesh & Texture cards
                        if (item.kind == 1 && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("CD_ASSET_MESH", item.path.c_str(), item.path.size() + 1);
                            w.is_dragging_asset_mesh = true;
                            w.drag_asset_mesh_id     = item.path;
                            ImGui::Text("Drag Mesh '%s' -> 3D Object / Inspector", item.name.c_str());
                            ImGui::EndDragDropSource();
                        }
                        else if ((item.kind == 2 || item.kind == 3) &&
                                 ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("CD_ASSET_TEXTURE", item.path.c_str(), item.path.size() + 1);
                            w.is_dragging_asset_texture = true;
                            w.drag_asset_texture_id     = item.path;
                            ImGui::Text("Drag Texture '%s' -> 3D Object / Inspector", item.name.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Asset Path: %s\nClick to Apply to Selected Entity, or Drag onto any 3D Object in Viewport / Scene / Inspector!",
                                              item.path.c_str());
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Console & Physics"))
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

                if (ImGui::SmallButton("Clean Ultra-FPS"))
                {
                    w.initialized = false;
                    init_clean_studio_workspace(s, log_push);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Ramp & Tower"))
                    load_studio_interactive_template(s, 1, log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Domino Chain"))
                    load_studio_interactive_template(s, 2, log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Shoot Ball (B)"))
                    shoot_physics_ball_from_camera(s, log_push);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Pos (R)"))
                    reset_physics_to_home_positions(s, log_push);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.42F, 0.96F, 0.64F, 1.0F),
                                   "| %.0f FPS (%.2f ms) | Phys: %s | RBs: %zu | Cols: %zu | Contacts: %zu",
                                   static_cast<double>(w.smoothed_fps),
                                   static_cast<double>(w.smoothed_frame_ms),
                                   (s.pie_playing || w.simulate_physics_in_edit) ? "RUNNING" : "PAUSED",
                                   dyn_rb_count, col_count, w.live_contacts.size());

                ImGui::Separator();
                ImGui::BeginChild("studio_log_scroll", ImVec2(0, 0), false);
                for (const auto& line : s.log)
                {
                    ImGui::TextUnformatted(line.c_str());
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Showcase & Project Hub (12)"))
            {
                ImGui::TextColored(ImVec4(0.55F, 0.88F, 1.0F, 1.0F),
                                   "Active Project: %s  |  World: %s  |  Level: %s",
                                   w.active_project_name.c_str(),
                                   w.active_world_name.c_str(),
                                   w.active_level_name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset Master 5-World Project"))
                {
                    s.cd_world.set_project(nullptr);
                    populate_master_showcase_project(s.cd_world);
                    if (const auto* np = s.cd_world.project())
                    {
                        w.active_project_name = std::string(np->name());
                        if (const auto* lvl0 = np->level(0))
                            (void)switch_to_project_level(s, lvl0->name(), log_push);
                    }
                }
                ImGui::Separator();

                if (ImGui::BeginTable("showcase_hub_grid", 4, ImGuiTableFlags_SizingStretchSame))
                {
                    for (std::size_t idx = 0; idx < kShowcaseCatalog.size(); ++idx)
                    {
                        const auto& sc = kShowcaseCatalog[idx];
                        ImGui::TableNextColumn();
                        ImGui::PushID(static_cast<int>(idx));
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18F, 0.30F, 0.44F, 1.0F));
                        if (ImGui::Button(sc.title, ImVec2(-1.0F, 26.0F)))
                        {
                            load_showcase_project_by_index(s, idx, log_push);
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Project: %s\nWorld: %s\nFile: %s",
                                              sc.id, sc.world_group, sc.cdproj_path);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Frame & GPU Profiler"))
            {
                const bool bloom_active    = (s.fx.bloom_post > 0.001F);
                const bool velocity_active = (s.fx.taa_amount > 0.001F || s.fx.motion_blur > 0.001F);
                ImGui::TextColored(ImVec4(0.40F, 0.98F, 0.62F, 1.0F),
                                   "Smoothed: %.0f FPS (%.2f ms)   |   Peak: %.0f FPS   |   Entities: %zu   |   Lights: %zu",
                                   static_cast<double>(w.smoothed_fps),
                                   static_cast<double>(w.smoothed_frame_ms),
                                   static_cast<double>(w.peak_fps),
                                   s.entities.size(),
                                   s.lights.size());
                ImGui::Text("Render Pass Bypass State:  ShadowMap [Cached Static]  |  TLAS [Hash-Gated]  |  Velocity GBuffer [%s]  |  7-Pass Bloom [%s]",
                            velocity_active ? "ACTIVE" : "BYPASSED (0 ms)",
                            bloom_active    ? "ACTIVE" : "BYPASSED (0 ms)");
                ImGui::PlotLines("Frame Time (ms)",
                                 w.frame_ms_history.data(),
                                 static_cast<int>(w.frame_ms_history.size()),
                                 static_cast<int>(w.frame_ms_cursor % w.frame_ms_history.size()),
                                 nullptr,
                                 0.0F, 16.6F,
                                 ImVec2(0.0F, 68.0F));
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Shader Node Graph"))
            {
                ImGui::BeginNodeGraph("BottomShaderGraph", ImVec2(0.0F, 150.0F));
                ImGui::BeginNode(10U, "PBR Texture Sampler", ImVec2(24.0F, 22.0F),
                                 ImVec2(190.0F, 72.0F), IM_COL32(45, 115, 185, 255));
                ImGui::NodePin(1001U, "Albedo RGB", true, IM_COL32(255, 180, 80, 255));
                ImGui::EndNode();

                ImGui::BeginNode(11U, "GGX Direct + IBL Eval", ImVec2(275.0F, 18.0F),
                                 ImVec2(200.0F, 92.0F), IM_COL32(35, 145, 95, 255));
                ImGui::NodePin(1002U, "BaseColor", false, IM_COL32(255, 180, 80, 255));
                ImGui::NodePin(1003U, "HDR Out", true, IM_COL32(95, 195, 255, 255));
                ImGui::EndNode();

                ImGui::BeginNode(12U, "Surface Output", ImVec2(540.0F, 26.0F),
                                 ImVec2(180.0F, 72.0F), IM_COL32(185, 85, 45, 255));
                ImGui::NodePin(1004U, "Final Color", false, IM_COL32(95, 195, 255, 255));
                ImGui::EndNode();

                ImGui::NodeLink(1001U, 1002U, IM_COL32(255, 180, 80, 235), 2.5F);
                ImGui::NodeLink(1003U, 1004U, IM_COL32(95, 195, 255, 245), 2.5F);
                ImGui::EndNodeGraph();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Timeline & Curves"))
            {
                static float s_dock_time = 1.65F;
                static float s_dock_curve[8] = {
                    0.0F, 0.0F, 0.25F, 0.80F, 0.75F, 0.20F, 1.0F, 1.0F
                };
                const float kfs[4] = { 0.4F, 1.65F, 3.1F, 4.5F };
                ImGui::TimelineScrubber("Camera Sequencer", &s_dock_time, 5.0F, kfs, 4);
                ImGui::CurveEditor("Easing", s_dock_curve, ImVec2(0.0F, 95.0F));
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace cd::studio
