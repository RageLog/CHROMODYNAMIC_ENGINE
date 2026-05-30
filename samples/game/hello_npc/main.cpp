// =============================================================================
// CHROMODYNAMIC - samples/game/hello_npc
// Phase 480 (G2.4 - closes Phase G2).
//
// Demonstrates the Wave-1 gameplay AI stack end-to-end:
//   * cd::game::fsm        - high-level NPC mode  { kPatrol, kChase, kAttack }
//   * cd::game::ai_bt      - per-mode tactical plan (walk-to-waypoint loop in
//                            Patrol, "walk-to-player" in Chase, swing-and-cool
//                            in Attack)
//   * "anim graph" stub    - the real cd::game::anim_graph (Phase C) is not yet
//                            available; we stand in with a printf log of the
//                            current locomotion clip and speed so the sample
//                            still tells a coherent story.
//
// The sample is a headless console program (no window / no Vulkan). Each
// frame represents a fixed 0.1 s dt. The "see player" event fires on a timer
// (default 2.5 s after start) so the run is deterministic and CI-friendly;
// passing any argv stretches the timer for live demo use.
//
// Dependencies (CLAUDE.md S7): cd::core, cd::game_fsm, cd::game_ai_bt.
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/game/ai_bt/BehaviorTree.hpp>
#include <cd/game/fsm/Fsm.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{

// -----------------------------------------------------------------------------
// World model - the absolute minimum we need to make the demo legible. Real
// gameplay code would route these through cd::scene / cd::ecs; here a couple
// of structs and a context aggregate keep the focus on the FSM + BT wiring.
// -----------------------------------------------------------------------------
struct Vec2
{
    float x {0.0F};
    float y {0.0F};
};

[[nodiscard]] float distance(Vec2 a, Vec2 b) noexcept
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Move `pos` toward `target` at `speed` units/s for the elapsed dt. Returns
// true once the NPC arrives (within `arrive_radius`).
[[nodiscard]] bool step_toward(Vec2& pos, Vec2 target, float speed, float dt, float arrive_radius)
{
    const float d = distance(pos, target);
    if (d <= arrive_radius)
    {
        pos = target;
        return true;
    }
    const float step = speed * dt;
    if (step >= d)
    {
        pos = target;
        return true;
    }
    const float nx = (target.x - pos.x) / d;
    const float ny = (target.y - pos.y) / d;
    pos.x += nx * step;
    pos.y += ny * step;
    return false;
}

// FSM mode tags.
enum class NpcMode : std::uint8_t
{
    kPatrol = 0,
    kChase  = 1,
    kAttack = 2,
};

[[nodiscard]] const char* mode_name(NpcMode m) noexcept
{
    switch (m)
    {
        case NpcMode::kPatrol: return "Patrol";
        case NpcMode::kChase:  return "Chase";
        case NpcMode::kAttack: return "Attack";
    }
    return "?";
}

// -----------------------------------------------------------------------------
// NpcCtx - shared context for both the FSM and the per-mode BT. Every leaf
// reads / mutates this; the Blackboard stores only loose scratch values
// (waypoint cursor, hit counter, etc.) so the structured world state lives
// in one place.
// -----------------------------------------------------------------------------
struct NpcCtx
{
    // World ------------------------------------------------------------------
    Vec2                       pos {0.0F, 0.0F};
    Vec2                       player_pos {10.0F, 10.0F};
    std::array<Vec2, 4>        waypoints {
        Vec2 {0.0F, 0.0F}, Vec2 {6.0F, 0.0F}, Vec2 {6.0F, 6.0F}, Vec2 {0.0F, 6.0F}};

    // Event flags (set externally; read by FSM predicates) -------------------
    bool                       see_player {false};
    bool                       in_attack_range {false};
    bool                       player_dead {false};

    // FSM -> BT bridge -------------------------------------------------------
    NpcMode                    mode {NpcMode::kPatrol};

