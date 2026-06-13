// =============================================================================
// HelloGameplayDemo.hpp
// -----------------------------------------------------------------------------
// phase1173 — samples-consolidation: hello_world (samples/game/hello_world)
// FOLDED into hello_engine as a CPU-only "Gameplay" R-Showcase probe.
//
// The retired hello_world sample drove four gameplay libraries
// (cd::game::{query,trigger,camera,particles_event}) behind ~600 lines of
// platform-window / Vulkan-swapchain / Renderer boilerplate that hello_engine
// already provides. Per the standing direction
// [feedback_sample_consolidation_and_per_feature_demo] new feature surfaces
// live as a simple panel/log demo inside hello_engine rather than a separate
// binary, so the simulation is reproduced here as a single CPU probe in the
// established Run25 "live CPU demo" shape (function-local statics + a
// CollapsingHeader, drawn only from the golden-gated panel block).
//
// What it exercises (same fidelity as Phase 504 / hello_world's sim):
//   1. cd::game::query             - QueryWorld rebuilt every frame from the
//                                    player AABB; sphere_query against the
//                                    checkpoint volume centre.
//   2. cd::game::trigger           - TriggerVolume sphere @ (5, 0.5, 5) r=1.0;
//                                    on_enter / on_stay / on_exit fire-counts.
//   3. cd::game::camera            - CameraBrain + VirtualCamera follow_cam
//                                    with critically-damped smoothing; the
//                                    brain output's eye/target are displayed.
//   4. cd::game::particles_event   - "jump_dust" recipe fired every 2 s; the
//                                    on_emit callback records the last burst.
//
// GOLDEN-NEUTRAL: this probe touches ONLY ImGui + the four gameplay libraries.
// It NEVER records a draw call or mutates the rendered scene, and it is only
// reached through draw_r_showcase_panel -> draw_gameplay_demo_panel, which the
// `if (!kHideEditorUiForGolden)` gate in main.cpp skips entirely during
// --golden-fixture capture. The chrome-probe golden therefore stays
// byte-identical.
//
// Time base: ImGui::GetIO().DeltaTime (clamped) drives the player walk and the
// 2 s particle cadence, so no cd::gameplay_time dependency is pulled in — the
// four gameplay libs are the only new edges hello_engine's CMakeLists gains.
// =============================================================================
#pragma once

#include <cd/ecs/Entity.hpp>
#include <cd/game/camera/VirtualCamera.hpp>
#include <cd/game/particles_event/ParticlesEvent.hpp>
#include <cd/game/query/Query.hpp>
#include <cd/game/trigger/Trigger.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/physics/Sphere.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

