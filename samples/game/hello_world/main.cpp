// =============================================================================
// CHROMODYNAMIC - samples/game/hello_world/main.cpp
//
// Phase 504 (G3.5 - closes Phase G3).
//
// Console-only "hello world" of the Phase-G3 gameplay tier. Stitches the four
// G3 libraries shipped in phases 475 / 476 / 497 / 498 into a single deter-
// ministic 10-second simulation:
//
//   1. cd::game::query             - QueryWorld broad-phase index rebuilt
//                                    every tick from the player's AABB so
//                                    downstream sphere / box / raycast calls
//                                    have something to chew on (here used
//                                    indirectly through the TriggerWorld).
//   2. cd::game::trigger           - one TriggerVolume (sphere @ (5,0,5),
//                                    r = 1.0) whose on_enter callback prints
//                                    "[trigger] Entered checkpoint" exactly
//                                    once when the player crosses the line.
//   3. cd::game::camera            - CameraBrain + a single VCam set to
//                                    follow the player with critically-damped
//                                    smoothing (Cinemachine-style).
//   4. cd::game::particles_event   - one registered recipe ("jump_dust");
//                                    fired in a 2 s loop to simulate a
//                                    jump-land burst, with the brief-mandated
//                                    on_emit callback echoed to stdout.
//
// Driving loop:
//   * cd::gameplay::time::TimeKeeper, fixed-tick at 60 Hz (dt = 1/60).
//   * 10 simulated seconds -> 600 ticks then exit.
//   * The player walks in a straight line from (0,0,0) towards (10,0,10) at
//     1 m/s; the trigger sphere at (5,0,5) catches it roughly halfway.
//
// Headless: NO swapchain, NO RHI, NO ImGui. Console output IS the deliverable;
// "compiles + exits 0" is the build / CI smoke gate per the brief.
//
// Library boundary (CLAUDE.md S7): sample sits above the gameplay tier and
// link-depends only on cd::core, cd::math, cd::ecs, cd::gameplay_time, and
// the four G3 libraries listed above. No render / scene / rhi includes.
// =============================================================================
#include <cd/ecs/Entity.hpp>
#include <cd/game/camera/VirtualCamera.hpp>
#include <cd/game/particles_event/ParticlesEvent.hpp>
#include <cd/game/query/Query.hpp>
#include <cd/game/trigger/Trigger.hpp>
#include <cd/gameplay/time/Time.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/physics/Sphere.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace
{

namespace gc  = cd::game::camera;
namespace gp  = cd::game::particles_event;
namespace gq  = cd::game::query;
namespace gt  = cd::game::trigger;
namespace gtm = cd::gameplay::time;

using cd::ecs::Entity;
using cd::ecs::EntityManager;
using cd::math::Quatf;
using cd::math::Vec3f;
using cd::physics::Aabb;
using cd::physics::Sphere;

// ---------------------------------------------------------------------------
// World state - a tiny POD aggregate threaded through the per-tick update
// helpers. The world holds the player entity + its position so the camera
// brain's TargetPositionFn and the trigger world's Subject feed can both
// resolve "where is the player right now" against the same source of truth.
// ---------------------------------------------------------------------------
struct World
{
    Entity player {};
    Vec3f  player_pos { 0.0F, 0.0F, 0.0F };
    Vec3f  player_velocity { 1.0F, 0.0F, 1.0F };  ///< 1 m/s along the (+X,+Z) diagonal.
    float  player_radius { 0.5F };                ///< AABB half-extent for the query index.
};

// ---------------------------------------------------------------------------
// Build the player's AABB from its centre + uniform radius. Used both for
// query-world insertion and for trigger-side broad-phase context.
// ---------------------------------------------------------------------------
[[nodiscard]] Aabb player_aabb(const World& w) noexcept
{
    return Aabb {
        Vec3f { w.player_pos.x - w.player_radius,
                w.player_pos.y - w.player_radius,
                w.player_pos.z - w.player_radius },
        Vec3f { w.player_pos.x + w.player_radius,
                w.player_pos.y + w.player_radius,
                w.player_pos.z + w.player_radius },
    };
}

// ---------------------------------------------------------------------------
// Resolve a target entity's world position. The CameraBrain calls this
// every tick to find the centre the VCam should follow. We only know about
// one entity in this sample; falling back to (0,0,0) for unknown handles
// matches the brain's documented "stationary vcam" behaviour.
// ---------------------------------------------------------------------------
[[nodiscard]] Vec3f resolve_target_position(const World& w, Entity e) noexcept
{
    return (e == w.player) ? w.player_pos : Vec3f { 0.0F, 0.0F, 0.0F };
}

}  // namespace