    // Locomotion (anim_graph stand-in) --------------------------------------
    // The cd::game::anim_graph library lands in Phase C. Until then the FSM
    // drives an explicit "locomotion clip" tag + speed and we log it on every
    // change - that is the same signal a real anim graph would consume to
    // blend walk and run.
    std::string                anim_clip {"idle"};
    float                      anim_speed_mps {0.0F};

    // Bookkeeping -----------------------------------------------------------
    float                      sim_time {0.0F};
    float                      see_player_at {2.5F};   // event timer (seconds)
};

// Update the anim "clip" + speed and log it iff anything changed - keeps the
// demo's stdout signal-rich without spamming every frame.
void set_anim(NpcCtx& ctx, const char* clip, float speed)
{
    const bool clip_changed  = ctx.anim_clip != clip;
    const bool speed_changed = std::fabs(ctx.anim_speed_mps - speed) > 0.001F;
    if (clip_changed || speed_changed)
    {
        ctx.anim_clip      = clip;
        ctx.anim_speed_mps = speed;
        std::printf("[anim] %s at %.1f m/s\n", clip, static_cast<double>(speed));
    }
}

// -----------------------------------------------------------------------------
// Per-mode BTs.
//
// Each BT carries an out-of-band reference to the shared NpcCtx, captured
// by reference inside the leaf lambdas. The Blackboard holds only the
// mode-local scratch values that benefit from BT machinery (cursors,
// counters). The structured world state lives in NpcCtx; pointing the BB
// at a void* with a custom variant alternative would just reinvent that
// reference at runtime. This mirrors the Unreal "Behavior Tree +
// Blackboard" idiom where the structured AIController lives in C++ and
// the BB carries loose tags.
// -----------------------------------------------------------------------------
constexpr const char* kBbWpCursor   = "wp_cursor";
constexpr const char* kBbAttackHits = "attack_hits";

// --- Patrol BT --------------------------------------------------------------
// SequenceNode { walk to current waypoint -> advance cursor }
// wrapped in a RepeaterNode(0) so it loops forever.
[[nodiscard]] std::unique_ptr<cd::game::ai_bt::Node> make_patrol_tree(NpcCtx& ctx)
{
    using namespace cd::game::ai_bt;

    auto walk_to_waypoint = make_leaf([&ctx](Blackboard& bb) -> Status {
        const int       wp  = bb.get_int(kBbWpCursor, 0);
        const Vec2      tgt = ctx.waypoints[static_cast<std::size_t>(wp) % ctx.waypoints.size()];
        constexpr float kPatrolSpeed = 1.0F;
        const float     dt           = bb.get_float("dt", 0.0F);

        set_anim(ctx, "walking", kPatrolSpeed);
        const bool arrived = step_toward(ctx.pos, tgt, kPatrolSpeed, dt, 0.05F);
        return arrived ? Status::kSuccess : Status::kRunning;
    });

    auto advance_cursor = make_leaf([](Blackboard& bb) -> Status {
        const int wp = bb.get_int(kBbWpCursor, 0);
        bb.set_int(kBbWpCursor, wp + 1);
        return Status::kSuccess;
    });

    auto seq = std::make_unique<SequenceNode>();
    seq->add_child(std::move(walk_to_waypoint));
    seq->add_child(std::move(advance_cursor));

    // Wrap in an infinite repeater so the BT keeps cycling for as long as
    // the FSM stays in kPatrol.
    return std::make_unique<RepeaterNode>(std::move(seq), /*count*/ 0);
}

// --- Chase BT ---------------------------------------------------------------
// Single leaf - "run toward the player". The FSM owns the decision to enter
// Chase; the BT owns the locomotion contract. Reports kSuccess when the NPC
// closes within attack range so the FSM predicate can pick it up next tick.
[[nodiscard]] std::unique_ptr<cd::game::ai_bt::Node> make_chase_tree(NpcCtx& ctx)
{
    using namespace cd::game::ai_bt;
    return make_leaf([&ctx](Blackboard& bb) -> Status {
        constexpr float kChaseSpeed  = 3.0F;
        constexpr float kAttackRange = 1.0F;
        const float     dt           = bb.get_float("dt", 0.0F);

        set_anim(ctx, "running", kChaseSpeed);
        (void)step_toward(ctx.pos, ctx.player_pos, kChaseSpeed, dt, 0.01F);

        if (distance(ctx.pos, ctx.player_pos) <= kAttackRange)
        {
            ctx.in_attack_range = true;
            return Status::kSuccess;
        }
        return Status::kRunning;
    });
}

