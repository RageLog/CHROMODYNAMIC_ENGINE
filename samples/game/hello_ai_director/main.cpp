// =============================================================================
// CHROMODYNAMIC — samples/game/hello_ai_director
// Phase 634 (M9 W2 closeout).
//
// Console-only proof that cd::game::ai_director is consumable by a downstream
// caller.  The sample:
//   1. Configures 3 EncounterTemplates (patrol_group / goblin_raid / boss_ambush).
//   2. Runs 20 fixed-length ticks (500 ms each = 10 s of simulated game time).
//   3. Injects player events at predetermined ticks to drive tension changes.
//   4. Prints TensionState transitions and request_next_encounter results.
//   5. Exits 0.
//
// No window, no RHI, no Vulkan — purely cd::core + cd::game_ai_director.
// Dependencies (CLAUDE.md §7): cd::core, cd::game_ai_director.
// =============================================================================
#include <cd/game/ai_director/AiDirector.hpp>

#include <array>
#include <cstdio>
#include <optional>
#include <string_view>

namespace
{

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
[[nodiscard]] const char* tension_name(cd::game::ai_director::TensionState s) noexcept
{
    using cd::game::ai_director::TensionState;
    switch (s)
    {
        case TensionState::kIdle:    return "Idle";
        case TensionState::kBuildUp: return "BuildUp";
        case TensionState::kPeak:    return "Peak";
        case TensionState::kRelief:  return "Relief";
    }
    return "?";
}

// Print a TensionState transition banner (only when state changed).
void print_transition(cd::game::ai_director::TensionState prev,
                      cd::game::ai_director::TensionState next,
                      float                               sim_time_s) noexcept
{
    if (prev != next)
    {
        std::printf("[tension] %.2f s  %s -> %s\n",
                    static_cast<double>(sim_time_s),
                    tension_name(prev),
                    tension_name(next));
    }
}

// Print a request_next_encounter result.
void print_encounter(const std::optional<cd::game::ai_director::EncounterTemplate>& enc,
                     float intensity,
                     float sim_time_s) noexcept
{
    if (enc.has_value())
    {
        std::printf("[encounter] %.2f s  intensity=%.2f  -> \"%s\" (tier=%u, contrib=%.2f",
                    static_cast<double>(sim_time_s),
                    static_cast<double>(intensity),
                    enc->template_id.c_str(),
                    enc->difficulty_tier,
                    static_cast<double>(enc->intensity_contribution));
        if (!enc->spawn_entity_ids.empty())
        {
            std::printf(", spawns=[");
            for (std::size_t i = 0; i < enc->spawn_entity_ids.size(); ++i)
            {
                if (i > 0) { std::printf(", "); }
                std::printf("%s", enc->spawn_entity_ids[i].c_str());
            }
            std::printf("]");
        }
        std::printf(")\n");
    }
    else
    {
        std::printf("[encounter] %.2f s  intensity=%.2f  -> (no encounter)\n",
                    static_cast<double>(sim_time_s),
                    static_cast<double>(intensity));
    }
}

// -----------------------------------------------------------------------------
// Player-event script
// Pairs of { tick_index, event_id, intensity_delta }.
// -----------------------------------------------------------------------------
struct ScriptedEvent
{
    int             tick          {0};
    std::string_view event_id    {};
    float           intensity_delta{0.0F};
};

constexpr std::array<ScriptedEvent, 8> kEventScript = {{
    {2,  "patrol_cleared",     0.15F},   // slight pressure at tick 2
    {4,  "enemy_killed",       0.20F},   // bump into BuildUp zone
    {6,  "damage_taken",       0.25F},   // push toward Peak
    {8,  "enemy_killed",       0.20F},   // push into Peak
    {10, "objective_complete", 0.30F},   // spike into Relief
    {13, "health_pickup",     -0.10F},   // minor relief event (negative delta)
    {16, "patrol_cleared",     0.12F},   // gentle rebuild
    {19, "enemy_killed",       0.18F},   // end on rising tension
}};

}  // namespace

// =============================================================================
// main
// =============================================================================
int main()
{
    std::printf("=== hello_ai_director — Phase 634 (M9 W2) ===\n\n");

    // -------------------------------------------------------------------------
    // 1. Configure encounter templates.
    // -------------------------------------------------------------------------
    using namespace cd::game::ai_director;

    const std::array<EncounterTemplate, 3> templates = {{
        EncounterTemplate{
            .template_id            = "patrol_group",
            .difficulty_tier        = 0U,
            .intensity_contribution = 0.10F,
            .spawn_entity_ids       = {"goblin_scout", "goblin_scout"},
        },
        EncounterTemplate{
            .template_id            = "goblin_raid",
            .difficulty_tier        = 1U,
            .intensity_contribution = 0.20F,
            .spawn_entity_ids       = {"goblin_warrior", "goblin_archer", "goblin_warrior"},
        },
        EncounterTemplate{
            .template_id            = "boss_ambush",
            .difficulty_tier        = 2U,
            .intensity_contribution = 0.40F,
            .spawn_entity_ids       = {"orc_champion", "orc_shaman"},
        },
    }};

    AiDirector director;
    director.configure_templates(std::span<const EncounterTemplate>{templates});

    std::printf("[config] 3 encounter templates registered:\n");
    for (const auto& t : templates)
    {
        std::printf("  tier=%u  %-14s  contrib=%.2f  spawns=%zu\n",
                    t.difficulty_tier,
                    t.template_id.c_str(),
                    static_cast<double>(t.intensity_contribution),
                    t.spawn_entity_ids.size());
    }
    std::printf("\n");

    // -------------------------------------------------------------------------
    // 2. Simulate 20 ticks at 500 ms each (10 s of game time).
    // -------------------------------------------------------------------------
    constexpr float kDtMs      = 500.0F;   // 500 ms per tick
    constexpr int   kNumTicks  = 20;

    TensionState prev_tension = director.state().tension;

    std::printf("--- simulation start ---\n");

    for (int tick = 0; tick < kNumTicks; ++tick)
    {
        const float sim_time_s = static_cast<float>(tick) * (kDtMs / 1000.0F);

        // Inject scripted player events for this tick.
        for (const auto& ev : kEventScript)
        {
            if (ev.tick == tick)
            {
                director.notify_player_event(ev.event_id, ev.intensity_delta);
                std::printf("[event]   %.2f s  %-22.*s  delta=%+.2f\n",
                            static_cast<double>(sim_time_s),
                            static_cast<int>(ev.event_id.size()),
                            ev.event_id.data(),
                            static_cast<double>(ev.intensity_delta));
            }
        }

        // Advance the director by one tick.
        director.tick(kDtMs);

        const DirectorState ds = director.state();

        // Print TensionState transition (if any).
        print_transition(prev_tension, ds.tension, sim_time_s + (kDtMs / 1000.0F));
        prev_tension = ds.tension;

        // Request next encounter every tick.
        const auto enc = director.request_next_encounter();
        print_encounter(enc, ds.intensity, sim_time_s + (kDtMs / 1000.0F));
    }

    // -------------------------------------------------------------------------
    // 3. Final summary.
    // -------------------------------------------------------------------------
    const DirectorState final_state = director.state();
    std::printf("\n--- simulation end ---\n");
    std::printf("[summary] tension=%-8s  intensity=%.3f  score=%u  time_in_state=%.0f ms\n",
                tension_name(final_state.tension),
                static_cast<double>(final_state.intensity),
                final_state.player_score,
                static_cast<double>(final_state.time_in_state_ms));

    std::printf("\n=== hello_ai_director done — exit 0 ===\n");
    return 0;
}
