// =============================================================================
// CHROMODYNAMIC — StudioBootProject.hpp
// phase1146 (on_boot extraction, batch 7): remaining boot-tail blocks lifted
// out of StudioApp::on_boot() — world container + .cdproject adoption +
// CD_HDR_LANES env read, AsyncStreamer start + random-viz histogram first
// fill, and the edit/select/camera/audio/net/random/help command-palette
// bucket. Template over the app state (same pattern as StudioBootScene) so
// the main.cpp-private EngineState stays private.
//
// Behaviour is verbatim from main.cpp — comments preserved because they
// document product decisions (phase1112 .cdproject adoption, phase1113 fx
// mapping, phase1121 CD_HDR_LANES golden parity) that future edits must
// not lose.
//
// Scope rule (same as StudioEngineCommands.hpp): palette commands that close
// over the file-local SceneEntity / LightRow / PrimitiveKind types or
// kind_from_name (Scene: Save / Scene: Load) stay in main.cpp, together
// with the Gizmo/Streamer commands registered between them, so the
// duplicate-ID registration order (FX 80/81/90/91 first, Scene/Streamer
// second) is preserved exactly.
// =============================================================================
#pragma once

#include "StudioEngineCommands.hpp"
#include "StudioFxCommands.hpp"

#include <cd/core/Compat.hpp>  // compat::read_env_var (CD_HDR_LANES)
#include <cd/math/Random.hpp>
#include <cd/world_container/ProjectIo.hpp>
#include <cd/world_container/World.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace cd_studio
{

// ---- setup_world_container ------------------------------------------------
// Build the passive editor outliner backing: one Project, one Main
// Level with stadium-sized bounds, two layers ("Lights", "UI"). The
// resulting World is purely descriptive metadata for the Outliner
// panel; entities still live in the ECS Scene.
// phase1146: moved VERBATIM from the main.cpp anon namespace (Phase 295 /
// Marathon Run 7 sub-N1G) so setup_boot_world_and_project() below can call
// it; the legacy inline-main callsite keeps working via a using-decl in
// main.cpp.
inline void setup_world_container(cd::world_container::World& w)
{
    w.set_name("Default_World");
    auto proj = std::make_unique<cd::world_container::Project>("Empty_Project");
    auto* lvl = proj->add_level("Empty_Stage");
    lvl->set_world_name("Default_World");
    lvl->set_description("Clean empty stage — load projects/objects/textures from Disk into Context");
    lvl->set_template_id(0);
    lvl->bounds().min = { -40.0F, -2.0F, -40.0F };
    lvl->bounds().max = { 40.0F, 10.0F, 40.0F };
    lvl->add_layer("Default");
    lvl->add_layer("Geometry");
    lvl->add_layer("Physics");
    lvl->add_layer("Characters");
    lvl->add_layer("Lights");
    lvl->add_layer("UI");
    (void)proj->set_active_level("Empty_Stage");
    w.set_project(std::move(proj));
}

// ---- setup_boot_world_and_project ------------------------------------------
// World container + CD_HDR_LANES env read. Starts with a clean Empty_Project
// so sample projects are explicitly selected and loaded from disk directory.
template <typename StateT>
inline void setup_boot_world_and_project(StateT& s)
{
    setup_world_container(s.cd_world);
    if (cd::core::compat::read_env_var("CD_HDR_LANES") == "1")
        s.fx.hdr_parallel_lanes = true;
}

// ---- register_boot_palette_commands -----------------------------------------
// The fx/engineering palette buckets (StudioFxCommands + StudioEngineCommands)
// plus the edit/select/transform/camera/audio/net/random/help commands.
// log_push_fn is taken by value because every command lambda stores a copy
// (it closes over the heap-stable EngineState, so the copies stay valid).
template <typename StateT, typename LogFn>
inline void register_boot_palette_commands(StateT& s, LogFn log_push_fn)
{
    // Command palette
    cd_studio::register_fx_palette_commands(s.palette, s.fx, log_push_fn);
    cd_studio::register_engine_gi_rhi_fx_palette_commands(
        s.palette, s.fx, log_push_fn, s.fx_gtao, s.fx_bloom);

    (void)s.fx_ssr; (void)s.fx_dof; (void)s.fx_mblur; (void)s.fx_taa;
    (void)s.fx_smaa; (void)s.fx_restir_di_reservoir;
    (void)s.fx_restir_gi_reservoir; (void)s.fx_ddgi_grid;
    (void)s.fx_atmosphere; (void)s.fx_lshafts;
    (void)s.fx_clouds; (void)s.fx_vfog; (void)s.prev_vp_valid;

    s.palette.register_command(1,  "Edit: Undo",
        [&s, log_push_fn]() mutable { if (s.history.undo()) log_push_fn("[palette] Undo"); });
    s.palette.register_command(2,  "Edit: Redo",
        [&s, log_push_fn]() mutable { if (s.history.redo()) log_push_fn("[palette] Redo"); });
    s.palette.register_command(3,  "Edit: Clear History",
        [&s, log_push_fn]() mutable { s.history.clear(); log_push_fn("[palette] History cleared"); });

    auto sel_cmd = [&s, log_push_fn](std::string name, std::uint32_t id) mutable
    {
        s.palette.register_command(id, "Select: " + name,
            [&s, nm = name, log_push_fn]() mutable
            {
                for (std::size_t i = 0; i < s.entities.size(); ++i)
                    if (s.entities[i].name == nm)
                    {
                        s.selected = static_cast<int>(i);
                        log_push_fn("[palette] Select " + nm);
                        break;
                    }
            });
    };
    sel_cmd("Cube",     10);
    sel_cmd("Sphere",   11);
    sel_cmd("Cone",     12);
    sel_cmd("Cylinder", 13);
    sel_cmd("Torus",    14);

    s.palette.register_command(20, "Transform: Reset Selected",
        [&s, log_push_fn]() mutable
        {
            if (s.selected >= 0 && std::cmp_less(s.selected,s.entities.size()))
            {
                auto& ent = s.entities[static_cast<std::size_t>(s.selected)];
                if (auto* lt = s.scene.local(ent.handle); lt)
                {
                    lt->value.position = {}; lt->value.scale = { 1,1,1 };
                    lt->value.rotation = { 0,0,0,1 };
                    log_push_fn("[palette] Reset selected transform");
                }
            }
        });
    s.palette.register_command(30, "Camera: Toggle Auto-Spin",
        [&s, log_push_fn]() mutable
        {
            s.scene_cam.set_auto_spin(!s.scene_cam.auto_spin());
            if (s.scene_cam.auto_spin()) s.app_state.free_look.manual_mode = false;
            log_push_fn(std::string("[palette] Auto-spin: ") +
                (s.scene_cam.auto_spin() ? "ON" : "OFF"));
        });
    s.palette.register_command(31, "Camera: Follow Selected",
        [&s, log_push_fn]() mutable
        {
            if (s.selected >= 0 && std::cmp_less(s.selected,s.entities.size()))
            {
                s.scene_cam.attach(s.cam, s.scene,
                    s.entities[static_cast<std::size_t>(s.selected)].handle);
                log_push_fn("[palette] Camera following: " +
                    s.entities[static_cast<std::size_t>(s.selected)].name);
            }
        });
    s.palette.register_command(40, "Audio: Toggle Mute",
        [&s, log_push_fn]() mutable
        {
            s.audio_state.muted = !s.audio_state.muted;
            if (s.audio_state.live_ok && s.audio_state.backend &&
                s.audio_state.live_voice.is_valid())
                s.audio_state.backend->set_volume(s.audio_state.live_voice,
                    s.audio_state.muted ? 0.0F : 0.65F);
            log_push_fn(std::string("[palette] Audio: ") +
                (s.audio_state.muted ? "MUTED" : "LIVE"));
        });
    s.palette.register_command(70, "Help: Print Shortcuts",
        [log_push_fn]() mutable
        {
            log_push_fn("Ctrl+Shift+P / F1: command palette");
            log_push_fn("Esc: close palette / quit");
            log_push_fn("WASD: move camera | Q/E: down/up");
            log_push_fn("Right-mouse drag: FPS look | wheel: zoom");
            log_push_fn("Left-click entity: select | empty space: unselect");
            log_push_fn("F: focus | Space: cycle gizmo mode");
        });
}

}  // namespace cd_studio