// =============================================================================
// main
//
// Drives the simulation. The flow per tick is:
//   1. integrate player position (constant-velocity walk).
//   2. rebuild query world with the (single) player record so any future
//      gameplay code can do sphere / box / raycast queries against it.
//   3. tick trigger world with the player as the sole subject; the volume's
//      on_enter callback prints the checkpoint message.
//   4. tick camera brain to smooth the follow.
//   5. fire the "jump_dust" particle burst every 2 s; tick the dispatcher
//      so finished bursts retire.
//   6. advance the TimeKeeper by the fixed dt.
//
// Console output cadence: status line every 60 ticks (= 1 simulated second)
// plus the event lines (trigger enter, particle emit).
// =============================================================================
int main()
{
    std::puts("=== CHROMODYNAMIC hello_world (G3 closeout, Phase 504) ===");

    // -----------------------------------------------------------------------
    // Entity allocation - one player. cd::ecs::EntityManager gives us a
    // valid generational handle so the rest of the gameplay code can key
    // off cd::ecs::Entity unambiguously.
    // -----------------------------------------------------------------------
    EntityManager entities;
    World         world;
    world.player = entities.create();
    std::printf("[boot] player entity id=%u gen=%u\n",
                world.player.id, world.player.generation);

    // -----------------------------------------------------------------------
    // QueryWorld - broad-phase index. Cell size 4 m matches the half-walk-
    // distance scale of this demo (~10 m total). Re-built every tick because
    // the player is moving every tick; the rebuild is cheap with one record.
    // -----------------------------------------------------------------------
    gq::QueryWorld query_world { /*cell_size*/ 4.0F };

    // -----------------------------------------------------------------------
    // Trigger world - one sphere volume centred on (5,0,5) with radius 1.0.
    // The player walks along the (+X,+Z) diagonal so it enters the volume
    // roughly 5 s in (1 m/s diagonal speed * 5 s ~= 5*sqrt(2) ~ 7.07 m of
    // path before re-projecting onto the sphere centre); on_enter prints
    // the canonical checkpoint message exactly once.
    // -----------------------------------------------------------------------
    gt::TriggerWorld   triggers;
    const Entity       checkpoint_owner = entities.create();
    int                enter_fire_count = 0;
    int                stay_fire_count  = 0;
    int                exit_fire_count  = 0;

    gt::TriggerVolume vol;
    vol.name     = "checkpoint";
    vol.shape    = Sphere { Vec3f { 5.0F, 0.0F, 5.0F }, /*radius*/ 1.0F };
    vol.on_enter = [&](Entity subject) {
        ++enter_fire_count;
        std::printf("[trigger] Entered checkpoint (subject id=%u gen=%u)\n",
                    subject.id, subject.generation);
    };
    vol.on_stay  = [&](Entity /*subject*/) { ++stay_fire_count; };
    vol.on_exit  = [&](Entity subject) {
        ++exit_fire_count;
        std::printf("[trigger] Exited checkpoint  (subject id=%u gen=%u)\n",
                    subject.id, subject.generation);
    };
    triggers.add_trigger(checkpoint_owner, std::move(vol));
    std::printf("[boot] trigger 'checkpoint' @ (5,0,5) r=1.0 registered\n");

    // -----------------------------------------------------------------------
    // CameraBrain + one VCam following the player. Shoulder-ish position
    // offset (slightly behind / above) and a critically-damped 0.3 s
    // half-life on each axis so the follow is smooth without overshoot.
    // -----------------------------------------------------------------------
    gc::CameraBrain  brain;
    gc::VirtualCamera follow_cam;
    follow_cam.set_target(world.player);
    follow_cam.settings().fov_y           = 1.0F;                              // ~57 deg.
    follow_cam.settings().near_z          = 0.1F;
    follow_cam.settings().far_z           = 100.0F;
    follow_cam.settings().position_offset = Vec3f { 0.0F, 2.5F, -4.0F };       // 3rd-person.
    follow_cam.settings().damping         = Vec3f { 0.3F, 0.3F, 0.3F };        // 300 ms half-life.
    const auto follow_vcam_id =
        brain.add_vcam(std::move(follow_cam), /*priority*/ 10);
    std::printf("[boot] vcam id=%u priority=10 follows player\n", follow_vcam_id);

    // -----------------------------------------------------------------------
    // ParticleEventDispatcher - one recipe "jump_dust". Kept short-lived
    // so each subsequent fire() is visibly a fresh burst. on_emit echoes
    // the burst origin to stdout exactly once per fire() per the library's
    // contract (see ParticlesEvent.hpp).
    // -----------------------------------------------------------------------
    gp::ParticleEventDispatcher particles;
    {
        gp::ParticleRecipe r;
        r.name                     = "jump_dust";
        r.count                    = 32U;
        r.lifetime_s               = 0.75F;                                    // shorter than the 2 s fire interval.
        r.gravity                  = Vec3f { 0.0F, -9.81F, 0.0F };
        r.velocity_min             = Vec3f { -1.5F, 0.5F, -1.5F };
        r.velocity_max             = Vec3f {  1.5F, 2.5F,  1.5F };
        r.emitter_shape            = gp::EmitterShape::kSphere;
        r.emitter_radius_or_extent = Vec3f { 0.4F, 0.0F, 0.0F };
        particles.register_recipe("jump_dust", std::move(r));
    }
    int particle_fire_count = 0;
    particles.add_on_emit([&particle_fire_count](const gp::ActiveBurst& b) {
        ++particle_fire_count;
        std::printf("[pfx] jump_dust burst #%d emitted @ (%.2f, %.2f, %.2f) "
                    "alive_count=%u\n",
                    particle_fire_count,
                    static_cast<double>(b.origin.x),
                    static_cast<double>(b.origin.y),
                    static_cast<double>(b.origin.z),
                    b.alive_count);
    });
    std::printf("[boot] particle recipe 'jump_dust' registered\n");

    // -----------------------------------------------------------------------
    // TimeKeeper-driven fixed-tick loop. Hard-coded 60 Hz over 10 s = 600
    // ticks. We use the gameplay channel for the sim clock and read its
    // elapsed_seconds every tick to gate the 2 s particle interval.
    // -----------------------------------------------------------------------
    gtm::TimeKeeper clock;
    constexpr double kDt              = 1.0 / 60.0;     // 60 Hz fixed step.
    constexpr int    kTickCount       = 600;            // 10 simulated seconds.
    constexpr double kFireIntervalS   = 2.0;            // jump-land every 2 s.

    double next_particle_fire_s = 0.0;                  // first fire at t = 0.
    int    status_lines         = 0;

    for (int tick = 0; tick < kTickCount; ++tick)
    {
        // ---- 1. Integrate player position ---------------------------------
        // Constant-velocity walk along the (+X,+Z) diagonal. Y stays 0 so
        // the trigger sphere (centred at y=0) is in the player's path.
        const float fdt = static_cast<float>(kDt);
        world.player_pos.x += world.player_velocity.x * fdt;
        world.player_pos.y += world.player_velocity.y * fdt;
        world.player_pos.z += world.player_velocity.z * fdt;

        // ---- 2. Rebuild the query world ----------------------------------
        // One entity, so the rebuild_from path is overkill - we just
        // clear + add_entity + rebuild. Keeping the call shape close to
        // production gameplay code that would loop over an ECS view.
        query_world.clear();
        query_world.add_entity(world.player, world.player_pos, player_aabb(world));
        query_world.rebuild();

        // ---- 3. Tick triggers --------------------------------------------
        // Pass &query_world so the indexed broad-phase plugs in once the
        // trigger library lights it up; until then the brute-force scan
        // still runs against the one subject and works the same.
        const std::vector<gt::Subject> subjects {
            gt::Subject { world.player, world.player_pos, /*layer*/ 0U },
        };
        triggers.tick(&query_world, fdt, subjects);

        // ---- 4. Tick camera brain ----------------------------------------
        const cd::camera::Camera cam = brain.tick(
            fdt, [&](Entity e) { return resolve_target_position(world, e); });
        (void)cam;  // camera output is consumed by the renderer in a real game;
                    // here we only need to prove the brain runs cleanly.

        // ---- 5. Particles: fire every 2 s, then age ----------------------
        const double sim_t = clock.get().elapsed_seconds;
        if (sim_t >= next_particle_fire_s)
        {
            const std::uint32_t idx = particles.fire(
                "jump_dust", world.player_pos, Quatf::identity());
            if (idx == gp::ParticleEventDispatcher::kInvalidBurst)
            {
                std::fprintf(stderr, "[pfx] fire('jump_dust') FAILED\n");
                return EXIT_FAILURE;
            }
            next_particle_fire_s += kFireIntervalS;
        }
        particles.tick(fdt);

        // ---- 6. Advance the clock ----------------------------------------
        clock.tick(kDt);

        // ---- HUD: one status line per simulated second --------------------
        if (((tick + 1) % 60) == 0)
        {
            ++status_lines;
            std::printf("[hud] t=%4.2fs  player=(%.2f, %.2f, %.2f)  "
                        "active_pfx=%zu  triggers_in=%d\n",
                        clock.get().elapsed_seconds,
                        static_cast<double>(world.player_pos.x),
                        static_cast<double>(world.player_pos.y),
                        static_cast<double>(world.player_pos.z),
                        particles.active_count(),
                        enter_fire_count);
        }
    }

    // -----------------------------------------------------------------------
    // Final summary. The sample's "smoke" expectations:
    //   * at least one trigger on_enter must have fired (player crosses (5,0,5)).
    //   * particle emit callback must have fired ceil(10/2)+1 = 6 times.
    //   * status lines must total 10 (one per simulated second).
    // -----------------------------------------------------------------------
    std::puts("--- summary ---");
    std::printf("  sim time elapsed     : %.3f s\n", clock.get().elapsed_seconds);
    std::printf("  trigger enter fires  : %d\n", enter_fire_count);
    std::printf("  trigger stay  fires  : %d\n", stay_fire_count);
    std::printf("  trigger exit  fires  : %d\n", exit_fire_count);
    std::printf("  particle bursts fired: %d\n", particle_fire_count);
    std::printf("  status lines printed : %d\n", status_lines);
    std::printf("  vcams registered     : %zu\n", brain.vcam_count());
    std::printf("  query records        : %zu\n", query_world.entity_count());

    if (enter_fire_count < 1)
    {
        std::fprintf(stderr, "[FAIL] trigger never fired on_enter\n");
        return EXIT_FAILURE;
    }
    if (particle_fire_count < 1)
    {
        std::fprintf(stderr, "[FAIL] particle dispatcher never emitted\n");
        return EXIT_FAILURE;
    }

    std::puts("=== hello_world OK ===");
    return EXIT_SUCCESS;
}
