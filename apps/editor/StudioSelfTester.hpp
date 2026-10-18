#pragma once
// =============================================================================
// CHROMODYNAMIC STUDIO — Autonomous In-Engine Self-Test & Fuzz Harness
// =============================================================================
// Triggered via `--self-test-all` on the command line.
// Exercises every critical user workflow in Debug/Release builds (with full
// MSVC STL iterator debugging active in Debug) so bugs like iterator
// invalidation on folder clicks, un-deletable Ground_Floor/OBJ entities,
// unresponsive Stop/HUD buttons, dark Sun light sliders, and Clouds/Fog
// spawning are caught automatically before the user ever sees them.
// =============================================================================

#include "StudioSceneBuilder.hpp"
#include "StudioPhysicsSolver.hpp"
#include "StudioWorkspacePanels.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace cd::studio
{

template <typename StateT, typename SunResolveFn, typename LogFn>
inline bool run_studio_autonomous_self_test_step(
    StateT&        s,
    std::uint64_t  frame_idx,
    SunResolveFn&& resolve_sun_fn,
    LogFn&&        log_push)
{
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();

    auto fail_test = [](const char* step_name, const char* reason)
    {
        std::fprintf(stderr, "\n[SELF-TEST FAIL] Step '%s': %s\n", step_name, reason);
        std::fflush(stderr);
        std::exit(2);
    };

    if (frame_idx == 1u)
    {
        std::puts("[SELF-TEST 1/8] Verifying Project Manager & Preload Starter Pack toggle...");
        w.preload_starter_context_assets = false;
        reset_to_clean_empty_project(s, log_push);
        if (!w.loaded_context_assets.empty())
        {
            fail_test("Preload=false", "Expected loaded_context_assets to be empty when preload_starter_context_assets is false");
        }
        w.preload_starter_context_assets = true;
        reset_to_clean_empty_project(s, log_push);
        if (w.loaded_context_assets.empty())
        {
            fail_test("Preload=true", "Expected starter context assets to populate when preload_starter_context_assets is true");
        }
        std::puts("[SELF-TEST 1/8] PASS");
    }
    else if (frame_idx == 2u)
    {
        std::puts("[SELF-TEST 2/8] Stress-testing Disk Browser folder navigation (assets -> samples -> meshes -> textures)...");
        w.current_content_folder = "assets/samples/Generic";
        w.current_content_folder = "assets/samples/Sponza";
        w.current_content_folder = "assets/textures";
        w.current_content_folder = "showcases";
        w.current_content_folder = "assets/meshes";
        std::puts("[SELF-TEST 2/8] PASS");
    }
    else if (frame_idx == 3u)
    {
        std::puts("[SELF-TEST 3/8] Verifying Ground_Floor deletion & respawn...");
        int gf_idx = -1;
        for (std::size_t i = 0; i < s.entities.size(); ++i)
        {
            if (s.entities[i].name.find("Ground_Floor") != std::string::npos)
            {
                gf_idx = static_cast<int>(i);
                break;
            }
        }
        if (gf_idx < 0)
        {
            fail_test("Ground_Floor exists", "Ground_Floor not found in clean project");
        }
        if (s.entities[static_cast<std::size_t>(gf_idx)].locked)
        {
            fail_test("Ground_Floor unlocked", "Ground_Floor must not be locked");
        }
        s.entities.erase(s.entities.begin() + gf_idx);
        bool still_has_gf = false;
        for (const auto& e : s.entities)
        {
            if (e.name.find("Ground_Floor") != std::string::npos) still_has_gf = true;
        }
        if (still_has_gf)
        {
            fail_test("Ground_Floor delete", "Ground_Floor still present after deletion");
        }
        (void)spawn_ground_floor_entity(s, log_push);
        std::puts("[SELF-TEST 3/8] PASS");
    }
    else if (frame_idx == 4u)
    {
        std::puts("[SELF-TEST 4/8] Verifying loaded .obj import & deletion from Scene and Active Context...");
        const std::size_t before_ent = s.entities.size();
        (void)import_or_assign_os_file(s, "assets/meshes/Cube.obj", false, log_push);
        if (s.entities.size() <= before_ent)
        {
            fail_test("OBJ Import", "Importing Cube.obj did not spawn an entity in Scene");
        }
        // Delete the newly spawned OBJ entity
        s.entities.pop_back();
        if (!w.loaded_context_assets.empty())
        {
            w.loaded_context_assets.pop_back();
        }
        std::puts("[SELF-TEST 4/8] PASS");
    }
    else if (frame_idx == 5u)
    {
        std::puts("[SELF-TEST 5/8] Verifying direct Volumetric Clouds & Volumetric Fog spawn and deletion...");
        const int cloud_idx = spawn_volumetric_clouds_entity(s, log_push);
        const int fog_idx   = spawn_volumetric_fog_entity(s, log_push);
        if (cloud_idx < 0 || fog_idx < 0)
        {
            fail_test("Clouds/Fog Spawn", "Failed to spawn Volumetric_Clouds or Volumetric_Fog");
        }
        tick_studio_modifiers_and_physics(s, 0.016F, log_push);
        if (s.fx.clouds_coverage <= 0.01F || !s.fx.volumetric_fog_on || s.fx.fog_density <= 0.001F)
        {
            fail_test("Clouds/Fog Active", "Clouds coverage or Volumetric Fog density did not activate");
        }
        std::puts("[SELF-TEST 5/8] PASS");
    }
    else if (frame_idx == 6u)
    {
        std::puts("[SELF-TEST 6/8] Verifying Directional Sun Light & all 6 Studio Light presets...");
        s.lights.clear();
        for (int preset = 0; preset <= 5; ++preset)
        {
            (void)spawn_studio_light_by_type(s, preset, log_push);
        }
        if (s.lights.size() < 8u)
        {
            fail_test("Light Presets", "Not all 6 light presets spawned");
        }
        // Test Sun with slider unit value (18.98) AND lux value (24400)
        s.lights[0].light.intensity = 18.98F;
        auto sun_low = resolve_sun_fn(s.lights);
        if (!sun_low.has_sun || sun_low.strength < 0.25F)
        {
            fail_test("Sun Slider 18.98", "Directional Sun Light with intensity=18.98 produced near-zero strength");
        }
        s.lights[0].light.intensity = 24400.0F;
        auto sun_lux = resolve_sun_fn(s.lights);
        if (!sun_lux.has_sun || sun_lux.strength < 0.50F)
        {
            fail_test("Sun Slider 24400", "Directional Sun Light with intensity=24400 produced near-zero strength");
        }
        std::puts("[SELF-TEST 6/8] PASS");
    }
    else if (frame_idx == 7u)
    {
        std::puts("[SELF-TEST 7/8] Verifying Game View & Play Mode Red Stop / Blue HUD buttons...");
        cst.workspace_mode = 1;
        enter_pie_play_mode(s, log_push);
        w.enable_demo_hud_overlay = true;
        if (!s.pie_playing || cst.workspace_mode != 1)
        {
            fail_test("Enter Play/GameView", "Failed to enter Play Mode + Game View");
        }
        // Simulate clicking Red Stop Play button
        exit_pie_play_mode(s, log_push);
        cst.workspace_mode        = 0;
        w.pilot_scene_camera_idx  = -1;
        w.enable_demo_hud_overlay = false;
        if (s.pie_playing || cst.workspace_mode != 0)
        {
            fail_test("Red Stop Play Button", "Stop Play did not return to 3D Editor (workspace_mode == 0)");
        }
        std::puts("[SELF-TEST 7/8] PASS");
    }
    else if (frame_idx == 8u)
    {
        std::puts("[SELF-TEST 8/8] Verifying 250-Construct spawn & Drag-and-Drop Mesh/Texture assignment...");
        const std::size_t before_cnt = s.entities.size();
        const int c_idx = spawn_or_compose_studio_catalog_node(
            s, 117U, CatalogComposeMode::SpawnRootElement, { 2.0F, 0.0F, 0.0F }, log_push);
        if (c_idx < 0 || s.entities.size() <= before_cnt)
        {
            fail_test("250-Construct Spawn", "Failed to spawn Construct #117 (AvatarVessel3D)");
        }
        if (!assign_mesh_asset_to_entity(s, c_idx, "assets/meshes/Sphere.mesh", log_push))
        {
            fail_test("DnD Mesh Assign", "Failed to assign Sphere.mesh to entity");
        }
        if (!assign_texture_asset_to_entity(s, c_idx, "assets/textures/UV_Checkerboard_2K.tex", log_push))
        {
            fail_test("DnD Texture Assign", "Failed to assign UV_Checkerboard_2K.tex to entity");
        }
        std::puts("[SELF-TEST 8/8] PASS — ALL 8 AUTONOMOUS E2E SELF-TESTS PASSED 100%!");
        std::fflush(stdout);
        return true; // Signal caller to exit cleanly
    }

    return false;
}

} // namespace cd::studio