namespace cd_sample {

// -----------------------------------------------------------------------------
// draw_gameplay_demo_panel — single CollapsingHeader hosting the folded
// hello_world gameplay simulation. State lives in function-local statics so the
// single call site in draw_r_showcase_panel preserves behaviour across frames.
// -----------------------------------------------------------------------------
inline void draw_gameplay_demo_panel(const std::function<void(std::string)>& log_push)
{
    namespace gc = cd::game::camera;
    namespace gp = cd::game::particles_event;
    namespace gq = cd::game::query;
    namespace gt = cd::game::trigger;

    using cd::ecs::Entity;
    using cd::math::Quatf;
    using cd::math::Vec3f;
    using cd::physics::Aabb;
    using cd::physics::Sphere;

    if (!ImGui::CollapsingHeader("Run25  Gameplay Stack Demo (folded hello_world)"))
        return;

    // ---- One-time gameplay world construction -----------------------------
    // EntityManager is held by a static unique-ptr so the gameplay objects
    // that reference its entity ids share one consistent lifetime.
    constexpr Vec3f kTriggerCentre { 5.0F, 0.5F, 5.0F };
    constexpr float kTriggerRadius { 1.0F };
    constexpr float kPlayerRadius  { 0.5F };
    constexpr float kFireIntervalS { 2.0F };

    static cd::ecs::EntityManager s_entities {};
    static Entity s_player        = s_entities.create();
    static Entity s_checkpoint    = s_entities.create();

    static Vec3f s_player_pos { 0.0F, 0.5F, 0.0F };
    static float s_sim_time   { 0.0F };
    static bool  s_running    { true };

    static gq::QueryWorld s_query { 4.0F };

    static int  s_enter_count { 0 };
    static int  s_stay_count  { 0 };
    static int  s_exit_count  { 0 };
    static bool s_inside      { false };

    static int   s_burst_count    { 0 };
    static Vec3f s_last_burst_pos  { 0.0F, 0.0F, 0.0F };
    static float s_last_burst_time { -1.0F };
    static std::uint32_t s_last_alive { 0 };

    // The trigger + particle dispatchers own std::function callbacks that
    // capture the statics above. They are constructed exactly once via an
    // immediately-invoked lambda guarded by a `bool` so the callbacks bind to
    // stable addresses (the statics never move).
    static gt::TriggerWorld s_triggers {};
    static gp::ParticleEventDispatcher s_particles {};
    static gc::CameraBrain s_brain {};
    static float s_next_fire_s { 0.0F };

    static const bool s_init = [&]() -> bool {
        // Trigger volume.
        gt::TriggerVolume vol;
        vol.name  = "checkpoint";
        vol.shape = Sphere { kTriggerCentre, kTriggerRadius };
        vol.on_enter = [](Entity /*subject*/) { ++s_enter_count; s_inside = true; };
        vol.on_stay  = [](Entity /*subject*/) { ++s_stay_count;  s_inside = true; };
        vol.on_exit  = [](Entity /*subject*/) { ++s_exit_count;  s_inside = false; };
        s_triggers.add_trigger(s_checkpoint, std::move(vol));

        // Particle recipe + on_emit callback.
        gp::ParticleRecipe r;
        r.name                     = "jump_dust";
        r.count                    = 32U;
        r.lifetime_s               = 0.75F;
        r.gravity                  = Vec3f { 0.0F, -9.81F, 0.0F };
        r.velocity_min             = Vec3f { -1.5F, 0.5F, -1.5F };
        r.velocity_max             = Vec3f {  1.5F, 2.5F,  1.5F };
        r.emitter_shape            = gp::EmitterShape::kSphere;
        r.emitter_radius_or_extent = Vec3f { 0.4F, 0.0F, 0.0F };
        s_particles.register_recipe("jump_dust", std::move(r));
        s_particles.add_on_emit([](const gp::ActiveBurst& b) {
            ++s_burst_count;
            s_last_burst_pos  = b.origin;
            s_last_burst_time = s_sim_time;
            s_last_alive      = b.alive_count;
        });

        // Follow virtual camera.
        gc::VirtualCamera follow_cam;
        follow_cam.set_target(s_player);
        follow_cam.settings().fov_y           = 1.0F;
        follow_cam.settings().near_z          = 0.1F;
        follow_cam.settings().far_z           = 100.0F;
        follow_cam.settings().position_offset = Vec3f { 0.0F, 2.5F, -4.0F };
        follow_cam.settings().damping         = Vec3f { 0.3F, 0.3F, 0.3F };
        (void)s_brain.add_vcam(std::move(follow_cam), /*priority*/ 10);
        return true;
    }();
    (void)s_init;

    // ---- Per-frame simulation (CPU-only) ----------------------------------
    // ImGui delta time drives the walk + cadence; clamp to avoid a giant step
    // after a stall (mirrors hello_world's kMaxDt = 1/10 clamp).
    ImGui::Checkbox("Run simulation", &s_running);
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        s_player_pos      = Vec3f { 0.0F, 0.5F, 0.0F };
        s_sim_time        = 0.0F;
        s_next_fire_s     = 0.0F;
        s_enter_count     = 0;
        s_stay_count      = 0;
        s_exit_count      = 0;
        s_inside          = false;
        s_burst_count     = 0;
        s_last_burst_time = -1.0F;
        s_last_alive      = 0;
        s_brain.warp();
    }

    if (s_running)
    {
        const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1F);
        s_sim_time += dt;

        // 1. Integrate player (constant +X/+Z diagonal walk, y fixed at 0.5).
        s_player_pos.x += 1.0F * dt;
        s_player_pos.z += 1.0F * dt;

        // 2. Rebuild query world from the player AABB.
        const Aabb player_box {
            Vec3f { s_player_pos.x - kPlayerRadius,
                    s_player_pos.y - kPlayerRadius,
                    s_player_pos.z - kPlayerRadius },
            Vec3f { s_player_pos.x + kPlayerRadius,
                    s_player_pos.y + kPlayerRadius,
                    s_player_pos.z + kPlayerRadius },
        };
        s_query.clear();
        s_query.add_entity(s_player, s_player_pos, player_box);
        s_query.rebuild();

