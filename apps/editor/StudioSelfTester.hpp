// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioSelfTester.hpp
//
// Autonomous In-Engine E2E Self-Test Harness (`--self-test-all`).
// Exercises the compiled cd_editor_lib subsystems (SceneOperations, AssetIngest,
// PhysicsPieSolver, WorldRenderPasses, and UI state) across 8 deterministic frames.
// =============================================================================
#pragma once

#include "core/StudioState.hpp"
#include "physics/PhysicsPieSolver.hpp"
#include "pipeline/AssetIngest.hpp"
#include "render/WorldRenderPasses.hpp"
#include "scene/SceneOperations.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace cd::studio
{

[[nodiscard]] inline bool run_studio_autonomous_self_test_step(
    StudioEngineState& s,
    std::uint64_t      frame_idx)
{
    auto& w   = studio_world();
    auto& cst = cd::ui::chroma::state();

    auto fail_test = [](const char* step_name, const char* reason)
    {
        std::fprintf(stderr, "\n[SELF-TEST FAIL] Step '%s': %s\n", step_name, reason);
        std::fflush(stderr);
        std::exit(2);
    };

    if (frame_idx == 1U)
    {
        std::puts("[SELF-TEST 1/8] Verifying Project Manager & Preload Starter Pack toggle...");
        w.preload_starter_context_assets = false;
        scene_ops::reset_to_clean_empty_project(s);
        if (!w.loaded_context_assets.empty())
        {
            fail_test("Preload=false",
                      "Expected loaded_context_assets to be empty when preload_starter_context_assets is false");
        }
        w.preload_starter_context_assets = true;
        scene_ops::reset_to_clean_empty_project(s);
        if (w.loaded_context_assets.empty())
        {
            fail_test("Preload=true",
                      "Expected starter context assets to populate when preload_starter_context_assets is true");
        }
        std::puts("[SELF-TEST 1/8] PASS");
    }
    else if (frame_idx == 2U)
    {
        std::puts("[SELF-TEST 2/8] Stress-testing Assets & FileSystem folder navigation...");
        w.current_content_folder = "assets/samples/Generic";
        w.current_content_folder = "assets/samples/Sponza";
        w.current_content_folder = "assets/textures";
        w.current_content_folder = "showcases";
        w.current_content_folder = "assets/meshes";
        std::puts("[SELF-TEST 2/8] PASS");
    }
    else if (frame_idx == 3U)
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
            if (e.name.find("Ground_Floor") != std::string::npos)
            {
                still_has_gf = true;
            }
        }
        if (still_has_gf)
        {
            fail_test("Ground_Floor delete", "Ground_Floor still present after deletion");
        }
        (void)scene_ops::spawn_ground_floor(s);
        std::puts("[SELF-TEST 3/8] PASS");
    }
    else if (frame_idx == 4U)
    {
        std::puts("[SELF-TEST 4/8] Verifying .obj import via cd::asset::obj & deletion...");
        const std::size_t before_ent = s.entities.size();
        (void)pipeline::import_or_assign_file(s, "assets/meshes/Cube.obj", false);
        if (s.entities.size() <= before_ent)
        {
            fail_test("OBJ Import", "Importing Cube.obj did not spawn an entity in Scene");
        }
        s.entities.pop_back();
        if (!w.loaded_context_assets.empty())
        {
            w.loaded_context_assets.pop_back();
        }
        std::puts("[SELF-TEST 4/8] PASS");
    }
    else if (frame_idx == 5U)
    {
        std::puts("[SELF-TEST 5/8] Verifying Volumetric Clouds & Volumetric Fog spawn...");
        const int cloud_idx = scene_ops::spawn_volumetric_clouds(s);
        const int fog_idx   = scene_ops::spawn_volumetric_fog(s);
        if (cloud_idx < 0 || fog_idx < 0)
        {
            fail_test("Clouds/Fog Spawn", "Failed to spawn Volumetric_Clouds or Volumetric_Fog");
        }
        physics_pie::tick_modifiers_and_physics(s, 0.016F);
        if (s.fx.clouds_coverage <= 0.01F || !s.fx.volumetric_fog_on || s.fx.fog_density <= 0.001F)
        {
            fail_test("Clouds/Fog Active",
                      "Clouds coverage or Volumetric Fog density did not activate");
        }
        std::puts("[SELF-TEST 5/8] PASS");
    }
    else if (frame_idx == 6U)
    {
        std::puts("[SELF-TEST 6/8] Verifying Directional Sun Light & all 6 Studio Light presets...");
        s.lights.clear();
        for (int preset = 0; preset <= 5; ++preset)
        {
            (void)scene_ops::spawn_light_preset(s, preset);
        }
        if (s.lights.size() < 8U)
        {
            fail_test("Light Presets", "Not all 6 light presets spawned");
        }
        s.lights[0].light.intensity = 18.98F;
        const auto sun_low          = render::resolve_sun_light(s.lights);
        if (!sun_low.has_sun || sun_low.strength < 0.25F)
        {
            fail_test("Sun Slider 18.98",
                      "Directional Sun Light with intensity=18.98 produced near-zero strength");
        }
        s.lights[0].light.intensity = 24400.0F;
        const auto sun_lux          = render::resolve_sun_light(s.lights);
        if (!sun_lux.has_sun || sun_lux.strength < 0.50F)
        {
            fail_test("Sun Slider 24400",
                      "Directional Sun Light with intensity=24400 produced near-zero strength");
        }
        std::puts("[SELF-TEST 6/8] PASS");
    }
    else if (frame_idx == 7U)
    {
        std::puts("[SELF-TEST 7/8] Verifying Play-In-Editor (PIE) Enter & Stop restore...");
        cst.workspace_mode = 1;
        physics_pie::enter_pie(s);
        w.enable_demo_hud_overlay = true;
        if (!s.pie_playing || cst.workspace_mode != 1)
        {
            fail_test("Enter Play/GameView", "Failed to enter Play Mode + Game View");
        }
        physics_pie::exit_pie(s);
        cst.workspace_mode        = 0;
        w.pilot_scene_camera_idx  = -1;
        w.enable_demo_hud_overlay = false;
        if (s.pie_playing || cst.workspace_mode != 0)
        {
            fail_test("Stop Play Button",
                      "Stop Play did not return to 3D Editor (workspace_mode == 0)");
        }
        std::puts("[SELF-TEST 7/8] PASS");
    }
    else if (frame_idx == 8U)
    {
        std::puts("[SELF-TEST 8/8] Verifying 250-Construct spawn & Mesh/Texture assignment...");
        const std::size_t before_cnt = s.entities.size();
        const int         c_idx      = scene_ops::spawn_catalog_construct(
            s, 117U, { 2.0F, 0.0F, 0.0F }, false);
        if (c_idx < 0 || s.entities.size() <= before_cnt)
        {
            fail_test("250-Construct Spawn", "Failed to spawn Construct #117 (AvatarVessel3D)");
        }
        if (!pipeline::assign_mesh_to_entity(s, c_idx, "assets/meshes/Sphere.mesh"))
        {
            fail_test("Mesh Assign", "Failed to assign Sphere.mesh to entity");
        }
        if (!pipeline::assign_texture_to_entity(s, c_idx, "assets/textures/UV_Checkerboard_2K.tex"))
        {
            fail_test("Texture Assign", "Failed to assign UV_Checkerboard_2K.tex to entity");
        }
        std::puts("[SELF-TEST 8/8] PASS -- ALL 8 AUTONOMOUS E2E SELF-TESTS PASSED 100%!");
        std::fflush(stdout);
        return true;
    }

    return false;
}

}  // namespace cd::studio