// --- Attack BT --------------------------------------------------------------
// SequenceNode { swing -> cooldown }, repeated until the player is "dead"
// (i.e. the attack counter hits a threshold).
[[nodiscard]] std::unique_ptr<cd::game::ai_bt::Node> make_attack_tree(NpcCtx& ctx)
{
    using namespace cd::game::ai_bt;

    auto swing = make_leaf([&ctx](Blackboard& bb) -> Status {
        set_anim(ctx, "attack_swing", 0.0F);
        const int hits = bb.get_int(kBbAttackHits, 0) + 1;
        bb.set_int(kBbAttackHits, hits);
        std::printf("[combat] swing #%d at player (%.1f, %.1f)\n",
                    hits,
                    static_cast<double>(ctx.player_pos.x),
                    static_cast<double>(ctx.player_pos.y));
        if (hits >= 3)
        {
            ctx.player_dead = true;
        }
        return Status::kSuccess;
    });

    auto cooldown = make_leaf([&ctx](Blackboard& /*bb*/) -> Status {
        set_anim(ctx, "attack_cooldown", 0.0F);
        return Status::kSuccess;
    });

    auto seq = std::make_unique<SequenceNode>();
    seq->add_child(std::move(swing));
    seq->add_child(std::move(cooldown));
    return std::make_unique<RepeaterNode>(std::move(seq), /*count*/ 0);
}

// -----------------------------------------------------------------------------
// FSM wiring.
//
// Three states (Patrol / Chase / Attack), three predicate-driven transitions:
//   Patrol -> Chase  when `see_player`
//   Chase  -> Attack when `in_attack_range`
//   Attack -> Patrol when `player_dead` (resume the patrol loop after the kill)
//
// Each state's on_enter resets the active BT's blackboard scratch values so
// re-entry starts clean (e.g. patrol cursor restarts at 0, attack hits at 0).
// -----------------------------------------------------------------------------
struct NpcController
{
    cd::game::fsm::StateMachine<NpcCtx> fsm;
    cd::game::fsm::StateId              s_patrol {cd::game::fsm::kInvalidStateId};
    cd::game::fsm::StateId              s_chase {cd::game::fsm::kInvalidStateId};
    cd::game::fsm::StateId              s_attack {cd::game::fsm::kInvalidStateId};

    cd::game::ai_bt::BehaviorTree       bt_patrol;
    cd::game::ai_bt::BehaviorTree       bt_chase;
    cd::game::ai_bt::BehaviorTree       bt_attack;
    cd::game::ai_bt::Blackboard         bb_patrol;
    cd::game::ai_bt::Blackboard         bb_chase;
    cd::game::ai_bt::Blackboard         bb_attack;
};

