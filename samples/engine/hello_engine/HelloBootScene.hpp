// =============================================================================
// CHROMODYNAMIC — HelloBootScene.hpp
// phase1123 (on_boot extraction, batch 1): boot-time camera + lights +
// cluster-grid setup lifted out of HelloEngineApp::on_boot(). Template
// over the app state (same pattern as HelloViewportDemos) so the
// main.cpp-private EngineState stays private.
//
// Behaviour is verbatim from main.cpp — comments preserved because they
// document calibration decisions (phase455 Sponza light placement,
// phase797 fixture camera pinning) that future edits must not lose.
// =============================================================================
#pragma once

#include "HelloGoldenCli.hpp"
#include "SponzaFixtures.hpp"

#include <cd/light/Light.hpp>

#include <numbers>
#include <string>

namespace cd_sample
{

// Camera — positioned inside the Sponza nave looking toward the atrium.
// Sponza vertices are in cm; with 0.01 entity scale one engine unit = 1 m.
// phase832-default-view-show-pbr-grid:
// Original default camera (Run 12 / W12-B) — restored after a brief
// (1.5, 2.0, 6.0) misadventure that sat outside the Sponza atrium's
// Z=+3.5 outer wall. Camera at (0, 2.5, +8) looking at (0, 1.5, 0)
// works because the PBR grid (phase799 mid-nave at Z=0) is now
// unblocked by CesiumMan — phase832 moved the character from world
// origin to (-4, 0, -1.5) so the centre-line of the M2 default view
// shows the chrome PBR demo grid (X∈[-1.58,+1.58]) instead of the
// 4 m glowing humanoid figure.
template <typename StateT, typename LogFn>
inline void setup_boot_camera(StateT& s, LogFn&& log_push_fn)
{
    s.cam.eye    = { 0.0F, 2.5F, 8.0F };
    s.cam.target = { 0.0F, 1.5F, 0.0F };
    s.cam.fov_y  = 0.9F;
    s.cam.near_z = 0.05F;
    s.cam.far_z  = 500.0F;  // phase424-vis1: 300->500 m; matches inline-main Sponza override
    s.scene_cam.attach(s.cam, s.scene, {});
    s.scene_cam.set_auto_spin(false);
    s.scene_cam.orbit().auto_spin_rate = 0.25F;

    // T1.7 phase543: golden-fixture camera override. When --golden-fixture
    // is set on the CLI, replace the default eye/target/fov_y with the
    // Tier 1 fixture so the captured frame matches the regression slot.
    if (cd::hello_engine::golden::enabled())
    {
        const auto idx = static_cast<std::size_t>(
            cd::hello_engine::golden::options().fixture_index);
        const auto& fx = cd::hello_engine::sponza_fixtures::kFixtures[idx];
        s.cam.eye    = { fx.eye[0],    fx.eye[1],    fx.eye[2] };
        s.cam.target = { fx.target[0], fx.target[1], fx.target[2] };
        // fov_y stored in radians inside cd::camera::Camera.
        s.cam.fov_y  = fx.fov_y_deg * (std::numbers::pi_v<float> / 180.0F);
        s.scene_cam.set_auto_spin(false);
        // phase797-rt-chrome-sponza-probe: manual_mode MUST be true so
        // update_free_look_camera() takes the manual branch (does nothing
        // when no WASD/right-drag held) instead of the auto-orbit branch
        // (which calls scene_cam.update() and overwrites the camera with
        // an orbit-derived pose every frame, defeating the entire purpose
        // of a fixture pin). Previously this was set to `false`, which is
        // why fixtures 0..4 silently drifted to whatever the orbit
        // controller produced — the fixture eye/target lasted exactly one
        // frame.
        s.app_state.free_look.manual_mode = true;
        log_push_fn(std::string { "[golden] fixture #" }
                    + std::to_string(idx) + " (" + std::string { fx.slug }
                    + ") active -- camera pinned");
    }
}

// Lights (W8-BB: all enabled)
// phase455-sponza-fix: when Sponza is loaded, override the default
// light positions so they sit INSIDE the atrium volume. Diagnosis
// (three parallel agents) confirmed that the shader / TLAS / CSM
// paths are all correct for Sponza receivers — multi-light loop is
// reached, BLAS is in TLAS, depth buffer clears to 1.0. The user
// symptom "non-sun lights don't visibly affect Sponza" traces to a
// CALIBRATION problem: defaults were tuned for the open-air PBR
// sphere grid scene (lights at y=3..5m, range 15..20m, well-spaced
// from receivers). Sponza is a ~24m × 14m × 12m roofed indoor space
// at 0.01 scale; the default lights at (2, 3, -3), (0, 5, 0), and
// (0, 4.5, 2) sit OUTSIDE the atrium walls/ceiling. RT shadow rays
// from Sponza interior fragments shooting outward toward those
// positions ALWAYS hit a wall first -> vis=0 -> non-sun lights
// contribute zero. Phase 451 reduced the bias to 0.01 to fight the
// (different) "ray origin pushed through wall" failure mode; the
// bias drop was correct but did NOT address the through-wall
// light-position problem because no bias can rescue a ray that
// legitimately has to traverse solid geometry.
//
// Sponza preset: nave-centered lights at y≈2..4m (well above floor
// y≈0, below roof y≈12m), spread along the long axis so each side
// of the atrium receives visible contribution. Range reduced to
// match the smaller indoor scale (8m instead of 15..20m).
template <typename StateT>
inline void setup_boot_lights_and_clusters(StateT& s)
{
    const bool sponza_loaded =
        !s.meshes.gltf_loaded_name.empty() &&
        s.meshes.gltf_loaded_name.find("Sponza") != std::string::npos;
    s.lights.push_back({ "Sun (cool 6500K)",
        cd::light::directional({ -0.35F, -0.65F, -0.7F }, { 1, 1, 1 }, 100000.0F),
        true, 6500.0F });
    if (sponza_loaded)
    {
        // Tungsten lantern: warm point at the western end of the nave,
        // 2m above floor. From (-6, 2, 0) most interior surfaces have
        // line-of-sight without crossing a wall.
        s.lights.push_back({ "Tungsten lantern (2700K)",
            cd::light::point({ -6.0F, 2.0F, 0.0F }, { 1, 1, 1 }, 3000.0F, 8.0F),
            true, 2700.0F });
        // Spot pointing down the nave's central axis from above the
        // entrance; cone aimed at the eastern end so the floor catches
        // an obvious oblique pool of warm light.
        s.lights.push_back({ "Nave spot (3200K)",
            cd::light::spot({ -4.0F, 4.0F, 0.0F }, { 0.8F, -0.5F, 0.0F },
                            { 1, 1, 1 }, 6000.0F, 12.0F, 0.30F, 0.50F),
            true, 3200.0F });
        // Cyan rect-area on the south wall facing into the nave. The
        // rect normal +Z points into the atrium so the +N hemisphere
        // hits the columns and floor without crossing the south wall.
        s.lights.push_back({ "South wall cyan (8000K)",
            cd::light::rect_area({ 0.0F, 3.0F, -4.0F }, { 0, 0, 1 }, { 1, 0, 0 },
                                 2.0F, 1.0F, { 0.6F, 0.85F, 1.0F }, 2500.0F),
            true, 8000.0F });
        // Magenta neon strip on the north wall facing back; pairs with
        // the cyan to give the dual side-lit nave look.
        s.lights.push_back({ "North wall magenta (25000K)",
            cd::light::rect_area({ 0.0F, 3.0F, 4.0F }, { 0, 0, -1 }, { 1, 0, 0 },
                                 2.0F, 0.4F, { 1.0F, 0.18F, 0.85F }, 6000.0F),
            true, 25000.0F });
    }
    else
    {
        s.lights.push_back({ "Tungsten point (2700K)",
            cd::light::point({ 2.0F, 3.0F, -3.0F }, { 1, 1, 1 }, 3000.0F, 15.0F),
            true, 2700.0F });
        s.lights.push_back({ "Halogen spot (3200K)",
            cd::light::spot({ 0.0F, 5.0F, 0.0F }, { 0.0F, -0.316F, -0.949F },
                            { 1, 1, 1 }, 6000.0F, 20.0F, 0.35F, 0.55F),
            true, 3200.0F });
        s.lights.push_back({ "Cyan rect-area (8000K)",
            cd::light::rect_area({ 0.0F, 4.5F, 2.0F }, { 0, 0, -1 }, { 1, 0, 0 },
                                 3.0F, 1.0F, { 0.6F, 0.85F, 1.0F }, 2500.0F),
            true, 8000.0F });
        s.lights.push_back({ "Magenta HDR neon (25000K)",
            cd::light::rect_area({ 0.0F, 1.8F, -7.5F }, { 0, 0, 1 }, { 1, 0, 0 },
                                 4.0F, 0.4F, { 1.0F, 0.18F, 0.85F }, 6000.0F),
            true, 25000.0F });
    }

    s.cluster_desc.tiles_x  = 8; s.cluster_desc.tiles_y  = 4;
    s.cluster_desc.slices_z = 8; s.cluster_desc.near_z = 0.1F;
    s.cluster_desc.far_z    = 100.0F;
    s.cluster_grid.configure(s.cluster_desc);
}

}  // namespace cd_sample