        // 3. Tick triggers (enter/stay/exit callbacks mutate the statics).
        const bool was_inside = s_inside;
        const std::vector<gt::Subject> subjects {
            gt::Subject { s_player, s_player_pos, /*layer*/ 0U },
        };
        s_triggers.tick(&s_query, dt, subjects);
        if (s_inside != was_inside)
        {
            log_push(s_inside ? "[gameplay] entered checkpoint trigger"
                              : "[gameplay] exited checkpoint trigger");
        }

        // 4. Tick the camera brain (output cached for the panel readout).
        const cd::camera::Camera cam = s_brain.tick(
            dt, [&](Entity e) -> Vec3f {
                return (e == s_player) ? s_player_pos : Vec3f { 0.0F, 0.0F, 0.0F };
            });
        // Stash the camera output in statics for display below.
        static Vec3f s_cam_eye {};
        static Vec3f s_cam_target {};
        s_cam_eye    = cam.eye;
        s_cam_target = cam.target;

        // 5. Fire "jump_dust" every 2 s, then tick the dispatcher.
        if (s_sim_time >= s_next_fire_s)
        {
            const int prev = s_burst_count;
            const std::uint32_t idx =
                s_particles.fire("jump_dust", s_player_pos, Quatf::identity());
            if (idx != gp::ParticleEventDispatcher::kInvalidBurst &&
                s_burst_count > prev)
            {
                log_push("[gameplay] jump_dust burst #" +
                         std::to_string(s_burst_count));
            }
            s_next_fire_s += kFireIntervalS;
        }
        s_particles.tick(dt);

        // ---- Panel readout ------------------------------------------------
        ImGui::Separator();
        ImGui::Text("Sim time     : %.2f s", static_cast<double>(s_sim_time));
        ImGui::Text("Player pos   : (%.2f, %.2f, %.2f)",
                    static_cast<double>(s_player_pos.x),
                    static_cast<double>(s_player_pos.y),
                    static_cast<double>(s_player_pos.z));
        ImGui::Text("Query records: %zu  (sphere hits @ checkpoint: %zu)",
                    s_query.entity_count(),
                    s_query.sphere_query(kTriggerCentre,
                                         kTriggerRadius + kPlayerRadius).size());
        ImGui::Separator();
        ImGui::Text("Trigger inside: %s", s_inside ? "YES" : "no");
        ImGui::Text("  enter=%d  stay=%d  exit=%d",
                    s_enter_count, s_stay_count, s_exit_count);
        ImGui::Separator();
        ImGui::Text("VCam eye     : (%.2f, %.2f, %.2f)",
                    static_cast<double>(s_cam_eye.x),
                    static_cast<double>(s_cam_eye.y),
                    static_cast<double>(s_cam_eye.z));
        ImGui::Text("VCam target  : (%.2f, %.2f, %.2f)",
                    static_cast<double>(s_cam_target.x),
                    static_cast<double>(s_cam_target.y),
                    static_cast<double>(s_cam_target.z));
        ImGui::Text("VCams        : %zu  blend %.0f%%",
                    s_brain.vcam_count(),
                    static_cast<double>(s_brain.blend_progress() * 100.0F));
        ImGui::Separator();
        ImGui::Text("Particle bursts: %d  (last alive=%u)",
                    s_burst_count, s_last_alive);
        if (s_last_burst_time >= 0.0F)
        {
            ImGui::Text("  last @ t=%.2f s  pos (%.2f, %.2f, %.2f)",
                        static_cast<double>(s_last_burst_time),
                        static_cast<double>(s_last_burst_pos.x),
                        static_cast<double>(s_last_burst_pos.y),
                        static_cast<double>(s_last_burst_pos.z));
        }
        ImGui::Text("Active bursts : %zu", s_particles.active_count());
    }
    else
    {
        ImGui::TextDisabled("Simulation paused — tick the checkbox to resume.");
    }

    ImGui::TextDisabled(
        "CPU-only fold of samples/game/hello_world; golden-neutral.");
}

}  // namespace cd_sample