void build_controller(NpcController& nc, NpcCtx& ctx)
{
    using cd::game::fsm::State;

    // ---- BTs --------------------------------------------------------------
    nc.bt_patrol.set_root(make_patrol_tree(ctx));
    nc.bt_chase .set_root(make_chase_tree (ctx));
    nc.bt_attack.set_root(make_attack_tree(ctx));

    // ---- FSM states ------------------------------------------------------
    auto patrol = std::make_unique<State<NpcCtx>>("Patrol");
    patrol->set_on_enter([&nc](NpcCtx& c) {
        c.mode = NpcMode::kPatrol;
        std::printf("[fsm] -> Patrol\n");
        nc.bt_patrol.reset();
        nc.bb_patrol.set_int(kBbWpCursor, 0);
    });
    patrol->set_on_update([&nc](NpcCtx& /*c*/, float dt) { (void)nc.bt_patrol.tick(nc.bb_patrol, dt); });

    auto chase = std::make_unique<State<NpcCtx>>("Chase");
    chase->set_on_enter([&nc](NpcCtx& c) {
        c.mode = NpcMode::kChase;
        std::printf("[fsm] -> Chase (player spotted)\n");
        nc.bt_chase.reset();
    });
    chase->set_on_update([&nc](NpcCtx& /*c*/, float dt) { (void)nc.bt_chase.tick(nc.bb_chase, dt); });

    auto attack = std::make_unique<State<NpcCtx>>("Attack");
    attack->set_on_enter([&nc](NpcCtx& c) {
        c.mode = NpcMode::kAttack;
        std::printf("[fsm] -> Attack (in range)\n");
        nc.bt_attack.reset();
        nc.bb_attack.set_int(kBbAttackHits, 0);
    });
    attack->set_on_update([&nc](NpcCtx& /*c*/, float dt) { (void)nc.bt_attack.tick(nc.bb_attack, dt); });

    nc.s_patrol = nc.fsm.add_state(std::move(patrol), /*mark_initial*/ true);
    nc.s_chase  = nc.fsm.add_state(std::move(chase));
    nc.s_attack = nc.fsm.add_state(std::move(attack));

    // ---- Transitions ----------------------------------------------------
    nc.fsm.add_transition(nc.s_patrol, nc.s_chase,
                          [](const NpcCtx& c) { return c.see_player; });
    nc.fsm.add_transition(nc.s_chase, nc.s_attack,
                          [](const NpcCtx& c) { return c.in_attack_range; });
    nc.fsm.add_transition(nc.s_attack, nc.s_patrol,
                          [](const NpcCtx& c) { return c.player_dead; });
}

}  // namespace

int main(int argc, char** argv)
{
    std::printf("=== hello_npc - G2.4 closeout demo (FSM + BT NPC) ===\n");

    NpcCtx        ctx;
    NpcController nc;

    // Stretch the see-player timer when run interactively so a human can
    // watch the patrol loop for a while. Headless / CI run uses the default
    // 2.5 s so the program terminates quickly and deterministically.
    if (argc > 1)
    {
        ctx.see_player_at = static_cast<float>(std::atof(argv[1]));
        if (ctx.see_player_at < 0.1F) { ctx.see_player_at = 0.1F; }
    }

    build_controller(nc, ctx);
    nc.fsm.start(ctx);

    constexpr float kDt        = 0.1F;          // 100 ms per sim step
    constexpr float kMaxRuntime = 12.0F;        // hard ceiling - bounded run

    while (ctx.sim_time < kMaxRuntime)
    {
        // Drive the "see player" event off the sim clock instead of stdin
        // so the demo is reproducible from a script / CI invocation. The
        // brief allows either stdin or timer; we choose timer for hermetic
        // smoke runs.
        if (!ctx.see_player && ctx.sim_time >= ctx.see_player_at)
        {
            ctx.see_player = true;
            std::printf("[event] sim_time=%.2fs : player spotted!\n",
                        static_cast<double>(ctx.sim_time));
        }

        (void)nc.fsm.tick(ctx, kDt);
        ctx.sim_time += kDt;

        if (ctx.player_dead)
        {
            // Tick once more so the Attack -> Patrol transition fires and
            // the user sees the cycle close. Then exit.
            (void)nc.fsm.tick(ctx, kDt);
            std::printf("[event] sim_time=%.2fs : player dead, NPC returns to patrol.\n",
                        static_cast<double>(ctx.sim_time));
            break;
        }
    }

    std::printf("[done] final mode=%s pos=(%.2f, %.2f) sim_time=%.2fs\n",
                mode_name(ctx.mode),
                static_cast<double>(ctx.pos.x),
                static_cast<double>(ctx.pos.y),
                static_cast<double>(ctx.sim_time));
    return 0;
}
